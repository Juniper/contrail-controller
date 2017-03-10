#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from analytic_client import AnalyticApiClient
import time, socket, os
from topology_uve import LinkUve
import gevent
from gevent.lock import Semaphore
from opserver.consistent_schdlr import ConsistentScheduler
import traceback
import ConfigParser
import signal
import random
import hashlib
from sandesh.topology_info.ttypes import TopologyInfo, TopologyUVE

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
        self._logger = self.uve.logger()
        self.sleep_time()
        self._keep_running = True
        self._vnc = None
        self._members = None
        self._partitions = None
        self._prouters = None

    def stop(self):
        self._keep_running = False

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
            try:
                d = self.analytic_api.get_vrouter(vr, 'VrouterAgent:phy_if')
            except Exception as e:
                traceback.print_exc()
                print str(e)
                d = {}
            if 'VrouterAgent' not in d:
                d['VrouterAgent'] = {}
            try:
                _ipl = self.analytic_api.get_vrouter(vr,
                        'VrouterAgent:self_ip_list')
            except Exception as e:
                traceback.print_exc()
                print str(e)
                _ipl = {}
            if 'VrouterAgent' in _ipl:
                d['VrouterAgent'].update(_ipl['VrouterAgent'])
            if 'VrouterAgent' not in d or\
                'self_ip_list' not in d['VrouterAgent'] or\
                'phy_if' not in d['VrouterAgent']:
                continue
            self.vrouters[vr] = {'ips': d['VrouterAgent']['self_ip_list'],
                'if': d['VrouterAgent']['phy_if'],
            }
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
                self.prouters.append(PRouter(pr, self.analytic_api.get_prouter(
                            pr, 'PRouterEntry')))
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
        d = dict(remote_system_name=remote_system_name,
                 local_interface_name=local_interface_name,
                 remote_interface_name=remote_interface_name,
                 local_interface_index=local_interface_index,
                 remote_interface_index=remote_interface_index,
                 type=link_type)
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
        if self._prouters != prouters:
            self._prouters = prouters
            topology_info.prouters = prouters
        if topology_info != TopologyInfo():
            topology_info.name = self._hostname
            TopologyUVE(data=topology_info).send()
    # end _send_topology_uve

    def bms_links(self, prouter, ifm):
        if not self._vnc:
            try:
                self._vnc = self._config.vnc_api()
            except:
                print 'Proceeding without any api-server'
                self._vnc = None # refresh
        if self._vnc:
            try:
                for li in self._vnc.logical_interfaces_list()[
                            'logical-interfaces']:
                    if prouter.name in li['fq_name']:
                        lif = self._vnc.logical_interface_read(id=li['uuid'])
                        for vmif in lif.get_virtual_machine_interface_refs():
                            vmi = self._vnc.virtual_machine_interface_read(
                                    id=vmif['uuid'])
                            for mc in vmi.virtual_machine_interface_mac_addresses.get_mac_address():
                                ifi = [k for k in ifm if ifm[k] in li[
                                                    'fq_name']][0]
                                rsys = '-'.join(['bms', 'host'] + mc.split(
                                            ':'))
                                if self._add_link(
                                        prouter=prouter,
                                        remote_system_name=rsys,
                                        local_interface_name=li['fq_name'][
                                                                    -1],
                                        remote_interface_name='em0',#no idea
                                        local_interface_index=ifi,
                                        remote_interface_index=1, #dont know TODO:FIX
                                        link_type=2):
                                    pass
            except:
                traceback.print_exc()
                self._vnc = None # refresh


    def compute(self):
        self.link = {}
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
                if pl['lldpRemLocalPortNum'] in ifm and self._chk_lnk(
                        d['PRouterEntry'], pl['lldpRemLocalPortNum']):
                    if pl['lldpRemPortId'].isdigit():
                        rii = int(pl['lldpRemPortId'])
                    else:
                        try:
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

                    if self._add_link(
                            prouter=prouter,
                            remote_system_name=pl['lldpRemSysName'],
                            local_interface_name=ifm[pl['lldpRemLocalPortNum']],
                            remote_interface_name=pl['lldpRemPortDesc'],
                            local_interface_index=pl['lldpRemLocalPortNum'],
                            remote_interface_index=rii,
                            link_type=1):
                        lldp_ints.append(ifm[pl['lldpRemLocalPortNum']])

            vrouter_neighbors = []
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
                                    remote_system_name=vrouter_mac_entry['vrname'],
                                    local_interface_name=ifname,
                                    remote_interface_name=vrouter_mac_entry[
                                                'ifname'],
                                    local_interface_index=snmpport,
                                    remote_interface_index=1, #dont know TODO:FIX
                                    link_type=2):
                                vrouter_neighbors.append(
                                        vrouter_mac_entry['vrname'])
            for arp in d['PRouterEntry']['arpTable']:
                if arp['ip'] in self.vrouter_ips:
                    if arp['mac'] in map(lambda x: x['mac_address'],
                            self.vrouters[self.vrouter_ips[arp['ip']]]['if']):
                        vr_name = arp['ip']
                        vr = self.vrouters[self.vrouter_ips[vr_name]]
                        if self.vrouter_ips[vr_name] in vrouter_neighbors:
                            continue
                        if ifm[arp['localIfIndex']].startswith('vlan'):
                            continue
                        if ifm[arp['localIfIndex']].startswith('irb'):
                            continue
                        is_lldp_int = any(ifm[arp['localIfIndex']] == lldp_int for lldp_int in lldp_ints)
                        if is_lldp_int:
                            continue
                        if self._add_link(
                                prouter=prouter,
                                remote_system_name=self.vrouter_ips[vr_name],
                                local_interface_name=ifm[arp['localIfIndex']],
                                remote_interface_name=vr['if'][-1]['name'],#TODO
                                local_interface_index=arp['localIfIndex'],
                                remote_interface_index=1, #dont know TODO:FIX
                                link_type=2):
                            pass

    def send_uve(self):
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
                            random_collectors = random.sample(collectors, len(collectors))
                            self.uve.sandesh_reconfig_collectors(random_collectors)
                except ConfigParser.NoOptionError as e:
                    pass
    # end sighup_handler
 
        
    def run(self):

        """ @sighup
        SIGHUP handler to indicate configuration changes 
        """
        gevent.signal(signal.SIGHUP, self.sighup_handler)

        self._sem = Semaphore()
        self.constnt_schdlr = ConsistentScheduler(
                            self.uve._moduleid,
                            zookeeper=self._config.zookeeper_server(),
                            delete_hndlr=self._del_uves,
                            logger=self._logger,
                            cluster_id=self._config.cluster_id())

        while self._keep_running:
            self.scan_data()
            if self.constnt_schdlr.schedule(self.prouters):
                members = self.constnt_schdlr.members()
                partitions = self.constnt_schdlr.partitions()
                prouters = map(lambda x: x.name,
                    self.constnt_schdlr.work_items())
                self._send_topology_uve(members, partitions, prouters)
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
        self.constnt_schdlr.finish()
