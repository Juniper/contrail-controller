#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for kube manager
"""
from builtins import str
from builtins import range
import json

from cfgm_common.vnc_db import DBBase
from bitstring import BitArray
from vnc_api.vnc_api import (KeyValuePair)
from kube_manager.vnc.vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from kube_manager.sandesh.kube_introspect import ttypes as introspect

INVALID_VLAN_ID = 4096
MAX_VLAN_ID = 4095
MIN_VLAN_ID = 1

class DBBaseKM(DBBase):
    obj_type = __name__
    _nested_mode = False

    # Infra annotations that will be added on objects with custom annotations.
    ann_fq_name_infra_key = ["project", "cluster", "owner"]

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
                    (namespace, name)
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
                    (namespace, name)
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

    @staticmethod
    def is_nested():
        """Return nested mode enable/disable config value."""
        return DBBaseKM._nested_mode

    @staticmethod
    def set_nested(val):
        """Configured nested mode value.

        True : Enable nested mode.
        False : Disable nested mode.
        """
        DBBaseKM._nested_mode = val

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

class LoadbalancerKM(DBBaseKM):
    _dict = {}
    obj_type = 'loadbalancer'
    ann_fq_name_key = ["kind", "name"]
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(LoadbalancerKM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.loadbalancer_listeners = set()
        self.selectors = None
        self.annotations = None
        self.external_ip = None
        self.service_name = None
        self.service_namespace = None
        self.firewall_rule_uuids = set()
        obj_dict = self.update(obj_dict)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.parent_uuid = obj['parent_uuid']
        self.annotations = obj.get('annotations', None)
        self.build_fq_name_to_uuid(self.uuid, obj)
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('loadbalancer_listener', obj)
        name = None
        namespace = None
        owner = None
        if self.annotations:
            for kvp in self.annotations['key_value_pair'] or []:
                if kvp['key'] == 'externalIP':
                    self.external_ip = kvp['value']
                elif kvp['key'] == 'name':
                    name = kvp['value']
                elif kvp['key'] == 'namespace':
                    namespace = kvp['value']
                elif kvp['key'] == 'owner':
                    owner = kvp['value']

            if owner == 'k8s':
                self.service_name = name
                self.service_namespace = namespace

        return obj

    def add_firewall_rule(self, fw_uuid):
        if fw_uuid:
            self.firewall_rule_uuids.add(fw_uuid)

    def remove_firewall_rule(self, fw_uuid):
        if fw_uuid in self.firewall_rule_uuids:
            self.firewall_rule_uuids.remove(fw_uuid)

    def get_firewall_rules(self):
        return self.firewall_rule_uuids

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.update_multiple_refs('loadbalancer_listener', {})
        super(LoadbalancerKM, cls).delete(uuid)
        del cls._dict[uuid]

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Loadbalancer DB lookup/introspect request. """
        lb_resp = introspect.LoadbalancerDatabaseListResp(lbs=[])

        # Iterate through all elements of Loadbalancer DB.
        for lb in LoadbalancerKM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.lb_uuid and req.lb_uuid != lb.uuid:
                continue

            lb_annotations = cls._build_annotation_dict(lb.annotations)

            lb_listeners = cls._build_cls_uuid_list(
                    introspect.LbListenerUuid, lb.loadbalancer_listeners)
            vmis = cls._build_cls_uuid_list(
                    introspect.VMIUuid, lb.virtual_machine_interfaces)

            # Construct response for an element.
            if 'Ingress' in lb.ann_fq_name:
                lb_instance = introspect.LoadbalancerInstance(
                    uuid_to_ingress=lb.uuid,
                    name=lb.fq_name[-1],
                    fq_name=lb.fq_name,
                    annotations=lb_annotations,
                    external_ip=str(lb.external_ip),
                    lb_listeners=lb_listeners,
                    selectors=None,
                    vm_interfaces=vmis)
            else:
                lb_instance = introspect.LoadbalancerInstance(
                    uuid_to_service=lb.uuid,
                    name=lb.fq_name[-1],
                    fq_name=lb.fq_name,
                    annotations=lb_annotations,
                    external_ip=str(lb.external_ip),
                    lb_listeners=lb_listeners,
                    selectors=None,
                    vm_interfaces=vmis)

            # Append the constructed element info to the response.
            lb_resp.lbs.append(lb_instance)

        # Send the reply out.
        lb_resp.response(req.context())


class LoadbalancerListenerKM(DBBaseKM):
    _dict = {}
    obj_type = 'loadbalancer_listener'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(LoadbalancerListenerKM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.loadbalancer = None
        self.loadbalancer_pool = None
        self.port_name = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('loadbalancer', {})
        obj.update_single_ref('loadbalancer_pool', {})
        del cls._dict[uuid]
    # end delete

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to LoadbalancerListener DB lookup/introspect request. """
        lbl_resp = introspect.LoadbalancerListenerDatabaseListResp(lbls=[])

        # Iterate through all elements of LoadbalancerListener DB.
        for lbl in LoadbalancerListenerKM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.lbl_uuid and req.lbl_uuid != lbl.uuid:
                continue

            lbl_annotations = cls._build_annotation_dict(lbl.annotations)
            id_perms = cls._build_string_dict(lbl.id_perms)

            # Construct response for an element.
            lbl_instance = introspect.LoadbalancerListenerInstance(
                uuid=lbl.uuid,
                name=lbl.display_name,
                fq_name=[lbl.display_name],
                annotations=lbl_annotations,
                id_perms=id_perms,
                loadbalancer=lbl.loadbalancer,
                loadbalancer_pool=lbl.loadbalancer_pool,
                port_name=lbl.port_name,
                parent_uuid=lbl.parent_uuid)

            # Append the constructed element info to the response.
            lbl_resp.lbls.append(lbl_instance)

        # Send the reply out.
        lbl_resp.response(req.context())
# end class LoadbalancerListenerKM

class LoadbalancerPoolKM(DBBaseKM):
    _dict = {}
    obj_type = 'loadbalancer_pool'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(LoadbalancerPoolKM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.members = set()
        self.loadbalancer_listener = None
        self.custom_attributes = []
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('loadbalancer_listener', {})
        del cls._dict[uuid]
    # end delete

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to LoadbalancerPool DB lookup/introspect request. """
        lbp_resp = introspect.LoadbalancerPoolDatabaseListResp(lbps=[])

        # Iterate through all elements of LoadbalancerPool DB.
        for lbp in LoadbalancerPoolKM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.lbp_uuid and req.lbp_uuid != lbp.uuid:
                continue

            lbp_annotations = cls._build_annotation_dict(lbp.annotations)
            id_perms = cls._build_string_dict(lbp.id_perms)
            params = cls._build_string_dict(lbp.params)

            # Construct response for an element.
            lbp_instance = introspect.LoadbalancerPoolInstance(
                uuid=lbp.uuid,
                name=lbp.fq_name[-1],
                fq_name=lbp.fq_name,
                annotations=lbp_annotations,
                custom_attributes=lbp.custom_attributes,
                id_perms=id_perms,
                loadbalancer_listener=lbp.loadbalancer_listener,
                members=list(lbp.members),
                params=params,
                parent_uuid=lbp.parent_uuid,
                provider=str(lbp.provider))

            # Append the constructed element info to the response.
            lbp_resp.lbps.append(lbp_instance)

        # Send the reply out.
        lbp_resp.response(req.context())
# end class LoadbalancerPoolKM

class LoadbalancerMemberKM(DBBaseKM):
    _dict = {}
    obj_type = 'loadbalancer_member'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(LoadbalancerMemberKM, self).__init__(uuid, obj_dict)
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

    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        if obj.loadbalancer_pool:
            parent = LoadbalancerPoolKM.get(obj.loadbalancer_pool)
        if parent:
            parent.members.discard(obj.uuid)
        del cls._dict[uuid]
    # end delete

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to LoadbalancerMember DB lookup/introspect request. """
        lbm_resp = introspect.LoadbalancerMemberDatabaseListResp(lbms=[])

        # Iterate through all elements of LoadbalancerMember DB.
        for lbm in LoadbalancerMemberKM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.lbm_uuid and req.lbm_uuid != lbm.uuid:
                continue

            lbm_annotations = cls._build_annotation_dict(lbm.annotations)

            # Construct response for an element.
            lbm_instance = introspect.LoadbalancerPoolInstance(
                uuid=lbm.uuid,
                name=lbm.fq_name[-1],
                fq_name=lbm.fq_name,
                annotations=lbm_annotations)

            # Append the constructed element info to the response.
            lbm_resp.lbms.append(lbm_instance)

        # Send the reply out.
        lbm_resp.response(req.context())
# end class LoadbalancerMemberKM

class HealthMonitorKM(DBBaseKM):
    _dict = {}
    obj_type = 'loadbalancer_healthmonitor'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(HealthMonitorKM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.loadbalancer_pools = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('loadbalancer_pool', {})
        del cls._dict[uuid]
    # end delete

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to HealthMonitor DB lookup/introspect request. """
        hm_resp = introspect.HealthMonitorDatabaseListResp(hms=[])

        # Iterate through all elements of HealthMonitor DB.
        for hm in HealthMonitorKM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.hm_uuid and req.hm_uuid != hm.uuid:
                continue

            hm_annotations = cls._build_annotation_dict(hm.annotations)

            # Construct response for an element.
            hm_instance = introspect.HealthMonitorInstance(
                uuid=hm.uuid,
                name=hm.fq_name[-1],
                fq_name=hm.fq_name,
                annotations=hm_annotations)

            # Append the constructed element info to the response.
            hm_resp.hms.append(hm_instance)

        # Send the reply out.
        hm_resp.response(req.context())
# end class HealthMonitorKM


class VirtualMachineKM(DBBaseKM):
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
        self.pod_namespace = None
        self.pod_node = None
        self.node_ip = None
        super(VirtualMachineKM, self).__init__(uuid, obj_dict)
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
                elif kvp['key'] == 'namespace':
                    self.pod_namespace = kvp['value']
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
        super(VirtualMachineKM, cls).delete(uuid)
        del cls._dict[uuid]

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Virtual Machine DB lookup/introspect request. """
        vm_resp = introspect.VirtualMachineDatabaseListResp(vms=[])

        # Iterate through all elements of Virtual Machine DB.
        for vm in VirtualMachineKM.objects():

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
                annotations=vm_annotations,
                owner=vm.owner,
                node_ip=str(vm.node_ip),
                pod_namespace=vm.pod_namespace,
                pod_node=vm.pod_node,
                pod_labels=vm.pod_labels,
                vm_interfaces=vmis,
                vrouter_uuid=vr)

            # Append the constructed element info to the response.
            vm_resp.vms.append(vm_instance)

        # Send the reply out.
        vm_resp.response(req.context())


class VirtualRouterKM(DBBaseKM):
    _dict = {}
    obj_type = 'virtual_router'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}
    _ip_addr_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(VirtualRouterKM, self).__init__(uuid, obj_dict)
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
        for vr in VirtualRouterKM.objects():

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


class VirtualMachineInterfaceKM(DBBaseKM):
    _dict = {}
    obj_type = 'virtual_machine_interface'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(VirtualMachineInterfaceKM, self).__init__(uuid, obj_dict)
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
                    parent_vmi = VirtualMachineInterfaceKM.locate(parent_vmi_id)
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
    def delete(cls, uuid, request_id=None):
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

    def set_vlan(self, vlan):
        if vlan < MIN_VLAN_ID or vlan > MAX_VLAN_ID:
            return
        if not self.vlan_bit_map:
            self.vlan_bit_map = BitArray(MAX_VLAN_ID + 1)
        self.vlan_bit_map[vlan] = 1

    def reset_vlan(self, vlan):
        if vlan < MIN_VLAN_ID or vlan > MAX_VLAN_ID:
            return
        if not self.vlan_bit_map:
            return
        self.vlan_bit_map[vlan] = 0

    def alloc_vlan(self):
        if not self.vlan_bit_map:
            self.vlan_bit_map = BitArray(MAX_VLAN_ID + 1)
        vid = self.vlan_bit_map.find('0b0', MIN_VLAN_ID)
        if vid:
            self.set_vlan(vid[0])
            return vid[0]
        return INVALID_VLAN_ID

    def get_vlan(self):
        return self.vlan_id

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Virtual Machine Interface DB lookup/introspect request. """
        vmi_resp = introspect.VirtualMachineInterfaceDatabaseListResp(vmis=[])

        # Iterate through all elements of Virtual Router DB.
        for vmi in VirtualMachineInterfaceKM.objects():

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

class VirtualNetworkKM(DBBaseKM):
    _dict = {}
    obj_type = 'virtual_network'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(VirtualNetworkKM, self).__init__(uuid, obj_dict)
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

    def is_k8s_namespace_isolated(self):
        return self.k8s_namespace_isolated

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Virtual Network DB lookup/introspect request. """
        vn_resp = introspect.VirtualNetworkDatabaseListResp(vns=[])

        # Iterate through all elements of Virtual Network DB.
        for vn in VirtualNetworkKM.objects():

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
                network_ipam_subnets=ipam_subnets,
                k8s_namespace=vn.k8s_namespace,
                k8s_namespace_isolated=vn.k8s_namespace_isolated)

            # Append the constructed element info to the response.
            vn_resp.vns.append(vn_instance)

        # Send the reply out.
        vn_resp.response(req.context())

class InstanceIpKM(DBBaseKM):
    _dict = {}
    obj_type = 'instance_ip'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(InstanceIpKM, self).__init__(uuid, obj_dict)
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
                vn_uuid = VirtualNetworkKM.get_fq_name_to_uuid(vn_fq_name)
                if vn_uuid and vn_uuid in iip_obj.virtual_networks:
                    return iip_obj
        return None

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to InstanceIp DB lookup/introspect request. """
        iip_resp = introspect.InstanceIpDatabaseListResp(iips=[])

        # Iterate through all elements of InstanceIp DB.
        for iip in InstanceIpKM.objects():

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

# end class InstanceIpKM

class ProjectKM(DBBaseKM):
    _dict = {}
    obj_type = 'project'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(ProjectKM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.ns_labels = {}
        self.virtual_networks = set()
        self.annotations = None
        self.k8s_namespace_isolated = False
        self.k8s_namespace_uuid = None
        self.k8s_namespace_name = None
        self.owner = None
        self.cluster = None
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
        for kvp in self.annotations.get('key_value_pair', []):
            if kvp.get('key') == 'isolated':
                self.k8s_namespace_isolated = True if kvp.get('value') ==\
                    'True' else False
            if kvp.get('key') == 'k8s_uuid':
                self.k8s_namespace_uuid = kvp.get('value')
            if kvp.get('key') == 'name':
                self.k8s_namespace_name = kvp.get('value')
            if kvp.get('owner') == 'owner':
                self.owner = kvp.get('value')
            if kvp.get('cluster') == 'cluster':
                self.cluster = kvp.get('value')
        return obj

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
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

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to Project DB lookup/introspect request. """
        project_resp = introspect.ProjectDatabaseListResp(projects=[])

        # Iterate through all elements of Project DB.
        for project in ProjectKM.objects():

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
                k8s_namespace_isolated=project.k8s_namespace_isolated,
                k8s_namespace_name=str(project.k8s_namespace_name),
                k8s_namespace_uuid=str(project.k8s_namespace_uuid),
                ns_labels=ns_labels,
                security_groups=sgs,
                virtual_networks=vns)

            # Append the constructed element info to the response.
            project_resp.projects.append(project_instance)

        # Send the reply out.
        project_resp.response(req.context())

class DomainKM(DBBaseKM):
    _dict = {}
    obj_type = 'domain'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(DomainKM, self).__init__(uuid, obj_dict)
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
        for domain in DomainKM.objects():

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

class SecurityGroupKM(DBBaseKM):
    _dict = {}
    obj_type = 'security_group'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}
    ann_fq_name_key = ["name"]

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(SecurityGroupKM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.project_uuid = None
        self.virtual_machine_interfaces = set()
        self.annotations = None
        self.namespace = None
        self.owner = None
        obj_dict = self.update(obj_dict)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.build_fq_name_to_uuid(self.uuid, obj)
        self.annotations = obj.get('annotations', {})
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
    def delete(cls, uuid, request_id=None):
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

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to SecurityGroup DB lookup/introspect request. """
        sg_resp = introspect.SecurityGroupDatabaseListResp(sgs=[])

        # Iterate through all elements of SecurityGroup DB.
        for sg in SecurityGroupKM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.sg_uuid and req.sg_uuid != sg.uuid:
                continue

            sg_annotations = cls._build_annotation_dict(sg.annotations)
            rule_entries = []
            sg_rule_entries = sg.rule_entries['policy_rule'] if sg.rule_entries\
                else []

            for re in sg_rule_entries:
                dst_addresses = []
                for addr in re['dst_addresses']:
                    dst_addr = introspect.SGAddress(
                        network_policy=str(addr['network_policy']),
                        security_group=str(addr['security_group']),
                        subnet=str(addr['subnet']),
                        subnet_list=addr['subnet_list'],
                        virtual_network=addr['virtual_network']
                    )
                    dst_addresses.append(dst_addr)

                dst_ports = [str(re['dst_ports'][i])
                             for i in range(len(re['dst_ports']))]

                src_addresses = []
                for addr in re['src_addresses']:
                    src_addr = introspect.SGAddress(
                        network_policy=str(addr['network_policy']),
                        security_group=str(addr['security_group']),
                        subnet=str(addr['subnet']),
                        subnet_list=addr['subnet_list'],
                        virtual_network=addr['virtual_network']
                    )
                    src_addresses.append(src_addr)

                src_ports = [str(re['src_ports'][i])
                             for i in range(len(re['src_ports']))]

                sgre = introspect.SGRuleEntry(
                    action_list=str(re['action_list']),
                    application=str(re['application']),
                    created=str(re['created']),
                    direction=str(re['direction']),
                    dst_addresses=dst_addresses,
                    dst_ports=dst_ports,
                    ethertype=str(re['ethertype']),
                    last_modified=str(re['last_modified']),
                    protocol=str(re['protocol']),
                    rule_sequence=str(re['rule_sequence']),
                    rule_uuid=str(re['rule_uuid']),
                    src_addresses=src_addresses,
                    src_ports=src_ports)
                rule_entries.append(sgre)

            vmis = cls._build_cls_uuid_list(
                    introspect.VMIUuid, sg.virtual_machine_interfaces)

            # Construct response for an element.
            sg_instance = introspect.SecurityGroupInstance(
                uuid=sg.uuid,
                name=sg.fq_name[-1],
                fq_name=sg.fq_name,
                annotations=sg_annotations,
                namespace_name=sg.namespace,
                owner=sg.owner,
                project_uuid=str(sg.project_uuid),
                rule_entries=rule_entries,
                vm_interfaces=vmis)

            # Append the constructed element info to the response.
            sg_resp.sgs.append(sg_instance)

        # Send the reply out.
        sg_resp.response(req.context())

class FloatingIpPoolKM(DBBaseKM):
    _dict = {}
    obj_type = 'floating_ip_pool'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(FloatingIpPoolKM, self).__init__(uuid, obj_dict)
        self.uuid = uuid
        self.virtual_network = None
        self.update(obj_dict)

    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('virtual_network', None)
        del cls._dict[uuid]

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to FloatingIpPool DB lookup/introspect request. """
        fip_pool_resp = introspect.FloatingIpPoolDatabaseListResp(fip_pools=[])

        # Iterate through all elements of FloatingIpPool DB.
        for fip_pool in FloatingIpPoolKM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.fip_pool_uuid and req.fip_pool_uuid != fip_pool.uuid:
                continue

            fip_pool_annotations = cls._build_annotation_dict(
                fip_pool.annotations)

            fip_pool_subnets = []
            if hasattr(fip_pool, 'floating_ip_pool_subnets') and\
                fip_pool.floating_ip_pool_subnets and \
                fip_pool.floating_ip_pool_subnets.get('subnet_uuid'):
                fip_pool_subnets = fip_pool.floating_ip_pool_subnets[
                    'subnet_uuid']

            # Construct response for an element.
            fip_pool_instance = introspect.FloatingIpPoolInstance(
                uuid=fip_pool.uuid,
                name=fip_pool.fq_name[-1],
                fq_name=fip_pool.fq_name,
                annotations=fip_pool_annotations,
                fip_pool_subnets=fip_pool_subnets,
                virtual_network=str(fip_pool.virtual_network))

            # Append the constructed element info to the response.
            fip_pool_resp.fip_pools.append(fip_pool_instance)

        # Send the reply out.
        fip_pool_resp.response(req.context())

class FloatingIpKM(DBBaseKM):
    _dict = {}
    obj_type = 'floating_ip'
    _ann_fq_name_to_uuid = {}
    ann_fq_name_key = ["kind", "name"]
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(FloatingIpKM, self).__init__(uuid, obj_dict)
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

    def update(self, obj=None, request_id=None):
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
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        del cls._dict[uuid]
    # end delete

    @classmethod
    def sandesh_handle_db_list_request(cls, req):
        """ Reply to FloatingIp DB lookup/introspect request. """
        fip_resp = introspect.FloatingIpDatabaseListResp(fips=[])

        # Iterate through all elements of FloatingIp DB.
        for fip in FloatingIpKM.objects():

            # If the request is for a specific entry, then locate the entry.
            if req.fip_uuid and req.fip_uuid != fip.uuid:
                continue

            fip_annotations = cls._build_annotation_dict(fip.annotations)

            vmis = cls._build_cls_uuid_list(
                    introspect.VMIUuid, fip.virtual_machine_interfaces)

            # Construct response for an element.
            fip_instance = introspect.FloatingIpInstance(
                uuid=fip.uuid,
                name=fip.fq_name[-1],
                fq_name=fip.fq_name,
                annotations=fip_annotations,
                address=str(fip.address),
                parent_uuid=str(fip.parent_uuid),
                virtual_ip=str(fip.virtual_ip),
                vm_interfaces=vmis)

            # Append the constructed element info to the response.
            fip_resp.fips.append(fip_instance)

        # Send the reply out.
        fip_resp.response(req.context())
# end class FloatingIpKM

class NetworkIpamKM(DBBaseKM):
    _dict = {}
    obj_type = 'network_ipam'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(NetworkIpamKM, self).__init__(uuid, obj_dict)
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
        for network_ipam in NetworkIpamKM.objects():

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
# end class NetworkIpamKM

class NetworkPolicyKM(DBBaseKM):
    _dict = {}
    obj_type = 'network_policy'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        super(NetworkPolicyKM, self).__init__(uuid, obj_dict)
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
# end class NetworkPolicyKM

class TagKM(DBBaseKM):
    _dict = {}
    obj_type = 'tag'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        self.uuid = uuid
        super(TagKM, self).__init__(uuid, obj_dict)
        obj_dict = self.update(obj_dict)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_single_ref('tag_type', obj)
        self.build_fq_name_to_uuid(self.uuid, obj)
        return obj

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('tag_type', {})
        super(TagKM, cls).delete(uuid)
        del cls._dict[uuid]
# end class TagKM

class PolicyManagementKM(DBBaseKM):
    _dict = {}
    obj_type = 'policy_management'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        self.uuid = uuid
        super(PolicyManagementKM, self).__init__(uuid, obj_dict)
        obj_dict = self.update(obj_dict)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.build_fq_name_to_uuid(self.uuid, obj)
        return obj

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        super(PolicyManagementKM, cls).delete(uuid)
        del cls._dict[uuid]
# end class PolicyManagementKM

class ApplicationPolicySetKM(DBBaseKM):
    _dict = {}
    obj_type = 'application_policy_set'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        self.uuid = uuid
        self.firewall_policy_refs = None
        # Hold refs to firewall policies on this APS, that are sorted by
        # sequence number of the refs.
        self.firewall_policies_sorted = []
        super(ApplicationPolicySetKM, self).__init__(uuid, obj_dict)
        obj_dict = self.update(obj_dict)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.parent_uuid = obj.get('parent_uuid', None)
        self.build_fq_name_to_uuid(self.uuid, obj)
        self.firewall_policy_refs = obj.get('firewall_policy_refs', None)

        # Construct a sorted list of firewall policy refs.
        if self.firewall_policy_refs:
            self.firewall_policies_sorted =\
                sorted(self.firewall_policy_refs,
                  key = lambda policy_ref: policy_ref['attr'].get('sequence'))
        else:
            self.firewall_policies_sorted = []
        return obj

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        super(ApplicationPolicySetKM, cls).delete(uuid)
        del cls._dict[uuid]

    def get_fq_name(self):
        return self.fq_name

    def get_firewall_policy_refs_sorted(self):
        return self.firewall_policies_sorted

    def get_firewall_policies(self):
        fw_policies = []
        for policy in self.firewall_policies_sorted:
            fw_policies.append(policy.get('uuid'))
        return fw_policies

# end class ApplicationPolicySetKM

class FirewallRuleKM(DBBaseKM):
    _dict = {}
    obj_type = 'firewall_rule'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        self.uuid = uuid
        super(FirewallRuleKM, self).__init__(uuid, obj_dict)
        self.address_groups = set()
        obj_dict = self.update(obj_dict)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.action_list = obj['action_list']
        self.endpoint_1 = obj['endpoint_1']
        self.endpoint_2 = obj['endpoint_2']
        self.match_tag_types = obj['match_tag_types']
        self.service = obj.get('service', None)
        self.update_multiple_refs('address_group', obj)
        self.build_fq_name_to_uuid(self.uuid, obj)
        return obj

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('address_group', {})
        super(FirewallRuleKM, cls).delete(uuid)
        del cls._dict[uuid]

    def get_fq_name(self):
        return self.fq_name
# end class FirewallRuleKM

class FirewallPolicyKM(DBBaseKM):
    _dict = {}
    obj_type = 'firewall_policy'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        self.uuid = uuid
        self.firewall_rules = set()
        self.deny_all_rule_uuid = None
        self.egress_deny_all_rule_uuid = None
        self.firewall_rules = set()

        # Marker to indicate if this is policy is the beginning of
        # collection of end/tail policys in an application set. The tail
        # section of an APS contains policy's that are meant to enforce
        # deafult behavior in the APS.
        self.tail = 'False'
        self.after_tail = 'False'
        self.spec = None
        self.cluster_name = None
        self.owner = None
        self.k8s_uuid = None
        self.k8s_name = None
        self.k8s_namespace = None

        super(FirewallPolicyKM, self).__init__(uuid, obj_dict)
        obj_dict = self.update(obj_dict)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.annotations = obj.get('annotations', None)
        if self.annotations:
            for kvp in self.annotations['key_value_pair'] or []:
                if kvp['key'] == 'tail':
                    self.tail = kvp['value']
                if kvp['key'] == 'after_tail':
                    self.after_tail = kvp['value']
                elif kvp['key'] == 'deny_all_rule_uuid':
                    self.deny_all_rule_uuid = kvp['value']
                elif kvp['key'] == 'egress_deny_all_rule_uuid':
                    self.egress_deny_all_rule_uuid = kvp['value']
                elif kvp['key'] == 'spec':
                    self.spec = kvp['value']
                elif kvp['key'] == 'cluster':
                    self.cluster_name = kvp['value']
                elif kvp['key'] == 'owner':
                    self.owner = kvp['value']
                elif kvp['key'] == 'k8s_uuid':
                    self.k8s_uuid = kvp['value']
                elif kvp['key'] == 'name':
                    self.k8s_name = kvp['value']
                elif kvp['key'] == 'namespace':
                    self.k8s_namespace = kvp['value']

        self.update_multiple_refs('firewall_rule', obj)
        self.build_fq_name_to_uuid(self.uuid, obj)
        return obj

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('firewall_rule', ())
        super(FirewallPolicyKM, cls).delete(uuid)
        del cls._dict[uuid]

    def get_fq_name(self):
        return self.fq_name

    def is_tail(self):
        return True if self.tail == 'True' else False

    def is_after_tail(self):
        return True if self.after_tail == 'True' else False

# end class FirewallPolicyKM

class AddressGroupKM(DBBaseKM):
    _dict = {}
    obj_type = 'address_group'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        self.uuid = uuid
        super(AddressGroupKM, self).__init__(uuid, obj_dict)
        obj_dict = self.update(obj_dict)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.build_fq_name_to_uuid(self.uuid, obj)
        return obj

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        super(AddressGroupKM, cls).delete(uuid)
        del cls._dict[uuid]
# end class AddressGroupKM

class GlobalVrouterConfigKM(DBBaseKM):
    _dict = {}
    obj_type = 'global_vrouter_config'
    _ann_fq_name_to_uuid = {}
    _fq_name_to_uuid = {}

    def __init__(self, uuid, obj_dict=None, request_id=None):
        self.uuid = uuid
        super(GlobalVrouterConfigKM, self).__init__(uuid, obj_dict)
        obj_dict = self.update(obj_dict)

    def update(self, obj=None, request_id=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.build_fq_name_to_uuid(self.uuid, obj)
        return obj

    @classmethod
    def delete(cls, uuid, request_id=None):
        if uuid not in cls._dict:
            return
        super(GlobalVrouterConfigKM, cls).delete(uuid)
        del cls._dict[uuid]
# end class GlobalVrouterConfigKM

