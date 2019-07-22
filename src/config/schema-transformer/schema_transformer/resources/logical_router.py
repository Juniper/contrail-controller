#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from vnc_api.gen.resource_xsd import RouteTargetList
import cfgm_common as common
from cfgm_common.exceptions import NoIdError, RefsExistError
from schema_transformer.sandesh.st_introspect import ttypes as sandesh


class LogicalRouterST(ResourceBaseST):
    _dict = {}
    obj_type = 'logical_router'
    ref_fields = ['virtual_machine_interface', 'route_table', 'bgpvpn']
    prop_fields = ['configured_route_target_list']
    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machine_interfaces = set()
        self.virtual_networks = set()
        self.route_tables = set()
        self.rt_list = set()
        self.configured_route_target_list = None
        self.bgpvpns = set()
        self.bgpvpn_rt_list = set()
        self.bgpvpn_import_rt_list = set()
        self.bgpvpn_export_rt_list = set()
        self.update_vnc_obj()
        self.logical_router_type = self.obj.get_logical_router_type()

        rt_ref = self.obj.get_route_target_refs()
        old_rt_key = None
        if rt_ref:
            rt_key = rt_ref[0]['to'][0]
            rtgt_num = int(rt_key.split(':')[-1])
            asn = ResourceBaseST.get_obj_type_map().get('global_system_config').get_autonomous_system()
            if rtgt_num < common.get_bgp_rtgt_min_id(asn):
                old_rt_key = rt_key
                rt_ref = None
        if not rt_ref:
            asn = ResourceBaseST.get_obj_type_map().get('global_system_config').get_autonomous_system()
            rtgt_num = self._object_db.alloc_route_target(name, asn, True)
            rt_key = "target:%s:%d" % (asn, rtgt_num)
            rtgt_obj = ResourceBaseST.get_obj_type_map().get('route_target').locate(rt_key)
            self.obj.set_route_target(rtgt_obj.obj)
            self._vnc_lib.logical_router_update(self.obj)

        if old_rt_key:
            ResourceBaseST.get_obj_type_map().get('route_target').delete_vnc_obj(old_rt_key)
        self.route_target = rt_key
    # end __init__

    def evaluate(self):
        self.update_virtual_networks()
        self.set_route_target_list()
    # end evaluate

    def delete_obj(self):
        self.update_multiple_refs('virtual_machine_interface', {})
        self.update_multiple_refs('route_table', {})
        self.update_multiple_refs('bgpvpn', {})
        self.update_virtual_networks()
        rtgt_num = int(self.route_target.split(':')[-1])
        self.delete_route_targets([self.route_target])
        asn = ResourceBaseST.get_obj_type_map().get('global_system_config').get_autonomous_system()
        self._object_db.free_route_target(self.name, asn)
    # end delete_obj

    def update_virtual_networks(self):
        vn_set = set()
        for vmi in self.virtual_machine_interfaces:
            vmi_obj = ResourceBaseST.get_obj_type_map().get('virtual_machine_interface').get(vmi)
            if vmi_obj is not None:
                vn_set.add(vmi_obj.virtual_network)
        if vn_set == self.virtual_networks:
            return
        self.set_virtual_networks(vn_set)
    # end update_virtual_networks

    def set_virtual_networks(self, vn_set):
        # do not add RT assigned to LR to the VN
        # when vxlan_routing is enabled
        if self.logical_router_type == 'vxlan-routing':
            self.virtual_networks = vn_set
            return
        for vn in self.virtual_networks - vn_set:
            vn_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                # error due to inconsistency in db
                if ri_obj is None:
                    self._logger.error(
                        "Primary RI is None for VN: " + str(vn_obj.name))
                    continue
                rt_del = (set([self.route_target]) |
                          self.rt_list |
                          self.bgpvpn_rt_list |
                          self.bgpvpn_import_rt_list |
                          self.bgpvpn_export_rt_list)
                ri_obj.update_route_target_list(rt_add=set(), rt_del=rt_del)
                self.delete_route_targets(rt_del - set([self.route_target]))
        for vn in vn_set - self.virtual_networks:
            vn_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                rt_add = (self.rt_list |
                          set([self.route_target]) |
                          self.bgpvpn_rt_list)
                ri_obj.update_route_target_list(
                    rt_add=rt_add,
                    rt_add_import=self.bgpvpn_import_rt_list,
                    rt_add_export=self.bgpvpn_export_rt_list)
        self.virtual_networks = vn_set
    # end set_virtual_networks

    def update_autonomous_system(self, asn):
        old_rt = self.route_target
        old_asn = int(old_rt.split(':')[1])
        if int(asn) == old_asn:
            return
        rtgt_num = int(old_rt.split(':')[-1])
        rt_key = "target:%s:%d" % (asn, rtgt_num)
        rtgt_obj = ResourceBaseST.get_obj_type_map().get('route_target').locate(rt_key)
        try:
            obj = self.read_vnc_obj(fq_name=self.name)
            obj.set_route_target(rtgt_obj.obj)
            self._vnc_lib.logical_router_update(obj)
        except NoIdError:
            self._logger.error(
                "NoIdError while accessing logical router %s" % self.name)

        # We need to execute this code only in case of SNAT routing.
        # If vxlan_routing is enabled, LR RTs will not have a back_ref
        # to the Routing Instance of all the connected VNs
        if not self.vxlan_routing:
            for vn in self.virtual_networks:
                vn_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').get(vn)
                if vn_obj is not None:
                    ri_obj = vn_obj.get_primary_routing_instance()
                    ri_obj.update_route_target_list(rt_del=[old_rt],
                                                    rt_add=[rt_key])
        self.delete_route_targets([old_rt])
        self.route_target = rt_key
    # end update_autonomous_system

    def set_route_target_list(self):
        old_rt_list = self.rt_list.copy()
        old_bgpvpn_rt_list = self.bgpvpn_rt_list.copy()
        old_bgpvpn_import_rt_list = self.bgpvpn_import_rt_list.copy()
        old_bgpvpn_export_rt_list = self.bgpvpn_export_rt_list.copy()

        # Set the system allocated route target
        config_rt_list = self.configured_route_target_list\
            or RouteTargetList()
        self.rt_list = set(config_rt_list.get_route_target())

        # Get BGP VPN's route targets associated to that router
        self.bgpvpn_rt_list = set()
        self.bgpvpn_import_rt_list = set()
        self.bgpvpn_export_rt_list = set()
        for bgpvpn_name in self.bgpvpns:
            bgpvpn = ResourceBaseST.get_obj_type_map().get('bgpvpn').get(bgpvpn_name)
            if bgpvpn is not None:
                self.bgpvpn_rt_list |= bgpvpn.rt_list
                self.bgpvpn_import_rt_list |= bgpvpn.import_rt_list
                self.bgpvpn_export_rt_list |= bgpvpn.export_rt_list

        rt_add = ((self.rt_list - old_rt_list) |
                  (self.bgpvpn_rt_list - old_bgpvpn_rt_list))
        rt_add_import = self.bgpvpn_import_rt_list - old_bgpvpn_import_rt_list
        rt_add_export = self.bgpvpn_export_rt_list - old_bgpvpn_export_rt_list
        rt_del = (
            ((old_rt_list - self.rt_list) |
             (old_bgpvpn_rt_list - self.bgpvpn_rt_list) |
             (old_bgpvpn_import_rt_list - self.bgpvpn_import_rt_list) |
             (old_bgpvpn_export_rt_list - self.bgpvpn_export_rt_list)) -
            (self.rt_list | self.bgpvpn_rt_list | self.bgpvpn_import_rt_list |
             self.bgpvpn_export_rt_list)
        )
        if not (rt_add or rt_add_import or rt_add_export or rt_del):
            return

        if self.logical_router_type == 'vxlan-routing':
            vn = ':'.join((self.obj.fq_name[:-1] +
                             [common.get_lr_internal_vn_name(self.obj.uuid)]))
            vn_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                ri_obj.update_route_target_list(rt_add=rt_add,
                                            rt_add_import=rt_add_import,
                                            rt_add_export=rt_add_export,
                                            rt_del=rt_del)
        else:
            for vn in self.virtual_networks:
                vn_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').get(vn)
                if vn_obj is not None:
                    ri_obj = vn_obj.get_primary_routing_instance()
                    ri_obj.update_route_target_list(rt_add=rt_add,
                                                rt_add_import=rt_add_import,
                                                rt_add_export=rt_add_export,
                                                rt_del=rt_del)
        self.delete_route_targets(rt_del)
    # end set_route_target_list

    @staticmethod
    def delete_route_targets(route_targets=None):
        for rt in route_targets or []:
            try:
                ResourceBaseST.get_obj_type_map().get('route_target').delete_vnc_obj(rt)
            except RefsExistError:
                pass

    def handle_st_object_req(self):
        resp = super(LogicalRouterST, self).handle_st_object_req()
        resp.obj_refs.extend([
            sandesh.RefList('route_target', self.rt_list),
        ])
        resp.properties.extend([
            sandesh.PropList('route_target', self.route_target),
            sandesh.PropList('bgpvpn_router_target_list',
                             ', '.join(self.bgpvpn_rt_list)),
            sandesh.PropList('bgpvpn_import_route_targt_list',
                             ', '.join(self.bgpvpn_import_rt_list)),
            sandesh.PropList('bgpvpn_export_route_target_list',
                             ', '.join(self.bgpvpn_export_rt_list)),
        ])
        return resp
    # end handle_st_object_req
# end LogicalRouterST
