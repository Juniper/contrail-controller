#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from vnc_api.gen.resource_xsd import AddressType, ActionListType, PortType
from vnc_api.gen.resource_xsd import PolicyRuleType, PolicyEntriesType
from vnc_api.gen.resource_client import NetworkPolicy
from cfgm_common.exceptions import NoIdError, RefsExistError
from cfgm_common import svc_info
from schema_transformer.sandesh.st_introspect import ttypes as sandesh
from schema_transformer.utils import _create_pprinted_prop_list


class ServiceInstanceST(ResourceBaseST):
    _dict = {}
    obj_type = 'service_instance'
    ref_fields = ['virtual_machine', 'service_template']
    prop_fields = ['service_instance_properties']
    vn_dict = {}

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_machines = set()
        self.service_template = None
        self.auto_policy = False
        self.left_vn_str = None
        self.right_vn_str = None
        self.routing_policys = {}
        self.port_tuples = set()
        self.route_aggregates = {}
        self.update(obj)
        self.network_policys = ResourceBaseST.get_obj_type_map().get('network_policy').get_by_service_instance(self.name)
        self.route_tables = ResourceBaseST.get_obj_type_map().get('route_table').get_by_service_instance(self.name)
        for ref in self.obj.get_routing_policy_back_refs() or []:
            self.routing_policys[':'.join(ref['to'])] = ref['attr']
        for ref in self.obj.get_route_aggregate_back_refs() or []:
            self.route_aggregates[':'.join(ref['to'])] = ref['attr']['interface_type']
        self.set_children('port_tuple', self.obj)
    # end __init__

    def update(self, obj=None):
        self.unset_vn_si_mapping()
        changed = self.update_vnc_obj(obj)
        if 'service_template' in changed:
            st_refs = self.obj.get_service_template_refs()
            if st_refs:
                self.service_template = ':'.join(st_refs[0]['to'])
        if 'service_instance_properties' in changed:
            changed.remove('service_instance_properties')
            props = self.obj.get_service_instance_properties()
            if props:
                changed = self.add_properties(props)
        self.set_vn_si_mapping()
        return changed
    # end update

    def get_allocated_interface_ip(self, side, version):
        vm_pt_list = []
        for vm_name in self.virtual_machines or []:
            vm_pt_list.append(ResourceBaseST.get_obj_type_map().get('virtual_machine').get(vm_name))
        for pt_name in self.port_tuples or []:
            vm_pt_list.append(ResourceBaseST.get_obj_type_map().get('port_tuple').get(pt_name))
        for vm_pt in vm_pt_list:
            if not vm_pt:
                continue
            vmi = vm_pt.get_vmi_by_service_type(side)
            if not vmi:
                continue
            return vmi.get_any_instance_ip_address(version)
        return None

    def get_virtual_networks(self, si_props):
        left_vn = None
        right_vn = None

        st_refs = self.obj.get_service_template_refs()
        uuid = st_refs[0]['uuid']
        st_obj = ResourceBaseST().read_vnc_obj(uuid, obj_type='service_template')
        st_props = st_obj.get_service_template_properties()

        st_if_list = st_props.get_interface_type() or []
        si_if_list = si_props.get_interface_list() or []
        for (st_if, si_if) in zip(st_if_list, si_if_list):
            if st_if.get_service_interface_type() == 'left':
                left_vn = si_if.get_virtual_network()
            elif st_if.get_service_interface_type() == 'right':
                right_vn = si_if.get_virtual_network()

        if left_vn == "":
            parent_str = self.obj.get_parent_fq_name_str()
            left_vn = parent_str + ':' + svc_info.get_left_vn_name()
        if right_vn == "":
            parent_str = self.obj.get_parent_fq_name_str()
            right_vn = parent_str + ':' + svc_info.get_right_vn_name()

        return left_vn, right_vn
    # end get_si_vns

    def add_properties(self, props):
        left_vn_str, right_vn_str = self.get_virtual_networks(props)
        ret = (self.auto_policy == props.auto_policy)
        if (left_vn_str, right_vn_str) != (self.left_vn_str, self.right_vn_str):
            self.left_vn_str = left_vn_str
            self.right_vn_str = right_vn_str
            ret = True
        if not props.auto_policy:
            self.delete_properties()
            return ret
        self.auto_policy = True
        if (not self.left_vn_str or not self.right_vn_str):
            self._logger.error(
                "%s: route table next hop service instance must "
                "have left and right virtual networks" % self.name)
            self.delete_properties()
            return ret

        policy_name = "_internal_" + self.name
        addr1 = AddressType(virtual_network=self.left_vn_str)
        addr2 = AddressType(virtual_network=self.right_vn_str)
        action_list = ActionListType(apply_service=[self.name])

        prule = PolicyRuleType(direction="<>", protocol="any",
                               src_addresses=[addr1], dst_addresses=[addr2],
                               src_ports=[PortType()], dst_ports=[PortType()],
                               action_list=action_list)
        pentry = PolicyEntriesType([prule])
        policy_obj = NetworkPolicy(policy_name, network_policy_entries=pentry)
        policy = ResourceBaseST.get_obj_type_map().get('network_policy').locate(policy_name, policy_obj)
        policy.virtual_networks = set([self.left_vn_str, self.right_vn_str])

        policy.set_internal()
        vn1 = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.left_vn_str)
        if vn1:
            vn1.add_policy(policy_name)
        vn2 = ResourceBaseST.get_obj_type_map().get('virtual_network').get(self.right_vn_str)
        if vn2:
            vn2.add_policy(policy_name)
    # add_properties

    def set_vn_si_mapping(self):
        if self.left_vn_str:
            ServiceInstanceST.vn_dict.setdefault(self.left_vn_str,
                                                 set()).add(self)
    # end set_vn_si_mapping

    def unset_vn_si_mapping(self):
        if self.left_vn_str:
            try:
                ServiceInstanceST.vn_dict[self.left_vn_str].remove(self)
                if not ServiceInstanceST.vn_dict[self.left_vn_str]:
                    del ServiceInstanceST.vn_dict[self.left_vn_str]
            except KeyError:
                pass
    # end unset_vn_si_mapping

    @classmethod
    def get_vn_si_mapping(cls, vn_str):
        if vn_str:
            return ServiceInstanceST.vn_dict.get(vn_str)
        return None
    # end get_vn_si_mapping

    def delete_properties(self):
        policy_name = '_internal_' + self.name
        policy = ResourceBaseST.get_obj_type_map().get('network_policy').get(policy_name)
        if policy is None:
            return
        for vn_name in policy.virtual_networks:
            vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(vn_name)
            if vn is None:
                continue
            del vn.network_policys[policy_name]
        # end for vn_name
        ResourceBaseST.get_obj_type_map().get('network_policy').delete(policy_name)
    # end delete_properties

    def delete_obj(self):
        self.unset_vn_si_mapping()
        self.update_multiple_refs('virtual_machine', {})
        self.delete_properties()
    # end delete_obj

    def _update_service_template(self):
        if self.service_template is None:
            self._logger.error("service template is None for service instance "
                               + self.name)
            return
        try:
            st_obj = self.read_vnc_obj(fq_name=self.service_template,
                                       obj_type='service_template')
        except NoIdError:
            self._logger.error("NoIdError while reading service template " +
                               self.service_template)
            return
        st_props = st_obj.get_service_template_properties()
        self.service_mode = st_props.get_service_mode() or 'transparent'
        self.virtualization_type = st_props.get_service_virtualization_type()
    # end get_service_mode

    def get_service_mode(self):
        if hasattr(self, 'service_mode'):
            return self.service_mode
        self._update_service_template()
        return getattr(self, 'service_mode', None)
    # end get_service_mode

    def get_virtualization_type(self):
        if hasattr(self, 'virtualization_type'):
            return self.virtualization_type
        self._update_service_template()
        return getattr(self, 'virtualization_type', None)
    # end get_virtualization_type

    def handle_st_object_req(self):
        resp = super(ServiceInstanceST, self).handle_st_object_req()
        resp.obj_refs.extend([
            self._get_sandesh_ref_list('port_tuple'),
            self._get_sandesh_ref_list('network_policy'),
            self._get_sandesh_ref_list('route_table'),
        ])
        resp.properties.extend([
            sandesh.PropList('left_network', self.left_vn_str),
            sandesh.PropList('right_network', self.right_vn_str),
            _create_pprinted_prop_list('auto_policy', self.auto_policy),
            sandesh.PropList('service_mode', self.get_service_mode()),
            sandesh.PropList('virtualization_type',
                             self.get_virtualization_type()),
        ])
        return resp
    # end handle_st_object_req
# end ServiceInstanceST
