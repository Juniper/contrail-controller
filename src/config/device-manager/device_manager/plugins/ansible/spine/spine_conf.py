#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of abstract config generation for spines 
"""

from ansible_role_common import AnsibleRoleCommon
from abstract_device_api.abstract_device_xsd import *

class SpineConf(AnsibleRoleCommon):
    _roles = ['spine']

    def __init__(self, vnc_lib, logger, params={}):
        self._vnc_lib = vnc_lib
        super(SpineConf, self).__init__(logger, params)
    # end __init__

    @classmethod
    def register(cls):
        qconf = {
              "roles": cls._roles,
              "class": cls
            }
        return super(SpineConf, cls).register(qconf)
    # end register

    def build_evpn_config(self):
        evpn = Evpn(encapsulation='vxlan')
        if self.is_spine():
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
            super(SpineConf, self).add_ibgp_export_policy(params, bgp_group)
    # end add_ibgp_export_policy

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        self.initialize()
        if is_delete:
            return self.send_conf(is_delete=True)
        if not self.ensure_bgp_config():
            return 0
        self.set_common_config()
        return self.send_conf()
    # end push_conf

# end SpineConf
