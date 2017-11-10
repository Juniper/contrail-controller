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
from vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from vnc_api.vnc_api import (KeyValuePair,KeyValuePairs)

INVALID_VLAN_ID = 4096
MAX_VLAN_ID = 4095
MIN_VLAN_ID = 1

class DBBaseKM(DBBase):
    obj_type = __name__
    _nested_mode = False

    # Infra annotations that will be added on objects with custom annotations.
    ann_fq_name_infra_key = ["project","cluster","owner"]

    def __init__(self, uuid, obj_dict=None):
        # By default there are no annotations added on an object.
        self.ann_fq_name = None

    @staticmethod
    def get_infra_annotations():
        """Get infra annotations."""
        annotations = {}
        annotations['owner'] = vnc_kube_config.cluster_owner()
        annotations['cluster'] = vnc_kube_config.cluster_name()

        # "project" annotations, though infrstructural, are namespace specific.
        # So "project" annotations are added when callee adds annotations on
        # objects.

        return annotations

    @classmethod
    def _get_annotations(cls, vnc_caller, namespace, name, k8s_type,
            **custom_ann_kwargs):
        """Get all annotations.

        Annotations are aggregated from multiple sources like infra info,
        input params and custom annotations. This method is meant to be an
        aggregator of all possible annotations.
        """
        # Get annotations declared on the caller.
        annotations = dict(vnc_caller.get_annotations())

        # Update annotations with infra specific annotations.
        infra_anns = cls.get_infra_annotations()
        infra_anns['project'] = vnc_kube_config.cluster_project_name(namespace)
        annotations.update(infra_anns)

        # Update annotations based on explicity input params.
        input_anns = {}
        input_anns['namespace'] = namespace
        input_anns['name'] = name
        if k8s_type:
            input_anns['kind'] = k8s_type
        annotations.update(input_anns)

        # Append other custom annotations.
        annotations.update(custom_ann_kwargs)

        return annotations

    @classmethod
    def add_annotations(cls, vnc_caller, obj, namespace, name, k8s_type=None,
            **custom_ann_kwargs):
        """Add annotations on the input object.

        Given an object, this method will add all required and specfied
        annotations on that object.
        """
        # Construct annotations to be added on the object.
        annotations = cls._get_annotations(vnc_caller, namespace, name,
                            k8s_type, **custom_ann_kwargs)

        # Validate that annotations have all the info to construct
        # the annotations-based-fq-name as required by the object's db.
        if hasattr(cls, 'ann_fq_name_key'):
            if not set(cls.ann_fq_name_key).issubset(annotations):
                err_msg = "Annotations required to contruct kube_fq_name for"+\
                    " object (%s:%s) was not found in input keyword args." %\
                    (namespace,name)
                raise Exception(err_msg)

        # Annotate the object.
        for ann_key, ann_value in annotations.iteritems():
            obj.add_annotations(KeyValuePair(key=ann_key, value=ann_value))

    @classmethod
    def _update_fq_name_to_uuid(cls, uuid, obj_dict):
        cls._fq_name_to_uuid[tuple(obj_dict['fq_name'])] = uuid

    @classmethod
    def get_fq_name_to_uuid(cls, fq_name):
        return cls._fq_name_to_uuid.get(tuple(fq_name))

    @classmethod
    def _get_ann_fq_name_from_obj(cls, obj_dict):
        """Get the annotated fully qualified name from the object.

        Annotated-fq-names are contructed from annotations found on the
        object. The format of the fq-name is specified in the object's db
        class. This method will construct the annoated-fq-name of the input
        object.
        """
        fq_name = None
        if hasattr(cls, 'ann_fq_name_key'):
            fq_name = []
            fq_name_key = cls.ann_fq_name_infra_key + cls.ann_fq_name_key
            if obj_dict.get('annotations') and\
              obj_dict['annotations'].get('key_value_pair'):
                kvps = obj_dict['annotations']['key_value_pair']
                for elem in fq_name_key:
                    for kvp in kvps:
                        if kvp.get("key") != elem:
                            continue
                        fq_name.append(kvp.get("value"))
                        break
        return fq_name

    @classmethod
    def _get_ann_fq_name_from_params(cls, **kwargs):
        """Construct annotated fully qualified name using input params."""
        fq_name = []
        fq_name_key = cls.ann_fq_name_infra_key + cls.ann_fq_name_key
        for elem in fq_name_key:
            for key,value in kwargs.iteritems():
                if key != elem:
                    continue
                fq_name.append(value)
                break
        return fq_name

    @classmethod
    def get_ann_fq_name_to_uuid(cls, vnc_caller, namespace, name,
            k8s_type=None, **kwargs):
        """Get vnc object uuid corresponding to an annotated-fq-name.

        The annotated-fq-name is constructed from the input params given
        by the caller.
        """
        # Construct annotations based on input params.
        annotations = cls._get_annotations(vnc_caller, namespace, name,
                        k8s_type, **kwargs)

        # Validate that annoatations has all info required for construction
        # of annotated-fq-name.
        if hasattr(cls, 'ann_fq_name_key'):
            if not set(cls.ann_fq_name_key).issubset(annotations):
                err_msg = "Annotations required to contruct kube_fq_name for"+\
                    " object (%s:%s) was not found in input keyword args." %\
                    (namespace,name)
                raise Exception(err_msg)

        # Lookup annnoated-fq-name in annotated-fq-name to uuid table.
        return cls._ann_fq_name_to_uuid.get(
            tuple(cls._get_ann_fq_name_from_params(**annotations)))

    @classmethod
    def _update_ann_fq_name_to_uuid(cls, uuid, ann_fq_name):
        cls._ann_fq_name_to_uuid[tuple(ann_fq_name)] = uuid

    def build_fq_name_to_uuid(self, uuid, obj_dict):
        """Populate uuid in all tables tracking uuid."""
        if not obj_dict:
            return

        # Update annotated-fq-name to uuid table.
        self.ann_fq_name = self._get_ann_fq_name_from_obj(obj_dict)
        if self.ann_fq_name:
            self._update_ann_fq_name_to_uuid(uuid, self.ann_fq_name)

        # Update vnc fq-name to uuid table.
        self._update_fq_name_to_uuid(uuid, obj_dict)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        if obj.ann_fq_name:
            del cls._ann_fq_name_to_uuid[tuple(obj.ann_fq_name)]
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

    @classmethod
    def objects(cls):
        # Get all vnc objects of this class.
        return cls._dict.values()

class LoadbalancerKM(DBBaseKM):
    _dict = {}
    obj_type = 'loadbalancer'
    ann_fq_name_key = ["kind","name"]
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.loadbalancer_listeners = set()
        self.selectors = None
        self.annotations = None
        self.external_ip = None
        super(LoadbalancerKM, self).__init__(uuid, obj_dict)
        obj_dict = self.update(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.parent_uuid = obj['parent_uuid']
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('loadbalancer_listener', obj)
        if self.annotations:
            for kvp in self.annotations['key_value_pair'] or []:
                if kvp['key'] == 'externalIP':
                    self.external_ip = kvp['value']
                    break
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
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.loadbalancer = None
        self.loadbalancer_pool = None
        self.port_name = None
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
        self.build_fq_name_to_uuid(self.uuid, obj)
        if self.annotations:
            for kvp in self.annotations['key_value_pair'] or []:
                if kvp['key'] == 'portName':
                    self.port_name = kvp['value']
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
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

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
        self.build_fq_name_to_uuid(self.uuid, obj)
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
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

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
        self.build_fq_name_to_uuid(self.uuid, obj)
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
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

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
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)
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
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind","name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.owner = None
        self.virtual_router = None
        self.virtual_machine_interfaces = set()
        self.pod_labels = None
        self.pod_namespace = None
        self.pod_node = None
        self.node_ip = None
        super(VirtualMachineKM, self).__init__(uuid, obj_dict)
        obj_dict = self.update(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
            if not obj:
                return
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)
        if self.annotations:
            for kvp in self.annotations['key_value_pair'] or []:
                if kvp['key'] == 'owner':
                    self.owner = kvp['value']
                elif kvp['key'] == 'namespace':
                    self.pod_namespace = kvp['value']
                elif kvp['key'] == 'labels':
                    self.pod_labels = json.loads(kvp['value'])
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
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}
    _ip_addr_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machines = set()
        self.update(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)
        self.update_multiple_refs('virtual_machine', obj)

        self.virtual_router_ip_address = obj['virtual_router_ip_address']
        self.build_ip_addr_to_uuid(self.uuid, obj)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine', {})
        del cls._dict[uuid]

    @classmethod
    def build_ip_addr_to_uuid(cls, uuid, obj_dict):
        cls._ip_addr_to_uuid[tuple(obj_dict['virtual_router_ip_address'])] = uuid

    @classmethod
    def get_ip_addr_to_uuid(cls, ip_addr):
        return cls._ip_addr_to_uuid.get(tuple(ip_addr))


class VirtualMachineInterfaceKM(DBBaseKM):
    _dict = {}
    obj_type = 'virtual_machine_interface'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind","name"]
    _fq_name_to_uuid = {}

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
        super(VirtualMachineInterfaceKM, self).__init__(uuid, obj_dict)
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)


    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)

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
        if vlan < MIN_VLAN_ID or vlan > MAX_VLAN_ID:
            return
        if not self.vlan_bit_map:
            self.vlan_bit_map = BitArray(MAX_VLAN_ID + 1)
        self.vlan_bit_map[vlan] = 1

    def reset_vlan (self, vlan):
        if vlan < MIN_VLAN_ID or vlan > MAX_VLAN_ID:
            return
        if not self.vlan_bit_map:
            return
        self.vlan_bit_map[vlan] = 0

    def alloc_vlan (self):
        if not self.vlan_bit_map:
            self.vlan_bit_map = BitArray(MAX_VLAN_ID + 1)
        vid = self.vlan_bit_map.find('0b0', MIN_VLAN_ID)
        if vid:
            self.set_vlan(vid[0])
            return vid[0]
        return INVALID_VLAN_ID

    def get_vlan (self):
        return vlan_id

class VirtualNetworkKM(DBBaseKM):
    _dict = {}
    obj_type = 'virtual_network'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}
    ann_fq_name_key = ["kind","name"]

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.instance_ips = set()
        self.network_ipams = set()
        self.network_ipam_subnets = {}
        self.annotations = None
        self.k8s_namespace = None
        self.k8s_namespace_isolated = False
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.build_fq_name_to_uuid(self.uuid, obj)

        # Cache ipam-subnet-uuid to ipam-fq-name mapping.
        # This is useful when we would like to locate an ipam in a VN,
        # from which we would like to request ip allocation.
        self.network_ipam_subnets = {}
        # Iterate through ipam's on this VN.
        for ipam in obj.get('network_ipam_refs', []):
            # Get the ipam's attributes.
            ipam_attr = ipam.get('attr', None)
            # Get the ipam fq-name.
            ipam_fq_name = ipam['to']
            if ipam_attr:
                # Iterate through ipam subnets to cache uuid - fqname mapping.
                for subnet in ipam_attr.get('ipam_subnets', []):
                    subnet_uuid = subnet.get('subnet_uuid', None)
                    if subnet_uuid:
                        self.network_ipam_subnets[subnet_uuid] = ipam_fq_name

        # Get annotations on this virtual network.
        self.annotations = obj.get('annotations', {})
        for kvp in self.annotations.get('key_value_pair', []):
            if kvp.get('key') == 'namespace':
                self.k8s_namespace = kvp.get('value')
            if kvp.get('key') == 'isolated':
                self.k8s_namespace_isolated = True if kvp.get('value') ==\
                    'True' else False

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

    # Given an ipam-fq-name, return its subnet uuid on this VN.
    def get_ipam_subnet_uuid(self, ipam_fq_name):
        for subnet_uuid, fq_name in self.network_ipam_subnets.iteritems():
            if fq_name == ipam_fq_name:
                return subnet_uuid
        return None

    def is_k8s_namespace_isolated(self):
        return self.k8s_namespace_isolated

class InstanceIpKM(DBBaseKM):
    _dict = {}
    obj_type = 'instance_ip'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind","name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.address = None
        self.family = None
        self.virtual_machine_interfaces = set()
        self.virtual_networks = set()
        self.floating_ips = set()
        super(InstanceIpKM, self).__init__(uuid, obj_dict)
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
        self.floating_ips = set([fip['uuid']
                            for fip in obj.get('floating_ips', [])])

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.update_multiple_refs('virtual_network', {})
        del cls._dict[uuid]

    @classmethod
    def get_object(cls, ip, vn_fq_name):
        items = cls._dict.items()
        for uuid, iip_obj in items:
            if ip == iip_obj.address:
                vn_uuid = VirtualNetworkKM.get_fq_name_to_uuid(vn_fq_name)
                if vn_uuid and vn_uuid in iip_obj.virtual_networks:
                    return iip_obj
        return None

# end class InstanceIpKM

class ProjectKM(DBBaseKM):
    _dict = {}
    obj_type = 'project'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.ns_labels = {}
        self.virtual_networks = set()
        self.annotations = None
        self.k8s_namespace_isolated = False
        self.k8s_namespace_uuid = None
        self.k8s_namespace_name = None
        self.security_groups = set()
        obj_dict = self.update(obj_dict)
        self.set_children('virtual_network', obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.build_fq_name_to_uuid(self.uuid, obj)

        # Update SecurityGroup info.
        sg_list = obj.get('security_groups', [])
        for sg in sg_list:
            self.security_groups.add(sg['uuid'])

        self.annotations = obj.get('annotations', {})
        for kvp in self.annotations.get('key_value_pair', []):
            if kvp.get('key') == 'isolated':
                self.k8s_namespace_isolated = True if kvp.get('value') ==\
                    'True' else False
            if kvp.get('key') == 'k8s_uuid':
                self.k8s_namespace_uuid = kvp.get('value')
            if kvp.get('key') == 'name':
                self.k8s_namespace_name = kvp.get('value')
        return obj

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        del cls._dict[uuid]

    def get_security_groups(self):
        return set(self.security_groups)

    def is_k8s_namespace_isolated(self):
        return self.k8s_namespace_isolated

    def get_k8s_namespace_uuid(self):
        return self.k8s_namespace_uuid

    def get_k8s_namespace_name(self):
        return self.k8s_namespace_name

    def add_security_group(self, sg_uuid):
        self.security_groups.add(sg_uuid)

    def remove_security_group(self, sg_uuid):
        self.security_groups.discard(sg_uuid)

class DomainKM(DBBaseKM):
    _dict = {}
    obj_type = 'domain'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        del cls._dict[uuid]

class SecurityGroupKM(DBBaseKM):
    _dict = {}
    obj_type = 'security_group'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}
    ann_fq_name_key = ["name"]

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.project_uuid = None
        self.virtual_machine_interfaces = set()
        self.annotations = None
        self.namespace = None
        self.owner = None
        self.np_spec = {}
        self.np_pod_selector = {}
        self.ingress_pod_selector = {}
        self.ingress_pod_sgs = set()
        self.ingress_ns_sgs = set()
        self.np_sgs = set()
        self.rule_entries = None
        super(SecurityGroupKM, self).__init__(uuid, obj_dict)
        obj_dict = self.update(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.build_fq_name_to_uuid(self.uuid, obj)
        self.annotations = obj.get('annotations', {})
        for kvp in self.annotations.get('key_value_pair', []):
            if kvp.get('key') == 'namespace':
                self.namespace = kvp.get('value')
            if kvp.get('key') == 'owner':
                self.owner = kvp.get('value')
            elif kvp.get('key') == 'np_spec':
                self.np_spec = json.loads(kvp.get('value'))
            elif kvp.get('key') == 'np_pod_selector':
                self.np_pod_selector = json.loads(kvp.get('value'))
            elif kvp.get('key') == 'ingress_pod_selector':
                self.ingress_pod_selector = json.loads(kvp.get('value'))
            elif kvp.get('key') == 'ingress_pod_sgs':
                self.ingress_pod_sgs = set(json.loads(kvp.get('value')))
            elif kvp.get('key') == 'ingress_ns_sgs':
                self.ingress_ns_sgs = set(json.loads(kvp.get('value')))
            elif kvp.get('key') == 'np_sgs':
                self.np_sgs = set(json.loads(kvp.get('value')))

        # Register this SG uuid with its project.
        #
        # This information is used during k8s namespace deletion to cross
        # validate SG-Project association.
        if obj['parent_type'] == "project":
            proj = ProjectKM.get(obj['parent_uuid'])
            if proj:
                proj.add_security_group(self.uuid)
                self.project_uuid = proj.uuid

        self.rule_entries = obj.get('security_group_entries', None)
        self.update_multiple_refs('virtual_machine_interface', obj)
        return obj

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]

        # Un-register this SG uuid from its project.
        if obj.project_uuid:
            proj = ProjectKM.get(obj.project_uuid)
            if proj:
                proj.remove_security_group(uuid)

        obj.update_multiple_refs('virtual_machine_interface', {})
        super(SecurityGroupKM, cls).delete(uuid)
        del cls._dict[uuid]

class FloatingIpPoolKM(DBBaseKM):
    _dict = {}
    obj_type = 'floating_ip_pool'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_network = None
        self.update(obj_dict)

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)
        self.update_single_ref('virtual_network', obj)
        if 'floating_ip_pool_subnets' in obj:
            self.floating_ip_pool_subnets = obj['floating_ip_pool_subnets']

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('virtual_network', None)
        del cls._dict[uuid]

class FloatingIpKM(DBBaseKM):
    _dict = {}
    obj_type = 'floating_ip'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind","name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.address = None
        self.parent_uuid = None
        self.virtual_machine_interfaces = set()
        self.virtual_ip = None
        self.update(obj_dict)
        if self.parent_uuid:
            iip = InstanceIpKM.get(self.parent_uuid)
            if iip:
                iip.floating_ips.add(self.uuid)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.address = obj['floating_ip_address']
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.parent_uuid = self.get_parent_uuid(obj)
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)
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
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        del cls._dict[uuid]
# end class NetworkIpamKM
