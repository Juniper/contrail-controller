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
        for vr in self.analytic_api.list_vrouters():
            d = self.analytic_api.get_vrouter(vr)
            self.vrouters[vr] = {'ips': d['VrouterAgent']['self_ip_list'],
                'if': d['VrouterAgent']['phy_if'],
            }
            for ip in d['VrouterAgent']['self_ip_list']:
                self.vrouter_ips[ip] = vr # index

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
                    'PRouterEntry'] or 'arpTable' not in d['PRouterEntry']:
                continue
            self.link[pr] = []
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
            for arp in d['PRouterEntry']['arpTable']:
                if arp['ip'] in self.vrouter_ips:
                    if arp['mac'] in map(lambda x: x['mac_address'],
                            self.vrouters[self.vrouter_ips[arp['ip']]]['if']):
                        vr_name = arp['ip']
                        vr = self.vrouters[self.vrouter_ips[vr_name]]
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
