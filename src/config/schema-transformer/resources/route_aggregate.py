#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from netaddr import IPNetwork
from cfgm_common.exceptions import NoIdError


class RouteAggregateST(ResourceBaseST):
    _dict = {}
    obj_type = 'route_aggregate'
    prop_fields = ['aggregate_route_entries']
    ref_fields = ['service_instance', 'routing_instance']

    def __init__(self, name, obj=None):
        self.name = name
        self.service_instances = {}
        self.routing_instances = set()
        self.aggregate_route_entries = None
        self.update(obj)
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'service_instance' in changed:
            new_refs = dict((':'.join(ref['to']), ref['attr'].interface_type)
                            for ref in self.obj.get_service_instance_refs() or [])
            for ref in set(self.service_instances.keys()) - set(new_refs.keys()):
                si = ResourceBaseST.get_obj_type_map().get('service_instance').get(ref)
                if si and self.name in si.route_aggregates:
                    del si.route_aggregates[self.name]
            for ref in set(new_refs.keys()):
                si = ResourceBaseST.get_obj_type_map().get('service_instance').get(ref)
                if si:
                    si.route_aggregates[self.name] = new_refs[ref]
            self.service_instances = new_refs
        return changed
    # end update

    def add_routing_instance(self, ri):
        if ri.name in self.routing_instances:
            return
        if not self.aggregate_route_entries or not self.aggregate_route_entries.route:
            return
        ip_version = IPNetwork(self.aggregate_route_entries.route[0]).version
        if ip_version == 4:
            if ri.service_chain_info is None:
                self._logger.error("No ipv4 service chain info found for %s"
                                   % ri.name)
                return
            next_hop = ri.service_chain_info.get_service_chain_address()
        elif ip_version == 6:
            if ri.v6_service_chain_info is None:
                self._logger.error("No ipv6 service chain info found for %s"
                                   % ri.name)
                return
            next_hop = ri.v6_service_chain_info.get_service_chain_address()
        else:
            self._logger.error("route aggregate %s: unknonwn ip version: %s"
                               % (self.name, ip_version))
            return
        self.obj.set_aggregate_route_nexthop(next_hop)
        self.obj.set_routing_instance(ri.obj)
        self._vnc_lib.route_aggregate_update(self.obj)
        self.routing_instances.add(ri.name)
    # end add_routing_instance

    def delete_routing_instance(self, ri):
        if ri.name not in self.routing_instances:
            return
        self.obj.set_aggregate_route_nexthop(None)
        self.obj.set_routing_instance_list([])
        try:
            self._vnc_lib.route_aggregate_update(self.obj)
        except NoIdError:
            pass
        self.routing_instances.discard(ri.name)
    # end delete_routing_instance

    def handle_st_object_req(self):
        resp = super(RouteAggregateST, self).handle_st_object_req()
        resp.obj_refs.extend([
            self._get_sandesh_ref_list('routing_instance'),
        ])
        return resp
    # end handle_st_object_req
# end RouteAggregateST
