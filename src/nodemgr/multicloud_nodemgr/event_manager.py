#!/bin/env python2
from socket import gethostname

from gevent import monkey

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from nodemgr.vrouter_nodemgr.process_stat import VrouterProcessStat
from pysandesh.sandesh_base import sandesh_global
from sandesh_common.vns.ttypes import Module
from vrouter.multicloud.ttypes import UveBirdMulticloud, UveHAMulticloud, UveInterfaceMulticloud,\
    UveMulticloud, UveMulticloudTrace, UveRouteUpdate, UveTunnelMulticloud

from bird import Bird
from ipsec import IPSec
from openvpn import OpenVPN
from route_update import IPRouteWatcher
from vrrp import VRRP

monkey.patch_all()


class MulticloudEventManager(EventManager):
    def __init__(self, config, unit_names):
        type_info = EventManagerTypeInfo(
            module_type=Module.MULTICLOUD_NODE_MGR,
            object_table='ObjectMCTable',
            sandesh_packages=['vrouter.multicloud'])

        super(MulticloudEventManager, self).__init__(
            config, type_info,
            sandesh_global, unit_names, update_process_list=True)

        self.bird = Bird(self.logger)
        self.ipsec = IPSec(self.logger)
        self.openvpn = OpenVPN(self.logger)
        self.vrrp = VRRP(self.logger)

        self.route_update = IPRouteWatcher()
        self.route_update.listen()

    def get_process_stat_object(self, pname):
        return VrouterProcessStat(pname, self.logger)

    def do_periodic_events(self):
        super(MulticloudEventManager, self).do_periodic_events()
        self.gather_data()

    def nodemgr_sighup_handler(self):
        self.update_current_process()
        super(MulticloudEventManager, self).nodemgr_sighup_handler()

    @property
    def containers(self):
        containers = [module.get_containers() for module in
                      [self.bird, self.ipsec, self.openvpn, self.vrrp]]
        # Flatten
        containers = [container for module in containers for container in module]

        return containers

    @property
    def bird_status(self):
        statuses = self.bird.get_tunnel_status()
        data = [UveBirdMulticloud(peer=status.peer, is_up=status.is_up) for status in statuses]

        return data

    @property
    def ha_status(self):
        statuses = self.vrrp.get_status()
        data = [UveHAMulticloud(is_master=status.master_status, network=status.network)
                for status in statuses]

        return data

    @property
    def interface_stats(self):
        interfaces = {}
        for module in (self.openvpn, self.ipsec):
            interfaces.update(module.get_interface_stats())

        data = [UveInterfaceMulticloud(
            name=k, bytes_tx=v.byte_tx, packets_tx=v.packet_tx, bytes_rx=v.byte_rx,
            packets_rx=v.packet_rx) for (k, v) in interfaces.iteritems()]

        return data

    @property
    def tunnel_status(self):
        tunnels = []
        for module in (self.openvpn, self.ipsec):
            tunnels += module.get_tunnel_status()

        data = [UveTunnelMulticloud(
            peer=tunnel.peer, since=tunnel.since, is_up=tunnel.is_up) for tunnel in tunnels]
        return data

    @property
    def route_updates(self):
        updates = self.route_update.new_messages

        ret = [UveRouteUpdate(
            timestamp=update.timestamp, op=update.operation, target=update.target,
            iface=update.interface, via=update.via) for update in updates]

        return ret

    def gather_data(self):
        data = UveMulticloud(
            name=gethostname(),
            bird_list=self.bird_status,
            tunnel_list=self.tunnel_status,
            interface_list=self.interface_stats,
            ha_list=self.ha_status,
            route_updates_list=self.route_updates
        )

        trace = UveMulticloudTrace(data=data)

        print 'Sending'
        trace.send()
