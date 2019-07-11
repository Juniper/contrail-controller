#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from schema_transformer.utils import _create_pprinted_prop_list


class RouteTableST(ResourceBaseST):
    _dict = {}
    obj_type = 'route_table'
    prop_fields = ['routes']

    _service_instances = {}

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_networks = set()
        self.logical_routers = set()
        self.service_instances = set()
        self.routes = []
        self.update(obj)
        self.update_multiple_refs('virtual_network', self.obj)
        self.update_multiple_refs('logical_router', self.obj)
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'routes' in changed:
            if self.routes is None:
                self.routes = []
            else:
                self.routes = self.routes.get_route() or []
            si_set = set([route.next_hop for route in self.routes
                          if route.next_hop_type != 'ip-address'])
            self.update_service_instances(si_set)
        return changed
    # end update

    def update_service_instances(self, si_set):
        old_si_set = self.service_instances
        for si_name in old_si_set - si_set:
            rt_list = self._service_instances.get(si_name)
            if rt_list:
                rt_list.discard(self.name)
            if not rt_list:
                del self._service_instances[si_name]
            si = ResourceBaseST.get_obj_type_map().get('service_instance').get(si_name)
            if si is None:
                continue
            si.route_tables.discard(self.name)

        for si_name in si_set - old_si_set:
            rt_list = self._service_instances.setdefault(si_name, set())
            rt_list.add(self.name)
            si = ResourceBaseST.get_obj_type_map().get('service_instance').get(si_name)
            if si is None:
                continue
            si.route_tables.add(self.name)
        self.service_instances = si_set
    # end update_service_instances

    @classmethod
    def get_by_service_instance(cls, si_name):
        return cls._service_instances.get(si_name, set())

    def delete_obj(self):
        self.update_multiple_refs('virtual_network', {})
        self.update_multiple_refs('logical_router', {})

    def handle_st_object_req(self):
        resp = super(RouteTableST, self).handle_st_object_req()
        resp.obj_refs = [
            self._get_sandesh_ref_list('virtual_network'),
            self._get_sandesh_ref_list('service_instance'),
            self._get_sandesh_ref_list('logical_router'),
        ]
        resp.properties = [
            _create_pprinted_prop_list('route', route)
            for route in self.routes
        ]
        return resp
    # end handle_st_object_req
# end RouteTableST
