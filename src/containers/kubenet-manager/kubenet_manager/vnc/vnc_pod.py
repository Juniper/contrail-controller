#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
VNC pod management for kubernetes
"""

from vnc_api.vnc_api import *
from config_db import *

class VncPod(object):

    def __init__(self, vnc_lib=None, label_cache=None, service_mgr=None):
        self._vnc_lib = vnc_lib
        self._label_cache = label_cache
        self._service_mgr = service_mgr

    def _get_network(self, pod_id, pod_namespace):
        vn_fq_name = ['default-domain', 'default', 'cluster-network']
        vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        return vn_obj

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

    def _create_vmi(self, pod_name, pod_namespace, vm_obj, vn_obj):
        proj_fq_name = ['default-domain', pod_namespace]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        vmi_obj = VirtualMachineInterface(name=pod_name, parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn_obj)
        vmi_obj.set_virtual_machine(vm_obj)
        try:
            self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        except RefsExistError:
            self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        VirtualMachineInterfaceKM.locate(vmi_obj.uuid)
        return vmi_obj

    def check_pod_label_actions(self, labels, pod_id):
        for label in labels.items():
            key = self._label_cache._get_key(label)
            self._label_cache._locate_label(key,
                self._label_cache.pod_label_cache, label, pod_id)
            service_ids = self._label_cache.service_label_cache.get(key, [])
            for service_id in service_ids:
                self._service_mgr.update_service(service_id, [pod_id])

    def _create_vm(self, pod_id, pod_name, labels):
        vm_obj = VirtualMachine(name=pod_name)
        vm_obj.uuid = pod_id
        try:
            self._vnc_lib.virtual_machine_create(vm_obj)
        except RefsExistError:
            vm_obj = self._vnc_lib.virtual_machine_read(id=pod_id)
        vm = VirtualMachineKM.locate(vm_obj.uuid)
        vm.pod_labels = labels
        self.check_pod_label_actions(labels, pod_id)
        return vm_obj

    def _link_vm_to_node(self, vm_obj, pod_node):
        vrouter_fq_name = ['default-global-system-config', pod_node]
        vrouter_obj = self._vnc_lib.virtual_router_read(fq_name=vrouter_fq_name)
        self._vnc_lib.ref_update('virtual-router', vrouter_obj.uuid,
            'virtual-machine', vm_obj.uuid, None, 'ADD')
        vm = VirtualMachineKM.get(vm_obj.uuid)
        if vm: 
            vm.virtual_router = vrouter_obj.uuid

    def vnc_pod_add(self, pod_id, pod_name, pod_namespace, pod_node, labels):
        vn_obj = self._get_network(pod_id, pod_namespace)
        vm_obj = self._create_vm(pod_id, pod_name, labels)
        vmi_obj = self._create_vmi(pod_name, pod_namespace, vm_obj, vn_obj)
        self._create_iip(pod_name, vn_obj, vmi_obj)
        self._link_vm_to_node(vm_obj, pod_node)

    def vnc_port_delete(self, vmi_id):
        vmi = VirtualMachineInterfaceKM.get(vmi_id)
        if not vmi:
            return
        for iip_id in vmi.instance_ips:
            try:
                self._vnc_lib.instance_ip_delete(id=iip_id)
            except NoIdError:
                pass

        try:
            self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
        except NoIdError:
            pass

    def vnc_pod_delete(self, pod_id, pod_name):
        vm = VirtualMachineKM.get(pod_id)
        if not vm:
            return

        if vm.virtual_router:
            self._vnc_lib.ref_update('virtual-router', vm.virtual_router,
                'virtual-machine', vm.uuid, None, 'DELETE')

        for vmi_id in vm.virtual_machine_interfaces:
            self.vnc_port_delete(vmi_id)

        try:
            self._vnc_lib.virtual_machine_delete(id=pod_id)
        except NoIdError:
            pass

    def process(self, event):
        pod_id = event['object']['metadata'].get('uid')
        pod_name = event['object']['metadata'].get('name')
        pod_namespace = event['object']['metadata'].get('namespace')

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            pod_node = event['object']['spec'].get('nodeName')
            if not pod_node or pod_node == 'master':
                return
            host_network = event['object']['spec'].get('hostNetwork')
            if host_network:
                return
            labels = event['object']['metadata']['labels']
            self.vnc_pod_add(pod_id, pod_name, pod_namespace,
                pod_node, labels)
        elif event['type'] == 'DELETED':
            self.vnc_pod_delete(pod_id, pod_name)
