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
        self.build_bgp_config()
        return self.send_conf()
    # end push_conf

# end QfxConf
