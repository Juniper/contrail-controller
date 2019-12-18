#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of netconf interface for QFX physical router
configuration manager
"""

from .qfx_conf import QfxConf
from device_api.juniper_common_xsd import *

class Qfx10kConf(QfxConf):
    _products = ['qfx10000', 'qfx10002', 'vqfx-10000']

    def __init__(self, logger, params={}):
        self._logger = logger
        self.physical_router = params.get("physical_router")
        super(Qfx10kConf, self).__init__()
    # end __init__

    @classmethod
    def register(cls):
        qconf = {
              "vendor": cls._vendor,
              "products": cls._products,
              "class": cls
            }
        return super(Qfx10kConf, cls).register(qconf)
    # end register

    def build_evpn_config(self, int_vn = False):
        # for internal VN (intervxlan routing) do not set anything
        if not int_vn:
            evpn = Evpn(encapsulation='vxlan')
        else:
            evpn = Evpn()
        if self.is_spine() and not int_vn:
            evpn.set_default_gateway("no-gateway-community")
        return evpn
    # end build_evpn_config

    def is_l3_supported(self, vn):
        if self.is_spine() and '_lr_internal_vn_' in vn.name:
            return True
        return False
    # end is_l3_supported

    def add_dynamic_tunnels(self, tunnel_source_ip,
                             ip_fabric_nets, bgp_router_ips):
        pass
    # end add_dynamic_tunnels

    def add_ibgp_export_policy(self, params, bgp_group):
        if self.is_spine():
            super(Qfx10kConf, self).add_ibgp_export_policy(params, bgp_group)
    # end add_ibgp_export_policy

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        if is_delete:
            return self.send_conf(is_delete=True)
        if not self.ensure_bgp_config():
            return 0
        self.set_qfx_common_config()
        return self.send_conf()
    # end push_conf

# end Qfx10kConf
