#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from gen.resource_xsd import PermType, IdPermsType

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
        self.perms = IdPermsType(perms, None, True, 0, 0)
    # end __init__
# end class Defaults(object):


class Provision(object):
    defaults = Defaults()
# end class Provision
