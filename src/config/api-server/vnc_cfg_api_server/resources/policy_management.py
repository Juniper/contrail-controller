#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import PolicyManagement

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


# Just decelared here to heritate 'locate' method of ResourceMixin class
class PolicyManagementServer(ResourceMixin, PolicyManagement):
    pass
