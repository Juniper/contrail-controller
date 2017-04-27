#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of netconf interface for QFX physical router
configuration manager
"""

from db import *
from dm_utils import DMUtils
from juniper_conf import JuniperConf
from device_api.juniper_common_xsd import *

class QfxConf(JuniperConf):
    _product = 'qfx'

    def __init__(self, logger, params={}):
        self._logger = logger
        self.physical_router = params.get("physical_router")
        super(QfxConf, self).__init__()
    # end __init__

    @classmethod
    def register(cls):
        qconf = {
              "vendor": cls._vendor,
              "product": cls._product,
              "class": cls
            }
        return super(QfxConf, cls).register(qconf)
    # end register

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        if is_delete:
            return self.send_conf(is_delete=True)
        bgp_router = BgpRouterDM.get(self.physical_router.bgp_router)
        if bgp_router:
            for peer_uuid, attr in bgp_router.bgp_routers.items():
                peer = BgpRouterDM.get(peer_uuid)
                if peer is None:
                    continue
                local_as = (bgp_router.params.get('local_autonomous_system') or
                               bgp_router.params.get('autonomous_system'))
                peer_as = (peer.params.get('local_autonomous_system') or
                               peer.params.get('autonomous_system'))
                external = (local_as != peer_as)
                self.add_bgp_peer(peer.params['address'],
                                                 peer.params, attr, external, peer)
            self.set_bgp_config(bgp_router.params, bgp_router)
            self.set_global_routing_options(bgp_router.params)
            bgp_router_ips = bgp_router.get_all_bgp_router_ips()
            tunnel_ip = self.physical_router.dataplane_ip
            if not tunnel_ip and bgp_router.params:
                tunnel_ip = bgp_router.params.get('address')
            if (tunnel_ip and self.physical_router.is_valid_ip(tunnel_ip)):
                self.add_dynamic_tunnels(
                    tunnel_ip,
                    GlobalSystemConfigDM.ip_fabric_subnets,
                    bgp_router_ips)

        if self.physical_router.loopback_ip:
            self.add_lo0_unit_0_interface(self.physical_router.loopback_ip)
        self.set_as_config()
        self.set_bgp_group_config()
        return self.send_conf()
    # end push_conf

# end QfxConf
