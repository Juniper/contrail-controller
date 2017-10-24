#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from analytic_client import AnalyticApiClient
import time, socket, os
from topology_uve import LinkUve
import gevent
from gevent.lock import Semaphore
from opserver.consistent_schdlr import ConsistentScheduler
from topology_config_handler import TopologyConfigHandler
import traceback
import ConfigParser
import signal
import random
import hashlib
from sandesh.topology_info.ttypes import TopologyInfo, TopologyUVE
from sandesh.link.ttypes import RemoteType, RemoteIfInfo, VRouterL2IfInfo,\
    VRouterL2IfUVE


class PRouter(object):
    def __init__(self, name, data):
        self.name = name
        self.data = data

class Controller(object):
    def __init__(self, config):
        self._config = config
        self._hostname = socket.gethostname()
        self.analytic_api = AnalyticApiClient(self._config)
        self._config.random_collectors = self._config.collectors()
        self._chksum = ""
        if self._config.collectors():
            self._chksum = hashlib.md5("".join(self._config.collectors())).hexdigest()
            self._config.random_collectors = random.sample(self._config.collectors(), \
                                                           len(self._config.collectors()))
        self.uve = LinkUve(self._config)
        self._sandesh = self.uve.sandesh_instance()
        self._logger = self.uve.logger()
        self.sleep_time()
        self._sem = Semaphore()
        self._members = None
        self._partitions = None
        self._prouters = {}
        self._vrouter_l2ifs = {}
        self._old_vrouter_l2ifs = {}
        self._config_handler = TopologyConfigHandler(self._sandesh,
            self._config.rabbitmq_params(), self._config.cassandra_params())
        self.constnt_schdlr = ConsistentScheduler(self.uve._moduleid,
            zookeeper=self._config.zookeeper_server(),
            delete_hndlr=self._del_uves, logger=self._logger,
            cluster_id=self._config.cluster_id())

    def sleep_time(self, newtime=None):
        if newtime:
            self._sleep_time = newtime
        else:
            self._sleep_time = self._config.frequency()
        return self._sleep_time

    def get_vrouters(self):
        self.analytic_api.get_vrouters(True)
        self.vrouters = {}
        self.vrouter_ips = {}
        self.vrouter_macs = {}
        for vr in self.analytic_api.list_vrouters():
            cfilt = ['VrouterAgent:phy_if', 'VrouterAgent:self_ip_list',
                'VRouterL2IfInfo']
            try:
                d = self.analytic_api.get_vrouter(vr, ','.join(cfilt))
            except Exception as e:
                traceback.print_exc()
                print str(e)
                d = {}
            if 'VrouterAgent' not in d or\
                'self_ip_list' not in d['VrouterAgent'] or\
                'phy_if' not in d['VrouterAgent']:
                continue
            self.vrouters[vr] = {'ips': d['VrouterAgent']['self_ip_list'],
                'if': d['VrouterAgent']['phy_if']
            }
            try:
                self.vrouters[vr]['l2_if'] = d['VRouterL2IfInfo']['if_info']
            except KeyError:
                pass
            for ip in d['VrouterAgent']['self_ip_list']:
                self.vrouter_ips[ip] = vr # index
            for intf in d['VrouterAgent']['phy_if']:
                try:
                    self.vrouter_macs[intf['mac_address']] = {}
                    self.vrouter_macs[intf['mac_address']]['vrname'] = vr
                    self.vrouter_macs[intf['mac_address']]['ifname'] = intf['name']
                except:
                    continue

    def get_prouters(self):
        self.analytic_api.get_prouters(True)
        self.prouters = []
        for pr in self.analytic_api.list_prouters():
            try:
                data = self.analytic_api.get_prouter(pr, 'PRouterEntry')
                if data:
                    self.prouters.append(PRouter(pr, data))
            except Exception as e:
                traceback.print_exc()
                print str(e)

    def _is_linkup(self, prouter, ifindex):
        if 'PRouterEntry' in prouter.data and \
            'ifIndexOperStatusTable' in prouter.data['PRouterEntry']:
                status = filter(lambda x: x['ifIndex'] == ifindex,
                        prouter.data['PRouterEntry']['ifIndexOperStatusTable'])
                if status and status[0]['ifOperStatus'] == 1:
                    return True
        return False

    def _add_link(self, prouter, remote_system_name, local_interface_name,
                  remote_interface_name, local_interface_index,
                  remote_interface_index, link_type):
        # If the remote_system_name or remote_interface_name is None, do not
        # add this link in the link_table.
        if not all([remote_system_name, remote_interface_name]):
            return False
        d = dict(remote_system_name=remote_system_name,
                 local_interface_name=local_interface_name,
                 remote_interface_name=remote_interface_name,
                 local_interface_index=local_interface_index,
                 remote_interface_index=remote_interface_index,
                 type=link_type)
        if link_type == RemoteType.VRouter:
            l2_if = self.vrouters[remote_system_name].get('l2_if')
            if l2_if and remote_interface_name in l2_if:
                if l2_if[remote_interface_name]['remote_system_name'] != \
                        prouter.name:
                    return False
        if self._is_linkup(prouter, local_interface_index):
            if prouter.name in self.link:
                self.link[prouter.name].append(d)
            else:
                self.link[prouter.name] = [d]
            return True
        return False

    def _chk_lnk(self, pre, index):
        if 'ifIndexOperStatusTable' in pre:
            for d in pre['ifIndexOperStatusTable']:
                if d['ifIndex'] == index:
                    return d['ifOperStatus'] == 1
        return False

    def _send_topology_uve(self, members, partitions, prouters):
        topology_info = TopologyInfo()
        if self._members != members:
            self._members = members
            topology_info.members = members
        if self._partitions != partitions:
            self._partitions = partitions
            topology_info.partitions = partitions
        new_prouters = {p.name: p for p in prouters}
        if self._prouters.keys() != new_prouters.keys():
            deleted_prouters = [v for p, v in self._prouters.iteritems() \
                if p not in new_prouters]
            self._del_uves(deleted_prouters)
            self._prouters = new_prouters
            topology_info.prouters = self._prouters.keys()
        if topology_info != TopologyInfo():
            topology_info.name = self._hostname
            TopologyUVE(data=topology_info).send()
    # end _send_topology_uve

    def bms_links(self, prouter, ifm):
        try:
            for lif_fqname, lif in self._config_handler.get_logical_interfaces():
                if prouter.name in lif_fqname:
                    for vmif in lif.obj.get_virtual_machine_interface_refs():
                        vmi = self._config_handler.\
                                get_virtual_machine_interface(uuid=vmif.uuid)
                        if not vmi:
                            continue
                        vmi = vmi.obj
                        for mc in vmi.virtual_machine_interface_mac_addresses.\
                                get_mac_address():
                            ifi = [k for k in ifm if ifm[k] in lif_fqname][0]
                            rsys = '-'.join(['bms', 'host'] + mc.split(':'))
                            self._add_link(prouter=prouter,
                                remote_system_name=rsys,
                                local_interface_name=lif.obj.fq_name[-1],
                                remote_interface_name='em0',#no idea
                                local_interface_index=ifi,
                                remote_interface_index=1, #dont know TODO:FIX
                                link_type=RemoteType.BMS)
        except:
            traceback.print_exc()


    def compute(self):
        self.link = {}
        self._old_vrouter_l2ifs = self._vrouter_l2ifs
        self._vrouter_l2ifs = {}
        for prouter in self.constnt_schdlr.work_items():
            pr, d = prouter.name, prouter.data
            if 'PRouterEntry' not in d or 'ifTable' not in d['PRouterEntry']:
                continue
            self.link[pr] = []
            lldp_ints = []
            ifm = dict(map(lambda x: (x['ifIndex'], x['ifDescr']),
                        d['PRouterEntry']['ifTable']))
            self.bms_links(prouter, ifm)
            for pl in d['PRouterEntry']['lldpTable']['lldpRemoteSystemsData']:
                if d['PRouterEntry']['lldpTable']['lldpLocalSystemData'][
                    'lldpLocSysDesc'].startswith('Cisco'):
                    loc_pname = [x for x in d['PRouterEntry']['lldpTable'][
                            'lldpLocalSystemData']['lldpLocPortTable'] if x[
                            'lldpLocPortNum'] == pl['lldpRemLocalPortNum']][
                            0]['lldpLocPortDesc']
                    pl['lldpRemLocalPortNum'] = [k for k in ifm if ifm[
                                            k] == loc_pname][0]
                elif d['PRouterEntry']['lldpTable']['lldpLocalSystemData'][
                       'lldpLocSysDesc'].startswith('Arista'):
                       loc_pname = [x for x in d['PRouterEntry']['lldpTable'][
                               'lldpLocalSystemData']['lldpLocPortTable'] if x[
                               'lldpLocPortNum'] == pl['lldpRemLocalPortNum']][
                               0]['lldpLocPortId']
                       pl['lldpRemLocalPortNum'] = [k for k in ifm if ifm[
                                               k] == loc_pname][0]
                if pl['lldpRemLocalPortNum'] in ifm and self._chk_lnk(
                        d['PRouterEntry'], pl['lldpRemLocalPortNum']):
                    if pl['lldpRemPortId'].isdigit():
                        rii = int(pl['lldpRemPortId'])
                    else:
                        try:
                            if d['PRouterEntry']['lldpTable']['lldpLocalSystemData'][
                                  'lldpLocSysDesc'].startswith('Arista'):
                                   rpn = filter(lambda y: y['lldpLocPortId'] == pl[
                                           'lldpRemPortId'], [
                                           x for x in self.prouters if x.name == pl[
                                           'lldpRemSysName']][0].data['PRouterEntry'][
                                           'lldpTable']['lldpLocalSystemData'][
                                           'lldpLocPortTable'])[0]['lldpLocPortId']
                            else:
                                 rpn = filter(lambda y: y['lldpLocPortId'] == pl[
                                  'lldpRemPortId'], [
                                  x for x in self.prouters if x.name == pl[
                                  'lldpRemSysName']][0].data['PRouterEntry'][
                                  'lldpTable']['lldpLocalSystemData'][
                                  'lldpLocPortTable'])[0]['lldpLocPortDesc']
                            rii = filter(lambda y: y['ifDescr'] == rpn,
                                    [ x for x in self.prouters \
                                    if x.name == pl['lldpRemSysName']][0].data[
                                    'PRouterEntry']['ifTable'])[0]['ifIndex']
                        except:
                            rii = 0

                    if d['PRouterEntry']['lldpTable']['lldpLocalSystemData'][
                         'lldpLocSysDesc'].startswith('Arista'):
                       if self._add_link(
                            prouter=prouter,
                            remote_system_name=pl['lldpRemSysName'],
                            local_interface_name=ifm[pl['lldpRemLocalPortNum']],
                            remote_interface_name=pl['lldpRemPortId'],
                            local_interface_index=pl['lldpRemLocalPortNum'],
                            remote_interface_index=rii,
                            link_type=RemoteType.PRouter):
                                lldp_ints.append(ifm[pl['lldpRemLocalPortNum']])
                    else:
                         if self._add_link(
                              prouter=prouter,
                              remote_system_name=pl['lldpRemSysName'],
                              local_interface_name=ifm[pl['lldpRemLocalPortNum']],
                              remote_interface_name=pl['lldpRemPortDesc'],
                              local_interface_index=pl['lldpRemLocalPortNum'],
                              remote_interface_index=rii,
                              link_type=RemoteType.PRouter):
                                  lldp_ints.append(ifm[pl['lldpRemLocalPortNum']])

            vrouter_l2ifs = {}
            if 'fdbPortIfIndexTable' in d['PRouterEntry']:
                dot1d2snmp = map (lambda x: (
                            x['dot1dBasePortIfIndex'],
                            x['snmpIfIndex']),
                        d['PRouterEntry']['fdbPortIfIndexTable'])
                dot1d2snmp_dict = dict(dot1d2snmp)
                if 'fdbPortTable' in d['PRouterEntry']:
                    for mac_entry in d['PRouterEntry']['fdbPortTable']:
                        if mac_entry['mac'] in self.vrouter_macs:
                            vrouter_mac_entry = self.vrouter_macs[mac_entry['mac']]
                            vr_name = vrouter_mac_entry['vrname']
                            vr_ifname = vrouter_mac_entry['ifname']
                            fdbport = mac_entry['dot1dBasePortIfIndex']
                            try:
                                snmpport = dot1d2snmp_dict[fdbport]
                                ifname = ifm[snmpport]
                            except:
                                continue
                            is_lldp_int = any(ifname == lldp_int for lldp_int in lldp_ints)
                            if is_lldp_int:
                                continue
                            if self._add_link(
                                    prouter=prouter,
                                    remote_system_name=vr_name,
                                    local_interface_name=ifname,
                                    remote_interface_name=vr_ifname,
                                    local_interface_index=snmpport,
                                    remote_interface_index=1, #dont know TODO:FIX
                                    link_type=RemoteType.VRouter):
                                if vr_name not in vrouter_l2ifs:
                                    vrouter_l2ifs[vr_name] = {}
                                vrouter_l2ifs[vr_name][vr_ifname] = {
                                    'remote_system_name': prouter.name,
                                    'remote_if_name': ifname,
                                }
            for arp in d['PRouterEntry']['arpTable']:
                if arp['ip'] in self.vrouter_ips:
                    if arp['mac'] in map(lambda x: x['mac_address'],
                            self.vrouters[self.vrouter_ips[arp['ip']]]['if']):
                        vr_name = self.vrouter_macs[arp['mac']]['vrname']
                        vr_ifname = self.vrouter_macs[arp['mac']]['ifname']
                        try:
                            if vrouter_l2ifs[vr_name][vr_ifname]\
                                ['remote_system_name'] == prouter.name:
                                del vrouter_l2ifs[vr_name][vr_ifname]
                                if not vrouter_l2ifs[vr_name]:
                                    del vrouter_l2ifs[vr_name]
                                continue
                        except KeyError:
                            pass
                        if ifm[arp['localIfIndex']].startswith('vlan'):
                            continue
                        if ifm[arp['localIfIndex']].startswith('irb'):
                            continue
                        is_lldp_int = any(ifm[arp['localIfIndex']] == lldp_int for lldp_int in lldp_ints)
                        if is_lldp_int:
                            continue
                        if self._add_link(
                                prouter=prouter,
                                remote_system_name=vr_name,
                                local_interface_name=ifm[arp['localIfIndex']],
                                remote_interface_name=vr_ifname,
                                local_interface_index=arp['localIfIndex'],
                                remote_interface_index=1, #dont know TODO:FIX
                                link_type=RemoteType.VRouter):
                            pass
            for vr, intf in vrouter_l2ifs.iteritems():
                if vr in self._vrouter_l2ifs:
                    self._vrouter_l2ifs[vr].update(vrouter_l2ifs[vr])
                else:
                    self._vrouter_l2ifs[vr] = intf

    def send_uve(self):
        old_vrs = set(self._old_vrouter_l2ifs.keys())
        new_vrs = set(self._vrouter_l2ifs.keys())
        del_vrs = old_vrs - new_vrs
        add_vrs = new_vrs - old_vrs
        same_vrs = old_vrs.intersection(new_vrs)
        for vr in del_vrs:
            vr_l2info = VRouterL2IfInfo(name=vr, deleted=True)
            VRouterL2IfUVE(data=vr_l2info).send()
        for vr in add_vrs:
            if_info = {}
            for vrif, remif_info in self._vrouter_l2ifs[vr].iteritems():
                if_info[vrif] = RemoteIfInfo(remif_info['remote_system_name'],
                    remif_info['remote_if_name'])
            vr_l2info = VRouterL2IfInfo(name=vr, if_info=if_info)
            VRouterL2IfUVE(data=vr_l2info).send()
        for vr in same_vrs:
            if self._vrouter_l2ifs[vr] != self._old_vrouter_l2ifs[vr]:
                if_info = {}
                for vrif, remif_info in self._vrouter_l2ifs[vr].iteritems():
                    if_info[vrif] = RemoteIfInfo(
                        remif_info['remote_system_name'],
                        remif_info['remote_if_name'])
                vr_l2info = VRouterL2IfInfo(name=vr, if_info=if_info)
                VRouterL2IfUVE(data=vr_l2info).send()
        self.uve.send(self.link)

    def switcher(self):
        gevent.sleep(0)

    def scan_data(self):
        t = []
        t.append(gevent.spawn(self.get_vrouters))
        t.append(gevent.spawn(self.get_prouters))
        gevent.joinall(t)

    def _del_uves(self, prouters):
        with self._sem:
            for prouter in prouters:
                self.uve.delete(prouter.name)

    def sighup_handler(self):
        if self._config._args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read(self._config._args.conf_file)
            if 'DEFAULTS' in config.sections():
                try:
                    collectors = config.get('DEFAULTS', 'collectors')
                    if type(collectors) is str:
                        collectors = collectors.split()
                        new_chksum = hashlib.md5("".join(collectors)).hexdigest()
                        if new_chksum != self._chksum:
                            self._chksum = new_chksum
                            self._config.random_collectors = \
                                random.sample(collectors, len(collectors))
                        # Reconnect to achieve load-balance irrespective of list
                        self.uve.sandesh_reconfig_collectors(
                                self._config.random_collectors)
                except ConfigParser.NoOptionError as e:
                    pass
    # end sighup_handler

    def _uve_scanner(self):
        while True:
            self.scan_data()
            if self.constnt_schdlr.schedule(self.prouters):
                members = self.constnt_schdlr.members()
                partitions = self.constnt_schdlr.partitions()
                self._send_topology_uve(members, partitions,
                    self.constnt_schdlr.work_items())
                try:
                    with self._sem:
                        self.compute()
                        self.send_uve()
                except Exception as e:
                    traceback.print_exc()
                    print str(e)
                gevent.sleep(self._sleep_time)
            else:
                gevent.sleep(1)
    # end _uve_scanner

    def run(self):
        """ @sighup
        SIGHUP handler to indicate configuration changes
        """
        gevent.signal(signal.SIGHUP, self.sighup_handler)

        self.gevs = [
            gevent.spawn(self._config_handler.start),
            gevent.spawn(self._uve_scanner)
        ]

        try:
            gevent.joinall(self.gevs)
        except KeyboardInterrupt:
            self._logger.error('Exiting on ^C')
        except gevent.GreenletExit:
            self._logger.error('Exiting on gevent-kill')
        finally:
            self._logger.error('stopping everything!')
            self.stop()
    # end run

    def stop(self):
        self.uve.stop()
        l = len(self.gevs)
        for i in range(0, l):
            self._logger.error('killing %d of %d' % (i+1, l))
            self.gevs[0].kill()
            self._logger.error('joining %d of %d' % (i+1, l))
            self.gevs[0].join()
            self._logger.error('stopped %d of %d' % (i+1, l))
            self.gevs.pop(0)
        self.constnt_schdlr.finish()
    # end stop
