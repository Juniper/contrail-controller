#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
VNC pod management for kubernetes
"""

import uuid

from vnc_api.vnc_api import *
from config_db import *
from kube_manager.common.kube_config_db import NamespaceKM
from kube_manager.common.kube_config_db import PodKM

class VncPod(object):

    def __init__(self, vnc_lib=None, label_cache=None, args=None, logger=None,
            service_mgr=None, network_policy_mgr=None, queue=None,
            svc_fip_pool=None):
        self._name = type(self).__name__
        self._vnc_lib = vnc_lib
        self._label_cache = label_cache
        self._service_mgr = service_mgr
        self._network_policy_mgr = network_policy_mgr
        self._queue = queue
        self._service_fip_pool = svc_fip_pool
        self._args = args
        self._logger = logger

    def _get_label_diff(self, new_labels, vm):
        old_labels = vm.pod_labels
        if old_labels == new_labels:
            return None

        diff = dict()
        added = {}
        removed = {}
        changed = {}
        keys = set(old_labels.keys()) | set(new_labels.keys())
        for k in keys:
            if k not in old_labels.keys():
                added[k] = new_labels[k]
                continue
            if k not in new_labels.keys():
                removed[k] = old_labels[k]
                continue
            if old_labels[k] == new_labels[k]:
                continue
            changed[k] = old_labels[k]

        diff['added'] = added
        diff['removed'] = removed
        diff['changed'] = changed
        return diff

    def _set_label_to_pod_cache(self, new_labels, vm):
        for label in new_labels.items():        
            key = self._label_cache._get_key(label)        
            self._label_cache._locate_label(key,        
                self._label_cache.pod_label_cache, label, vm.uuid)        
        vm.pod_labels = new_labels

    def _clear_label_to_pod_cache(self, vm):        
        if not vm.pod_labels:
            return
        for label in vm.pod_labels.items() or []:
            key = self._label_cache._get_key(label)        
            self._label_cache._remove_label(key,
                self._label_cache.pod_label_cache, label, vm.uuid)        
        vm.pod_labels = None

    def _update_label_to_pod_cache(self, new_labels, vm):        
        self._clear_label_to_pod_cache(vm)
        self._set_label_to_pod_cache(new_labels, vm)

    def _get_network(self, pod_id, pod_namespace):
        """
        Get network corresponding to this namesapce.
        """
        vn_fq_name = None
        if self._is_pod_network_isolated(pod_namespace) == True:
            ns = self._get_namespace(pod_namespace)
            if ns:
                vn_fq_name = ns.get_network_fq_name()

        # If no network was found on the namesapce, default to the cluster
        # pod network.
        if not vn_fq_name:
            vn_fq_name = ['default-domain', 'default', 'cluster-network']

        vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        return vn_obj

    def _get_namespace(self, pod_namespace):
        return NamespaceKM.find_by_name_or_uuid(pod_namespace)

    def _is_pod_network_isolated(self, pod_namespace):
        return self._get_namespace(pod_namespace).is_isolated()


    def _is_pod_nested(self):
        # Pod is nested if we are configured to run in nested mode.
        return DBBaseKM.is_nested()

    def _get_host_ip(self, pod_name):
        pod = PodKM.find_by_name_or_uuid(pod_name)
        if pod:
            return pod.get_host_ip()
        return None

    def _create_iip(self, pod_name, vn_obj, vmi):
        iip_obj = InstanceIp(name=pod_name)
        iip_obj.add_virtual_network(vn_obj)

        # Creation of iip requires the vmi vnc object.
        vmi_obj = self._vnc_lib.virtual_machine_interface_read(
                      fq_name=vmi.fq_name)
        iip_obj.add_virtual_machine_interface(vmi_obj)
        try:
            self._vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            self._vnc_lib.instance_ip_update(iip_obj)
        InstanceIpKM.locate(iip_obj.uuid)
        return iip_obj

    def _get_host_vmi(self, pod_name):
        host_ip = self._get_host_ip(pod_name)
        if host_ip:
            iip = InstanceIpKM.get_object(host_ip)
            if iip:
                for vmi_id in iip.virtual_machine_interfaces:
                    vm_vmi = VirtualMachineInterfaceKM.get(vmi_id)
                    if vm_vmi and vm_vmi.host_id:
                        return vm_vmi

        return None

    def _create_cluster_service_fip(self, pod_name, vmi_obj):
        """
        Isolated Pods in the cluster will be allocated a floating ip
        from the cluster service network, so that the pods can talk
        to cluster services.
        """
        if not self._service_fip_pool:
            return

        # Construct parent ref.
        fip_pool_obj = FloatingIpPool()
        fip_pool_obj.uuid = self._service_fip_pool.uuid
        fip_pool_obj.fq_name = self._service_fip_pool.fq_name
        fip_pool_obj.name = self._service_fip_pool.name

        # Create Floating-Ip object.
        fip_obj = FloatingIp(name="cluster-svc-fip-%s"% (pod_name),
                             parent_obj=fip_pool_obj,
                             floating_ip_traffic_direction='egress')
        fip_obj.set_virtual_machine_interface(vmi_obj)

        try:
            fip_uuid = self._vnc_lib.floating_ip_create(fip_obj)
        except RefsExistError:
            fip_uuid = self._vnc_lib.floating_ip_update(fip_obj)

        # Cached service floating ip.
        FloatingIpKM.locate(fip_uuid)

        return

    def _create_vmi(self, pod_name, pod_namespace, vm_obj, vn_obj,
            parent_vmi):
        proj_fq_name = ['default-domain', pod_namespace]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        vmi_prop = None
        if self._is_pod_nested() and parent_vmi:
            # Pod is nested.
            # Allocate a vlan-id for this pod from the vlan space managed
            # in the VMI of the underlay VM.
            parent_vmi = VirtualMachineInterfaceKM.get(parent_vmi.uuid)
            vlan_id = parent_vmi.alloc_vlan()
            vmi_prop = VirtualMachineInterfacePropertiesType(
                sub_interface_vlan_tag=vlan_id)

        obj_uuid = str(uuid.uuid1())
        name = 'pod' + '-' + pod_name + '-' + obj_uuid
        vmi_obj = VirtualMachineInterface(name=name, parent_obj=proj_obj,
                      virtual_machine_interface_properties=vmi_prop)
        vmi_obj.uuid = obj_uuid
        vmi_obj.set_virtual_network(vn_obj)
        vmi_obj.set_virtual_machine(vm_obj)
        sg_obj = SecurityGroup("default", proj_obj)
        vmi_obj.add_security_group(sg_obj)
        try:
            vmi_uuid = self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        except RefsExistError:
            vmi_uuid = self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        VirtualMachineInterfaceKM.locate(vmi_uuid)
        return vmi_uuid

    def _create_vm(self, pod_id, pod_name, labels):
        vm_obj = VirtualMachine(name=pod_name)
        vm_obj.uuid = pod_id
        annotations = {}
        annotations['device_owner'] = 'K8S:POD'
        for key in annotations:
            vm_obj.add_annotations(KeyValuePair(key=key, value=annotations[key]))
        vm_obj.add_annotations(KeyValuePair(key='labels', value=json.dumps(labels)))
        try:
            self._vnc_lib.virtual_machine_create(vm_obj)
        except RefsExistError:
            vm_obj = self._vnc_lib.virtual_machine_read(id=pod_id)
        vm = VirtualMachineKM.locate(vm_obj.uuid)
        return vm_obj

    def _link_vm_to_node(self, vm_obj, pod_node):
        vrouter_fq_name = ['default-global-system-config', pod_node]
        try:
            vrouter_obj = self._vnc_lib.virtual_router_read(fq_name=vrouter_fq_name)
        except Exception as e:
            return

        self._vnc_lib.ref_update('virtual-router', vrouter_obj.uuid,
            'virtual-machine', vm_obj.uuid, None, 'ADD')
        vm = VirtualMachineKM.get(vm_obj.uuid)
        if vm: 
            vm.virtual_router = vrouter_obj.uuid

    def _check_pod_uuid_change(self, pod_uuid, pod_name, pod_namespace):
        vm_fq_name = [pod_name]
        vm_uuid = LoadbalancerKM.get_fq_name_to_uuid(vm_fq_name)
        if vm_uuid != pod_uuid:
            self.vnc_pod_delete(vm_uuid)

    def vnc_pod_add(self, pod_id, pod_name, pod_namespace, pod_node, labels,
            vm_vmi):
        vm = VirtualMachineKM.get(pod_id)
        if vm:
            self._set_label_to_pod_cache(labels, vm)
            return
        if not vm:
            self._check_pod_uuid_change(pod_id, pod_name, pod_namespace)

        vn_obj = self._get_network(pod_id, pod_namespace)
        vm_obj = self._create_vm(pod_id, pod_name, labels)

        vmi_uuid = self._create_vmi(pod_name, pod_namespace, vm_obj, vn_obj,
                       vm_vmi)

        vmi = VirtualMachineInterfaceKM.get(vmi_uuid)

        if self._is_pod_nested() and vm_vmi:
            # Pod is nested.
            # Link the pod VMI to the VMI of the underlay VM.
            self._vnc_lib.ref_update('virtual-machine-interface', vm_vmi.uuid,
                'virtual-machine-interface', vmi_uuid, None, 'ADD')
            self._vnc_lib.ref_update('virtual-machine-interface', vmi_uuid,
                'virtual-machine-interface', vm_vmi.uuid, None, 'ADD')

            # get host id for vm vmi
            vr_uuid = None
            for vr in VirtualRouterKM.values():
                if vr.name == vm_vmi.host_id:
                    vr_uuid = vr.uuid
                    break
            if not vr_uuid:
                self._logger.error("No virtual-router object found for host: "
                        + vm_vmi.host_id + ". Unable to add VM reference to a"
                        " valid virtual-router")
                return
            self._vnc_lib.ref_update('virtual-router', vr_uuid,
                'virtual-machine', vm_obj.uuid, None, 'ADD')

        self._create_iip(pod_name, vn_obj, vmi)

        if self._is_pod_network_isolated(pod_namespace):
            self._create_cluster_service_fip(pod_name, vmi)

        self._link_vm_to_node(vm_obj, pod_node)

    def vnc_pod_update(self, pod_id, pod_name, pod_namespace, pod_node, labels,
            vm_vmi):
        label_diff = None
        vm = VirtualMachineKM.get(pod_id)
        if not vm:
            # If the vm is not created yet, do so now.
            self.vnc_pod_add(pod_id, pod_name, pod_namespace,
                pod_node, labels, vm_vmi)
            vm = VirtualMachineKM.get(pod_id)

        if vm:
            label_diff = self._get_label_diff(labels, vm)
            if not label_diff:
                return label_diff
            self._update_label_to_pod_cache(labels, vm)
        return label_diff

    def vnc_port_delete(self, vmi_id):
        vmi = VirtualMachineInterfaceKM.get(vmi_id)
        if not vmi:
            return
        for iip_id in list(vmi.instance_ips):
            try:
                self._vnc_lib.instance_ip_delete(id=iip_id)
            except NoIdError:
                pass

        # Cleanup floating ip's on this interface.
        for fip_id in list(vmi.floating_ips):
            try:
                self._vnc_lib.floating_ip_delete(id=fip_id)
            except NoIdError:
                pass

        try:
            self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
        except NoIdError:
            pass

    def vnc_pod_delete(self, pod_id):
        vm = VirtualMachineKM.get(pod_id)
        if not vm:
            return

        self._clear_label_to_pod_cache(vm)

        if vm.virtual_router:
            self._vnc_lib.ref_update('virtual-router', vm.virtual_router,
                'virtual-machine', vm.uuid, None, 'DELETE')

        for vmi_id in list(vm.virtual_machine_interfaces):
            self.vnc_port_delete(vmi_id)

        try:
            self._vnc_lib.virtual_machine_delete(id=pod_id)
        except NoIdError:
            pass

    def _create_pod_event(self, event_type, pod_id, vm_obj):
        event = {}
        object = {}
        object['kind'] = 'Pod'
        object['metadata'] = {}
        object['metadata']['uid'] = pod_id
        object['metadata']['labels'] = vm_obj.pod_labels
        if event_type == 'delete':
            event['type'] = 'DELETED'
            event['object'] = object
            self._queue.put(event)
        return

    def _sync_pod_vm(self):
        vm_uuid_list = list(VirtualMachineKM.keys())
        pod_uuid_list = list(PodKM.keys())
        for uuid in vm_uuid_list:
            if uuid in pod_uuid_list:
                continue
            vm = VirtualMachineKM.get(uuid)
            if not vm:
                continue
            if not vm.annotations:
                continue
            for kvp in vm.annotations['key_value_pair'] or []:
                if kvp['key'] == 'device_owner' \
                   and kvp['value'] == 'K8S:POD':
                    self._create_pod_event('delete', uuid, vm)
                    break
        return

    def pod_timer(self):
        self._sync_pod_vm()
        return

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        pod_id = event['object']['metadata'].get('uid')
        pod_name = event['object']['metadata'].get('name')
        pod_namespace = event['object']['metadata'].get('namespace')
        labels = event['object']['metadata'].get('labels', {})

        print("%s - Got %s %s %s:%s"
              %(self._name, event_type, kind, pod_namespace, pod_name))
        self._logger.debug("%s - Got %s %s %s:%s"
              %(self._name, event_type, kind, pod_namespace, pod_name))

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':

            # Proceed ONLY if host network is specified.
            pod_node = event['object']['spec'].get('nodeName')
            host_network = event['object']['spec'].get('hostNetwork')
            if host_network:
                return

            # If the pod is nested, proceed ONLY if host vmi is found.
            vm_vmi = None
            if self._is_pod_nested():
                vm_vmi = self._get_host_vmi(pod_name)
                if not vm_vmi:
                    return

            if event['type'] == 'ADDED':
                self.vnc_pod_add(pod_id, pod_name, pod_namespace,
                    pod_node, labels, vm_vmi)
                self._network_policy_mgr.vnc_pod_add(event)
            else:
                label_diff = self.vnc_pod_update(pod_id, pod_name,
                                pod_namespace, pod_node, labels, vm_vmi)
                self._network_policy_mgr.vnc_pod_update(event, label_diff)
        elif event['type'] == 'DELETED':
            self.vnc_pod_delete(pod_id)
            self._network_policy_mgr.vnc_pod_delete(event)
