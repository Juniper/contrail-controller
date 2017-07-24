#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of netconf interface for QFX physical router
configuration manager
"""

from qfx_conf import QfxConf
from device_api.juniper_common_xsd import *

class Qfx10kConf(QfxConf):
    _products = ['qfx10000', 'qfx10002']

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

    def set_product_specific_config(self):
        pass
    # end set_product_specific_config

    def build_evpn_config(self):
        evpn = Evpn(encapsulation='vxlan')
        if self.is_spine():
            evpn.set_default_gateway("no-gateway-community")
        else:
            evpn.set_default_gateway("do-not-advertise")
        return evpn
    # end build_evpn_config

    def is_l3_supported(self, vn):
        if '_lr_internal_vn__' in vn.name:
            return True
        return False
    # end is_l3_supported

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        if is_delete:
            return self.send_conf(is_delete=True)
        self.set_qfx_common_config()
        return self.send_conf()
    # end push_conf

# end Qfx10kConf
