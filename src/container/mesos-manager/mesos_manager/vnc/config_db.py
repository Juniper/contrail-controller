#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for mesos manager
"""
from builtins import str
from builtins import range
import json

from cfgm_common.vnc_db import DBBase
from bitstring import BitArray
from vnc_api.vnc_api import (KeyValuePair)
from mesos_manager.vnc.vnc_mesos_config import VncMesosConfig as vnc_mesos_config
from mesos_manager.sandesh.mesos_introspect import ttypes as introspect

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
        for ann_key, ann_value in annotations.items():
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
            for key, value in kwargs.items():
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
    def delete(cls, uuid, request_id=None):
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
        return list(cls._dict.values())

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
            for key, value in src_dict.items():
                dst_dict[str(key)] = str(value)
        return dst_dict

    @staticmethod
    def _build_cls_uuid_list(cls, collection):
        return [cls(str(list(collection)[i]))
                   for i in range(len(collection))] \
            if collection else []

class VirtualMachineMM(DBBaseMM):
    _dict = {}
    obj_type = 'virtual_machine'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
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

    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('virtual_router', {})
        obj.update_multiple_refs('virtual_machine_interface', {})
        super(VirtualMachineMM, cls).delete(uuid)
        del cls._dict[uuid]

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Virtual Machine DB lookup/introspect request. """
        vm_resp = introspect.VirtualMachineDatabaseListResp(vms=[])

        # Iterate through all elements of Virtual Machine DB.
        for vm in VirtualMachineMM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.vm_uuid and req.vm_uuid != vm.uuid:
                continue

            vm_annotations = cls._build_annotation_dict(vm.annotations)

            vmis = cls._build_cls_uuid_list(
                    introspect.VMIUuid, vm.virtual_machine_interfaces)
            vr = introspect.VRUuid(vr_uuid=str(vm.virtual_router)) \
                if vm.virtual_router else None

            # Construct response for an element.
            vm_instance = introspect.VirtualMachineInstance(
                uuid=vm.uuid,
                name=vm.name,
                cluster=vm.cluster,
                annotations=vm_annotations,
                owner=vm.owner,
                node_ip=str(vm.node_ip),
                pod_node=vm.pod_node,
                pod_labels=vm.pod_labels,
                vm_interfaces=vmis,
                vrouter_uuid=vr)

            # Append the constructed element info to the response.
            vm_resp.vms.append(vm_instance)

        # Send the reply out.
        vm_resp.response(req.context())

class VirtualRouterMM(DBBaseMM):
    _dict = {}
    obj_type = 'virtual_router'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}
    _ip_addr_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(VirtualRouterMM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.virtual_machines = set()
        self.update(obj_dict)

    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
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

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Virtual Router DB lookup/introspect request. """
        vr_resp = introspect.VirtualRouterDatabaseListResp(vrs=[])

        # Iterate through all elements of Virtual Router DB.
        for vr in VirtualRouterMM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.vr_uuid and req.vr_uuid != vr.uuid:
                continue

            vr_annotations = cls._build_annotation_dict(vr.annotations)

            vms = cls._build_cls_uuid_list(
                    introspect.VMUuid, vr.virtual_machines)

            # Construct response for an element.
            vr_instance = introspect.VirtualRouterInstance(
                uuid=vr.uuid,
                name=vr.fq_name[-1],
                fq_name=vr.fq_name,
                annotations=vr_annotations,
                virtual_machines=vms)

            # Append the constructed element info to the response.
            vr_resp.vrs.append(vr_instance)

        # Send the reply out.
        vr_resp.response(req.context())

class VirtualMachineInterfaceMM(DBBaseMM):
    _dict = {}
    obj_type = 'virtual_machine_interface'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
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


    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
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

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Virtual Machine Interface DB lookup/introspect request. """
        vmi_resp = introspect.VirtualMachineInterfaceDatabaseListResp(vmis=[])

        # Iterate through all elements of Virtual Router DB.
        for vmi in VirtualMachineInterfaceMM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.vmi_uuid and req.vmi_uuid != vmi.uuid:
                continue

            vmi_annotations = cls._build_annotation_dict(vmi.annotations)

            fips = cls._build_cls_uuid_list(
                    introspect.FIPUuid, vmi.floating_ips)
            sgs = cls._build_cls_uuid_list(
                    introspect.SGUuid, vmi.security_groups)
            vmis = cls._build_cls_uuid_list(
                    introspect.VMIUuid, vmi.virtual_machine_interfaces)

            # Construct response for an element.
            vmi_instance = introspect.VirtualMachineInterfaceInstance(
                uuid=vmi.uuid,
                name=vmi.fq_name[-1],
                fq_name=vmi.fq_name,
                annotations=vmi_annotations,
                floating_ips=fips,
                host_id=vmi.host_id,
                security_groups=sgs,
                virtual_machine=str(vmi.virtual_machine),
                virtual_machine_interfaces=vmis,
                virtual_network=str(vmi.virtual_network))

            # Append the constructed element info to the response.
            vmi_resp.vmis.append(vmi_instance)

        # Send the reply out.
        vmi_resp.response(req.context())

class VirtualNetworkMM(DBBaseMM):
    _dict = {}
    obj_type = 'virtual_network'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(VirtualNetworkMM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.instance_ips = set()
        self.network_ipams = set()
        self.network_ipam_subnets = {}
        self.annotations = None
        obj_dict = self.update(obj_dict)
        self.add_to_parent(obj_dict)

    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
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
        for subnet_uuid, fq_name in self.network_ipam_subnets.items():
            if fq_name == ipam_fq_name:
                return subnet_uuid
        return None

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Virtual Network DB lookup/introspect request. """
        vn_resp = introspect.VirtualNetworkDatabaseListResp(vns=[])

        # Iterate through all elements of Virtual Network DB.
        for vn in VirtualNetworkMM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.vn_uuid and req.vn_uuid != vn.uuid:
                continue

            vn_annotations = cls._build_annotation_dict(vn.annotations)

            ipam_subnets = [introspect.NetworkIpamSubnetInstance(
                uuid=sub[0], fq_name=sub[1])
                            for sub
                            in vn.network_ipam_subnets.items()]

            vmis = cls._build_cls_uuid_list(
                    introspect.VMIUuid, vn.virtual_machine_interfaces)
            iips = cls._build_cls_uuid_list(
                    introspect.IIPUuid, vn.instance_ips)
            nipams = cls._build_cls_uuid_list(
                    introspect.NIPAMUuid, vn.network_ipams)

            # Construct response for an element.
            vn_instance = introspect.VirtualNetworkInstance(
                uuid=vn.uuid,
                name=vn.fq_name[-1],
                fq_name=vn.fq_name,
                annotations=vn_annotations,
                virtual_machine_interfaces=vmis,
                instance_ips=iips,
                network_ipams=nipams,
                network_ipam_subnets=ipam_subnets)

            # Append the constructed element info to the response.
            vn_resp.vns.append(vn_instance)

        # Send the reply out.
        vn_resp.response(req.context())

class InstanceIpMM(DBBaseMM):
    _dict = {}
    obj_type = 'instance_ip'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(InstanceIpMM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.address = None
        self.family = None
        self.virtual_machine_interfaces = set()
        self.virtual_networks = set()
        self.floating_ips = set()
        self.update(obj_dict)

    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.update_multiple_refs('virtual_network', {})
        del cls._dict[uuid]

    @classmethod
    def get_object(cls, ip, vn_fq_name):
        items = list(cls._dict.items())
        for uuid, iip_obj in items:
            if ip == iip_obj.address:
                vn_uuid = VirtualNetworkMM.get_fq_name_to_uuid(vn_fq_name)
                if vn_uuid and vn_uuid in iip_obj.virtual_networks:
                    return iip_obj
        return None

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to InstanceIp DB lookup/introspect request. """
        iip_resp = introspect.InstanceIpDatabaseListResp(iips=[])

        # Iterate through all elements of InstanceIp DB.
        for iip in InstanceIpMM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.iip_uuid and req.iip_uuid != iip.uuid:
                continue

            vmis = cls._build_cls_uuid_list(
                    introspect.VMIUuid, iip.virtual_machine_interfaces)
            vns = cls._build_cls_uuid_list(
                    introspect.VNUuid, iip.virtual_networks)
            fips = cls._build_cls_uuid_list(
                    introspect.FIPUuid, iip.floating_ips)

            # Construct response for an element.
            iip_instance = introspect.InstanceIpInstance(
                uuid=iip.uuid,
                name=iip.fq_name[-1],
                fq_name=iip.fq_name,
                address=str(iip.address),
                family=iip.family,
                vm_interfaces=vmis,
                virtual_networks=vns,
                floating_ips=fips)

            # Append the constructed element info to the response.
            iip_resp.iips.append(iip_instance)

        # Send the reply out.
        iip_resp.response(req.context())

# end class InstanceIpMM

class ProjectMM(DBBaseMM):
    _dict = {}
    obj_type = 'project'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(ProjectMM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.ns_labels = {}
        self.virtual_networks = set()
        self.annotations = None
        self.security_groups = set()
        obj_dict = self.update(obj_dict)
        self.set_children('virtual_network', obj_dict)

    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        del cls._dict[uuid]

    def get_security_groups(self):
        return set(self.security_groups)

    def add_security_group(self, sg_uuid):
        self.security_groups.add(sg_uuid)

    def remove_security_group(self, sg_uuid):
        self.security_groups.discard(sg_uuid)

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Project DB lookup/introspect request. """
        project_resp = introspect.ProjectDatabaseListResp(projects=[])

        # Iterate through all elements of Project DB.
        for project in ProjectMM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.project_uuid and req.project_uuid != project.uuid:
                continue

            project_annotations = cls._build_annotation_dict(
                project.annotations)

            ns_labels = cls._build_string_dict(project.ns_labels)

            sgs = cls._build_cls_uuid_list(
                    introspect.SGUuid, project.security_groups)
            vns = cls._build_cls_uuid_list(
                    introspect.VNUuid, project.virtual_networks)

            # Construct response for an element.
            project_instance = introspect.ProjectInstance(
                uuid=project.uuid,
                name=project.fq_name[-1],
                fq_name=project.fq_name,
                annotations=project_annotations,
                ns_labels=ns_labels,
                security_groups=sgs,
                virtual_networks=vns)

            # Append the constructed element info to the response.
            project_resp.projects.append(project_instance)

        # Send the reply out.
        project_resp.response(req.context())

class DomainMM(DBBaseMM):
    _dict = {}
    obj_type = 'domain'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(DomainMM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.update(obj_dict)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        del cls._dict[uuid]

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Domain DB lookup/introspect request. """
        domain_resp = introspect.DomainDatabaseListResp(domains=[])

        # Iterate through all elements of Domain DB.
        for domain in DomainMM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.domain_uuid and req.domain_uuid != domain.uuid:
                continue

            domain_annotations = cls._build_annotation_dict(
                domain.annotations)

            # Construct response for an element.
            domain_instance = introspect.DomainInstance(
                uuid=domain.uuid,
                name=domain.fq_name[-1],
                fq_name=domain.fq_name,
                annotations=domain_annotations)

            # Append the constructed element info to the response.
            domain_resp.domains.append(domain_instance)

        # Send the reply out.
        domain_resp.response(req.context())

class NetworkIpamMM(DBBaseMM):
    _dict = {}
    obj_type = 'network_ipam'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(NetworkIpamMM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)
    # end update

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        del cls._dict[uuid]

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to NetworkIpam DB lookup/introspect request. """
        network_ipam_resp = introspect.NetworkIpamDatabaseListResp(
            network_ipams=[])

        # Iterate through all elements of NetworkIpam DB.
        for network_ipam in NetworkIpamMM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.network_ipam_uuid \
                and req.network_ipam_uuid != network_ipam.uuid:
                continue

            network_ipam_annotations = cls._build_annotation_dict(
                network_ipam.annotations)

            # Construct response for an element.
            network_ipam_instance = introspect.NetworkIpamInstance(
                uuid=network_ipam.uuid,
                name=network_ipam.fq_name[-1],
                fq_name=network_ipam.fq_name,
                annotations=network_ipam_annotations)

            # Append the constructed element info to the response.
            network_ipam_resp.network_ipams.append(network_ipam_instance)

        # Send the reply out.
        network_ipam_resp.response(req.context())

# end class NetworkIpamMM

class NetworkPolicyMM(DBBaseMM):
    _dict = {}
    obj_type = 'network_policy'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(NetworkPolicyMM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)
    # end update

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        del cls._dict[uuid]
# end class NetworkPolicyMM
