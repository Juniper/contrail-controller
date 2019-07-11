#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC endpoints management for kubernetes
"""


from cfgm_common import importutils
from kube_manager.vnc.config_db import (
    DBBaseKM, InstanceIpKM, LoadbalancerKM, LoadbalancerListenerKM,
    LoadbalancerMemberKM, LoadbalancerPoolKM, VirtualMachineInterfaceKM,
    VirtualMachineKM)
from kube_manager.vnc.vnc_common import VncCommon
from kube_manager.vnc.vnc_kubernetes_config \
    import VncKubernetesConfig as vnc_kube_config
from vnc_api.vnc_api import (NoIdError, VirtualMachine)
from kube_manager.vnc.label_cache import XLabelCache


class VncEndpoints(VncCommon):
    def __init__(self):
        super(VncEndpoints, self).__init__('Endpoint')
        self._name = type(self).__name__
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self.logger = vnc_kube_config.logger()
        self._kube = vnc_kube_config.kube()
        self._labels = XLabelCache('Endpoint')
        self._args = vnc_kube_config.args()

        self.service_lb_pool_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbPoolManager')
        self.service_lb_member_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbMemberManager')

    @staticmethod
    def _is_nested():
        # nested if we are configured to run in nested mode.
        return DBBaseKM.is_nested()

    @staticmethod
    def _get_host_vm(host_ip):
        iip = InstanceIpKM.get_object(
            host_ip, vnc_kube_config.cluster_default_network_fq_name())
        if iip:
            for vmi_id in iip.virtual_machine_interfaces:
                vm_vmi = VirtualMachineInterfaceKM.get(vmi_id)
                if vm_vmi and vm_vmi.virtual_machine:
                    return vm_vmi.virtual_machine

        return None

    def _vnc_create_member(self, pool, pod_id, vmi_id, protocol_port):
        pool_obj = self.service_lb_pool_mgr.read(pool.uuid)
        address = None
        annotations = {
            'vmi': vmi_id,
            'vm': pod_id
        }
        return self.service_lb_member_mgr.create(
            pool_obj, address, protocol_port, annotations)

    def _get_loadbalancer_id_or_none(self, service_name, service_namespace):
        """
        Get ID of loadbalancer given service name and namespace.
        Return None if loadbalancer for the given service does not exist.
        """
        service_info = self._kube.get_resource(
            'services', service_name, service_namespace)
        if service_info is None or 'metadata' not in service_info:
            return None

        service_uid = service_info['metadata'].get('uid')
        if not service_uid:
            return None

        lb_name = VncCommon.make_name(service_name, service_uid)
        project_fq_name = vnc_kube_config.cluster_project_fq_name(
            service_namespace)
        lb_fq_name = project_fq_name + [lb_name]
        try:
            loadbalancer = self._vnc_lib.loadbalancer_read(fq_name=lb_fq_name)
        except NoIdError:
            return None
        if loadbalancer is None:
            return None

        return loadbalancer.uuid

    @staticmethod
    def _get_loadbalancer_pool(lb_listener_id, port=None):
        lb_listener = LoadbalancerListenerKM.get(lb_listener_id)
        if not lb_listener:
            return None
        if not lb_listener.params['protocol_port']:
            return None

        if port:
            if lb_listener.params['protocol'] != port['protocol']:
                return None
            if lb_listener.port_name and port.get('name') and \
                    lb_listener.port_name != port['name']:
                return None

        return LoadbalancerPoolKM.get(lb_listener.loadbalancer_pool)

    def _get_vmi_from_ip(self, host_ip):
        vmi_list = self._vnc_lib.virtual_machine_interfaces_list(detail=True)
        for vmi in vmi_list:
            if vmi.parent_type == "virtual-router":
                vr_obj = self._vnc_lib.virtual_router_read(id=vmi.parent_uuid)
                if host_ip == vr_obj.get_virtual_router_ip_address():
                    return vmi.uuid

    def _add_pod_to_service(self, service_id, pod_id, port=None, address=None):
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            return
        vm = VirtualMachineKM.get(pod_id)
        host_vmi = None
        if not vm:
            if not self._args.host_network_service:
                return
            host_vmi = self._get_vmi_from_ip(address)
            if host_vmi == None:
                return
            else:
                vm = VirtualMachine(name="host", display_name="host")
                vm.virtual_machine_interfaces = [host_vmi]


        for lb_listener_id in lb.loadbalancer_listeners:
            pool = self._get_loadbalancer_pool(lb_listener_id, port)
            if not pool:
                continue

            for vmi_id in vm.virtual_machine_interfaces:
                if host_vmi == None:
                    vmi = VirtualMachineInterfaceKM.get(vmi_id)
                else:
                    vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
                if not vmi:
                    continue

                # Add VMI only if it matches the default address for endpoint,
                # ignore other interfaces for pod
                ip_found = False
                for iip_uuid in vmi.instance_ips:
                    iip = InstanceIpKM.get(iip_uuid)
                    if iip and iip.address == address:
                        ip_found = True
                        break

                if ip_found == False:
                    continue

                for member_id in pool.members:
                    member = LoadbalancerMemberKM.get(member_id)
                    if member and member.vmi == vmi_id:
                        break
                else:
                    self.logger.debug(
                        "Creating LB member for Pod/VM: %s in LB: %s with "
                        "target-port: %d"
                        % (vm.fq_name, lb.name, port['port']))
                    member_obj = self._vnc_create_member(
                        pool, pod_id, vmi_id, port['port'])

                    try:
                        vmi_obj = self._vnc_lib.virtual_machine_interface_read(
                                      id = vmi_id)
                    except:
                        raise

                    # Attach the service label to underlying pod vmi.
                    self._labels.append(vmi_id,
                        self._labels.get_service_label(lb.service_name))
                    # Set tags on the vmi.
                    self._vnc_lib.set_tags(vmi_obj,
                        self._labels.get_labels_dict(vmi_id))

                    LoadbalancerMemberKM.locate(member_obj.uuid)

    def _remove_pod_from_service(self, service_id, pod_id, port=None):
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            return

        for lb_listener_id in lb.loadbalancer_listeners:
            pool = self._get_loadbalancer_pool(lb_listener_id, port)
            if not pool:
                continue

            for member_id in pool.members:
                member = LoadbalancerMemberKM.get(member_id)
                if member and member.vm == pod_id:
                    self.logger.debug(
                        "Delete LB member for Pod/VM: %s from LB: %s"
                        % (pod_id, lb.name))

                    try:
                        vmi_obj = self._vnc_lib.virtual_machine_interface_read(
                                      id = member.vmi)

                        # Remove service member label from vmi.
                        svc_member_label = self._labels.get_service_label(
                            lb.service_name)
                        for k,v in svc_member_label.iteritems():
                            self._vnc_lib.unset_tag(vmi_obj, k)

                    except NoIdError:
                        # VMI has already been deleted. Nothing to unset/remove.
                        pass
                    except:
                        raise

                    self.service_lb_member_mgr.delete(member_id)
                    LoadbalancerMemberKM.delete(member.uuid)
                    break

    def _get_pods_attached_to_service(self, service_id, port=None):
        """
        Get list of Pods attached to the Service for a given port.
        """
        pod_members = set()
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            return pod_members

        # No listeners on LB. Error condition. Handle gracefully..
        if len(lb.loadbalancer_listeners) == 0:
            self.logger.warning("No listeners on LB ({})".format(lb.name))
            return pod_members

        for lb_listener_id in lb.loadbalancer_listeners:
            pool = self._get_loadbalancer_pool(lb_listener_id, port)
            if not pool:
                continue

            for member_id in pool.members:
                member = LoadbalancerMemberKM.get(member_id)
                if member.vm:
                    pod_members.add(member.vm)

        return pod_members

    @staticmethod
    def _get_ports_from_event(event):
        """
        Get list of ports from event.
        Only ports for the first subset are returned. Other ignored!
        """
        ports = []
        subsets = event['object'].get('subsets', [])
        for subset in subsets if subsets else []:
            ports = subset.get('ports', [])
            break
        return ports

    def _get_pods_from_event(self, event):
        """
        Get list of Pods matching Service Selector as listed in event.
        Pods are same for all ports.
        """
        pods_in_event = set()
        pods_to_ip = {}
        subsets = event['object'].get('subsets', [])
        for subset in subsets if subsets else []:
            endpoints = subset.get('addresses', [])
            for endpoint in endpoints:
                pod = endpoint.get('targetRef')
                if pod and pod.get('uid'):
                    pod_uid = pod.get('uid')
                    pods_in_event.add(pod_uid)
                    pods_to_ip[pod_uid] = endpoint.get('ip')
                else:  # hosts
                    host_ip = endpoint.get('ip')
                    if self._is_nested():
                        host_vm = self._get_host_vm(host_ip)
                        if host_vm:
                            pods_in_event.add(host_vm)
                            pods_to_ip[host_vm] = endpoint.get('ip')

        return pods_in_event, pods_to_ip

    def vnc_endpoint_add(self, name, namespace, event):
        # Does service exists in contrail-api server?
        # If No, log warning and return
        service_id = self._get_loadbalancer_id_or_none(name, namespace)
        if service_id is None:
            self.logger.debug(
                "Add/Modify endpoints event received while service {} does "
                "not exist".format(name))
            return

        event_pod_ids, pods_to_ip = self._get_pods_from_event(event)
        ports = self._get_ports_from_event(event)

        for port in ports:

            attached_pod_ids = self._get_pods_attached_to_service(
                service_id, port)

            # If Pod present only in event, add Pod to Service
            for pod_id in event_pod_ids.difference(attached_pod_ids):
                self._add_pod_to_service(service_id, pod_id, port, pods_to_ip[pod_id])

            # If Pod not present in event, delete Pod from Service
            for pod_id in attached_pod_ids.difference(event_pod_ids):
                self._remove_pod_from_service(service_id, pod_id, port)

            # If Pod present in both lists, do nothing

    def vnc_endpoint_delete(self, name, namespace, event):
        # Does service exists in contrail-api server?
        # If No, log warning and return
        service_id = self._get_loadbalancer_id_or_none(name, namespace)
        if service_id is None:
            self.logger.warning(
                "Delete endpoints event received while service {} does "
                "not exist".format(name))
            return

        attached_pod_ids = self._get_pods_attached_to_service(service_id)
        event_pod_ids, pods_to_ip = self._get_pods_from_event(event)

        # Compare 2 lists. Should be same.. any diff is a sign of warning
        if attached_pod_ids.symmetric_difference(event_pod_ids):
            self.logger.warning(
                "Pods listed in the received event differ from actual pods "
                "attached to service {}".format(name))

        # Actual members are source of truth. Delete them'all
        for pod_id in attached_pod_ids:
            self._remove_pod_from_service(service_id, pod_id)

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        namespace = event['object']['metadata'].get('namespace')
        name = event['object']['metadata'].get('name')
        uid = event['object']['metadata'].get('uid')

        print("%s - Got %s %s %s:%s:%s"
              % (self._name, event_type, kind, namespace, name, uid))
        self.logger.debug(
            "%s - Got %s %s %s:%s:%s"
            % (self._name, event_type, kind, namespace, name, uid))

        if event['type'] in ('ADDED', 'MODIFIED'):
            self.vnc_endpoint_add(name, namespace, event)
        elif event['type'] == 'DELETED':
            self.vnc_endpoint_delete(name, namespace, event)
        else:
            self.logger.warning(
                'Unknown event type: "{}" Ignoring'.format(event['type']))
