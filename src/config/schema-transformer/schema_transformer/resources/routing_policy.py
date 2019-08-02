#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from vnc_api.gen.resource_xsd import RoutingPolicyType


class RoutingPolicyST(ResourceBaseST):
    _dict = {}
    obj_type = 'routing_policy'
    ref_fields = ['service_instance']

    def __init__(self, name, obj=None):
        self.name = name
        self.service_instances = {}
        self.routing_instances = set()
        self.update(obj)
        ri_refs = self.obj.get_routing_instance_refs() or []
        for ref in ri_refs:
            ri_name = ':'.join(ref['to'])
            self.routing_instances.add(ri_name)
            ri = ResourceBaseST.get_obj_type_map().get('routing_instance').get(ri_name)
            if ri:
                ri.routing_policys[self.name] = ref['attr'].sequence
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'service_instance' not in changed:
            return False
        new_refs = dict((':'.join(ref['to']), ref['attr'])
                        for ref in self.obj.get_service_instance_refs() or [])
        for ref in set(self.service_instances.keys()) - set(new_refs.keys()):
            si = ResourceBaseST.get_obj_type_map().get('service_instance').get(ref)
            if si and self.name in si.routing_policys:
                del si.routing_policys[self.name]
        for ref in set(new_refs.keys()):
            si = ResourceBaseST.get_obj_type_map().get('service_instance').get(ref)
            if si:
                si.routing_policys[self.name] = new_refs[ref]
        self.service_instances = new_refs
        return True
    # end update

    def add_routing_instance(self, ri, seq):
        if ri.name in self.routing_instances:
            return
        self._vnc_lib.ref_update('routing-policy', self.obj.uuid,
                                 'routing-instance', ri.obj.uuid,
                                 None, 'ADD', RoutingPolicyType(seq))
        self.routing_instances.add(ri.name)
    # end add_routing_instance

    def delete_routing_instance(self, ri):
        if ri.name not in self.routing_instances:
            return
        self._vnc_lib.ref_update('routing-policy', self.obj.uuid,
                                 'routing-instance', ri.obj.uuid,
                                 None, 'DELETE')
        self.routing_instances.discard(ri.name)
    # end delete_routing_instance

    def handle_st_object_req(self):
        resp = super(RoutingPolicyST, self).handle_st_object_req()
        resp.obj_refs.extend([
            self._get_sandesh_ref_list('routing_instance'),
        ])
        return resp
    # end handle_st_object_req
# end RoutingPolicyST
