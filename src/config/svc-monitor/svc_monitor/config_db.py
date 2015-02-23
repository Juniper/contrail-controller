#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for SVC monitor
"""
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common.vnc_db import DBBase
from cfgm_common import svc_info

class LoadbalancerPoolSM(DBBase):
    _dict = {}
    obj_type = 'loadbalancer_pool'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.members = set()
        self.loadbalancer_healthmonitors = set()
        self.service_instance = None
        self.virtual_machine_interface = None
        self.virtual_ip = None
        self.update(obj_dict)
        self.last_sent = None
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.params = obj['loadbalancer_pool_properties']
        self.provider = obj['loadbalancer_pool_provider']
        self.members = set([lm['uuid'] for lm in obj.get('loadbalancer_members', [])])
        self.id_perms = obj['id_perms']
        self.parent_uuid = obj['parent_uuid']
        self.display_name = obj['display_name']
        self.update_single_ref('service_instance', obj)
        self.update_single_ref('virtual_ip', obj)
        self.update_single_ref('virtual_machine_interface', obj)
        self.update_multiple_refs('loadbalancer_healthmonitor', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        cls._manager.loadbalancer_agent.delete_loadbalancer_pool(obj)
        obj.update_single_ref('service_instance', {})
        obj.update_single_ref('virtual_ip', {})
        obj.update_single_ref('virtual_machine_interface', {})
        obj.update_multiple_refs('loadbalancer_healthmonitor', {})
        del cls._dict[uuid]
    # end delete

    def add(self):
        self.last_sent = self._manager.loadbalancer_agent.loadbalancer_pool_add(self)
        if len(self.members):
            for member in self.members:
                member_obj = LoadbalancerMemberSM.get(member)
                member_obj.last_sent = self._manager.loadbalancer_agent.loadbalancer_member_add(member_obj)
        if self.virtual_ip:
            vip_obj = VirtualIpSM.get(self.virtual_ip)
            vip_obj.last_sent = self._manager.loadbalancer_agent.virtual_ip_add(vip_obj)
    # end add
# end class LoadbalancerPoolSM

class LoadbalancerMemberSM(DBBase):
    _dict = {}
    obj_type = 'loadbalancer_member'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.loadbalancer_pool = {}
        self.update(obj_dict)
        self.last_sent = None
        if self.loadbalancer_pool:
            parent = LoadbalancerPoolSM.get(self.loadbalancer_pool)
            parent.members.add(self.uuid)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.params = obj['loadbalancer_member_properties']
        self.loadbalancer_pool = self.get_parent_uuid(obj)
        self.id_perms = obj['id_perms']
    # end update

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

class VirtualIpSM(DBBase):
    _dict = {}
    obj_type = 'virtual_ip'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interface = None
        self.loadbalancer_pool = None
        self.update(obj_dict)
        self.last_sent = None
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.params = obj['virtual_ip_properties']
        self.update_single_ref('virtual_machine_interface', obj)
        self.update_single_ref('loadbalancer_pool', obj)
        self.id_perms = obj['id_perms']
        self.parent_uuid = obj['parent_uuid']
        self.display_name = obj['display_name']
    # end update

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

class HealthMonitorSM(DBBase):
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
        self.params = obj['loadbalancer_healthmonitor_properties']
        self.update_multiple_refs('loadbalancer_pool', obj)
        self.id_perms = obj['id_perms']
        self.parent_uuid = obj['parent_uuid']
        self.display_name = obj['display_name']
        self.last_sent = self._manager.loadbalancer_agent.update_hm(self)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('loadbalancer_pool', {})
        del cls._dict[uuid]
    # end delete
# end class HealthMonitorSM


class VirtualMachineSM(DBBase):
    _dict = {}
    obj_type = 'virtual_machine'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_instance = None
        self.virtual_router = None
        self.virtual_machine_interfaces = set()
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
            self.display_name = obj['display_name']
            self.virtualization_type = self.display_name.split('__')[-1]
            self.proj_fq_name = self.display_name.split('__')[0:2]
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
# end VirtualMachineSM

class VirtualRouterSM(DBBase):
    _dict = {}
    obj_type = 'virtual_router'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_single_ref('virtual_machine', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('virtual_machine', {})
        del cls._dict[uuid]
    # end delete
# end VirtualRouterSM


class VirtualMachineInterfaceSM(DBBase):
    _dict = {}
    obj_type = 'virtual_machine_interface'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.params = None
        self.if_type = None
        self.virtual_ip = None
        self.virtual_network = None
        self.virtual_machine = None
        self.loadbalancer_pool = None
        self.logical_interface = None
        self.instance_ip = None
        self.interface_route_table = None
        self.security_group = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        if obj.get('virtual_machine_interface_properties', None):
            self.params = obj['virtual_machine_interface_properties']
            self.if_type = self.params.get('service_interface_type', None)
        self.update_single_ref('virtual_ip', obj)
        self.update_single_ref('loadbalancer_pool', obj)
        self.update_single_ref('instance_ip', obj)
        self.update_single_ref('virtual_network', obj)
        self.update_single_ref('virtual_machine', obj)
        self.update_single_ref('logical_interface', obj)
        self.update_single_ref('interface_route_table', obj)
        self.update_single_ref('security_group', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('virtual_ip', {})
        obj.update_single_ref('loadbalancer_pool', {})
        obj.update_single_ref('instance_ip', {})
        obj.update_single_ref('virtual_network', {})
        obj.update_single_ref('virtual_machine', {})
        obj.update_single_ref('logical_interface', {})
        obj.update_single_ref('interface_route_table', {})
        obj.update_single_ref('security_group', {})
        del cls._dict[uuid]
    # end delete
# end VirtualMachineInterfaceSM

class ServiceInstanceSM(DBBase):
    _dict = {}
    obj_type = 'service_instance'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_template = None
        self.loadbalancer_pool = None
        self.virtual_machines = set()
        self.params = None
        self.state = 'init'
        self.image = None
        self.flavor = None
        self.max_instances = 0
        self.availability_zone = None
        self.ha_mode = None
        self.local_preference = [None, None]
        self.vn_info = []
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.proj_name = obj['fq_name'][-2]
        self.params = obj['service_instance_properties']
        self.update_single_ref('service_template', obj)
        self.update_single_ref('loadbalancer_pool', obj)
        self.update_multiple_refs('virtual_machine', obj)
        self.id_perms = obj['id_perms']
        self.ha_mode = self.params.get('ha_mode', None)
        if self.ha_mode and self.ha_mode == 'active-standby':
            self.max_instances = 2
            self.local_preference = [svc_info.get_active_preference(),
                svc_info.get_standby_preference()]
        else:
            scale_out = self.params.get('scale_out', None)
            if scale_out:
                self.max_instances = scale_out.get('max_instances', 1)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('service_template', {})
        obj.update_single_ref('loadbalancer_pool', {})
        obj.update_multiple_refs('virtual_machine', {})
        del cls._dict[uuid]
    # end delete
# end class ServiceInstanceSM


class ServiceTemplateSM(DBBase):
    _dict = {}
    obj_type = 'service_template'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_instances = set()
        self.virtualization_type = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.params = obj.get('service_template_properties')
        self.virtualization_type = self.params.get(
            'service_virtualization_type', None)
        if not self.virtualization_type:
            self.virtualization_type = 'virtual-machine'
        self.update_multiple_refs('service_instance', obj)
        self.id_perms = obj['id_perms']
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('service_instance', {})
        del cls._dict[uuid]
    # end delete
# end class ServiceTemplateSM


class VirtualNetworkSM(DBBase):
    _dict = {}
    obj_type = 'virtual_network'

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

# end class VirtualNetworkSM


class InstanceIpSM(DBBase):
    _dict = {}
    obj_type = 'instance_ip'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.address = None
        self.virtual_machine_interfaces = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.address = obj['instance_ip_address']
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
# end class InstanceIpSM

class LogicalInterfaceSM(DBBase):
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
        self.logical_interface_vlan_tag = obj.get('logical_interface_vlan_tag', 0)
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

class PhysicalInterfaceSM(DBBase):
    _dict = {}
    obj_type = 'physical_interface'

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
        self.physical_router = self.get_parent_uuid(obj)
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        pr = PhysicalRouterSM.get(obj.physical_router)
        if pr:
            pr.physical_interfaces.discard(obj.uuid)
        del cls._dict[uuid]
    # end delete
# end PhysicalInterfaceSM

class PhysicalRouterSM(DBBase):
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


class ProjectSM(DBBase):
    _dict = {}
    obj_type = 'project'

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
        obj = cls._dict[uuid]
        del cls._dict[uuid]
    # end delete
# end ProjectSM

class DomainSM(DBBase):
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

class SecurityGroupSM(DBBase):
    _dict = {}
    obj_type = 'security_group'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        if self.name != 'default':
            self.delete(self.uuid)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        del cls._dict[uuid]
    # end delete
# end SecurityGroupSM

class InterfaceRouteTableSM(DBBase):
    _dict = {}
    obj_type = 'interface_route_table'

    def __init__(self, uuid):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
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
# end InterfaceRouteTableSM

class ServiceApplianceSM(DBBase):
    _dict = {}
    obj_type = 'service_appliance'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_appliance_set = None
        self.kvpairs = []
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        kvpairs = obj.get('service_appliance_set_properties', None)
        if kvpairs:
            self.kvpairs = kvpairs.get('key_value_pair', [])
        self.user_credential = obj.get('service-appliance-user-credentials', None)
        self.ip_address = obj.get('service-appliance-ip-address', None)
        self.service_appliance_set = self.get_parent_uuid(obj)
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
            parent.service_instances.discard(obj.uuid)
        del cls._dict[uuid]
    # end delete
# end ServiceApplianceSM

class ServiceApplianceSetSM(DBBase):
    _dict = {}
    obj_type = 'service_appliance'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_appliances = set()
        self.kvpairs = []
        self.update(obj_dict)
    # end __init__

    def add(self):
        self._manager.loadbalancer_agent.load_driver(self)
    # end add
    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.driver = obj.get('service_appliance_driver', None)
        kvpairs = obj.get('service_appliance_set_properties', None)
        if kvpairs:
            self.kvpairs = kvpairs.get('key_value_pair', [])
        self.service_appliances = set([sa['uuid'] for sa in obj.get('service_appliances', [])])
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        cls._manager.loadbalancer_agent.unload_driver(obj)
        del cls._dict[uuid]
    # end delete
# end ServiceApplianceSetSM


DBBase._OBJ_TYPE_MAP = {
    'loadbalancer_pool': LoadbalancerPoolSM,
    'loadbalancer_member': LoadbalancerMemberSM,
    'virtual_ip': VirtualIpSM,
    'loadbalancer_healthmonitor': HealthMonitorSM,
    'service_template': ServiceTemplateSM,
    'service_instance': ServiceInstanceSM,
    'virtual_network': VirtualNetworkSM,
    'virtual_machine': VirtualMachineSM,
    'virtual_machine_interface': VirtualMachineInterfaceSM,
    'interface_route_table': InterfaceRouteTableSM,
    'instance_ip': InstanceIpSM,
    'logical_interface': LogicalInterfaceSM,
    'physical_interface': PhysicalInterfaceSM,
    'virtual_router': VirtualRouterSM,
    'physical_router': PhysicalRouterSM,
    'project': ProjectSM,
    'domain': DomainSM,
    'security_group': SecurityGroupSM,
    'service_appliance': ServiceApplianceSM,
    'service_appliance_set': ServiceApplianceSetSM,
}
