#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from gen.resource_xsd import PermType, PermType2, IdPermsType

PERMS_NONE = 0
PERMS_X = 1
PERMS_W = 2
PERMS_R = 4
PERMS_WX = 3
PERMS_RX = 5
PERMS_RW = 6
PERMS_RWX = 7


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
