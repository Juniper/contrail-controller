#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from builtins import object
from vnc_api.gen.resource_xsd import PermType, PermType2, IdPermsType
from cfgm_common import *

class Defaults(object):

    def __init__(self):
        perms = PermType('cloud-admin', PERMS_RWX,
                         'cloud-admin-group', PERMS_RWX,
                         PERMS_RWX)
        self.perms = IdPermsType(permissions=perms, enable=True)

        # set default perms2 of a new object
        # cloud-admin owner with full access, not shared with anyone
        self.perms2 = PermType2(
                    'cloud-admin', PERMS_RWX,    # tenant, tenant-access
                    PERMS_NONE,                  # global-access
                    [])                          # share list
        super(Defaults, self).__init__()
    # end __init__
# end class Defaults(object):


class Provision(object):
    defaults = Defaults()
# end class Provision
