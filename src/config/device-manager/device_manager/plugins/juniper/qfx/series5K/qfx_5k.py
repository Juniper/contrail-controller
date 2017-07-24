#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of netconf interface for QFX physical router
configuration manager
"""

from qfx_conf import QfxConf
from device_api.juniper_common_xsd import *

class Qfx5kConf(QfxConf):
    _products = ['qfx5100', 'qfx5110']

    def __init__(self, logger, params={}):
        self._logger = logger
        self.physical_router = params.get("physical_router")
        super(Qfx5kConf, self).__init__()
    # end __init__

    @classmethod
    def register(cls):
        qconf = {
              "vendor": cls._vendor,
              "products": cls._products,
              "class": cls
            }
        return super(Qfx5kConf, cls).register(qconf)
    # end register

    def set_product_specific_config(self):
        if self.global_switch_options_config is None:
            self.global_switch_options_config = SwitchOptions(comment=DMUtils.switch_options_comment())
        if self.routing_instances:
            self.global_switch_options_config.set_route_distinguisher(
                                 RouteDistinguisher(rd_type=self.bgp_params['identifier'] + ":1"))
    # end set_product_specific_config

    def build_evpn_config(self):
        return Evpn(encapsulation='vxlan')
    # end build_evpn_config

    def add_dynamic_tunnels(self, tunnel_source_ip,
                             ip_fabric_nets, bgp_router_ips):
        # not supported
        pass
    # end add_dynamic_tunnels

    def is_l3_supported(self, vn):
        return False
    # end is_l3_supported

    def add_ibgp_export_policy(self, params, bgp_group):
        # not needed
        pass
    # end add_ibgp_export_policy

    def set_resolve_bgp_route_target_family_config(self):
        if not self.global_routing_options_config:
            self.global_routing_options_config = RoutingOptions(
                                       comment=DMUtils.routing_options_comment())
        resolve = Resolution(rib=RIB(name="bgp.rtarget.0",
                                       resolution_ribs="inet.0"))
        self.global_routing_options_config.set_resolution(resolve)
    # end set_resolve_bgp_route_target_family_config

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        if is_delete:
            return self.send_conf(is_delete=True)
        self.set_qfx_common_config()
        return self.send_conf()
    # end push_conf

# end Qfx5kConf
