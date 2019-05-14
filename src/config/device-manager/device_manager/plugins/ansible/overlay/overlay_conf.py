#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of abstract config generation for leafs
"""

from ansible_role_common import AnsibleRoleCommon
from abstract_device_api.abstract_device_xsd import *

class OverlayConf(AnsibleRoleCommon):
    _roles = ['leaf', 'spine']

    def __init__(self, logger, params={}):
        super(OverlayConf, self).__init__(logger, params)
    # end __init__

    @classmethod
    def register(cls):
        qconf = {
              "roles": cls._roles,
              "class": cls
            }
        return super(OverlayConf, cls).register(qconf)
    # end register

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        if self.physical_router.managed_state == 'rma':
            # do not push any config to this device
            self._logger.debug("No config push for PR(%s) in RMA state" % (
                               self.physical_router.name))
            return 0
        if is_delete:
            return self.send_conf(is_delete=True)
        self.set_common_config()
        return self.send_conf()
    # end push_conf

# end LeafConf
