#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of abstract config generation for PNFs
"""

from ansible_role_common import AnsibleRoleCommon
from abstract_device_api.abstract_device_xsd import *

class PnfConf(AnsibleRoleCommon):
    _roles = ['pnf']

    def __init__(self, logger, params={}):
        super(PnfConf, self).__init__(logger, params)
    # end __init__

    @classmethod
    def register(cls):
        qconf = {
              "roles": cls._roles,
              "class": cls
            }
        return super(PnfConf, cls).register(qconf)
    # end register

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        if is_delete:
            return self.send_conf(is_delete=True)
        self.set_pnf_config()
        return self.send_conf()
    # end push_conf

# end PnfConf
