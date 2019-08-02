#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from vnc_api.gen.resource_client import RoutingInstance, RouteTarget
from vnc_api.gen.resource_xsd import  ServiceChainInfo
from vnc_api.gen.resource_xsd import StaticRouteEntriesType, InstanceTargetType
from vnc_api.gen.resource_xsd import ConnectionType
from vnc_api.gen.resource_xsd import StaticRouteType
from cfgm_common.uve.virtual_network.ttypes import\
    UveServiceChain, UveServiceChainData
from netaddr import IPNetwork, IPAddress
import cfgm_common as common
from cfgm_common.exceptions import NoIdError, RefsExistError, BadRequest
from schema_transformer.sandesh.st_introspect import ttypes as sandesh

# a struct to store attributes related to Routing Instance needed by
# schema transformer


class RoutingInstanceST(ResourceBaseST):
    _dict = {}
    obj_type = 'routing_instance'

    def __init__(self, name, obj=None):
        self.name = name
        self.obj = obj or self.read_vnc_obj(fq_name=name)
        self.stale_route_targets = []
        self.service_chain = self._get_service_id_from_ri(self.name)
        self.connections = set()
        self.virtual_network = None
        self.is_default = self.obj.get_routing_instance_is_default()
        self.add_to_parent(self.obj)
        self.route_target = None
        self.routing_policys = {}
        self.route_aggregates = set()
        self.service_chain_info = self.obj.get_service_chain_information()
        self.v6_service_chain_info = self.obj.get_ipv6_service_chain_information()
        if self.obj.get_fq_name() in (common.IP_FABRIC_RI_FQ_NAME,
                                      common.LINK_LOCAL_RI_FQ_NAME):
            return
        self.locate_route_target()
        for ri_ref in self.obj.get_routing_instance_refs() or []:
            conn_fq_name = ':'.join(ri_ref['to'])
            if conn_fq_name != 'ERROR':
                self.connections.add(conn_fq_name)
            else:
                self._logger.debug("Invalid connection detected in RI " + name)
                # Remove the connection ref in the API server as well.
                try:
                    self._vnc_lib.ref_update('routing-instance', self.obj.uuid,
                                             'routing-instance', ri_ref['uuid'],
                                             None, 'DELETE')
                except NoIdError as e:
                    self._logger.debug("Ref not found in DB for RI " + name)
                    self._logger.debug(e)
        if self.is_default:
            vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.virtual_network)
            if vn is None:
                return

            for network in vn.expand_connections():
                vn.add_ri_connection(network, self)
            # if primary RI is connected to another primary RI, we need to
            # also create connection between the VNs
            for connection in self.connections:
                remote_ri_fq_name = connection.split(':')
                if remote_ri_fq_name[-1] == remote_ri_fq_name[-2]:
                    vn.connections.add(':'.join(remote_ri_fq_name[0:-1]))
        vmi_refs = self.obj.get_virtual_machine_interface_back_refs() or []
        self.virtual_machine_interfaces = set([':'.join(ref['to'])
                                              for ref in vmi_refs])
        self.update_multiple_refs('route_aggregate', self.obj)
        for ref in self.obj.get_routing_policy_back_refs() or []:
            rp_name = ':'.join(ref['to'])
            self.routing_policys[rp_name] = ref['attr']['sequence']
        if self.is_default:
            self.update_static_routes()
    # end __init__

    def update(self, obj=None):
        # Nothing to do
        return False

    @classmethod
    def create(cls, fq_name, vn_obj, has_pnf=False):
        try:
            name = fq_name.split(':')[-1]
            ri_obj = RoutingInstance(name, parent_obj=vn_obj.obj,
                                     routing_instance_has_pnf=has_pnf)
            cls._vnc_lib.routing_instance_create(ri_obj)
            return ri_obj
        except RefsExistError:
            return cls.read_vnc_obj(fq_name=fq_name)
    # end create

    @staticmethod
    def _get_service_id_from_ri(name):
        ri_name = name.split(':')[-1]
        if not ri_name.startswith('service-'):
            return None
        return ri_name[8:44]
    # end _get_service_id_from_ri

    def update_routing_policy_and_aggregates(self):
        if not self.service_chain:
            return
        sc = ResourceBaseST.get_obj_type_map().get('service_chain').get(self.service_chain)
        if sc is None:
            return
        for si_name in sc.service_list:
            if not self.name.endswith(si_name.replace(':', '_')):
                continue
            si = ResourceBaseST.get_obj_type_map().get('service_instance').get(si_name)
            if si is None:
                return
            if sc.left_vn == self.virtual_network:
                rp_dict = dict((rp, attr.get_left_sequence())
                               for rp, attr in si.routing_policys.items()
                               if attr.get_left_sequence())
                ra_set = set(ra for ra, if_type in si.route_aggregates.items()
                             if if_type == 'left')
            elif sc.right_vn == self.virtual_network:
                rp_dict = dict((rp, attr.get_right_sequence())
                               for rp, attr in si.routing_policys.items()
                               if attr.get_right_sequence())
                ra_set = set(ra for ra, if_type in si.route_aggregates.items()
                             if if_type == 'right')
            else:
                break
            for rp_name in self.routing_policys:
                if rp_name not in rp_dict:
                    rp = ResourceBaseST.get_obj_type_map().get('routing_policy').get(rp_name)
                    if rp:
                        rp.delete_routing_instance(self)
            for rp_name, seq in rp_dict.items():
                if (rp_name not in self.routing_policys or
                    seq != self.routing_policys[rp_name]):
                    rp = ResourceBaseST.get_obj_type_map().get('routing_policy').get(rp_name)
                    if rp:
                        rp.add_routing_instance(self, seq)
            self.routing_policys = rp_dict
            for ra_name in self.route_aggregates - ra_set:
                ra = ResourceBaseST.get_obj_type_map().get('route_aggregate').get(ra_name)
                if ra:
                    ra.delete_routing_instance(self)
            for ra_name in ra_set - self.route_aggregates:
                ra = ResourceBaseST.get_obj_type_map().get('route_aggregate').get(ra_name)
                if ra:
                    ra.add_routing_instance(self)
            self.route_aggregates = ra_set
    # end update_routing_policy_and_aggregates

    def import_default_ri_route_target_to_service_ri(self):
        update_ri = False
        if not self.service_chain:
            return update_ri
        sc = ResourceBaseST.get_obj_type_map().get('service_chain').get(self.service_chain)
        if sc is None or not sc.created:
            return update_ri
        left_vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(sc.left_vn)
        right_vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(sc.right_vn)
        if left_vn is None or right_vn is None:
            self._logger.debug("left or right vn not found for RI " + self.name)
            return update_ri

        right_si_name = sc.service_list[-1]
        right_si = ResourceBaseST.get_obj_type_map().get('service_instance').get(right_si_name)
        multi_policy_enabled = (
            left_vn.multi_policy_service_chains_enabled and
            right_vn.multi_policy_service_chains_enabled and
            right_si.get_service_mode() != 'in-network-nat')

        if not multi_policy_enabled:
            return update_ri
        vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.virtual_network)
        if sc.left_vn == vn.name:
            si_name = sc.service_list[0]
        elif sc.right_vn == vn.name:
            si_name = sc.service_list[-1]
        else:
            return update_ri
        service_ri_name = vn.get_service_name(sc.name, si_name)
        if service_ri_name == self.name:
            rt = vn.get_route_target()
            if rt not in self.stale_route_targets:
                rt_obj = ResourceBaseST.get_obj_type_map().get('route_target').get(rt)
                self.obj.add_route_target(rt_obj.obj,
                                          InstanceTargetType('import'))
                update_ri = True
            else:
                self.stale_route_targets.remove(rt)
        return update_ri
    # end import_default_ri_route_target_to_service_ri

    def locate_route_target(self):
        old_rtgt = self._object_db.get_route_target(self.name)
        asn = ResourceBaseST.get_obj_type_map().get('global_system_config').get_autonomous_system()
        rtgt_num = self._object_db.alloc_route_target(self.name, asn)

        rt_key = "target:%s:%d" % (asn, rtgt_num)
        rtgt_obj = ResourceBaseST.get_obj_type_map().get('route_target').locate(rt_key).obj
        if self.is_default:
            inst_tgt_data = InstanceTargetType()
        elif ResourceBaseST.get_obj_type_map().get('virtual_network')._ri_needs_external_rt(self.virtual_network, self.name):
            inst_tgt_data = InstanceTargetType(import_export="export")
        else:
            inst_tgt_data = None

        vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.virtual_network)
        if vn is None:
            self._logger.error("Parent VN not found for RI: " + self.name)
            return

        try:
            if self.obj.parent_uuid != vn.obj.uuid:
                # Stale object. Delete it.
                self._vnc_lib.routing_instance_delete(id=self.obj.uuid)
                self.obj = None
            else:
                update_ri = False
                self.stale_route_targets = [':'.join(rt_ref['to'])
                        for rt_ref in self.obj.get_route_target_refs() or []]
                if rt_key not in self.stale_route_targets:
                    self.obj.add_route_target(rtgt_obj, InstanceTargetType())
                    update_ri = True
                else:
                    self.stale_route_targets.remove(rt_key)
                if inst_tgt_data:
                    for rt in vn.rt_list | vn.bgpvpn_rt_list:
                        if rt not in self.stale_route_targets:
                            rtgt_obj = ResourceBaseST.get_obj_type_map().get('route_target').locate(rt)
                            self.obj.add_route_target(rtgt_obj.obj, inst_tgt_data)
                            update_ri = True
                        else:
                            self.stale_route_targets.remove(rt)
                    if self.is_default:
                        for rt in (vn.export_rt_list |
                                   vn.bgpvpn_export_rt_list):
                            if rt not in self.stale_route_targets:
                                rtgt_obj = ResourceBaseST.get_obj_type_map().get('route_target').locate(rt)
                                self.obj.add_route_target(
                                    rtgt_obj.obj, InstanceTargetType('export'))
                                update_ri = True
                            else:
                                self.stale_route_targets.remove(rt)
                        for rt in (vn.import_rt_list |
                                   vn.bgpvpn_import_rt_list):
                            if rt not in self.stale_route_targets:
                                rtgt_obj = ResourceBaseST.get_obj_type_map().get('route_target').locate(rt)
                                self.obj.add_route_target(
                                    rtgt_obj.obj, InstanceTargetType('import'))
                                update_ri = True
                            else:
                                self.stale_route_targets.remove(rt)
                    elif vn.allow_transit:
                        if vn.get_route_target() not in self.stale_route_targets:
                            rtgt_obj = RouteTarget(vn.get_route_target())
                            self.obj.add_route_target(rtgt_obj, inst_tgt_data)
                            update_ri = True
                        else:
                            self.stale_route_targets.remove(vn.get_route_target())
                update_ri |= self.import_default_ri_route_target_to_service_ri()
                if update_ri:
                    try:
                        self._vnc_lib.routing_instance_update(self.obj)
                    except Exception as e:
                        # error due to inconsistency in db
                        self._logger.error(
                            "Error while updating routing instance: " + str(e))
                    return
        except NoIdError as e:
            self._logger.error(
                "Error while updating routing instance: " + str(e))
            raise
            return

        self.route_target = rt_key
        if self.is_default:
            vn.set_route_target(rt_key)

        asn = ResourceBaseST.get_obj_type_map().get('global_system_config').get_autonomous_system()
        if 0 < old_rtgt < common.get_bgp_rtgt_min_id(asn):
            rt_key = "target:%s:%d" % (asn, old_rtgt)
            ResourceBaseST.get_obj_type_map().get('route_target').delete_vnc_obj(rt_key)
    # end locate_route_target

    def get_fq_name(self):
        return self.obj.get_fq_name()
    # end get_fq_name

    def get_fq_name_str(self):
        return self.obj.get_fq_name_str()
    # end get_fq_name_str

    def add_connection(self, ri2):
        self.connections.add(ri2.name)
        ri2.connections.add(self.name)

        conn_data = ConnectionType()
        self._vnc_lib.ref_update('routing-instance', self.obj.uuid,
                                 'routing-instance', ri2.obj.uuid,
                                 None, 'ADD', conn_data)
    # end add_connection

    def delete_connection(self, ri2):
        self.connections.discard(ri2.name)
        ri2.connections.discard(self.name)
        try:
            self._vnc_lib.ref_update('routing-instance', self.obj.uuid,
                                     'routing-instance', ri2.obj.uuid,
                                     None, 'DELETE')
        except NoIdError:
            return
    # end delete_connection

    def fill_service_info(self, service_info, ip_version, remote_vn,
                          service_instance, address, source_ri,
                          service_chain_id):
        if service_info is None:
            service_info = ServiceChainInfo(service_chain_id=service_chain_id)
        if service_instance:
            service_info.set_service_instance(service_instance)
        if address:
            service_info.set_routing_instance(
                remote_vn.get_primary_routing_instance().get_fq_name_str())
            service_info.set_service_chain_address(address)
        if source_ri:
            service_info.set_source_routing_instance(source_ri)
        prefixes = remote_vn.get_prefixes(ip_version)
        service_info.set_prefix(prefixes)
        if service_info.get_service_chain_address() is None:
            self._logger.error(
                "service chain ip address is None for: " + service_instance)
            return None
        return service_info
    # fill_service_info

    def add_service_info(self, remote_vn, service_instance=None,
                         v4_address=None, v6_address=None, source_ri=None,
                         service_chain_id=None):
        v4_info = self.obj.get_service_chain_information()
        v4_info = self.fill_service_info(v4_info, 4, remote_vn,
                                         service_instance, v4_address,
                                         source_ri, service_chain_id)
        self.service_chain_info = v4_info
        self.obj.set_service_chain_information(v4_info)
        v6_info = self.obj.get_ipv6_service_chain_information()
        v6_info = self.fill_service_info(v6_info, 6, remote_vn,
                                         service_instance, v6_address,
                                         source_ri, service_chain_id)
        self.v6_service_chain_info = v6_info
        self.obj.set_ipv6_service_chain_information(v6_info)
    # end add_service_info

    def update_route_target_list(self, rt_add=None, rt_add_import=None,
                                 rt_add_export=None, rt_del=None):
        update = False
        for rt in rt_del or []:
            if rt in self.stale_route_targets:
                self.stale_route_targets.remove(rt)
            rtgt_obj = RouteTarget(rt)
            self.obj.del_route_target(rtgt_obj)
            update = True
        for rt in rt_add or []:
            if rt not in self.stale_route_targets:
                rtgt_obj = ResourceBaseST.get_obj_type_map().get('route_target').locate(rt).obj
                inst_tgt_data = InstanceTargetType(import_export=None)
                self.obj.add_route_target(rtgt_obj, inst_tgt_data)
                update = True
            else:
                self.stale_route_targets.remove(rt)
        for rt in rt_add_import or []:
            if rt not in self.stale_route_targets:
                rtgt_obj = ResourceBaseST.get_obj_type_map().get('route_target').locate(rt).obj
                inst_tgt_data = InstanceTargetType(import_export='import')
                self.obj.add_route_target(rtgt_obj, inst_tgt_data)
                update = True
            else:
                self.stale_route_targets.remove(rt)
        for rt in rt_add_export or []:
            if rt not in self.stale_route_targets:
                rtgt_obj = ResourceBaseST.get_obj_type_map().get('route_target').locate(rt).obj
                inst_tgt_data = InstanceTargetType(import_export='export')
                self.obj.add_route_target(rtgt_obj, inst_tgt_data)
                update = True
            else:
                self.stale_route_targets.remove(rt)
        if update:
            try:
                self._vnc_lib.routing_instance_update(self.obj)
            except NoIdError:
                return
    # end update_route_target_list

    def update_static_routes(self):
        if not self.is_default:
            return
        old_static_routes = self.obj.get_static_route_entries()
        static_routes = StaticRouteEntriesType()
        old_route_target_list = set()
        if old_static_routes:
            for sr in old_static_routes.get_route():
                old_route_target_list |= set(sr.route_target or [])
        all_route_targets = set()
        si_set = ResourceBaseST.get_obj_type_map().get('service_instance').get_vn_si_mapping(self.virtual_network)
        for si in si_set or []:
            mode = si.get_service_mode()
            if mode is None or mode != 'in-network-nat':
                self._logger.debug("service mode for %s-%s, skip" % (si.name, mode))
                continue
            route_tables = ResourceBaseST.get_obj_type_map().get('route_table').get_by_service_instance(si.name)
            sc_address = {4: si.get_allocated_interface_ip("left", 4),
                          6: si.get_allocated_interface_ip("left", 6)}
            for route_table_name in route_tables:
                route_table = ResourceBaseST.get_obj_type_map().get('route_table').get(route_table_name)
                if route_table is None or not route_table.routes:
                    self._logger.debug("route table/routes None for: " +
                                       route_table_name)
                    continue
                route_targets = set()
                for lr_name in route_table.logical_routers:
                    lr = ResourceBaseST.get_obj_type_map().get('logical_router').get(lr_name)
                    if lr is None:
                        self._logger.debug("lr is None for: " + lr_name)
                        continue
                    route_targets = route_targets | lr.rt_list | set([lr.route_target])
                if not route_targets:
                    self._logger.debug("route targets None for: " + route_table_name)
                    continue
                for route in route_table.routes:
                    if route.next_hop != si.name:
                        continue
                    version = IPNetwork(route.prefix).version
                    if not sc_address[version]:
                        continue
                    static_route = StaticRouteType(prefix=route.prefix,
                                                   next_hop=sc_address[version],
                                                   route_target=list(route_targets))
                    static_routes.add_route(static_route)
                    all_route_targets |= route_targets

        if old_route_target_list != all_route_targets and si_set is not None:
            self.update_route_target_list(
                    rt_add_import=all_route_targets - old_route_target_list,
                    rt_del=old_route_target_list - all_route_targets)

        #update static ip routes
        vn_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.virtual_network)
        for rt_name in vn_obj.route_tables:
            route_table = ResourceBaseST.get_obj_type_map().get('route_table').get(rt_name)
            if route_table is None or route_table.routes is None:
                continue
            for route in route_table.routes or []:
                if route.next_hop_type == "ip-address":
                    cattr = route.get_community_attributes()
                    communities = cattr.community_attribute if cattr else None
                    static_route = StaticRouteType(prefix=route.prefix,
                                                   next_hop=route.next_hop,
                                                   community=communities)
                    static_routes.add_route(static_route)
            # end for route
        # end for route_table
        if static_routes != old_static_routes:
            self.obj.set_static_route_entries(static_routes)
            try:
                self._vnc_lib.routing_instance_update(self.obj)
            except NoIdError:
                pass
    # end update_static_routes

    def delete_obj(self):
        for ri2_name in self.connections:
            ri2 = RoutingInstanceST.get(ri2_name)
            if ri2:
                ri2.connections.discard(self.name)

        asn = ResourceBaseST.get_obj_type_map().get('global_system_config').get_autonomous_system()
        rtgt_list = self.obj.get_route_target_refs()
        self._object_db.free_route_target(self.name, asn)

        service_chain = self.service_chain
        vn_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.virtual_network)
        if vn_obj is not None and service_chain is not None:
            vn_obj.free_service_chain_ip(self.obj.name)

            uve = UveServiceChainData(name=service_chain, deleted=True)
            uve_msg = UveServiceChain(data=uve, sandesh=self._sandesh)
            uve_msg.send(sandesh=self._sandesh)

            for vmi_name in list(self.virtual_machine_interfaces):
                vmi = ResourceBaseST.get_obj_type_map().get('virtual_machine_interface').get(vmi_name)
                if vmi:
                    vm_pt_list = vmi.get_virtual_machine_or_port_tuple()
                    for vm_pt in vm_pt_list:
                        self._object_db.free_service_chain_vlan(vm_pt.uuid,
                                                             service_chain)
                    vmi.delete_routing_instance(self)
            # end for vmi_name

        for rp_name in self.routing_policys:
            rp = ResourceBaseST.get_obj_type_map().get('routing_policy').get(rp_name)
            if rp:
                rp.delete_routing_instance(self)
            else:
                try:
                    rp_obj = ResourceBaseST.get_obj_type_map().get('routing_policy').read_vnc_obj(fq_name=rp_name)
                    if rp_obj:
                        rp_obj.del_routing_instance(self.obj)
                        self._vnc_lib.routing_policy_update(rp_obj)
                except NoIdError:
                    pass
        for ra_name in self.route_aggregates:
            ra = ResourceBaseST.get_obj_type_map().get('route_aggregate').get(ra_name)
            if ra:
                ra.delete_routing_instance(self)
            else:
                try:
                    ra_obj = ResourceBaseST.get_obj_type_map().get('route_aggregate').read_vnc_obj(fq_name=ra_name)
                    if ra_obj:
                        ra_obj.del_routing_instance(self.obj)
                        self._vnc_lib.route_aggregate_update(ra_obj)
                except NoIdError:
                    pass
        self.routing_policys = {}
        self.route_aggregates = set()
        bgpaas_server_name = self.obj.get_fq_name_str() + ':bgpaas-server'
        bgpaas_server = ResourceBaseST.get_obj_type_map().get('bgp_router').get(bgpaas_server_name)
        if bgpaas_server:
            try:
                self._vnc_lib.bgp_router_delete(id=bgpaas_server.obj.uuid)
            except NoIdError:
                pass
            ResourceBaseST.get_obj_type_map().get('bgp_router').delete(bgpaas_server_name)
        try:
            # The creation/deletion of the default virtual network routing
            # instance is handled by the vnc api now
            if not self.is_default:
                ResourceBaseST._vnc_lib.routing_instance_delete(id=self.obj.uuid)

        except NoIdError:
            pass

        for rtgt in rtgt_list or []:
            try:
                ResourceBaseST.get_obj_type_map().get('route_target').delete_vnc_obj(rtgt['to'][0])
            except RefsExistError:
                # if other routing instances are referring to this target,
                # it will be deleted when those instances are deleted
                pass
    # end delete_obj

    @classmethod
    def delete(cls, key, delete_vnc_obj=False):
        obj = cls.get(key)
        if obj is None:
            return
        if obj.is_default or delete_vnc_obj:
            obj.delete_obj()
            del cls._dict[key]

    def handle_st_object_req(self):
        resp = super(RoutingInstanceST, self).handle_st_object_req()
        resp.obj_refs = [
            self._get_sandesh_ref_list('virtual_network'),
            sandesh.RefList('routing_instance', self.connections),
        ]
        resp.properties = [
            sandesh.PropList('service_chain', self.service_chain),
            sandesh.PropList('is_default', str(self.is_default)),
            sandesh.PropList('route_target', self.route_target),
        ]
        return resp
    # end handle_st_object_req
# end class RoutingInstanceST
