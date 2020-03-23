#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC pod management for kubernetes
"""
from __future__ import print_function

from future import standard_library
standard_library.install_aliases()
from builtins import str
from six import StringIO
import json
import uuid

from cfgm_common.exceptions import RefsExistError, NoIdError
from vnc_api.vnc_api import (
    InstanceIp, FloatingIp, FloatingIpPool, VirtualMachine,
    VirtualMachineInterface, VirtualMachineInterfacePropertiesType,
    SecurityGroup, PermType2)
from kube_manager.vnc.config_db import (
    DBBaseKM, VirtualNetworkKM, VirtualRouterKM, VirtualMachineKM,
    VirtualMachineInterfaceKM, InstanceIpKM, FloatingIpKM, LoadbalancerKM,
    TagKM)
from kube_manager.vnc.vnc_common import VncCommon
from kube_manager.common.kube_config_db import (NamespaceKM, PodKM, NetworkKM)
from kube_manager.vnc.vnc_kubernetes_config import (
    VncKubernetesConfig as vnc_kube_config)
from kube_manager.vnc.label_cache import XLabelCache

from cfgm_common import *
from cfgm_common.utils import cgitb_hook


class VncPod(VncCommon):
    vnc_pod_instance = None

    def __init__(self, service_mgr, network_policy_mgr):
        super(VncPod, self).__init__('Pod')
        self._name = type(self).__name__
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self._label_cache = vnc_kube_config.label_cache()
        self._labels = XLabelCache('Pod')
        self._service_mgr = service_mgr
        self._network_policy_mgr = network_policy_mgr
        self._queue = vnc_kube_config.queue()
        self._args = vnc_kube_config.args()
        self._logger = vnc_kube_config.logger()
        self._kube = vnc_kube_config.kube()
        if not VncPod.vnc_pod_instance:
            VncPod.vnc_pod_instance = self

    def _set_label_to_pod_cache(self, new_labels, vm):
        namespace_label = self._label_cache. \
            _get_namespace_label(vm.pod_namespace)
        new_labels.update(namespace_label)
        for label in list(new_labels.items()):
            key = self._label_cache._get_key(label)
            pod_label_cache = self._label_cache.pod_label_cache
            self._label_cache._locate_label(key, pod_label_cache, label,
                                            vm.uuid)
        vm.pod_labels = new_labels

    def _clear_label_to_pod_cache(self, vm):
        if not vm.pod_labels:
            return
        for label in list(vm.pod_labels.items()) or []:
            key = self._label_cache._get_key(label)
            pod_label_cache = self._label_cache.pod_label_cache
            self._label_cache._remove_label(key, pod_label_cache, label,
                                            vm.uuid)
        vm.pod_labels = None

    def _update_label_to_pod_cache(self, new_labels, vm):
        self._clear_label_to_pod_cache(vm)
        self._set_label_to_pod_cache(new_labels, vm)

    def _get_default_network(self, pod_id, pod_name, pod_namespace):
        """
        Get virtual network to be associated with the pod.
        The heuristics to determine which virtual network to use for the pod
        is as follows:
        if (virtual network is annotated in the pod config):
            Use virtual network configured on the pod.
        else if (virtual network if annotated in the pod's namespace):
            Use virtual network configured on the namespace.
        else if (pod is in a isolated namespace):
            Use the virtual network associated with isolated namespace.
        else:
            Use the pod virtual network associated with kubernetes cluster.
        """

        # Check for virtual-network configured on the pod.
        pod = PodKM.find_by_name_or_uuid(pod_id)
        if not pod:
            self._logger.notice("%s - Pod %s:%s:%s Not Found"
                                "(Might Got Delete Event From K8s)"
                                %(self._name, pod_namespace, pod_name, pod_id))
            return

        vn_fq_name = pod.get_vn_fq_name()
        ns = self._get_namespace(pod_namespace)

        # FIXME: Check if ns is not None
        # Check of virtual network configured on the namespace.
        if not vn_fq_name:
            vn_fq_name = ns.get_annotated_network_fq_name()

        # If the pod's namespace is isolated, use the isolated virtual
        # network.
        if not vn_fq_name:
            if self._is_pod_network_isolated(pod_namespace):
                vn_fq_name = ns.get_isolated_pod_network_fq_name()

        # Finally, if no network was found, default to the cluster
        # pod network.
        if not vn_fq_name:
            vn_fq_name = vnc_kube_config.cluster_default_pod_network_fq_name()

        vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        return vn_obj

    def _get_user_defined_network(self, nw_name, ns_name):

        nw = NetworkKM.get_network_fq_name(nw_name, ns_name)
        if not nw or not nw.is_contrail_nw():
            return None

        vn_obj = None
        try:
            vn_obj = self._vnc_lib.virtual_network_read(
                                    fq_name=nw.annotated_vn_fq_name)
        except:
            return None

        return vn_obj

    @staticmethod
    def _get_namespace(pod_namespace):
        return NamespaceKM.find_by_name_or_uuid(pod_namespace)

    @staticmethod
    def _get_namespace_labels(pod_namespace):
        labels = {}

        # Get the explicit labels on a pod.
        ns = NamespaceKM.find_by_name_or_uuid(pod_namespace)
        if ns and ns.labels:
            labels = dict(ns.labels)

        # Append the implicit namespace tag to a pod.
        labels['namespace'] = pod_namespace

        return labels

    def _is_pod_network_isolated(self, pod_namespace):
        return self._get_namespace(pod_namespace).is_isolated()

    @staticmethod
    def _is_pod_nested():
        # Pod is nested if we are configured to run in nested mode.
        return DBBaseKM.is_nested()

    @staticmethod
    def _get_host_ip(pod_name):
        pod = PodKM.find_by_name_or_uuid(pod_name)
        if pod:
            return pod.get_host_ip()
        return None

    def _get_ip_fabric_forwarding(self, ns_name):
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.get_ip_fabric_forwarding()
        return None

    def _is_ip_fabric_forwarding_enabled(self, ns_name):
        ip_fabric_forwarding = self._get_ip_fabric_forwarding(ns_name)
        if ip_fabric_forwarding != None:
            return ip_fabric_forwarding
        else:
            return self._args.ip_fabric_forwarding

    def _create_iip(self, pod_name, pod_namespace, proj_uuid, vn_obj, vmi):
        # Instance-ip for pods are ALWAYS allocated from pod ipam on this
        # VN. Get the subnet uuid of the pod ipam on this VN, so we can request
        # an IP from it.
        vn = VirtualNetworkKM.find_by_name_or_uuid(vn_obj.get_uuid())
        if not vn:
            # It is possible our cache may not have the VN yet. Locate it.
            vn = VirtualNetworkKM.locate(vn_obj.get_uuid())

        if self._is_pod_network_isolated(pod_namespace):
            vn_namespace = pod_namespace
        else:
            vn_namespace = 'default'

        if self._is_ip_fabric_forwarding_enabled(vn_namespace):
            ipam_fq_name = vnc_kube_config.ip_fabric_ipam_fq_name()
        else:
            ipam_fq_name = vnc_kube_config.pod_ipam_fq_name()
        pod_ipam_subnet_uuid = vn.get_ipam_subnet_uuid(ipam_fq_name)

        # Create instance-ip.
        iip_uuid = str(uuid.uuid1())
        iip_name = VncCommon.make_name(pod_name, iip_uuid)
        perms2 = PermType2()
        perms2.owner = proj_uuid
        perms2.owner_access = PERMS_RWX
        iip_obj = InstanceIp(name=iip_name, subnet_uuid=pod_ipam_subnet_uuid,
                             display_name=iip_name, perms2=perms2)
        iip_obj.uuid = iip_uuid
        iip_obj.add_virtual_network(vn_obj)

        # Creation of iip requires the vmi vnc object.
        vmi_obj = self._vnc_lib.virtual_machine_interface_read(
            fq_name=vmi.fq_name)
        iip_obj.add_virtual_machine_interface(vmi_obj)

        InstanceIpKM.add_annotations(self, iip_obj, pod_namespace, pod_name)
        self._logger.debug("%s: Create IIP from ipam_fq_name [%s]"
                            " pod_ipam_subnet_uuid [%s]"
                            " vn [%s] vmi_fq_name [%s]" %\
                            (self._name, ipam_fq_name, pod_ipam_subnet_uuid,
                            vn.name, vmi.fq_name))
        try:
            self._vnc_lib.instance_ip_create(iip_obj)
        except RefsExistError:
            self._vnc_lib.instance_ip_update(iip_obj)
        InstanceIpKM.locate(iip_obj.uuid)
        return iip_obj

    def _get_host_vmi(self, pod_name):
        host_ip = self._get_host_ip(pod_name)
        if host_ip:
            net_fq_name = vnc_kube_config.cluster_default_network_fq_name()
            iip = InstanceIpKM.get_object(host_ip, net_fq_name)

            if iip:
                for vmi_id in iip.virtual_machine_interfaces:
                    vm_vmi = VirtualMachineInterfaceKM.get(vmi_id)
                    if vm_vmi and vm_vmi.host_id:
                        return vm_vmi

        return None

    @staticmethod
    def _associate_security_groups(vmi_obj, proj_obj, ns):
        sg_name = "-".join([vnc_kube_config.cluster_name(), ns, 'default-sg'])
        sg_obj = SecurityGroup(sg_name, proj_obj)
        vmi_obj.add_security_group(sg_obj)
        return

    def _create_vmi(self, pod_name, pod_namespace, pod_id, vm_obj, vn_obj,
                    proj_obj, parent_vmi, idx, network=None):
        if network and 'namespace' in network:
            network.pop('namespace')

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
        name = VncCommon.make_name(pod_name, obj_uuid)
        vmi_obj = VirtualMachineInterface(
            name=name, parent_obj=proj_obj,
            virtual_machine_interface_properties=vmi_prop,
            display_name=name)

        vmi_obj.uuid = obj_uuid
        vmi_obj.set_virtual_network(vn_obj)
        vmi_obj.set_virtual_machine(vm_obj)
        self._associate_security_groups(vmi_obj, proj_obj, pod_namespace)
        vmi_obj.port_security_enabled = True
        VirtualMachineInterfaceKM.add_annotations(self, vmi_obj, pod_namespace,
                                        pod_name, index=idx, **network)

        try:
            vmi_uuid = self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        except RefsExistError:
            vmi_uuid = self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        VirtualMachineInterfaceKM.locate(vmi_uuid)
        return vmi_uuid

    def _create_vm(self, pod_namespace, pod_id, pod_name, labels, proj_uuid):
        vm_name = VncCommon.make_name(pod_name, pod_id)
        display_name = VncCommon.make_display_name(pod_namespace, pod_name)
        perms2 = PermType2()
        perms2.owner = proj_uuid
        perms2.owner_access = PERMS_RWX
        vm_obj = VirtualMachine(name=vm_name, perms2=perms2, display_name=display_name)
        vm_obj.uuid = pod_id
        vm_obj.set_server_type("container")

        VirtualMachineKM.add_annotations(self, vm_obj, pod_namespace, pod_name,
                                         k8s_uuid=str(pod_id),
                                         labels=json.dumps(labels))
        try:
            self._vnc_lib.virtual_machine_create(vm_obj)
        except RefsExistError:
            vm_obj = self._vnc_lib.virtual_machine_read(id=pod_id)
        VirtualMachineKM.locate(vm_obj.uuid)
        return vm_obj

    def _link_vm_to_node(self, vm_obj, pod_node, node_ip):
        if node_ip is None:
            return

        vm = VirtualMachineKM.locate(vm_obj.uuid)
        if vm:
            vm.node_ip = node_ip

        vr_uuid = VirtualRouterKM.get_ip_addr_to_uuid(node_ip)
        if vr_uuid is None:
            for vr in list(VirtualRouterKM.values()):
                if vr.name.lower() == pod_node:
                    vr_uuid = vr.uuid
        if vr_uuid is None:
            self._logger.debug("%s - Vrouter %s Not Found for Pod %s"
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

    def _check_pod_uuid_change(self, pod_uuid, pod_name):
        vm_fq_name = [pod_name]
        vm_uuid = LoadbalancerKM.get_fq_name_to_uuid(vm_fq_name)
        if vm_uuid != pod_uuid:
            self.vnc_pod_delete(vm_uuid)

    def _set_tags_on_pod_vmi(self, pod_id, vmi_obj=None):
        vmi_obj_list = []
        if not vmi_obj:
            vm = VirtualMachineKM.get(pod_id)
            if vm:
                for vmi_id in list(vm.virtual_machine_interfaces):
                    vmi_obj_list.append(
                        self._vnc_lib.virtual_machine_interface_read(id=vmi_id))
        else:
            vmi_obj_list.append(vmi_obj)

        for vmi_obj in vmi_obj_list:
            self._vnc_lib.set_tags(vmi_obj, self._labels.get_labels_dict(pod_id))

    def _unset_tags_on_pod_vmi(self, pod_id, vmi_id=None, labels={}):
        vmi_obj_list = []
        if not vmi_id:
            vm = VirtualMachineKM.get(pod_id)
            if vm:
                for vmi_id in list(vm.virtual_machine_interfaces):
                    vmi_obj_list.append(self._vnc_lib.virtual_machine_interface_read(id=vmi_id))
        else:
            vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
            vmi_obj_list.append(vmi_obj)

        for vmi_obj in vmi_obj_list:
            if not labels:
                for k,v in self._labels.get_labels_dict(pod_id).items():
                    self._vnc_lib.unset_tag(vmi_obj, k)
            else:
                for k,v in labels.items():
                    self._vnc_lib.unset_tag(vmi_obj, k)

    def _update_network_status(self, pod_name, pod_namespace, network_status):
        net_status_dict_list = []
        for nw_name,vmi_uuid in list(network_status.items()):
            vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_uuid)
            vmi = VirtualMachineInterfaceKM.locate(vmi_uuid)
            pod_iips = []
            for iip_uuid in list(vmi.instance_ips):
                iip_obj = self._vnc_lib.instance_ip_read(id=iip_uuid)
                if not iip_obj.get_instance_ip_secondary():
                    ip = iip_obj.get_instance_ip_address()
                    pod_iips.append(ip)
            ns_dict = {}
            ns_dict['name'] = nw_name
            ns_dict['ips'] = ''.join(pod_iips)
            ns_dict['mac'] = \
                ''.join(vmi_obj.get_virtual_machine_interface_mac_addresses(\
                        ).get_mac_address())
            net_status_dict_list.append(ns_dict)

        patch = {'metadata': {'annotations': {\
            'k8s.v1.cni.cncf.io/network-status':\
                    json.dumps(net_status_dict_list, sort_keys=True,
                                indent=4, separators=(',', ': '))}}}
        if self._kube is not None:
            self._kube.patch_resource("pod", pod_name, patch, pod_namespace)

    def vnc_pod_vmi_create(self, pod_id, pod_name, pod_namespace,
                           pod_node, node_ip, vm_obj, vn_obj,
                           proj_obj, vm_vmi, idx, network=None):

        vmi_uuid = self._create_vmi(pod_name, pod_namespace, pod_id,
                                    vm_obj, vn_obj, proj_obj, vm_vmi,
                                    idx, network=network)
        vmi = VirtualMachineInterfaceKM.get(vmi_uuid)

        if self._is_pod_nested() and vm_vmi:
            # Pod is nested.
            # Link the pod VMI to the VMI of the underlay VM.
            self._vnc_lib.ref_update('virtual-machine-interface', vm_vmi.uuid,
                                     'virtual-machine-interface', vmi_uuid,
                                     None, 'ADD')
            self._vnc_lib.ref_update('virtual-machine-interface', vmi_uuid,
                                     'virtual-machine-interface', vm_vmi.uuid,
                                     None, 'ADD')

            # get host id for vm vmi
            vr_uuid = None
            for vr in list(VirtualRouterKM.values()):
                if vr.name == vm_vmi.host_id:
                    vr_uuid = vr.uuid
                    break

            if not vr_uuid:
                # Unable to determine VRouter for the parent VM.
                #
                # HACK ALERT
                #
                # It is possible that this is a case of FQDN mismatch between
                # the host name associated with the VM and the host name
                # associated with the corresponding vrouter. So try to look for
                # vrouter again with a non-FQDN name.
                #
                # This needs to be removed when provisioning can guarantee that
                # FQDN will be uniform across all config objects.
                #
                if '.' in vm_vmi.host_id:
                    # Host name on VM is a FQNAME. Ignore domain name.
                    host_id_prefix = vm_vmi.host_id.split('.')[0]
                    for vr in list(VirtualRouterKM.values()):
                        if vr.name == host_id_prefix:
                            vr_uuid = vr.uuid
                            break

                if not vr_uuid:
                    # Host name on vrouter is a FQNAME. Ignore domain name.
                    # This can happen, as post R5.1, vrouter is using FQNAME and
                    # VM object created by Openstack could contain non-FQ name.
                    for vr in list(VirtualRouterKM.values()):
                        if '.' in  vr.name:
                            host_id_prefix = vr.name.split('.')[0]
                            if vm_vmi.host_id == host_id_prefix:
                                vr_uuid = vr.uuid
                                break


            if not vr_uuid:
                self._logger.error("No virtual-router object found for host: "
                                   + vm_vmi.host_id
                                   + ". Unable to add VM reference to a"
                                   + " valid virtual-router")
                return
            self._vnc_lib.ref_update('virtual-router', vr_uuid,
                                     'virtual-machine', vm_obj.uuid, None,
                                     'ADD')

        iip_obj = self._create_iip(pod_name, pod_namespace,\
                                   proj_obj.uuid, vn_obj, vmi)
        return vmi_uuid

    def vnc_pod_add(self, pod_id, pod_name, pod_namespace, pod_node, node_ip,
                    labels, vm_vmi):
        vm = VirtualMachineKM.get(pod_id)
        if vm:
            vm.pod_namespace = pod_namespace
            if not vm.virtual_router:
                self._link_vm_to_node(vm, pod_node, node_ip)
            self._set_label_to_pod_cache(labels, vm)

            # Update tags.
            self._set_tags_on_pod_vmi(pod_id)

            return vm
        else:
            self._check_pod_uuid_change(pod_id, pod_name)

        vn_obj = self._get_default_network(pod_id, pod_name, pod_namespace)
        if not vn_obj:
            return

        pod = PodKM.find_by_name_or_uuid(pod_id)
        total_interface_count =  len(pod.networks) + 1

        # network_status: Dict of network name to vmi_uuid
        network_status = {}
        proj_fq_name = vnc_kube_config.cluster_project_fq_name(pod_namespace)
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        vm_obj = self._create_vm(pod_namespace, pod_id,\
                         pod_name, labels, proj_obj.uuid)
        index = str(0) + "/" + str(total_interface_count)
        default_network = {'network':'default'}
        vmi_uuid = self.vnc_pod_vmi_create(pod_id, pod_name, pod_namespace,\
                                pod_node, node_ip, vm_obj, vn_obj, proj_obj,\
                                vm_vmi, index, default_network)
        network_status['cluster-wide-default'] = vmi_uuid

        for idx, network in enumerate(pod.networks, start=1):
            net_namespace = pod_namespace
            net_name = network['network']
            if 'namespace' in network:
                net_namespace = network['namespace']
            vn_obj = self._get_user_defined_network(net_name, net_namespace)
            index = str(idx) + "/" + str(total_interface_count)
            vmi_uuid = self.vnc_pod_vmi_create(pod_id, pod_name, pod_namespace,\
                                pod_node, node_ip, vm_obj, vn_obj, proj_obj,\
                                vm_vmi, index, network)
            network_status[net_name] = vmi_uuid

        if not self._is_pod_nested():
            self._link_vm_to_node(vm_obj, pod_node, node_ip)

        vm = VirtualMachineKM.locate(pod_id)
        if vm:
            vm.pod_namespace = pod_namespace
            vm.pod_node = pod_node
            vm.node_ip = node_ip
            self._set_label_to_pod_cache(labels, vm)
            self._set_tags_on_pod_vmi(pod_id)
            # Update network-status in pod description
            self._update_network_status(pod_name, pod_namespace, network_status)
            return vm

    def vnc_pod_update(self, pod_id, pod_name, pod_namespace, \
                                pod_node, node_ip, labels, vm_vmi):
        vm = VirtualMachineKM.get(pod_id)
        if not vm:
            # If the vm is not created yet, do so now.
            vm = self.vnc_pod_add(pod_id, pod_name, pod_namespace,
                pod_node, node_ip, labels, vm_vmi)
            if not vm:
                return
        vm.pod_namespace = pod_namespace
        if not vm.virtual_router:
            self._link_vm_to_node(vm, pod_node, node_ip)
        self._update_label_to_pod_cache(labels, vm)
        self._set_tags_on_pod_vmi(pod_id)

        return vm

    def vnc_port_delete(self, vmi_id, pod_id):

        self._unset_tags_on_pod_vmi(pod_id, vmi_id=vmi_id)

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
                self._vnc_lib.ref_update('floating-ip', fip_id,
                                         'virtual-machine-interface', vmi_id, None,
                                         'DELETE')
                FloatingIpKM.update(fip_id)
            except NoIdError:
                pass

        try:
            self._vnc_lib.virtual_machine_interface_delete(id=vmi_id)
        except NoIdError:
            pass

        VirtualMachineInterfaceKM.delete(vmi_id)

    def vnc_pod_delete(self, pod_id):
        vm = VirtualMachineKM.get(pod_id)
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
            VirtualMachineKM.delete(vm.uuid)
            return

        if vm.virtual_router:
            self._vnc_lib.ref_update('virtual-router', vm.virtual_router,
                                     'virtual-machine', vm.uuid, None,
                                     'DELETE')

        for vmi_id in list(vm.virtual_machine_interfaces):
            self.vnc_port_delete(vmi_id, pod_id)

        try:
            self._vnc_lib.virtual_machine_delete(id=pod_id)
        except NoIdError:
            pass

        # Cleanup local cache.
        VirtualMachineKM.delete(pod_id)

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
        vm_uuid_set = set(VirtualMachineKM.keys())
        pod_uuid_set = set(PodKM.keys())
        deleted_pod_set = vm_uuid_set - pod_uuid_set
        for pod_uuid in deleted_pod_set:
            vm = VirtualMachineKM.get(pod_uuid)
            if not vm or\
               vm.owner != 'k8s' or\
               vm.cluster != vnc_kube_config.cluster_name():
                continue
            self._create_pod_event('delete', pod_uuid, vm)
        for uuid in pod_uuid_set:
            vm = VirtualMachineKM.get(uuid)
            if not vm or\
               vm.owner != 'k8s' or\
               vm.cluster != vnc_kube_config.cluster_name():
                continue
            if not vm.virtual_router:
                pod = PodKM.get(uuid)
                if not pod:
                    continue
                self._link_vm_to_node(vm, pod.nodename, pod.host_ip)
        return

    def pod_timer(self):
        self._sync_pod_vm()
        return

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        pod_namespace = event['object']['metadata'].get('namespace')
        pod_name = event['object']['metadata'].get('name')
        pod_id = event['object']['metadata'].get('uid')
        labels = event['object']['metadata'].get('labels', {})

        print("%s - Got %s %s %s:%s:%s"
              %(self._name, event_type, kind, pod_namespace, pod_name, pod_id))
        self._logger.debug("%s - Got %s %s %s:%s:%s"
                           %(self._name, event_type, kind, pod_namespace,
                             pod_name, pod_id))

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':

            # Proceed ONLY if host network is specified.
            pod_node = event['object']['spec'].get('nodeName')
            node_ip = event['object']['status'].get('hostIP')
            host_network = event['object']['spec'].get('hostNetwork')
            if host_network:
                return

            # If the pod is nested, proceed ONLY if host vmi is found.
            vm_vmi = None
            if self._is_pod_nested():
                vm_vmi = self._get_host_vmi(pod_name)
                if not vm_vmi:
                    self._logger.debug(
                        "Nested Mode: Pod processing skipped. Unable to "
                        "determine host vmi for Pod[%s] Namespace[%s] "
                        "Event[%s] HostIP[%s])"
                        %(pod_name, pod_namespace, event_type,
                          self._get_host_ip(pod_name)))
                    return

            # Add implicit namespace labels on this pod.
            labels.update(self._get_namespace_labels(pod_namespace))
            self._labels.process(pod_id, labels)

            if event['type'] == 'ADDED':
                vm = self.vnc_pod_add(pod_id, pod_name, pod_namespace,
                                      pod_node, node_ip, labels, vm_vmi)
            else:
                vm = self.vnc_pod_update(pod_id, pod_name,
                    pod_namespace, pod_node, node_ip, labels, vm_vmi)

        elif event['type'] == 'DELETED':
            self.vnc_pod_delete(pod_id)
            self._labels.process(pod_id)
        else:
            self._logger.warning(
                'Unknown event type: "{}" Ignoring'.format(event['type']))

    @classmethod
    def add_labels(cls, pod_id_list, labels):
        if not cls.vnc_pod_instance:
            return

        for pod_id in pod_id_list:
            cls.vnc_pod_instance._labels.append(pod_id, labels)
            cls.vnc_pod_instance._set_tags_on_pod_vmi(pod_id)

    @classmethod
    def remove_labels(cls, pod_id_list, labels):
        if not cls.vnc_pod_instance:
            return

        for pod_id in pod_id_list:
            cls.vnc_pod_instance._unset_tags_on_pod_vmi(pod_id, labels=labels)
            cls.vnc_pod_instance._labels.remove(pod_id, labels)
