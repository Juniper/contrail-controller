#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
VNC pod management for kubernetes
"""

from vnc_api.vnc_api import *
from config_db import *
from kube_manager.common.kube_config_db import NamespaceKM
from kube_manager.common.kube_config_db import PodKM

class VncPod(object):

    def __init__(self, vnc_lib=None, label_cache=None, service_mgr=None,
            network_policy_mgr=None, queue=None, svc_fip_pool=None):
        self._vnc_lib = vnc_lib
        self._label_cache = label_cache
        self._service_mgr = service_mgr
        self._network_policy_mgr = network_policy_mgr
        self._queue = queue
        self._service_fip_pool = svc_fip_pool

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

    def _create_iip(self, pod_name, vn_obj, vmi_obj):
        iip_obj = InstanceIp(name=pod_name)
        iip_obj.add_virtual_network(vn_obj)
        iip_obj.add_virtual_machine_interface(vmi_obj)
        try:
            self._vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            self._vnc_lib.instance_ip_update(iip_obj)
        InstanceIpKM.locate(iip_obj.uuid)
        return iip_obj

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

    def _create_vmi(self, pod_name, pod_namespace, vm_obj, vn_obj):
        proj_fq_name = ['default-domain', pod_namespace]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        vmi_obj = VirtualMachineInterface(name=pod_name, parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn_obj)
        vmi_obj.set_virtual_machine(vm_obj)
        sg_obj = SecurityGroup("default", proj_obj)
        vmi_obj.add_security_group(sg_obj)
        try:
            self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        except RefsExistError:
            self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        VirtualMachineInterfaceKM.locate(vmi_obj.uuid)
        return vmi_obj

    def _create_vm(self, pod_id, pod_name, labels):
        vm_obj = VirtualMachine(name=pod_name)
        vm_obj.uuid = pod_id
        annotations = {}
        annotations['device_owner'] = 'K8S:POD'
        for key in annotations:
            vm_obj.add_annotations(KeyValuePair(key=key, value=annotations[key]))
        vm_obj.add_annotations(KeyValuePair(key='labels', value=labels))
        try:
            self._vnc_lib.virtual_machine_create(vm_obj)
        except RefsExistError:
            vm_obj = self._vnc_lib.virtual_machine_read(id=pod_id)
        vm = VirtualMachineKM.locate(vm_obj.uuid)
        vm.pod_labels = labels
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

    def vnc_pod_add(self, pod_id, pod_name, pod_namespace, pod_node, labels):
        vm = VirtualMachineKM.get(pod_id)
        if vm:
            vm.pod_labels = labels
            return
        if not vm:
            self._check_pod_uuid_change(pod_id, pod_name, pod_namespace)

        vn_obj = self._get_network(pod_id, pod_namespace)
        vm_obj = self._create_vm(pod_id, pod_name, labels)
        vmi_obj = self._create_vmi(pod_name, pod_namespace, vm_obj, vn_obj)
        self._create_iip(pod_name, vn_obj, vmi_obj)

        if self._is_pod_network_isolated(pod_namespace):
            self._create_cluster_service_fip(pod_name, vmi_obj)

        self._link_vm_to_node(vm_obj, pod_node)

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

        if vm.virtual_router:
            self._vnc_lib.ref_update('virtual-router', vm.virtual_router,
                'virtual-machine', vm.uuid, None, 'DELETE')

        for vmi_id in list(vm.virtual_machine_interfaces):
            self.vnc_port_delete(vmi_id)

        try:
            self._vnc_lib.virtual_machine_delete(id=pod_id)
        except NoIdError:
            pass

        return

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
        pod_id = event['object']['metadata'].get('uid')
        pod_name = event['object']['metadata'].get('name')
        pod_namespace = event['object']['metadata'].get('namespace')
        labels = event['object']['metadata'].get('labels', {})

        if event['type'] == 'ADDED':
            pod_node = event['object']['spec'].get('nodeName')
            host_network = event['object']['spec'].get('hostNetwork')
            if host_network:
                return
            self.vnc_pod_add(pod_id, pod_name, pod_namespace,
                pod_node, labels)
            self._network_policy_mgr.vnc_pod_add(event)
        elif event['type'] == 'MODIFIED':
            pod_node = event['object']['spec'].get('nodeName')
            host_network = event['object']['spec'].get('hostNetwork')
            if host_network:
                return
            self._network_policy_mgr.vnc_pod_update(event)
            self.vnc_pod_add(pod_id, pod_name, pod_namespace,
                pod_node, labels)
        elif event['type'] == 'DELETED':
            self.vnc_pod_delete(pod_id)
            self._network_policy_mgr.vnc_pod_delete(event)
