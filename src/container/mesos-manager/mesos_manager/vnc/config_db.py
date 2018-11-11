#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for mesos manager
"""
import json

from cfgm_common.vnc_db import DBBase
from bitstring import BitArray
from vnc_api.vnc_api import (KeyValuePair)
from mesos_manager.vnc.vnc_mesos_config import VncMesosConfig as vnc_mesos_config
#from mesos_manager.sandesh.mesos_introspect import ttypes as introspect

class DBBaseMM(DBBase):
    obj_type = __name__

    # Infra annotations that will be added on objects with custom annotations.
    ann_fq_name_infra_key = ["project", "cluster", "owner"]

    def __init__(self, uuid, obj_dict=None):
        # By default there are no annotations added on an object.
        self.ann_fq_name = None

    @staticmethod
    def get_infra_annotations():
        """Get infra annotations."""
        annotations = {}
        annotations['owner'] = vnc_mesos_config.cluster_owner()
        annotations['cluster'] = vnc_mesos_config.cluster_name()

        return annotations

    @classmethod
    def _get_annotations(cls, vnc_caller, name, mesos_type,
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
        infra_anns['project'] = vnc_mesos_config.cluster_project_name()
        annotations.update(infra_anns)

        # Update annotations based on explicity input params.
        input_anns = {}
        input_anns['name'] = name
        if mesos_type:
            input_anns['kind'] = mesos_type
        annotations.update(input_anns)

        # Append other custom annotations.
        annotations.update(custom_ann_kwargs)

        return annotations

    @classmethod
    def add_annotations(cls, vnc_caller, obj, name, mesos_type=None,
                        **custom_ann_kwargs):
        """Add annotations on the input object.

        Given an object, this method will add all required and specfied
        annotations on that object.
        """
        # Construct annotations to be added on the object.
        annotations = cls._get_annotations(vnc_caller, name,
                                           mesos_type, **custom_ann_kwargs)

        # Validate that annotations have all the info to construct
        # the annotations-based-fq-name as required by the object's db.
        if hasattr(cls, 'ann_fq_name_key'):
            if not set(cls.ann_fq_name_key).issubset(annotations):
                err_msg = "Annotations required to contruct mesos_fq_name for"+\
                    " object (%s:%s) was not found in input keyword args." %\
                    (name)
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
            for key, value in kwargs.iteritems():
                if key != elem:
                    continue
                fq_name.append(value)
                break
        return fq_name

    @classmethod
    def get_ann_fq_name_to_uuid(cls, vnc_caller, name,
                                mesos_type=None, **kwargs):
        """Get vnc object uuid corresponding to an annotated-fq-name.

        The annotated-fq-name is constructed from the input params given
        by the caller.
        """
        # Construct annotations based on input params.
        annotations = cls._get_annotations(vnc_caller, name,
                                           mesos_type, **kwargs)

        # Validate that annoatations has all info required for construction
        # of annotated-fq-name.
        if hasattr(cls, 'ann_fq_name_key'):
            if not set(cls.ann_fq_name_key).issubset(annotations):
                err_msg = "Annotations required to contruct mesos_fq_name for"+\
                    " object (%s:%s) was not found in input keyword args." %\
                    (name)
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
            if tuple(obj.ann_fq_name) in cls._ann_fq_name_to_uuid:
                del cls._ann_fq_name_to_uuid[tuple(obj.ann_fq_name)]

        if tuple(obj.fq_name) in cls._fq_name_to_uuid:
            del cls._fq_name_to_uuid[tuple(obj.fq_name)]

    def evaluate(self):
        # Implement in the derived class
        pass

    @classmethod
    def objects(cls):
        # Get all vnc objects of this class.
        return cls._dict.values()

    @staticmethod
    def _build_annotation_dict(annotation_dict):
        return {str(annot['key']): str(annot['value'])
                      for annot
                      in annotation_dict['key_value_pair']} \
            if annotation_dict and annotation_dict.get('key_value_pair') \
            else {}

    @staticmethod
    def _build_string_dict(src_dict):
        dst_dict = {}
        if src_dict:
            for key, value in src_dict.iteritems():
                dst_dict[str(key)] = str(value)
        return dst_dict

    @staticmethod
    def _build_cls_uuid_list(cls, collection):
        return [cls(str(list(collection)[i]))
                   for i in xrange(len(collection))] \
            if collection else []

class VirtualMachineMM(DBBaseMM):
    _dict = {}
    obj_type = 'virtual_machine'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.owner = None
        self.cluster = None
        self.virtual_router = None
        self.virtual_machine_interfaces = set()
        self.pod_labels = None
        self.pod_node = None
        self.node_ip = None
        super(VirtualMachineMM, self).__init__(uuid, obj_dict)
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
                elif kvp['key'] == 'cluster':
                    self.cluster = kvp['value']
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
        super(VirtualMachineMM, cls).delete(uuid)
        del cls._dict[uuid]

class VirtualRouterMM(DBBaseMM):
    _dict = {}
    obj_type = 'virtual_router'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}
    _ip_addr_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        super(VirtualRouterMM, self).__init__(uuid, obj_dict)
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

        self.virtual_router_ip_address = obj.get('virtual_router_ip_address')
        if self.virtual_router_ip_address:
            self.build_ip_addr_to_uuid(
                self.uuid, self.virtual_router_ip_address)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine', {})
        del cls._dict[uuid]

    @classmethod
    def build_ip_addr_to_uuid(cls, uuid, ip_addr):
        cls._ip_addr_to_uuid[tuple(ip_addr)] = uuid

    @classmethod
    def get_ip_addr_to_uuid(cls, ip_addr):
        return cls._ip_addr_to_uuid.get(tuple(ip_addr))

class VirtualMachineInterfaceMM(DBBaseMM):
    _dict = {}
    obj_type = 'virtual_machine_interface'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        super(VirtualMachineInterfaceMM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.host_id = None
        self.virtual_network = None
        self.virtual_machine = None
        self.instance_ips = set()
        self.floating_ips = set()
        self.virtual_machine_interfaces = set()
        self.security_groups = set()
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

        return obj

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]

        obj.update_multiple_refs('instance_ip', {})
        obj.update_multiple_refs('floating_ip', {})
        obj.update_single_ref('virtual_network', {})
        obj.update_single_ref('virtual_machine', {})
        obj.update_multiple_refs('security_group', {})
        obj.update_multiple_refs('virtual_machine_interface', {})

        obj.remove_from_parent()
        del cls._dict[uuid]

class VirtualNetworkMM(DBBaseMM):
    _dict = {}
    obj_type = 'virtual_network'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]

    def __init__(self, uuid, obj_dict=None):
        super(VirtualNetworkMM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.instance_ips = set()
        self.network_ipams = set()
        self.network_ipam_subnets = {}
        self.annotations = None
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

class InstanceIpMM(DBBaseMM):
    _dict = {}
    obj_type = 'instance_ip'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        super(InstanceIpMM, self).__init__(uuid, obj_dict)
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
                vn_uuid = VirtualNetworkMM.get_fq_name_to_uuid(vn_fq_name)
                if vn_uuid and vn_uuid in iip_obj.virtual_networks:
                    return iip_obj
        return None

# end class InstanceIpMM

class ProjectMM(DBBaseMM):
    _dict = {}
    obj_type = 'project'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        super(ProjectMM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.ns_labels = {}
        self.virtual_networks = set()
        self.annotations = None
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
        return obj

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        del cls._dict[uuid]

    def get_security_groups(self):
        return set(self.security_groups)

    def add_security_group(self, sg_uuid):
        self.security_groups.add(sg_uuid)

    def remove_security_group(self, sg_uuid):
        self.security_groups.discard(sg_uuid)

class DomainMM(DBBaseMM):
    _dict = {}
    obj_type = 'domain'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        super(DomainMM, self).__init__(uuid, obj_dict)
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
        del cls._dict[uuid]

class NetworkIpamMM(DBBaseMM):
    _dict = {}
    obj_type = 'network_ipam'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        super(NetworkIpamMM, self).__init__(uuid, obj_dict)
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

# end class NetworkIpamMM

class NetworkPolicyMM(DBBaseMM):
    _dict = {}
    obj_type = 'network_policy'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None):
        super(NetworkPolicyMM, self).__init__(uuid, obj_dict)
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
# end class NetworkPolicyMM
