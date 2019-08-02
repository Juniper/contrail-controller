#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST


class PhysicalRouterST(ResourceBaseST):
    _dict = {}
    obj_type = 'physical_router'
    ref_fields = ['bgp_router', 'fabric']

    def __init__(self, name, obj=None):
        self.name = name
        self.bgp_router = None
        self.fabric = None
        self.update(obj)
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'bgp_router' in changed:
            bgp_rtr = ResourceBaseST.get_obj_type_map().get('bgp_router').locate(self.bgp_router)
            if bgp_rtr:
                bgp_rtr.physical_router = self.name
        return changed
    # end update

# end class PhysicalRouterST
