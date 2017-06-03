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

    _FAMILY_MAP = {
        'route-target': '',
        'e-vpn': FamilyEvpn(signaling='')
    }

    @classmethod
    def is_product_supported(cls, name):
        if name.lower() in cls._products:
            return True
        return False
    # end is_product_supported

    def __init__(self):
        super(QfxConf, self).__init__()
    # end __init__

# end QfxConf
