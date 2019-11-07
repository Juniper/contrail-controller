#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
VNC pod task management for  mesos
"""
from __future__ import print_function

from future import standard_library
standard_library.install_aliases()
from builtins import str
from builtins import object
import json
import uuid

from six import StringIO
from cfgm_common.exceptions import RefsExistError, NoIdError
from cfgm_common.utils import cgitb_hook
from vnc_api.vnc_api import ( InstanceIp, VirtualMachine,
                             VirtualMachineInterface,
                             VirtualMachineInterfacePropertiesType,
                             KeyValuePair)
from mesos_manager.vnc.config_db import (
    DBBaseMM, VirtualNetworkMM, VirtualRouterMM, VirtualMachineMM,
    VirtualMachineInterfaceMM, InstanceIpMM)
from mesos_manager.vnc.vnc_common import VncCommon
from mesos_manager.vnc.vnc_mesos_config import (
    VncMesosConfig as vnc_mesos_config)
from mesos_manager.mesos.pod_task_monitor import PodTaskMonitor
from cfgm_common.utils import cgitb_hook

class VncPodTask(VncCommon):
    vnc_pod_task_instance = None

    def __init__(self):
        super(VncPodTask, self).__init__('PodTask')
        self._name = type(self).__name__
        self._vnc_lib = vnc_mesos_config.vnc_lib()
        self._queue = vnc_mesos_config.queue()
        self._sync_queue = vnc_mesos_config.sync_queue()
        self._args = vnc_mesos_config.args()
        self._logger = vnc_mesos_config.logger()
        if not VncPodTask.vnc_pod_task_instance:
            VncPodTask.vnc_pod_task_instance = self

    def _get_network(self, custom_network=None):
        if custom_network == None:
            vn_fq_name = vnc_mesos_config.cluster_default_pod_task_network_fq_name()
        else:
            vn_fq_name = custom_network.split(':')
        vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        return vn_obj

    def _link_vm_to_node(self, vm_obj, node_name, node_ip):
        if node_ip is None:
            return

        vm = VirtualMachineMM.locate(vm_obj.uuid)
        if vm:
            vm.node_ip = node_ip

        vr_uuid = VirtualRouterMM.get_ip_addr_to_uuid(node_ip)
        if vr_uuid is None:
            for vr in list(VirtualRouterMM.values()):
                if vr.name == node_name:
                    vr_uuid = vr.uuid
        if vr_uuid is None:
            self._logger.debug("%s - Vrouter %s Not Found for PodTask %s"
                %(self._name, node_ip, vm_obj.uuid))
            return

        try:
            vrouter_obj = self._vnc_lib.virtual_router_read(id=vr_uuid)
        except Exception as e:
            self._logger.debug("%s - Vrouter %s Not Found for Pod %s"
                %(self._name, node_ip, vm_obj.uuid))
            string_buf = StringIO()
            cgitb_hook(file=string_buf, format="text")
            err_msg = string_buf.getvalue()
            self._logger.error("_link_vm_to_node: %s - %s" %(self._name, err_msg))
            return

        self._vnc_lib.ref_update('virtual-router', vrouter_obj.uuid,
            'virtual-machine', vm_obj.uuid, None, 'ADD')
        if vm:
            vm.virtual_router = vrouter_obj.uuid

    def _create_vmi(self, pod_task_id, vm_obj, vn_obj):
        proj_fq_name = vnc_mesos_config.cluster_project_fq_name('default')
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)

        vmi_prop = None
        obj_uuid = str(uuid.uuid1())
        vmi_obj = VirtualMachineInterface(
            name=vm_obj.name, parent_obj=proj_obj,
            virtual_machine_interface_properties=vmi_prop,
            display_name=vm_obj.name)

        vmi_obj.uuid = obj_uuid
        vmi_obj.set_virtual_network(vn_obj)
        vmi_obj.set_virtual_machine(vm_obj)
        vmi_obj.add_annotations(KeyValuePair(key='index', value='0/1'))
        vmi_obj.port_security_enabled = True
        VirtualMachineInterfaceMM.add_annotations(self, vmi_obj, pod_task_id)

        try:
            vmi_uuid = self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        except RefsExistError:
            vmi_uuid = self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        VirtualMachineInterfaceMM.locate(vmi_uuid)
        return vmi_uuid

    def _create_vm(self, pod_task_id, node_ip):
        pod_task_name = PodTaskMonitor.get_task_pod_name_from_cid(pod_task_id,
                                                                  node_ip)
        if pod_task_name is None:
            vm_obj = VirtualMachine(name=pod_task_id)
        else:
            vm_obj = VirtualMachine(name=pod_task_name)

        vm_obj.uuid = pod_task_id
        vm_obj.set_server_type("container")

        VirtualMachineMM.add_annotations(self, vm_obj, pod_task_id)
        try:
            self._vnc_lib.virtual_machine_create(vm_obj)
        except RefsExistError:
            vm_obj = self._vnc_lib.virtual_machine_read(id=pod_task_id)
        VirtualMachineMM.locate(vm_obj.uuid)
        return vm_obj

    def _create_iip(self, pod_task_id, vn_obj, vmi, custom_ipam=None):
        vn = VirtualNetworkMM.find_by_name_or_uuid(vn_obj.get_uuid())
        if not vn:
            # It is possible our cache may not have the VN yet. Locate it.
            vn = VirtualNetworkMM.locate(vn_obj.get_uuid())
        if custom_ipam is None:
            ipam_fq_name = vnc_mesos_config.pod_task_ipam_fq_name()
        else:
            ipam_fq_name = custom_ipam.split(':')
        pod_ipam_subnet_uuid = vn.get_ipam_subnet_uuid(ipam_fq_name)

        # Create instance-ip.
        iip_uuid = str(uuid.uuid1())
        iip_obj = InstanceIp(name=pod_task_id, subnet_uuid=pod_ipam_subnet_uuid)
        iip_obj.uuid = iip_uuid
        iip_obj.add_virtual_network(vn_obj)

        # Creation of iip requires the vmi vnc object.
        vmi_obj = self._vnc_lib.virtual_machine_interface_read(
            fq_name=vmi.fq_name)
        iip_obj.add_virtual_machine_interface(vmi_obj)

        InstanceIpMM.add_annotations(self, iip_obj, pod_task_id)
        self._logger.debug("%s: Create IIP from ipam_fq_name [%s]"
                            " pod_ipam_subnet_uuid [%s]"
                            " vn [%s] vmi_fq_name [%s]" %\
                            (self._name, ipam_fq_name, pod_ipam_subnet_uuid,
                            vn.name, vmi.fq_name))
        try:
            self._vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            self._vnc_lib.instance_ip_update(iip_obj)
        InstanceIpMM.locate(iip_obj.uuid)
        return iip_obj

    def vnc_pod_task_add(self, obj_labels):
        node_name = obj_labels.node_name
        node_ip = obj_labels.node_ip

        vm = VirtualMachineMM.get(obj_labels.pod_task_uuid)
        if vm:
            if not vm.virtual_router:
                self._link_vm_to_node(vm, node_name, node_ip)
            #TODO: Clear and set new cache
            return vm
        #else:
            #TODO: If needed check for old container with new uuid.
        vn_obj = self._get_network(obj_labels.networks)
        if not vn_obj:
            return

        vm_obj = self._create_vm(obj_labels.pod_task_uuid, node_ip)
        vmi_uuid = self._create_vmi(obj_labels.pod_task_uuid, vm_obj, vn_obj)
        vmi = VirtualMachineInterfaceMM.get(vmi_uuid)

        self._create_iip(vm_obj.name, vn_obj, vmi, obj_labels.pod_subnets)

        self._link_vm_to_node(vm_obj, node_name, node_ip)

        vm = VirtualMachineMM.locate(obj_labels.pod_task_uuid)
        if vm:
            vm.pod_node = node_name
            vm.node_ip = node_ip
            return vm

    def _clear_label_to_pod_cache(self, vm):
        if not vm.pod_labels:
            return
        for label in list(vm.pod_labels.items()) or []:
            key = self._label_cache._get_key(label)
            pod_label_cache = self._label_cache.pod_label_cache
            self._label_cache._remove_label(key, pod_label_cache, label,
                                            vm.uuid)
        vm.pod_labels = None

    def vnc_port_delete(self, vmi_id, pod_task_id):

        vmi = VirtualMachineInterfaceMM.get(vmi_id)
        if not vmi:
            return
        for iip_id in list(vmi.instance_ips):
            try:
                self._vnc_lib.instance_ip_delete(id=iip_id)
            except NoIdError:
                pass

        try:
            self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
        except NoIdError:
            pass

        VirtualMachineInterfaceMM.delete(vmi_id)

    def vnc_pod_task_delete(self, pod_task_id):
        vm = VirtualMachineMM.get(pod_task_id)
        if not vm:
            return

        # If this VM's vrouter info is not available in our config db,
        # then it is a case of race between delete and ref updates.
        # So explicitly update this entry in config db.
        if not vm.virtual_router:
            try:
                vm.update()
            except NoIdError:
                pass

        self._clear_label_to_pod_cache(vm)

        try:
            vm_obj = self._vnc_lib.virtual_machine_read(id=vm.uuid)
        except NoIdError:
            # Unable to find VM object in cache. Cleanup local cache.
            VirtualMachineMM.delete(vm.uuid)
            return

        if vm.virtual_router:
            self._vnc_lib.ref_update('virtual-router', vm.virtual_router,
                                     'virtual-machine', vm.uuid, None,
                                     'DELETE')

        for vmi_id in list(vm.virtual_machine_interfaces):
            self.vnc_port_delete(vmi_id, pod_task_id)

        try:
            self._vnc_lib.virtual_machine_delete(id=pod_task_id)
        except NoIdError:
            pass

        # Cleanup local cache.
        VirtualMachineMM.delete(pod_task_id)

    def process(self, event):
        """Process ADD/DEL event"""
        obj_labels = MesosCniLabels(event, self._logger)
        if obj_labels.operation == 'ADD':
            self._logger.info('Add request.')
            vm = self.vnc_pod_task_add(obj_labels)
            if vm.uuid == vm.name:
                event['event_type'] = vm.uuid
                self._sync_queue.put(event)
        elif obj_labels.operation == 'DEL':
            self._logger.info('Delete request')
            vm = self.vnc_pod_task_delete(obj_labels.pod_task_uuid)
        else:
            self._logger.error('Invalid operation')

class MesosCniLabels(object):
    """Handle label processing"""
    def __init__(self, event, logger):
        """Initialize all labels to default vaule"""
        self._logger = logger
        self.operation = event['cmd']
        self.pod_task_uuid = event['cid']
        self.domain_name = ''
        self.project_name = ''
        self.node_name = ''
        self.node_ip = ''
        self.networks = None
        self.pod_subnets = None
        self.security_groups = ''
        self.floating_ips = ''
        self._extract_values(event)

    def _extract_values(self, event):
            """Extract values from  args"""
            labels = event['labels']
            """Extract values from label"""
            if 'domain-name' in list(labels.keys()):
                self.domain_name = labels['domain-name']
            if 'project-name' in list(labels.keys()):
                self.project_name = labels['project-name']
            if 'networks' in list(labels.keys()):
                self.networks = labels['networks']
            if 'pod_subnets' in list(labels.keys()):
                self.pod_subnets =  labels['pod-subnets']
            if 'security-groups' in list(labels.keys()):
                self.security_groups = labels['security-groups']
            if 'floating-ips' in list(labels.keys()):
                self.floating_ips = labels['floating-ips']
            if 'node-name' in list(labels.keys()):
                self.node_name = labels['node-name']
            if 'node-ip' in list(labels.keys()):
                self.node_ip = labels['node-ip']
            print ("Debug:{} {} {} {} {} {} {}"
                             .format(self.domain_name, self.project_name,
                                     self.networks, self.security_groups,
                                     self.floating_ips, self.node_name,
                                     self.node_ip))
            self._logger.info("Extracting labels done")

