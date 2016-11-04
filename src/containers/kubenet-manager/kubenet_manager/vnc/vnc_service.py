#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

from vnc_api.vnc_api import *
from config_db import *

class VncService(object):

    def __init__(self, vnc_lib=None, label_cache=None):
        self._vnc_lib = vnc_lib
        self._label_cache = label_cache

    def _get_network(self):
        vn_fq_name = ['default-domain', 'default', 'cluster-network']
        vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        return vn_obj

    def _create_iip(self, service_name, vn_obj, vmi_obj):
        iip_obj = InstanceIp(name=service_name)
        iip_obj.add_virtual_network(vn_obj)
        iip_obj.add_virtual_machine_interface(vmi_obj)
        try:
            self._vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            self._vnc_lib.instance_ip_update(iip_obj)
        InstanceIpKM.locate(iip_obj.uuid)
        return iip_obj

    def _create_vmi(self, service_name, service_namespace, vn_obj):
        proj_fq_name = ['default-domain', service_namespace]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        vmi_obj = VirtualMachineInterface(name=service_name, parent_obj=proj_obj)
        vmi_obj.set_virtual_network(vn_obj)
        try:
            self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        except RefsExistError:
            self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        VirtualMachineInterfaceKM.locate(vmi_obj.uuid)
        return vmi_obj

    def check_service_label_actions(self, labels, service_id):
        for label in labels.items():
            key = self._label_cache._get_key(label)
            self._label_cache._locate_label(key,
                self._label_cache.service_label_cache, label, service_id)
            pod_ids = self._label_cache.pod_label_cache.get(key, [])
            if len(pod_ids):
                self.update_service(service_id, pod_ids)

    def _create_lb(self, service_id, service_name, service_namespace,
            selectors, vmi_obj):
        proj_fq_name = ['default-domain', service_namespace]
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        lb_obj = Loadbalancer(name=service_name, parent_obj=proj_obj)
        lb_obj.uuid = service_id
        lb_obj.set_virtual_machine_interface(vmi_obj)
        try:
            self._vnc_lib.loadbalancer_create(lb_obj)
        except RefsExistError:
            self._vnc_lib.loadbalancer_update(lb_obj)
        lb = LoadbalancerKM.locate(lb_obj.uuid)
        if selectors:
            lb.selectors = selectors
            self.check_service_label_actions(selectors, service_id)
        return vmi_obj

    def vnc_service_add(self, service_id, service_name,
            service_namespace, service_ip, selectors):
        vn_obj = self._get_network()
        vmi_obj = self._create_vmi(service_name, service_namespace, vn_obj)
        self._create_iip(service_name, vn_obj, vmi_obj)
        self._create_lb(service_id, service_name, service_namespace,
            selectors, vmi_obj)

    def vnc_service_port_delete(self, vmi_id):
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


    def vnc_service_delete(self, service_id, service_name):
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            return

        for vmi_id in lb.virtual_machine_interfaces:
            self.vnc_service_port_delete(vmi_id)

        try:
            self._vnc_lib.loadbalancer_delete(id=service_id)
        except NoIdError:
            pass

    def process(self, event):
        service_id = event['object']['metadata'].get('uid')
        service_name = event['object']['metadata'].get('name')
        service_namespace = event['object']['metadata'].get('namespace')
        service_ip = event['object']['spec'].get('clusterIP')

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            selectors = event['object']['spec'].get('selector', None)
            self.vnc_service_add(service_id, service_name,
                service_namespace, service_ip, selectors)
        elif event['type'] == 'DELETED':
            self.vnc_service_delete(service_id, service_name)

    def update_service(self, service_id, pod_list):
        lb = LoadbalancerKM.get(service_id)
        for pod_id in pod_list:
            vm = VirtualMachineKM.get(pod_id)
