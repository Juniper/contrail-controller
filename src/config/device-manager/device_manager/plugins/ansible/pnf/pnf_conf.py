#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
# This file contains implementation of abstract config generation for PNFs
#

from .ansible_role_common import AnsibleRoleCommon


class PnfConf(AnsibleRoleCommon):
    _roles = ["pnf"]

    def __init__(self, logger, params={}):
        """Initialize PnfConf init params."""
        super(PnfConf, self).__init__(logger, params)

    # end __init__

    @classmethod
    def register(cls):
        qconf = {"roles": cls._roles, "class": cls}
        return super(PnfConf, cls).register(qconf)

    # end register

    def push_conf(self, feature_configs=None, is_delete=False, **kwargs):
        if not self.physical_router:
            return 0
        if is_delete:
            return self.send_conf(is_delete=True)
        return self.send_conf(feature_configs=feature_configs)

    # end push_conf

# end PnfConf
