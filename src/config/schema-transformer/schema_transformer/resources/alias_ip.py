#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from netaddr import IPAddress
from schema_transformer.resources._resource_base import ResourceBaseST

class AliasIpST(ResourceBaseST):
    _dict = {}
    obj_type = 'alias_ip'
    prop_fields = ['floating_ip_address']
    ref_fields = ['virtual_machine_interface']

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interface = None
        self.alias_ip_address = None
        self.ip_version = None
        self.update(obj)
    # end __init

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'alias_ip_address' in changed and self.alias_ip_address:
            self.ip_version = IPAddress(self.alias_ip_address).version
        return changed
    # end update
# end AliasIpST
