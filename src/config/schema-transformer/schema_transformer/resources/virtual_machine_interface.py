#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from vnc_api.gen.resource_xsd import AddressType, PortType
from vnc_api.gen.resource_xsd import VrfAssignTableType, MatchConditionType
from vnc_api.gen.resource_xsd import VrfAssignRuleType, SubnetType
import jsonpickle
import copy
from vnc_api.vnc_api import *
from cfgm_common.exceptions import NoIdError, RefsExistError, BadRequest
from schema_transformer.sandesh.st_introspect import ttypes as sandesh


class VirtualMachineInterfaceST(ResourceBaseST):
    _dict = {}
    obj_type = 'virtual_machine_interface'
    ref_fields = ['virtual_network', 'virtual_machine', 'port_tuple',
                  'logical_router', 'bgp_as_a_service', 'routing_instance']
    prop_fields = ['virtual_machine_interface_properties']

    def __init__(self, name, obj=None):
        self.name = name
        self.service_interface_type = None
        self.interface_mirror = None
        self.virtual_network = None
        self.virtual_machine = None
        self.port_tuples = set()
        self.logical_router = None
        self.bgp_as_a_service = None
        self.uuid = None
        self.instance_ips = set()
        self.floating_ips = set()
        self.alias_ips = set()
        self.routing_instances = {}
        self.update(obj)
        self.uuid = self.obj.uuid
        self.update_multiple_refs('instance_ip', self.obj)
        self.update_multiple_refs('floating_ip', self.obj)
        self.update_multiple_refs('alias_ip', self.obj)
        self.vrf_table = jsonpickle.encode(self.obj.get_vrf_assign_table())
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if 'virtual_machine_interface_properties' in changed:
            self.set_properties()
        if 'routing_instance' in changed:
            self.update_routing_instances(self.obj.get_routing_instance_refs())
        return changed
    # end update

    def delete_obj(self):
        self.update_single_ref('virtual_network', {})
        self.update_single_ref('virtual_machine', {})
        self.update_single_ref('logical_router', {})
        self.update_multiple_refs('instance_ip', {})
        self.update_multiple_refs('port_tuple', {})
        self.update_multiple_refs('floating_ip', {})
        self.update_multiple_refs('alias_ip', {})
        self.update_single_ref('bgp_as_a_service', {})
        self.update_routing_instances([])
    # end delete_obj

    def evaluate(self, **kwargs):
        self.set_virtual_network()
        self._add_pbf_rules()
        self.process_analyzer()
        self.recreate_vrf_assign_table()
    # end evaluate

    def is_left(self):
        return (self.service_interface_type == 'left')

    def is_right(self):
        return (self.service_interface_type == 'right')

    def get_any_instance_ip_address(self, ip_version=0):
        for ip_name in self.instance_ips:
            ip = ResourceBaseST.get_obj_type_map().get('instance_ip').get(ip_name)
            if ip is None or ip.instance_ip_address is None:
                continue
            if not ip.service_instance_ip:
                continue
            if not ip_version or ip.ip_version == ip_version:
                return ip.instance_ip_address
        return None
    # end get_any_instance_ip_address

    def get_primary_instance_ip_address(self, ip_version=4):
        for ip_name in self.instance_ips:
            ip = ResourceBaseST.get_obj_type_map().get('instance_ip').get(ip_name)
            if ip.is_primary() and ip.instance_ip_address and ip_version == ip.ip_version:
                return ip.instance_ip_address
        return None
    # end get_primary_instance_ip_address

    def set_properties(self):
        props = self.obj.get_virtual_machine_interface_properties()
        if props:
            service_interface_type = props.service_interface_type
            interface_mirror = props.interface_mirror
        else:
            service_interface_type = None
            interface_mirror = None
        ret = False
        if service_interface_type != self.service_interface_type:
            self.service_interface_type = service_interface_type
            ret = True
        if interface_mirror != self.interface_mirror:
            self.interface_mirror = interface_mirror
            ret = True
        return ret
    # end set_properties

    def update_routing_instances(self, ri_refs):
        routing_instances = dict((':'.join(ref['to']), ref['attr'])
                                 for ref in ri_refs or [])
        old_ri_set = set(self.routing_instances.keys())
        new_ri_set = set(routing_instances.keys())
        for ri_name in old_ri_set - new_ri_set:
            ri = ResourceBaseST.get_obj_type_map().get('routing_instance').get(ri_name)
            if ri:
                ri.virtual_machine_interfaces.discard(self.name)
        for ri_name in new_ri_set - old_ri_set:
            ri = ResourceBaseST.get_obj_type_map().get('routing_instance').get(ri_name)
            if ri:
                ri.virtual_machine_interfaces.add(self.name)
        self.routing_instances = routing_instances
    # end update_routing_instances

    def add_routing_instance(self, ri, pbf):
        if self.routing_instances.get(ri.name) == pbf:
            return
        self._vnc_lib.ref_update(
            'virtual-machine-interface', self.uuid, 'routing-instance',
            ri.obj.uuid, None, 'ADD', pbf)
        self.routing_instances[ri.name] = pbf
        ri.virtual_machine_interfaces.add(self.name)
    # end add_routing_instance

    def delete_routing_instance(self, ri):
        if ri.name not in self.routing_instances:
            return
        try:
            self._vnc_lib.ref_update(
                'virtual-machine-interface', self.uuid, 'routing-instance',
                ri.obj.uuid, None, 'DELETE')
        except NoIdError:
            # NoIdError could happen if RI is deleted while we try to remove
            # the link from VMI
            pass
        del self.routing_instances[ri.name]
        ri.virtual_machine_interfaces.discard(self.name)
    # end delete_routing_instance

    def get_virtual_machine_or_port_tuple(self):
        if self.port_tuples:
            pt_list = [ResourceBaseST.get_obj_type_map().get('port_tuple').get(x) for x in self.port_tuples if x is not None]
            return pt_list
        elif self.virtual_machine:
            vm = ResourceBaseST.get_obj_type_map().get('virtual_machine').get(self.virtual_machine)
            return [vm] if vm is not None else []
        return []
    # end get_service_instance

    def _add_pbf_rules(self):
        if not (self.is_left() or self.is_right()):
            return

        vm_pt_list = self.get_virtual_machine_or_port_tuple()
        for vm_pt in vm_pt_list:
            if vm_pt.get_service_mode() != 'transparent':
                return
            for service_chain in ResourceBaseST.get_obj_type_map().get('service_chain').values():
                if vm_pt.service_instance not in service_chain.service_list:
                    continue
                if not service_chain.created:
                    continue
                if self.is_left():
                    vn_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').locate(service_chain.left_vn)
                    vn1_obj = vn_obj
                else:
                    vn1_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').locate(service_chain.left_vn)
                    vn_obj = ResourceBaseST.get_obj_type_map().get('virtual_network').locate(service_chain.right_vn)

                service_name = vn_obj.get_service_name(service_chain.name,
                                                       vm_pt.service_instance)
                service_ri = ResourceBaseST.get_obj_type_map().get('routing_instance').get(service_name)
                v4_address, v6_address = vn1_obj.allocate_service_chain_ip(
                    service_name)
                vlan = self._object_db.allocate_service_chain_vlan(
                    vm_pt.uuid, service_chain.name)

                service_chain.add_pbf_rule(self, service_ri, v4_address,
                                           v6_address, vlan)
            #end for service_chain
        #end for vm_pt
    # end _add_pbf_rules

    def set_virtual_network(self):
        lr = ResourceBaseST.get_obj_type_map().get('logical_router').get(self.logical_router)
        if lr is not None:
            lr.update_virtual_networks()
    # end set_virtual_network

    def process_analyzer(self):
        if (self.interface_mirror is None or
            self.interface_mirror.mirror_to is None or
            self.virtual_network is None):
            return
        vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.virtual_network)
        if vn is None:
            return

        old_mirror_to = copy.deepcopy(self.interface_mirror.mirror_to)

        vn.process_analyzer(self.interface_mirror)

        if old_mirror_to == self.interface_mirror.mirror_to:
            return

        self.obj.set_virtual_machine_interface_properties(
            self.obj.get_virtual_machine_interface_properties())
        try:
            self._vnc_lib.virtual_machine_interface_update(self.obj)
        except NoIdError:
            self._logger.error("NoIdError while updating interface " +
                               self.name)
    # end process_analyzer

    def recreate_vrf_assign_table(self):
        if not (self.is_left() or self.is_right()):
            self._set_vrf_assign_table(None)
            return
        vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.virtual_network)
        if vn is None:
            self._set_vrf_assign_table(None)
            return
        vm_pt_list = self.get_virtual_machine_or_port_tuple()
        if not vm_pt_list:
            self._set_vrf_assign_table(None)
            return

        policy_rule_count = 0
        vrf_table = VrfAssignTableType()
        for vm_pt in vm_pt_list:
            smode = vm_pt.get_service_mode()
            if smode not in ['in-network', 'in-network-nat']:
                self._set_vrf_assign_table(None)
                return

            ip_list = []
            for ip_name in self.instance_ips:
                ip = ResourceBaseST.get_obj_type_map().get('instance_ip').get(ip_name)
                if ip and ip.instance_ip_address:
                    ip_list.append((ip.ip_version, ip.instance_ip_address))
            for ip_name in self.floating_ips:
                ip = ResourceBaseST.get_obj_type_map().get('floating_ip').get(ip_name)
                if ip and ip.floating_ip_address:
                    ip_list.append((ip.ip_version, ip.floating_ip_address))
            for ip_name in self.alias_ips:
                ip = ResourceBaseST.get_obj_type_map().get('alias_ip').get(ip_name)
                if ip and ip.alias_ip_address:
                    ip_list.append((ip.ip_version, ip.alias_ip_address))
            for (ip_version, ip_address) in ip_list:
                if ip_version == 6:
                    address = AddressType(subnet=SubnetType(ip_address, 128))
                else:
                    address = AddressType(subnet=SubnetType(ip_address, 32))

                mc = MatchConditionType(src_address=address,
                                        protocol='any',
                                        src_port=PortType(),
                                        dst_port=PortType())

                vrf_rule = VrfAssignRuleType(match_condition=mc,
                                             routing_instance=vn._default_ri_name,
                                             ignore_acl=False)
                vrf_table.add_vrf_assign_rule(vrf_rule)

            si_name = vm_pt.service_instance
            for service_chain_list in vn.service_chains.values():
                for service_chain in service_chain_list:
                    if not service_chain.created:
                        continue
                    service_list = service_chain.service_list
                    if si_name not in service_chain.service_list:
                        continue
                    if ((si_name == service_list[0] and self.is_left()) or
                        (si_name == service_list[-1] and self.is_right())):
                        # Do not generate VRF assign rules for 'book-ends'
                        continue
                    ri_name = vn.get_service_name(service_chain.name, si_name)
                    for sp in service_chain.sp_list:
                        for dp in service_chain.dp_list:
                            if self.is_left():
                                mc = MatchConditionType(src_port=dp,
                                                        dst_port=sp,
                                                        protocol=service_chain.protocol)
                            else:
                                mc = MatchConditionType(src_port=sp,
                                                        dst_port=dp,
                                                        protocol=service_chain.protocol)
                            vrf_rule = VrfAssignRuleType(match_condition=mc,
                                                         routing_instance=ri_name,
                                                         ignore_acl=True)
                            vrf_table.add_vrf_assign_rule(vrf_rule)
                            policy_rule_count += 1
                        # end for dp
                    # end for sp
                # end for service_chain
            # end for service_chain_list
        # end for vm_pt_list
        if policy_rule_count == 0:
            vrf_table = None
        self._set_vrf_assign_table(vrf_table)
    # end recreate_vrf_assign_table

    def _set_vrf_assign_table(self, vrf_table):
        vrf_table_pickle = jsonpickle.encode(vrf_table)
        if vrf_table_pickle != self.vrf_table:
            self.obj.set_vrf_assign_table(vrf_table)
            try:
                self._vnc_lib.virtual_machine_interface_update(self.obj)
                self.vrf_table = vrf_table_pickle
            except NoIdError as e:
                if e._unknown_id == self.uuid:
                    VirtualMachineInterfaceST.delete(self.name)
    # _set_vrf_assign_table

    def handle_st_object_req(self):
        resp = super(VirtualMachineInterfaceST, self).handle_st_object_req()
        resp.obj_refs.extend([
            self._get_sandesh_ref_list('instance_ip'),
            self._get_sandesh_ref_list('floating_ip'),
            self._get_sandesh_ref_list('alias_ip'),
        ])
        resp.properties = [
            sandesh.PropList('service_interface_type',
                             self.service_interface_type),
            sandesh.PropList('interface_mirror', str(self.interface_mirror)),
        ]
        return resp
    # end handle_st_object_req

    def get_v4_default_gateway(self):
        if not self.virtual_network:
            return None
        vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.virtual_network)
        if not vn:
            return None
        v4_address = self.get_primary_instance_ip_address(ip_version=4)
        if not v4_address:
            return None
        return vn.get_gateway(v4_address)
    # end get_v4_default_gateway

    def get_v6_default_gateway(self):
        if not self.virtual_network:
            return None
        vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.virtual_network)
        if not vn:
            return None
        v6_address = self.get_primary_instance_ip_address(ip_version=6)
        if not v6_address:
            return None
        return vn.get_gateway(v6_address)
    # end get_v6_default_gateway

    def get_ipv4_mapped_ipv6_gateway(self):
        return '::ffff:%s' % self.get_v4_default_gateway()
    # end get_ipv4_mapped_ipv6_gateway
# end VirtualMachineInterfaceST
