#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
from analytic_client import AnalyticApiClient
from topology_uve import LinkUve
import time
import gevent

class Controller(object):
    def __init__(self, config):
        self._config = config
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
        self.prouters = {}
        for vr in self.analytic_api.list_prouters():
            d = self.analytic_api.get_prouter(vr)
            self.prouters[vr] = self.analytic_api.get_prouter(vr)
 
    def compute(self):
        self.link = {}
        for pr, d in self.prouters.items():
            if 'PRouterEntry' not in d or 'ifTable' not in d[
                    'PRouterEntry']:
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
            dot1d2snmp = map (lambda x: (x['dot1dBasePortIfIndex'], x['snmpIfIndex']), d['PRouterEntry']['fdbPortIfIndexTable'])
            dot1d2snmp_dict = dict(dot1d2snmp)
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

    def run(self):
        while self._keep_running:
            t = []
            t.append(gevent.spawn(self.get_vrouters))
            t.append(gevent.spawn(self.get_prouters))
            gevent.joinall(t)
            del t
            self.compute()
            self.send_uve()
            gevent.sleep(self._sleep_time)
