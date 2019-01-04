import socket

from cfgm_common.vnc_amqp import VncAmqpHandle
from cfgm_common import vnc_etcd
from config_db import DBBaseST, VirtualNetworkST, BgpRouterST


class STAmqpHandleEtcd(vnc_etcd.VncEtcdWatchHandle):

    def __init__(self, logger, reaction_map, args, timer_obj=None):
        q_name_prefix = 'schema_transformer'
        etcd_cfg = vnc_etcd.etcd_args(args)
        if 'host_ip' in args:
            host_ip = args.host_ip
        else:
            host_ip = socket.gethostbyname(socket.getfqdn())
        super(STAmqpHandleEtcd, self).__init__(logger._sandesh, logger, DBBaseST,
                                           reaction_map, etcd_cfg, host_ip=host_ip, timer_obj=timer_obj)

    def evaluate_dependency(self):
        if not self.dependency_tracker:
            return
        super(STAmqpHandleEtcd, self).evaluate_dependency()
        for vn_id in self.dependency_tracker.resources.get(
                'virtual_network', []):
            vn = VirtualNetworkST.get(vn_id)
            if vn is not None:
                vn.uve_send()
        for bgp_router_id in \
                self.dependency_tracker.resources.get('bgp_router', []):
            bgp_router = BgpRouterST.get(bgp_router_id)
            if bgp_router is not None:
                bgp_router.update_peering()

    def greenlets(self):
        return self._vnc_etcd_watcher.greenlets()