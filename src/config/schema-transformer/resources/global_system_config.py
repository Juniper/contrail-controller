#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.db_base import DBBaseST
from schema_transformer.resources.bgp_router import BgpRouterST
from schema_transformer.resources.route_target import RouteTargetST
from schema_transformer.resources.routing_instance import RoutingInstanceST
from schema_transformer.resources.logical_router import LogicalRouterST
from vnc_api.gen.resource_common import RouteTarget
from vnc_api.gen.resource_xsd import InstanceTargetType
import cfgm_common as common


class GlobalSystemConfigST(DBBaseST):
    _dict = {}
    obj_type = 'global_system_config'
    _autonomous_system = 0
    ibgp_auto_mesh = None
    prop_fields = ['autonomous_system', 'ibgp_auto_mesh', 'bgpaas_parameters']

    @classmethod
    def reinit(cls):
        for gsc in cls.list_vnc_obj():
            try:
                cls.locate(gsc.get_fq_name_str(), gsc)
            except Exception as e:
                cls._logger.error("Error in reinit for %s %s: %s" % (
                    cls.obj_type, gsc.get_fq_name_str(), str(e)))
    # end reinit

    def __init__(self, name, obj):
        self.name = name
        self.uuid = obj.uuid
        self.update(obj)
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'autonomous_system' in changed:
            self.update_autonomous_system(self.obj.autonomous_system)
        if 'bgpaas_parameters' in changed:
            self.update_bgpaas_parameters(self.obj.bgpaas_parameters)
        if 'ibgp_auto_mesh' in changed:
            GlobalSystemConfigST.ibgp_auto_mesh = self.ibgp_auto_mesh
        return changed
    # end update

    def update_bgpaas_parameters(self, bgpaas_params):
        if bgpaas_params:
            new_range = {'start': bgpaas_params.port_start,
                         'end': bgpaas_params.port_end}
            self._object_db._bgpaas_port_allocator.reallocate([new_range])

    @classmethod
    def get_autonomous_system(cls):
        return cls._autonomous_system
    # end get_autonomous_system

    @classmethod
    def get_ibgp_auto_mesh(cls):
        if cls.ibgp_auto_mesh is None:
            return True
        return cls.ibgp_auto_mesh
    # end get_ibgp_auto_mesh

    @classmethod
    def update_autonomous_system(cls, new_asn):
        if int(new_asn) == cls._autonomous_system:
            return False
        # From the global route target list, pick ones with the
        # changed ASN, and update the routing instances' referred
        # by the route target
        for route_tgt in RouteTargetST.values():
            _, asn, target = route_tgt.obj.get_fq_name()[0].split(':')
            if int(asn) != cls.get_autonomous_system():
                continue
            if int(target) < common.BGP_RTGT_MIN_ID:
                continue

            new_rtgt_name = "target:%s:%s" % (new_asn, target)
            new_rtgt_obj = RouteTargetST.locate(new_rtgt_name)
            old_rtgt_name = "target:%d:%s" % (cls._autonomous_system, target)
            old_rtgt_obj = RouteTarget(old_rtgt_name)

            route_tgt.obj = RouteTargetST.read_vnc_obj(
                fq_name=[old_rtgt_name],
                fields=['logical_router_back_refs', 'routing_instance_back_refs'])

            for ri_ref in route_tgt.obj.get_routing_instance_back_refs() or []:
                rt_inst = RoutingInstanceST.get(':'.join(ri_ref['to']))
                if rt_inst:
                    ri = rt_inst.obj
                else:
                    continue
                inst_tgt_data = InstanceTargetType()
                ri.del_route_target(old_rtgt_obj)
                ri.add_route_target(new_rtgt_obj.obj, inst_tgt_data)
                cls._vnc_lib.routing_instance_update(ri)
                # Also, update the static_routes, if any, in the routing
                # instance with the new route target
                static_route_entries = ri.get_static_route_entries()
                if static_route_entries is None:
                    continue
                for static_route in static_route_entries.get_route() or []:
                    if old_rtgt_name in static_route.route_target:
                        static_route.route_target.remove(old_rtgt_name)
                        static_route.route_target.append(new_rtgt_name)
                    ri.set_static_route_entries(static_route_entries)
                    cls._vnc_lib.routing_instance_update(ri)

            # Updating the logical router referred by the route target with
            # new route target.
            for router_ref in route_tgt.obj.get_logical_router_back_refs() or []:
                lr = LogicalRouterST.get(':'.join(router_ref['to']))
                if lr:
                    logical_router = lr.obj
                else:
                    continue
                logical_router.del_route_target(old_rtgt_obj)
                logical_router.add_route_target(new_rtgt_obj.obj)
                cls._vnc_lib.logical_router_update(logical_router)

            RouteTargetST.delete_vnc_obj(old_rtgt_obj.get_fq_name()[0])

        cls._autonomous_system = int(new_asn)
        return True
    # end update_autonomous_system

    def evaluate(self):
        for router in BgpRouterST.values():
            router.update_global_asn(self._autonomous_system)
            router.update_peering()
        # end for router
        for router in LogicalRouterST.values():
            router.update_autonomous_system(self._autonomous_system)
        # end for router
        if self.obj.bgpaas_parameters:
            self.update_bgpaas_parameters(self.obj.bgpaas_parameters)
    # end evaluate
# end GlobalSystemConfigST