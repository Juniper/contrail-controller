#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from netaddr import IPAddress


class InstanceIpST(ResourceBaseST):
    _dict = {}
    obj_type = 'instance_ip'
    prop_fields = ['instance_ip_address', 'service_instance_ip',
                   'instance_ip_secondary']
    ref_fields = ['virtual_machine_interface']

    def __init__(self, name, obj=None):
        self.name = name
        self.is_secondary = False
        self.virtual_machine_interfaces = set()
        self.ip_version = None
        self.instance_ip_address = None
        self.service_instance_ip = None
        self.instance_ip_secondary = False
        self.update(obj)
    # end __init

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'instance_ip_address' in changed and self.instance_ip_address:
            self.ip_version = IPAddress(self.instance_ip_address).version
        return changed
    # end update

    def is_primary(self):
        return not self.instance_ip_secondary
# end InstanceIpST
