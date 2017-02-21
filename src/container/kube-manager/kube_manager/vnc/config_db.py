#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for kube manager
"""
import json

from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common.vnc_db import DBBase
from bitstring import BitArray

INVALID_VLAN_ID = 4096
MAX_VLAN_ID = 4095

class DBBaseKM(DBBase):
    obj_type = __name__
    _fq_name_to_uuid = {}
    _nested_mode = False

    def __init__(self, uuid, obj_dict=None):
        self._fq_name_to_uuid[tuple(obj_dict['fq_name'])] = uuid

    @classmethod
    def get_fq_name_to_uuid(cls, fq_name):
        return cls._fq_name_to_uuid.get(tuple(fq_name))

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        del cls._fq_name_to_uuid[tuple(obj.fq_name)]

    def evaluate(self):
        # Implement in the derived class
        pass

    @staticmethod
    def is_nested ():
        """Return nested mode enable/disable config value."""
        return DBBaseKM._nested_mode

    @staticmethod
    def set_nested (val):
        """Configured nested mode value.

        True : Enable nested mode.
        False : Disable nested mode.
        """
        DBBaseKM._nested_mode = val

class LoadbalancerKM(DBBaseKM):
    _dict = {}
    obj_type = 'loadbalancer'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.loadbalancer_listeners = set()
        self.selectors = None
        self.annotations = None
        obj_dict = self.update(obj_dict)
        super(LoadbalancerKM, self).__init__(uuid, obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.parent_uuid = obj['parent_uuid']
        self.annotations = obj.get('annotations', None)
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('loadbalancer_listener', obj)
        return obj

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.update_multiple_refs('loadbalancer_listener', {})
        super(LoadbalancerKM, cls).delete(uuid)
        del cls._dict[uuid]

class LoadbalancerListenerKM(DBBaseKM):
    _dict = {}
    obj_type = 'loadbalancer_listener'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.loadbalancer = None
        self.loadbalancer_pool = None
        self.target_port = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.display_name = obj.get('display_name', None)
        self.parent_uuid = obj['parent_uuid']
        self.id_perms = obj.get('id_perms', None)
        self.params = obj.get('loadbalancer_listener_properties', None)
        self.update_single_ref('loadbalancer', obj)
        self.update_single_ref('loadbalancer_pool', obj)
        self.annotations = obj.get('annotations', None)
        if self.annotations:
            for kvp in self.annotations['key_value_pair'] or []:
                if kvp['key'] == 'targetPort':
                    self.target_port = kvp['value']
                    break
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('loadbalancer', {})
        obj.update_single_ref('loadbalancer_pool', {})
        del cls._dict[uuid]
    # end delete
# end class LoadbalancerListenerKM

class LoadbalancerPoolKM(DBBaseKM):
    _dict = {}
    obj_type = 'loadbalancer_pool'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.members = set()
        self.loadbalancer_listener = None
        self.custom_attributes = []
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.params = obj.get('loadbalancer_pool_properties', None)
        self.provider = obj.get('loadbalancer_pool_provider', None)
        self.annotations = obj.get('annotations', None)
        kvpairs = obj.get('loadbalancer_pool_custom_attributes', None)
        if kvpairs:
            self.custom_attributes = kvpairs.get('key_value_pair', [])
        self.members = set([lm['uuid']
                            for lm in obj.get('loadbalancer_members', [])])
        self.id_perms = obj.get('id_perms', None)
        self.parent_uuid = obj['parent_uuid']
        self.display_name = obj.get('display_name', None)
        self.update_single_ref('loadbalancer_listener', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('loadbalancer_listener', {})
        del cls._dict[uuid]
    # end delete
# end class LoadbalancerPoolKM

class LoadbalancerMemberKM(DBBaseKM):
    _dict = {}
    obj_type = 'loadbalancer_member'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.vmi = None
        self.vm = None
        self.loadbalancer_pool = {}
        self.update(obj_dict)
        if self.loadbalancer_pool:
            parent = LoadbalancerPoolKM.get(self.loadbalancer_pool)
            if parent:
                parent.members.add(self.uuid)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.params = obj.get('loadbalancer_member_properties', None)
        self.loadbalancer_pool = self.get_parent_uuid(obj)
        self.id_perms = obj.get('id_perms', None)
        self.annotations = obj.get('annotations', None)
        if self.annotations:
            for kvp in self.annotations['key_value_pair'] or []:
                if kvp['key'] == 'vmi':
                    self.vmi = kvp['value']
                if kvp['key'] == 'vm':
                    self.vm = kvp['value']
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        if obj.loadbalancer_pool:
            parent = LoadbalancerPoolKM.get(obj.loadbalancer_pool)
        if parent:
            parent.members.discard(obj.uuid)
        del cls._dict[uuid]
    # end delete
# end class LoadbalancerMemberKM

class HealthMonitorKM(DBBaseKM):
    _dict = {}
    obj_type = 'loadbalancer_healthmonitor'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.loadbalancer_pools = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.params = obj.get('loadbalancer_healthmonitor_properties', None)
        self.update_multiple_refs('loadbalancer_pool', obj)
        self.id_perms = obj.get('id_perms', None)
        self.parent_uuid = obj['parent_uuid']
        self.display_name = obj.get('display_name', None)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('loadbalancer_pool', {})
        del cls._dict[uuid]
    # end delete
# end class HealthMonitorKM


class VirtualMachineKM(DBBaseKM):
    _dict = {}
    obj_type = 'virtual_machine'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_router = None
        self.virtual_machine_interfaces = set()
        self.pod_labels = None
        obj_dict = self.update(obj_dict)
        super(VirtualMachineKM, self).__init__(uuid, obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.annotations = obj.get('annotations', None)
        if self.annotations:
            for kvp in self.annotations['key_value_pair'] or []:
                if kvp['key'] == 'labels':
                    self.pod_labels = json.loads(kvp['value'])
                    break
        self.update_single_ref('virtual_router', obj)
        self.update_multiple_refs('virtual_machine_interface', obj)
        return obj

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('virtual_router', {})
        obj.update_multiple_refs('virtual_machine_interface', {})
        super(VirtualMachineKM, cls).delete(uuid)
        del cls._dict[uuid]

class VirtualRouterKM(DBBaseKM):
    _dict = {}
    obj_type = 'virtual_router'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machines = set()
        self.update(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_multiple_refs('virtual_machine', obj)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine', {})
        del cls._dict[uuid]


class VirtualMachineInterfaceKM(DBBaseKM):
    _dict = {}
    obj_type = 'virtual_machine_interface'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.host_id = None
        self.virtual_network = None
        self.virtual_machine = None
        self.instance_ips = set()
        self.floating_ips = set()
        self.virtual_machine_interfaces = set()
        self.vlan_id = None
        self.vlan_bit_map = None
        self.security_groups = set()
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']

        # Cache bindings on this VMI.
        if obj.get('virtual_machine_interface_bindings', None):
            bindings = obj['virtual_machine_interface_bindings']
            kvps = bindings.get('key_value_pair', None)
            for kvp in kvps or []:
                if kvp['key'] == 'host_id':
                    self.host_id = kvp['value']

        self.update_multiple_refs('instance_ip', obj)
        self.update_multiple_refs('floating_ip', obj)
        self.update_single_ref('virtual_network', obj)
        self.update_single_ref('virtual_machine', obj)
        self.update_multiple_refs('security_group', obj)
        self.update_multiple_refs('virtual_machine_interface', obj)

        # Update VMI properties.
        vlan_id = None
        if obj.get('virtual_machine_interface_properties', None):
            props = obj['virtual_machine_interface_properties']
            # Property: Vlan ID.
            vlan_id = props.get('sub_interface_vlan_tag', None)

        # If vlan is configured on this interface, cache the appropriate
        # info.
        #
        # In nested mode, if the interface is a sub-interface, the vlan id
        # space is allocated and managed in the parent interface. So check to
        # see if the interface has a parent. If it does, invoke the appropriate
        # method on the parent interface for vlan-id management.
        if (vlan_id is not None or self.vlan_id is not None) and\
                vlan_id is not self.vlan_id:
            # Vlan is configured on this interface.

            if DBBaseKM.is_nested():
                # We are in nested mode.
                # Check if this interface has a parent. If yes, this is
                # is a sub-interface and the vlan-id should be managed on the
                # vlan-id space of the parent interface.
                parent_vmis = self.virtual_machine_interfaces
                for parent_vmi_id in parent_vmis:
                    parent_vmi = VirtualMachineInterfaceKM.locate(
                                     parent_vmi_id)
                    if not parent_vmi:
                        continue
                    if self.vlan_id is not None:
                        parent_vmi.reset_vlan(self.vlan_id)
                    if vlan_id is not None:
                        parent_vmi.set_vlan(vlan_id)
                        # Cache the Vlan-id.
                    self.vlan_id = vlan_id
            else:
                # Nested mode is not configured.
                # Vlan-id is NOT managed on the parent interface.
                # Just cache and proceed.
                self.vlan_id = vlan_id

        return obj

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]

        # If vlan-id is configured and if we are in nested mode,
        # free up the vlan-id which was claimed from the parent
        # interface.
        if obj.vlan_id and DBBaseKM.is_nested():
            parent_vmi_ids = obj.virtual_machine_interfaces
            for parent_vmi_id in parent_vmi_ids:
                parent_vmi = VirtualMachineInterfaceKM.get(parent_vmi_id)
                if parent_vmi:
                    parent_vmi.reset_vlan(obj.vlan_id)

        obj.update_multiple_refs('instance_ip', {})
        obj.update_multiple_refs('floating_ip', {})
        obj.update_single_ref('virtual_network', {})
        obj.update_single_ref('virtual_machine', {})
        obj.update_multiple_refs('security_group', {})
        obj.update_multiple_refs('virtual_machine_interface', {})

        obj.remove_from_parent()
        del cls._dict[uuid]

    def set_vlan (self, vlan):
        if vlan < 0 or vlan > MAX_VLAN_ID:
            return
        if not self.vlan_bit_map:
            self.vlan_bit_map = BitArray(MAX_VLAN_ID + 1)
        self.vlan_bit_map[vlan] = 1

    def reset_vlan (self, vlan):
        if vlan < 0 or vlan > MAX_VLAN_ID:
            return
        if not self.vlan_bit_map:
            return
        self.vlan_bit_map[vlan] = 0

    def alloc_vlan (self):
        if not self.vlan_bit_map:
            self.vlan_bit_map = BitArray(MAX_VLAN_ID + 1)
        vid = self.vlan_bit_map.find('0b0')
        if vid:
            self.set_vlan(vid[0])
            return vid[0]
        return INVALID_VLAN_ID

    def get_vlan (self):
        return vlan_id

class VirtualNetworkKM(DBBaseKM):
    _dict = {}
    obj_type = 'virtual_network'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.instance_ips = set()
        self.network_ipams = set()
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('instance_ip', obj)
        self.update_multiple_refs('network_ipam', obj)
        return obj

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.update_multiple_refs('instance_ip', {})
        obj.update_multiple_refs('network_ipam', {})
        obj.remove_from_parent()
        del cls._dict[uuid]

class FloatingIpKM(DBBaseKM):
    _dict = {}
    obj_type = 'floating_ip'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.address = None
        self.virtual_machine_interfaces = set()
        self.update(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.address = obj['floating_ip_address']
        self.update_multiple_refs('virtual_machine_interface', obj)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        del cls._dict[uuid]
# end class FloatingIpKM

class InstanceIpKM(DBBaseKM):
    _dict = {}
    obj_type = 'instance_ip'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.address = None
        self.family = None
        self.virtual_machine_interfaces = set()
        self.virtual_networks = set()
        self.floating_ips = set()
        self.update(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.family = obj.get('instance_ip_family', 'v4')
        self.address = obj.get('instance_ip_address', None)
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('virtual_network', obj)
        self.update_multiple_refs('floating_ip', obj)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.update_multiple_refs('virtual_network', {})
        del cls._dict[uuid]

    @classmethod
    def get_object(cls, ip):
        items = cls._dict.items()
        for uuid, iip_obj in items:
            if ip == iip_obj.address:
                return iip_obj
        return None

# end class InstanceIpKM

class ProjectKM(DBBaseKM):
    _dict = {}
    obj_type = 'project'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_networks = set()
        obj_dict = self.update(obj_dict)
        self.set_children('virtual_network', obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        return obj

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        del cls._dict[uuid]


class DomainKM(DBBaseKM):
    _dict = {}
    obj_type = 'domain'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        del cls._dict[uuid]


class SecurityGroupKM(DBBaseKM):
    _dict = {}
    obj_type = 'security_group'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.annotations = None
        self.rule_entries = None
        self.src_ns_selector = None
        self.src_pod_selector = None
        self.dst_pod_selector = None
        self.dst_ports = None
        obj_dict = self.update(obj_dict)
        super(SecurityGroupKM, self).__init__(uuid, obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.annotations = obj.get('annotations', None)
        self._set_selectors(self.annotations)
        self.rule_entries = obj.get('security_group_entries', None)
        return obj

    def _set_selectors(self, annotations):
        if not annotations:
            return
        for kvp in annotations.get('key_value_pair', []):
            if kvp.get('key') == 'spec':
                break
        specjson = json.loads(kvp.get('value'))

        pod_selector = specjson.get('podSelector')
        if pod_selector:
            self.dst_pod_selector = pod_selector.get('matchLabels')

        ingress = specjson.get('ingress')
        if not ingress:
            return
        for rule in ingress:
            self.dst_ports = rule.get('ports')
            from_rule = rule.get('from')
            if not from_rule:
                continue
            for item in from_rule or []:
                ns_selector = item.get('namespaceSelector')
                if ns_selector:
                    self.src_ns_selector = ns_selector.get('matchLabels')
                    continue
                pod_selector = item.get('podSelector')
                if pod_selector:
                    self.src_pod_selector = pod_selector.get('matchLabels')

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        super(SecurityGroupKM, cls).delete(uuid)
        del cls._dict[uuid]

class FloatingIpPoolKM(DBBaseKM):
    _dict = {}
    obj_type = 'floating_ip_pool'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_network = None
        self.update(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_single_ref('virtual_network', obj)
        if 'floating_ip_pool_subnets' in obj:
            self.floating_ip_pool_subnets = obj['floating_ip_pool_subnets']

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        self.update_single_ref('virtual_network', None)
        del cls._dict[uuid]

class FloatingIpKM(DBBaseKM):
    _dict = {}
    obj_type = 'floating_ip'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.address = None
        self.virtual_machine_interfaces = set()
        self.virtual_ip = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.address = obj['floating_ip_address']
        self.update_multiple_refs('virtual_machine_interface', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        del cls._dict[uuid]
    # end delete
# end class FloatingIpKM

class NetworkIpamKM(DBBaseKM):
    _dict = {}
    obj_type = 'network_ipam'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        del cls._dict[uuid]
# end class NetworkIpamKM
