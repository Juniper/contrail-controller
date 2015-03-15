#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from analytic_client import AnalyticApiClient
from topology_uve import LinkUve
import time, socket, os
import gevent
from libpartition.partition_helper import LibPartitionHelper

class PRouter(object):
    def __init__(self, name, data):
        self.name = name
        self.data = data

class Controller(object):
    def __init__(self, config):
        self._config = config
        self._me = socket.gethostname() + ':' + str(os.getpid())
        self.analytic_api = AnalyticApiClient(self._config)
        self.uve = LinkUve(self._config)
        self.sleep_time()
        self._keep_running = True

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
            d = self.analytic_api.get_vrouter(vr)
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
        for vr in self.analytic_api.list_prouters():
            self.prouters.append(PRouter(vr, self.analytic_api.get_prouter(vr)))

    def compute(self):
        self.link = {}
        for prouter in self.lph.work_items():
            pr, d = prouter.name, prouter.data
            if 'PRouterEntry' not in d or 'ifTable' not in d['PRouterEntry']:
                continue
            self.link[pr] = []
            lldp_ints = []
            ifm = dict(map(lambda x: (x['ifIndex'], x['ifDescr']),
                        d['PRouterEntry']['ifTable']))
            for pl in d['PRouterEntry']['lldpTable']['lldpRemoteSystemsData']:
                self.link[pr].append({
                        'remote_system_name': pl['lldpRemSysName'],
                        'local_interface_name': ifm[pl['lldpRemLocalPortNum']],
                        'remote_interface_name': pl['lldpRemPortDesc'],
                        'local_interface_index': pl['lldpRemLocalPortNum'],
                        'remote_interface_index': int(pl['lldpRemPortId']),
                        'type': 1
                        })
                lldp_ints.append(ifm[pl['lldpRemLocalPortNum']])

            vrouter_neighbors = []
            if 'fdbPortIfIndexTable' in d['PRouterEntry']:
                dot1d2snmp = map (lambda x: (x['dot1dBasePortIfIndex'], x['snmpIfIndex']), d['PRouterEntry']['fdbPortIfIndexTable'])
                dot1d2snmp_dict = dict(dot1d2snmp)
                if 'fdbPortTable' in d['PRouterEntry']:
                    for mac_entry in d['PRouterEntry']['fdbPortTable']:
                        if mac_entry['mac'] in self.vrouter_macs:
                            vrouter_mac_entry = self.vrouter_macs[mac_entry['mac']]
                            fdbport = mac_entry['dot1dBasePortIfIndex']
                            try:
                                snmpport = dot1d2snmp_dict[fdbport]
                            except:
                                continue
                            is_lldp_int = any(ifm[snmpport] == lldp_int for lldp_int in lldp_ints)
                            if is_lldp_int:
                                continue
                            self.link[pr].append({
                                'remote_system_name': vrouter_mac_entry['vrname'],
                                'local_interface_name': ifm[snmpport],
                                'remote_interface_name': vrouter_mac_entry['ifname'],
                                'local_interface_index': snmpport,
                                'remote_interface_index': 1, #dont know TODO:FIX
                                'type': 2
                                    })
                            vrouter_neighbors.append(vrouter_mac_entry['vrname'])
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
                        self.link[pr].append({
                            'remote_system_name': self.vrouter_ips[vr_name],
                            'local_interface_name': ifm[arp['localIfIndex']],
                            'remote_interface_name': vr['if'][-1]['name'],#TODO
                            'local_interface_index': arp['localIfIndex'],
                            'remote_interface_index': 1, #dont know TODO:FIX
                            'type': 2
                                })

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
        for prouter in prouters:
            self.uve.delete(prouter.name)

    def run(self):
        self.lph = LibPartitionHelper(self._me, self.uve._moduleid,
                                     self._config.disc_svr(self.uve._moduleid),
                                     self._config.zookeeper_server(),
                                     self._del_uves)
        while self._keep_running:
            self.scan_data()
            self.lph.refresh_workers()
            self.lph.populate_work_items(self.prouters)
            gevent.sleep(0)
            try:
                self.compute()
                self.send_uve()
            except Exception as e:
                import traceback; traceback.print_exc()
                print str(e)
            self.lph.disc_pub()
            gevent.sleep(self._sleep_time)
