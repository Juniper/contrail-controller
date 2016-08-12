#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for SVC monitor
"""
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common.vnc_db import DBBase
from cfgm_common import svc_info


class DBBaseSM(DBBase):
    obj_type = __name__

    def evaluate(self):
        # Implement in the derived class
        pass

class LoadbalancerSM(DBBaseSM):
    _dict = {}
    obj_type = 'loadbalancer'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interface = None
        self.service_instance = None
        self.loadbalancer_listeners = set()
        self.last_sent = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.display_name = obj.get('display_name', None)
        self.parent_uuid = obj['parent_uuid']
        self.id_perms = obj.get('id_perms', None)
        self.params = obj.get('loadbalancer_properties', None)
        self.provider = obj.get('loadbalancer_provider', None)
        self.update_single_ref('virtual_machine_interface', obj)
        self.update_single_ref('service_instance', obj)
        self.update_multiple_refs('loadbalancer_listener', obj)
    # end update

    def add(self):
        self.last_sent = \
            self._manager.loadbalancer_agent.loadbalancer_add(self)
    # end add

    def evaluate(self):
        self.add()
    # end evaluate

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        cls._manager.loadbalancer_agent.delete_loadbalancer(obj)
        obj.update_single_ref('virtual_machine_interface', {})
        obj.update_single_ref('service_instance', {})
        obj.update_multiple_refs('loadbalancer_listener', {})
        del cls._dict[uuid]
    # end delete
# end class LoadbalancerSM

class LoadbalancerListenerSM(DBBaseSM):
    _dict = {}
    obj_type = 'loadbalancer_listener'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.loadbalancer = None
        self.loadbalancer_pool = None
        self.last_sent = None
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
    # end update

    def add(self):
        self.last_sent = \
            self._manager.loadbalancer_agent.listener_add(self)
    # end add

    def evaluate(self):
        self.add()
    # end evaluate

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        cls._manager.loadbalancer_agent.delete_listener(obj)
        obj.update_single_ref('loadbalancer', {})
        obj.update_single_ref('loadbalancer_pool', {})
        del cls._dict[uuid]
    # end delete
# end class LoadbalancerListenerSM

class LoadbalancerPoolSM(DBBaseSM):
    _dict = {}
    obj_type = 'loadbalancer_pool'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.members = set()
        self.loadbalancer_healthmonitors = set()
        self.service_instance = None
        self.virtual_machine_interface = None
        self.virtual_ip = None
        self.loadbalancer_listener = None
        self.loadbalancer_id = None
        self.last_sent = None
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
        kvpairs = obj.get('loadbalancer_pool_custom_attributes', None)
        if kvpairs:
            self.custom_attributes = kvpairs.get('key_value_pair', [])
        self.members = set([lm['uuid']
                            for lm in obj.get('loadbalancer_members', [])])
        self.id_perms = obj.get('id_perms', None)
        self.parent_uuid = obj['parent_uuid']
        self.display_name = obj.get('display_name', None)
        self.update_single_ref('service_instance', obj)
        self.update_single_ref('virtual_ip', obj)
        self.update_single_ref('loadbalancer_listener', obj)
        self.update_single_ref('virtual_machine_interface', obj)
        self.update_multiple_refs('loadbalancer_healthmonitor', obj)
    # end update

    def add(self):
        if self.loadbalancer_listener:
            ll_obj = LoadbalancerListenerSM.get(self.loadbalancer_listener)
            self.loadbalancer_id = ll_obj.loadbalancer

        self.last_sent = \
            self._manager.loadbalancer_agent.loadbalancer_pool_add(self)

        if len(self.members):
            for member in self.members:
                member_obj = LoadbalancerMemberSM.get(member)
                if member_obj:
                    member_obj.last_sent = \
                        self._manager.loadbalancer_agent.loadbalancer_member_add(
                            member_obj)

        if self.virtual_ip:
            vip_obj = VirtualIpSM.get(self.virtual_ip)
            if vip_obj:
                vip_obj.last_sent = \
                    self._manager.loadbalancer_agent.virtual_ip_add(vip_obj)
    # end add

    def evaluate(self):
        self.add()
    # end evaluate

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        cls._manager.loadbalancer_agent.delete_loadbalancer_pool(obj)
        obj.update_single_ref('service_instance', {})
        obj.update_single_ref('virtual_ip', {})
        obj.update_single_ref('loadbalancer_listener', {})
        obj.update_single_ref('virtual_machine_interface', {})
        obj.update_multiple_refs('loadbalancer_healthmonitor', {})
        del cls._dict[uuid]
    # end delete
# end class LoadbalancerPoolSM

class LoadbalancerMemberSM(DBBaseSM):
    _dict = {}
    obj_type = 'loadbalancer_member'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.loadbalancer_pool = {}
        self.last_sent = None
        self.update(obj_dict)
        if self.loadbalancer_pool:
            parent = LoadbalancerPoolSM.get(self.loadbalancer_pool)
            parent.members.add(self.uuid)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.params = obj.get('loadbalancer_member_properties', None)
        self.loadbalancer_pool = self.get_parent_uuid(obj)
        self.id_perms = obj.get('id_perms', None)
    # end update

    def evaluate(self):
        pass
    # end evaluate

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        cls._manager.loadbalancer_agent.delete_loadbalancer_member(obj)
        if obj.loadbalancer_pool:
            parent = LoadbalancerPoolSM.get(obj.loadbalancer_pool)
        if parent:
            parent.members.discard(obj.uuid)
        del cls._dict[uuid]
    # end delete
# end class LoadbalancerMemberSM

class VirtualIpSM(DBBaseSM):
    _dict = {}
    obj_type = 'virtual_ip'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interface = None
        self.loadbalancer_pool = None
        self.last_sent = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.params = obj.get('virtual_ip_properties', None)
        self.update_single_ref('virtual_machine_interface', obj)
        self.update_single_ref('loadbalancer_pool', obj)
        self.id_perms = obj.get('id_perms', None)
        self.parent_uuid = obj['parent_uuid']
        self.display_name = obj.get('display_name', None)
    # end update

    def evaluate(self):
        pass
    # end evaluate

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        cls._manager.loadbalancer_agent.delete_virtual_ip(obj)
        obj.update_single_ref('virtual_machine_interface', {})
        obj.update_single_ref('loadbalancer_pool', {})
        del cls._dict[uuid]
    # end delete
# end class VirtualIpSM

class HealthMonitorSM(DBBaseSM):
    _dict = {}
    obj_type = 'loadbalancer_healthmonitor'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.loadbalancer_pools = set()
        self.last_sent = None
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
        self.last_sent = self._manager.loadbalancer_agent.update_hm(self)
    # end update

    def evaluate(self):
        pass
    # end evaluate

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('loadbalancer_pool', {})
        del cls._dict[uuid]
    # end delete
# end class HealthMonitorSM

class VirtualMachineSM(DBBaseSM):
    _dict = {}
    obj_type = 'virtual_machine'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_instance = None
        self.service_id = None
        self.virtual_router = None
        self.virtual_machine_interfaces = set()
        self.virtualization_type = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_single_ref('service_instance', obj)
        self.update_single_ref('virtual_router', obj)
        self.update_multiple_refs('virtual_machine_interface', obj)

        if self.service_instance:
            self.service_id = self.service_instance

        self.display_name = obj.get('display_name', None)
        if self.display_name is None:
            return
        display_list = self.display_name.split('__')
        if self.service_instance:
            if len(display_list) == 5:
                self.virtualization_type = display_list[-1]
                self.proj_fq_name = display_list[0:2]
                self.index = int(display_list[-2]) - 1
            else:
                self.index = -1

    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('service_instance', {})
        obj.update_single_ref('virtual_router', {})
        obj.update_multiple_refs('virtual_machine_interface', {})
        del cls._dict[uuid]
    # end delete

    def evaluate(self):
        if self.service_id and not self.service_instance:
            self._manager.delete_service_instance(self)
# end VirtualMachineSM


class VirtualRouterSM(DBBaseSM):
    _dict = {}
    obj_type = 'virtual_router'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.agent_state = False
        self.agent_down_count = 0
        self.virtual_machines = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_multiple_refs('virtual_machine', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine', {})
        del cls._dict[uuid]
    # end delete

    def set_agent_state(self, up):
        if up:
            self.agent_down_count = 0
            self.agent_state = True
        else:
            self.agent_down_count += 1
            if not (self.agent_down_count % 3):
                self.agent_state = False

    def set_netns_version(self, netns_version):
        self.netns_version = netns_version

# end VirtualRouterSM


class VirtualMachineInterfaceSM(DBBaseSM):
    _dict = {}
    obj_type = 'virtual_machine_interface'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.params = None
        self.if_type = None
        self.virtual_ip = None
        self.loadbalancer = None
        self.virtual_network = None
        self.virtual_machine = None
        self.loadbalancer_pool = None
        self.logical_interface = None
        self.instance_ips = set()
        self.floating_ips = set()
        self.interface_route_tables = set()
        self.service_health_checks = set()
        self.security_groups = set()
        self.service_instance = None
        self.instance_id = None
        self.physical_interface = None
        self.port_tuple = None
        self.fat_flow_ports = set()
        self.aaps = None
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        if obj.get('virtual_machine_interface_properties', None):
            self.params = obj['virtual_machine_interface_properties']
            self.if_type = self.params.get('service_interface_type', None)
        self.aaps = obj.get('virtual_machine_interface_allowed_address_pairs', None)
        if self.aaps:
            self.aaps = self.aaps.get('allowed_address_pair', None)
        self.fat_flow_ports.clear()
        ffps = obj.get('virtual_machine_interface_fat_flow_protocols', None)
        if ffps:
            for ffp in ffps.get('fat_flow_protocol', []):
                if ffp['port']:
                    self.fat_flow_ports.add(ffp['port'])
        self.update_single_ref('virtual_ip', obj)
        self.update_single_ref('loadbalancer', obj)
        self.update_single_ref('loadbalancer_pool', obj)
        self.update_multiple_refs('instance_ip', obj)
        self.update_multiple_refs('floating_ip', obj)
        self.update_single_ref('virtual_network', obj)
        self.update_single_ref('virtual_machine', obj)
        self.update_single_ref('logical_interface', obj)
        self.update_multiple_refs('interface_route_table', obj)
        self.update_multiple_refs('service_health_check', obj)
        self.update_single_ref('physical_interface',obj)
        self.update_multiple_refs('security_group', obj)
        self.update_single_ref('port_tuple', obj)
        if self.virtual_machine:
            vm = VirtualMachineSM.get(self.virtual_machine)
            if vm:
                self.service_instance = vm.service_instance
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('virtual_ip', {})
        obj.update_single_ref('loadbalancer', {})
        obj.update_single_ref('loadbalancer_pool', {})
        obj.update_multiple_refs('instance_ip', {})
        obj.update_multiple_refs('floating_ip', {})
        obj.update_single_ref('virtual_network', {})
        obj.update_single_ref('virtual_machine', {})
        obj.update_single_ref('logical_interface', {})
        obj.update_multiple_refs('interface_route_table', {})
        obj.update_multiple_refs('service_health_check', {})
        obj.update_multiple_refs('security_group', {})
        obj.update_single_ref('port_tuple', {})
        obj.remove_from_parent()
        del cls._dict[uuid]
    # end delete

    def evaluate(self):
        vm = VirtualMachineSM.get(self.virtual_machine)
        if vm:
            self._manager.port_delete_or_si_link(vm, self)

        self._manager.port_tuple_agent.update_port_tuple(self)

# end VirtualMachineInterfaceSM


class ServiceInstanceSM(DBBaseSM):
    _dict = {}
    obj_type = 'service_instance'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_template = None
        self.loadbalancer = None
        self.loadbalancer_pool = None
        self.interface_route_tables = {}
        self.service_health_checks = {}
        self.instance_ips = set()
        self.virtual_machines = set()
        self.logical_router = None
        self.params = None
        self.bindings = None
        self.kvps = None
        self.state = 'init'
        self.launch_count = 0
        self.back_off = -1
        self.image = None
        self.flavor = None
        self.max_instances = 0
        self.availability_zone = None
        self.ha_mode = None
        self.vr_id = None
        self.vn_changed = False
        self.local_preference = [None, None]
        self.vn_info = []
        self.port_tuples = set()
        obj_dict = self.update(obj_dict)
        self.set_children('port_tuple', obj_dict)
        self.add_to_parent(obj_dict)
        if self.ha_mode == 'active-standby':
            self.max_instances = 2
            self.local_preference = [svc_info.get_active_preference(),
                                     svc_info.get_standby_preference()]
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.proj_name = obj['fq_name'][-2]
        self.check_vn_changes(obj)
        self.params = obj.get('service_instance_properties', None)
        self.bindings = obj.get('service_instance_bindings', None)
        if self.bindings:
            self.kvps = self.bindings.get('key_value_pair', None)
        self.update_single_ref('service_template', obj)
        self.update_single_ref('loadbalancer', obj)
        self.update_single_ref('loadbalancer_pool', obj)
        self.update_single_ref('logical_router', obj)
        self.update_multiple_refs_with_attr('interface_route_table', obj)
        self.update_multiple_refs_with_attr('service_health_check', obj)
        self.update_multiple_refs('instance_ip', obj)
        self.update_multiple_refs('virtual_machine', obj)
        self.id_perms = obj.get('id_perms', None)
        if not self.params:
            return obj
        self.vr_id = self.params.get('virtual_router_id', None)
        self.ha_mode = self.params.get('ha_mode', None)
        if self.ha_mode != 'active-standby':
            scale_out = self.params.get('scale_out', None)
            if scale_out:
                self.max_instances = scale_out.get('max_instances', 1)
        return obj
    # end update

    def check_vn_changes(self, obj):
        self.vn_changed = False
        if not self.params or not obj.get('service_instance_properties'):
            return
        old_ifs = self.params.get('interface_list', [])
        new_ifs = obj['service_instance_properties'].get('interface_list', [])
        for index in range(0, len(old_ifs)):
            try:
                old_if = old_ifs[index]
                new_if = new_ifs[index]
            except IndexError:
                continue

            if not old_if['virtual_network'] or not new_if['virtual_network']:
                continue

            if old_if['virtual_network'] != new_if['virtual_network']:
                self.vn_changed = True
                return
    # end check_vn_changes

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('service_template', {})
        obj.update_single_ref('loadbalancer', {})
        obj.update_single_ref('loadbalancer_pool', {})
        obj.update_single_ref('logical_router', {})
        obj.update_multiple_refs_with_attr('interface_route_table', {})
        obj.update_multiple_refs_with_attr('service_health_check', {})
        obj.update_multiple_refs('instance_ip', {})
        obj.update_multiple_refs('virtual_machine', {})
        obj.remove_from_parent()
        del cls._dict[uuid]
    # end delete

    def evaluate(self):
        self.state = 'launch'
        self._manager.create_service_instance(self)

        for pt_id in self.port_tuples:
            self._manager.port_tuple_agent.update_port_tuple(pt_id=pt_id)

# end class ServiceInstanceSM


class ServiceTemplateSM(DBBaseSM):
    _dict = {}
    obj_type = 'service_template'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_instances = set()
        self.virtualization_type = 'virtual-machine'
        self.service_appliance_set = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.params = obj.get('service_template_properties')
        if self.params:
            self.virtualization_type = self.params.get(
                'service_virtualization_type') or 'virtual-machine'
        self.update_multiple_refs('service_instance', obj)
        self.update_single_ref('service_appliance_set', obj)
        self.id_perms = obj.get('id_perms', None)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('service_instance', {})
        obj.update_single_ref('service_appliance_set', {})
        del cls._dict[uuid]
    # end delete
# end class ServiceTemplateSM


class VirtualNetworkSM(DBBaseSM):
    _dict = {}
    obj_type = 'virtual_network'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.instance_ips = set()
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('instance_ip', obj)
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.update_multiple_refs('instance_ip', {})
        obj.remove_from_parent()
        del cls._dict[uuid]
    # end delete

    def evaluate(self):
        for si_id in ServiceInstanceSM:
            si = ServiceInstanceSM.get(si_id)
            intf_list = []
            if si.params:
                intf_list = si.params.get('interface_list', [])
            for intf in intf_list:
                if (':').join(self.fq_name) in intf.values():
                    self._manager.create_service_instance(si)

# end class VirtualNetworkSM


class FloatingIpSM(DBBaseSM):
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

    def evaluate(self):
        self._manager.netns_manager.add_fip_to_vip_vmi(self)

# end class FloatingIpSM


class InstanceIpSM(DBBaseSM):
    _dict = {}
    obj_type = 'instance_ip'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.address = None
        self.family = None
        self.service_instance = None
        self.service_instance_ip = None
        self.instance_ip_secondary = None
        self.secondary_tracking_ip = None
        self.service_health_check_ip = None
        self.virtual_machine_interfaces = set()
        self.virtual_networks = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.instance_ip_mode = obj.get('instance_ip_mode', None)
        self.service_instance_ip = obj.get('service_instance_ip', False)
        self.instance_ip_secondary = obj.get('instance_ip_secondary', False)
        self.secondary_tracking_ip = obj.get('secondary_ip_tracking_ip', None)
        self.service_health_check_ip = obj.get('service_health_check_ip', None)
        self.family = obj.get('instance_ip_family', 'v4')
        self.address = obj.get('instance_ip_address', None)
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('virtual_network', obj)
        self.update_single_ref('service_instance', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('service_instance', {})
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.update_multiple_refs('virtual_network', {})
        del cls._dict[uuid]
    # end delete

    def evaluate(self):
        self._manager.port_tuple_agent.delete_shared_iip(self)

# end class InstanceIpSM


class LogicalInterfaceSM(DBBaseSM):
    _dict = {}
    obj_type = 'logical_interface'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interface = None
        self.logical_interface_vlan_tag = 0
        self.update(obj_dict)
        if self.physical_interface:
            parent = PhysicalInterfaceSM.get(self.physical_interface)
        elif self.physical_router:
            parent = PhysicalRouterSM.get(self.physical_router)
        if parent:
            parent.logical_interfaces.add(self.uuid)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        if obj['parent_type'] == 'physical-router':
            self.physical_router = self.get_parent_uuid(obj)
            self.physical_interface = None
        else:
            self.physical_interface = self.get_parent_uuid(obj)
            self.physical_router = None

        self.update_single_ref('virtual_machine_interface', obj)
        self.name = obj['fq_name'][-1]
        self.logical_interface_vlan_tag = obj.get(
            'logical_interface_vlan_tag', 0)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        if obj.physical_interface:
            parent = PhysicalInterfaceSM.get(obj.physical_interface)
        elif obj.physical_router:
            parent = PhysicalInterfaceSM.get(obj.physical_router)
        if parent:
            parent.logical_interfaces.discard(obj.uuid)
        obj.update_single_ref('virtual_machine_interface', {})
        del cls._dict[uuid]
    # end delete
# end LogicalInterfaceSM


class PhysicalInterfaceSM(DBBaseSM):
    _dict = {}
    fq_name = None
    obj_type = 'physical_interface'
    virtual_machine_interfaces = set()
    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
        pr = PhysicalRouterSM.get(self.physical_router)
        if pr:
            pr.physical_interfaces.add(self.uuid)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.physical_router = self.get_parent_uuid(obj)
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
        self.update_multiple_refs('virtual_machine_interface',obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        pr = PhysicalRouterSM.get(obj.physical_router)
        if pr:
            pr.physical_interfaces.discard(obj.uuid)
        obj.update_multiple_refs('virtual_machine_interface',{})
        del cls._dict[uuid]
    # end delete
# end PhysicalInterfaceSM


class PhysicalRouterSM(DBBaseSM):
    _dict = {}
    obj_type = 'physical_router'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.management_ip = obj.get('physical_router_management_ip')
        self.vendor = obj.get('physical_router_vendor_name')
        self.physical_interfaces = set([pi['uuid'] for pi in
                                        obj.get('physical_interfaces', [])])
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        del cls._dict[uuid]
    # end delete
# end PhysicalRouterSM


class ProjectSM(DBBaseSM):
    _dict = {}
    obj_type = 'project'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_instances = set()
        self.virtual_networks = set()
        obj_dict = self.update(obj_dict)
        self.set_children('virtual_network', obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_multiple_refs('service_instance', obj)
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('service_instance', {})
        del cls._dict[uuid]
    # end delete
# end ProjectSM


class DomainSM(DBBaseSM):
    _dict = {}
    obj_type = 'domain'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        del cls._dict[uuid]
    # end delete
# end DomainSM


class SecurityGroupSM(DBBaseSM):
    _dict = {}
    obj_type = 'security_group'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
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

    def evaluate(self):
        self._manager.netns_manager.add_sg_to_vip_vmi(self)

# end SecurityGroupSM


class InterfaceRouteTableSM(DBBaseSM):
    _dict = {}
    obj_type = 'interface_route_table'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.service_instances = {}
        self.si_uuid = None
        self.if_type = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs_with_attr('service_instance', obj)
        name_split = self.name.split(' ')
        if len(name_split) == 2:
            self.si_uuid = name_split[0]
            self.if_type = name_split[1]
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.update_multiple_refs_with_attr('service_instance', {})
        del cls._dict[uuid]
    # end delete

    def evaluate(self):
        if self.si_uuid and not len(self.virtual_machine_interfaces):
            self._manager.delete_interface_route_table(self.uuid)

# end InterfaceRouteTableSM


class ServiceApplianceSM(DBBaseSM):
    _dict = {}
    obj_type = 'service_appliance'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_appliance_set = None
        self.physical_interfaces = {}
        self.kvpairs = []
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        kvpairs = obj.get('service_appliance_properties', None)
        if kvpairs:
            self.kvpairs = kvpairs.get('key_value_pair', [])
        self.user_credential = obj.get(
            'service_appliance_user_credentials', None)
        self.ip_address = obj.get('service_appliance_ip_address', None)
        self.service_appliance_set = self.get_parent_uuid(obj)
        self.physical_interfaces = {}
        ref_objs = obj.get("physical_interface_refs",[])
        for ref in ref_objs:
            self.physical_interfaces[ref[
                    'attr'].get('interface_type')] = ref['uuid']
        if self.service_appliance_set:
            parent = ServiceApplianceSetSM.get(self.service_appliance_set)
            parent.service_appliances.add(self.uuid)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        if obj.service_appliance_set:
            parent = ServiceApplianceSetSM.get(obj.service_appliance_set)
        if parent:
            parent.service_appliances.discard(obj.uuid)
        del cls._dict[uuid]
    # end delete
# end ServiceApplianceSM


class ServiceApplianceSetSM(DBBaseSM):
    _dict = {}
    obj_type = 'service_appliance_set'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_appliances = set()
        self.service_template = None
        self.kvpairs = []
        self.ha_mode = "standalone"
        self.update(obj_dict)
    # end __init__

    def add(self):
        self._manager.loadbalancer_agent.load_driver(self)
    # end add

    def evaluate(self):
        self.add()

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.driver = obj.get('service_appliance_driver', None)
        self.update_single_ref("service_template", obj)
        kvpairs = obj.get('service_appliance_set_properties', None)
        if kvpairs:
            self.kvpairs = kvpairs.get('key_value_pair', [])
        self.service_appliances = set(
            [sa['uuid'] for sa in obj.get('service_appliances', [])])
        if 'service_appliance_ha_mode' in obj:
            self.ha_mode = obj['service_appliance_ha_mode']
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        cls._manager.loadbalancer_agent.unload_driver(obj)
        obj.update_single_ref("service_template",{})
        del cls._dict[uuid]
    # end delete
# end ServiceApplianceSetSM


class LogicalRouterSM(DBBaseSM):
    _dict = {}
    obj_type = 'logical_router'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_instance = None
        self.virtual_network = None
        self.virtual_machine_interfaces = set()
        self.last_virtual_machine_interfaces = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.parent_uuid = obj['parent_uuid']
        self.update_single_ref('service_instance', obj)
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_single_ref('virtual_network', obj)
        self.name = obj['fq_name'][-1]
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        cls._manager.snat_agent.delete_snat_instance(obj)
        obj.update_single_ref('service_instance', {})
        obj.update_single_ref('virtual_network', {})
        obj.update_multiple_refs('virtual_machine_interface', {})
        del cls._dict[uuid]
    # end delete

    def evaluate(self):
        self._manager.snat_agent.update_snat_instance(self)

# end LogicalRouterSM

class PortTupleSM(DBBaseSM):
    _dict = {}
    obj_type = 'port_tuple'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.parent_uuid = self.get_parent_uuid(obj)
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.name = obj['fq_name'][-1]
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.remove_from_parent()
        del cls._dict[uuid]
    # end delete

    def evaluate(self):
        self._manager.port_tuple_agent.update_port_tuple(pt_id=self.uuid)
# end PortTupleSM

class ServiceHealthCheckSM(DBBaseSM):
    _dict = {}
    obj_type = 'service_health_check'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.service_instances = {}
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.parent_uuid = obj['parent_uuid']
        self.name = obj['fq_name'][-1]
        self.params = obj.get('service_health_check_properties', None)
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs_with_attr('service_instance', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs_with_attr('service_instance', {})
        obj.update_multiple_refs('virtual_machine_interface', {})
        del cls._dict[uuid]
    # end delete
# end ServiceHealthCheckSM
