#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

from cStringIO import StringIO
from vnc_api.vnc_api import *
from config_db import *
from loadbalancer import *
from kube_manager.common.kube_config_db import NamespaceKM, ServiceKM
from cfgm_common import importutils
from cfgm_common.utils import cgitb_hook
import link_local_manager as ll_mgr
from vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from vnc_common import VncCommon
from kube_manager.common.utils import get_fip_pool_fq_name_from_dict_string
from kube_manager.vnc.label_cache import XLabelCache
from netaddr import IPNetwork, IPAddress

class VncService(VncCommon):

    def __init__(self, ingress_mgr):
        self._k8s_event_type = 'Service'
        super(VncService,self).__init__(self._k8s_event_type)
        self._name = type(self).__name__
        self._ingress_mgr = ingress_mgr
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self._label_cache = vnc_kube_config.label_cache()
        self._labels = XLabelCache(self._k8s_event_type)
        self._labels.reset_resource()
        self._args = vnc_kube_config.args()
        self.logger = vnc_kube_config.logger()
        self._queue = vnc_kube_config.queue()
        self.kube = vnc_kube_config.kube()
        self._fip_pool_obj = None

        # Cache kubernetes API server params.
        self._kubernetes_api_server = self._args.kubernetes_api_server
        self._kubernetes_api_secure_port =\
            int(self._args.kubernetes_api_secure_port)

        # Cache kuberneter service name.
        self._kubernetes_service_name = self._args.kubernetes_service_name

        # Config knob to control enable/disable of link local service.
        if self._args.api_service_link_local == 'True':
            api_service_ll_enable = True
        else:
            api_service_ll_enable = False

        # If Kubernetes API server info is incomplete, disable link-local create,
        # as create is not possible.
        if not self._kubernetes_api_server:
            self._create_linklocal = False
        elif vnc_kube_config.is_cluster_network_configured() and\
             DBBaseKM.is_nested():
            # In nested mode, if cluster network is configured, then the k8s api
            # server is in the same network as the k8s cluster. So there is no
            # need for link local.
            self._create_linklocal = False
        else:
            self._create_linklocal = api_service_ll_enable

        self.service_lb_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbManager')
        self.service_ll_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbListenerManager')
        self.service_lb_pool_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbPoolManager')
        self.service_lb_member_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbMemberManager')

    def _get_project(self, service_namespace):
        proj_fq_name =\
            vnc_kube_config.cluster_project_fq_name(service_namespace)
        try:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
            return proj_obj
        except NoIdError:
            return None

    @staticmethod
    def _get_namespace(service_namespace):
        return NamespaceKM.find_by_name_or_uuid(service_namespace)

    def _get_annotated_ns_fip_pool(self, service_namespace):
        fip_pool_obj = None
        ns = self._get_namespace(service_namespace)
        try:
            if ns.get_annotated_ns_fip_pool_fq_name() != None:
                fip_pool_obj = self._vnc_lib.floating_ip_pool_read(fq_name=ns.get_annotated_ns_fip_pool_fq_name())
        except NoIdError:
            return None
        return fip_pool_obj

    def _get_cluster_service_network(self, service_namespace):
        ns = self._get_namespace(service_namespace)
        if ns and ns.is_isolated():
            vn_fq_name = ns.get_isolated_service_network_fq_name()
        else:
            vn_fq_name = vnc_kube_config.cluster_default_service_network_fq_name()
        try:
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        except NoIdError:
            return None
        return vn_obj

    def _get_service_ipam_subnet_uuid(self, vn_obj):
        service_ipam_subnet_uuid = None
        fq_name = vnc_kube_config.service_ipam_fq_name()
        vn = VirtualNetworkKM.find_by_name_or_uuid(vn_obj.get_uuid())
        if vn:
            service_ipam_subnet_uuid = vn.get_ipam_subnet_uuid(fq_name)
        if service_ipam_subnet_uuid is None:
            self.logger.error("%s - %s Not Found" %(self._name, fq_name))
        return service_ipam_subnet_uuid

    def _get_specified_fip_pool(self, specified_fip_pool_fq_name_str):
        if specified_fip_pool_fq_name_str == None:
            return None

        fip_pool_fq_name = get_fip_pool_fq_name_from_dict_string(
            specified_fip_pool_fq_name_str)
        try:
            fip_pool_obj = self._vnc_lib.floating_ip_pool_read(fq_name=fip_pool_fq_name)
        except NoIdError:
	    self.logger.notice("FIP Pool %s not found. "
                                "Floating IP will not be available "
                                "until FIP pool is configured."
                                % (specified_fip_pool_fq_name_str));
            return None
        return fip_pool_obj

    def _get_public_fip_pool(self):
        if self._fip_pool_obj:
            return self._fip_pool_obj

        if not vnc_kube_config.is_public_fip_pool_configured():
            return None

        fip_pool_fq_name = get_fip_pool_fq_name_from_dict_string(
            self._args.public_fip_pool)
        try:
            fip_pool_obj = self._vnc_lib.floating_ip_pool_read(fq_name=fip_pool_fq_name)
        except NoIdError:
            self.logger.notice("Public FIP Pool not found. "
                                "Floating IP will not be available "
                                "until FIP pool is configured.");
	    return None

        self._fip_pool_obj = fip_pool_obj
        return fip_pool_obj

    def _get_virtualmachine(self, id):
        try:
            vm_obj = self._vnc_lib.virtual_machine_read(id=id)
        except NoIdError:
            return None
        obj = self._vnc_lib.virtual_machine_read(id = id, fields = ['virtual_machine_interface_back_refs'])
        back_refs = getattr(obj, 'virtual_machine_interface_back_refs', None)
        vm_obj.virtual_machine_interface_back_refs = back_refs
        return vm_obj

    def check_service_selectors_actions(self, selectors, service_id, ports):
        for selector in selectors.items():
            key = self._label_cache._get_key(selector)
            self._label_cache._locate_label(key,
                self._label_cache.service_selector_cache, selector, service_id)
            pod_ids = self._label_cache.pod_label_cache.get(key, [])
            if len(pod_ids):
                self.add_pods_to_service(service_id, pod_ids, ports)

    def _vnc_create_pool(self, namespace, ll, port):
        proj_obj = self._get_project(namespace)
        ll_obj = self.service_ll_mgr.read(ll.uuid)
        pool_obj = self.service_lb_pool_mgr.create(ll_obj, proj_obj, port)
        return pool_obj

    def _vnc_create_listener(self, namespace, lb, port):
        proj_obj = self._get_project(namespace)
        lb_obj = self.service_lb_mgr.read(lb.uuid)
        ll_obj = self.service_ll_mgr.create(lb_obj, proj_obj, port)
        return ll_obj

    def _create_listeners(self, namespace, lb, ports):
        for port in ports:
            listener_found = False
            for ll_id in lb.loadbalancer_listeners:
                ll = LoadbalancerListenerKM.get(ll_id)
                if not ll:
                    continue
                if not ll.params['protocol_port']:
                    continue
                if not ll.params['protocol']:
                    continue

                if ll.params['protocol_port'] == port['port'] and \
                   ll.params['protocol'] == port['protocol']:
                    listener_found = True
                    break

            if not listener_found:
                ll_obj = self._vnc_create_listener(namespace, lb, port)
                ll = LoadbalancerListenerKM.locate(ll_obj._uuid)

            pool_id = ll.loadbalancer_pool
            if pool_id:
                pool = LoadbalancerPoolKM.get(pool_id)
            # SAS FIXME: If pool_id present, check for targetPort value
            if not pool_id or not pool:
                pool_obj = self._vnc_create_pool(namespace, ll, port)
                LoadbalancerPoolKM.locate(pool_obj._uuid)

    def _create_link_local_service(self, svc_name, svc_ns, svc_ip, ports):
        # Create link local service only if enabled.
        if self._create_linklocal:
            # Create link local service, one for each port.
            for port in ports:
                try:
                    ll_mgr.create_link_local_service_entry(self._vnc_lib,
                        name=svc_name + '-' + port['port'].__str__(),
                        k8s_ns=svc_ns,
                        service_ip=svc_ip, service_port=port['port'],
                        fabric_ip=self._kubernetes_api_server,
                        fabric_port=self._kubernetes_api_secure_port)
                except:
                    self.logger.error("Create link-local service failed for"
                        " service " + svc_name + " port " +
                        port['port'].__str__())

    def _delete_link_local_service(self, svc_name, svc_ns, ports):
        # Delete link local service only if enabled.
        if self._create_linklocal:
            # Delete link local service, one for each port.
            for port in ports:
                try:
                    ll_mgr.delete_link_local_service_entry(self._vnc_lib,
                        svc_name + '-' + port['port'].__str__(), svc_ns)
                except:
                    self.logger.error("Delete link local service failed for"
                        " service " + svc_name + " port " +
                        port['port'].__str__())

    def _vnc_create_lb(self, service_id, service_name,
                       service_namespace, service_ip):
        proj_obj = self._get_project(service_namespace)
        vn_obj = self._get_cluster_service_network(service_namespace)
        service_ipam_subnet_uuid = self._get_service_ipam_subnet_uuid(vn_obj)
        lb_obj = self.service_lb_mgr.create(self._k8s_event_type,
            service_namespace, service_id, service_name, proj_obj,
            vn_obj, service_ip, service_ipam_subnet_uuid)
        return lb_obj

    def _lb_create(self, service_id, service_name,
            service_namespace, service_ip, ports):
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            lb_obj = self._vnc_create_lb(service_id, service_name,
                                        service_namespace, service_ip)
            if not lb_obj:
                raise NoIdError
            lb = LoadbalancerKM.locate(service_id)

        self._create_listeners(service_namespace, lb, ports)

    def _read_allocated_floating_ips(self, service_id):
        floating_ips = set()
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            return
        vmi_ids = lb.virtual_machine_interfaces
        if vmi_ids is None:
            return None
        interface_found=False
        for vmi_id in vmi_ids:
            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            if vmi is not None:
                interface_found=True
                break
        if interface_found is False:
            return

        fip_ids = vmi.floating_ips
        if fip_ids is None:
            return None
        for fip_id in list(fip_ids):
            fip = FloatingIpKM.get(fip_id)
            if fip is not None:
                floating_ips.add(fip.address)

        return floating_ips

    def _allocate_floating_ips(self, service_id, specified_fip_pool_fq_name_str, service_namespace, external_ips=set()):
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            return None
        vmi_ids = lb.virtual_machine_interfaces
        if vmi_ids is None:
            return None
        interface_found=False
        for vmi_id in vmi_ids:
            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            if vmi is not None:
                interface_found=True
                break
        if interface_found is False:
            return

        vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
        if vmi_obj is None:
            return None

        fip_pool = None
        if specified_fip_pool_fq_name_str != None:
            fip_pool = self._get_specified_fip_pool(specified_fip_pool_fq_name_str)
        if fip_pool is None and self._get_annotated_ns_fip_pool(service_namespace) != None:
            fip_pool = self._get_annotated_ns_fip_pool(service_namespace)
        if fip_pool is None:
            fip_pool = self._get_public_fip_pool()
        if fip_pool is None:
            self.logger.warning("public_fip_pool doesn't exists")
            return None

        def _check_ip_with_fip_pool(external_ip, fip_pool_obj):
            try:
                vn_obj = self._vnc_lib.virtual_network_read(id=fip_pool_obj.parent_uuid)
            except NoIdError:
                return True
            ipam_refs = vn_obj.__dict__.get('network_ipam_refs', [])
            for ipam_ref in ipam_refs:
                vnsn_data = ipam_ref['attr'].__dict__
                ipam_subnets = vnsn_data.get('ipam_subnets', [])
                for ipam_subnet in ipam_subnets:
                    subnet_dict = ipam_subnet.__dict__.get('subnet', {})
                    if 'ip_prefix' in subnet_dict.__dict__:
                       ip_subnet_str = subnet_dict.__dict__.get('ip_prefix','')+'/' \
                                       +str(subnet_dict.__dict__.get('ip_prefix_len'))
                       if IPAddress(external_ip) in IPNetwork(ip_subnet_str):
                           return True
            self.logger.error("external_ip not in fip_pool subnet")
            return False

        def _allocate_floating_ip(lb, vmi, fip_pool, external_ip=None):
            fip_obj = FloatingIp(lb.name + str(external_ip) + "-externalIP", fip_pool)
            fip_obj.set_virtual_machine_interface(vmi_obj)
            if external_ip:
                if not(_check_ip_with_fip_pool(external_ip, fip_pool)):
                    err_str = "external_ip " +  external_ip + " not in fip_pool subnet"
                    self.logger.error(err_str)
                    return None
                fip_obj.set_floating_ip_address(external_ip)
            project = self._vnc_lib.project_read(id=lb.parent_uuid)
            fip_obj.set_project(project)
            try:
                self._vnc_lib.floating_ip_create(fip_obj)
            except RefsExistError as e:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self.logger.error("%s" %(err_msg))
            except:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self.logger.error("%s" %(err_msg))

            fip = FloatingIpKM.locate(fip_obj.uuid)
            self.logger.notice("floating ip allocated : %s for Service (%s)" %
                           (fip.address, service_id))
            return(fip.address)

        fips = set()
        if len(external_ips) is 0:
            fip_addr = _allocate_floating_ip(lb, vmi, fip_pool)
            if fip_addr:
                fips.add(fip_addr)

            return fips

        for external_ip in external_ips:
            fip_addr = _allocate_floating_ip(lb, vmi, fip_pool, external_ip)
            if fip_addr:
                fips.add(fip_addr)
        return fips

    def _deallocate_floating_ips(self, service_id):
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            return
        vmi_ids = lb.virtual_machine_interfaces
        if vmi_ids is None:
            return None
        interface_found=False
        for vmi_id in vmi_ids:
            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            if vmi is not None:
                interface_found=True
                break
        if interface_found is False:
            return

        fip_ids = vmi.floating_ips.copy()
        for fip_id in fip_ids:
            try:
                self._vnc_lib.floating_ip_delete(id=fip_id)
            except NoIdError:
                pass

    def _update_service_external_ip(self, service_namespace, service_name, external_ips):
        merge_patch = {'spec': {'externalIPs': [', '.join(external_ips)]}}
        self.kube.patch_resource(resource_type="services", resource_name=service_name,
                           namespace=service_namespace, merge_patch=merge_patch)
        self.logger.notice("Service (%s, %s) updated with EXTERNAL-IP (%s)"
                               % (service_namespace, service_name, external_ips));

    def _update_service_public_ip(self, service_id, service_name,
                        service_namespace, service_type, external_ips, loadBalancerIp, specified_fip_pool_fq_name_str):
        allocated_fips = self._read_allocated_floating_ips(service_id)

        if service_type in ["LoadBalancer"]:
            if allocated_fips is not None and len(allocated_fips) is 0:
                # Allocate floating-ip from public-pool, if none exists.
                # if "loadBalancerIp" if specified in Service definition, allocate
                #     loadBalancerIp as floating-ip.
                # if  external ips specified, allocate external_ips as floating-ips.
                # if None specficied, then let contrail allocate a floating-ip and
                #     update the allocated fip to kubernetes
                if loadBalancerIp:
                    allocated_fip = self._allocate_floating_ips(service_id,
                                                                specified_fip_pool_fq_name_str,
                                                                service_namespace,
                                                                set([loadBalancerIp]))
                    if allocated_fip:
                        self._update_service_external_ip(service_namespace,
                                                         service_name,
                                                         allocated_fip)
                elif external_ips:
                    allocated_fips = self._allocate_floating_ips(service_id,
                                                                 specified_fip_pool_fq_name_str,
                                                                 service_namespace,
                                                                 external_ips)
                else:
                    allocated_fip = self._allocate_floating_ips(service_id,
                                                                specified_fip_pool_fq_name_str,
                                                                service_namespace)
                    if allocated_fip:
                        self._update_service_external_ip(service_namespace,
                                                         service_name,
                                                         allocated_fip)

                return

            if allocated_fips is not None and len(allocated_fips):
                if loadBalancerIp and loadBalancerIp in allocated_fips:
                    self._deallocate_floating_ips(service_id)
                    self._allocate_floating_ips(service_id,
                                                specified_fip_pool_fq_name_str,
                                                service_namespace,
                                                set([loadBalancerIp]))
                    self._update_service_external_ip(service_namespace,
                                                     service_name,
                                                     loadBalancerIp)
                    return

                if external_ips and external_ips != allocated_fips:
                    # If Service's EXTERNAL-IP is not same as allocated floating-ip,
                    # update kube-api server with allocated fip as the EXTERNAL-IP
                    self._deallocate_floating_ips(service_id)
                    self._allocate_floating_ips(service_id,
                                                specified_fip_pool_fq_name_str,
                                                service_namespace,
                                                external_ips)
                    return

                if not external_ips :
                    self._update_service_external_ip(service_namespace, service_name, allocated_fips)
                    return
            return

        if service_type in ["ClusterIP"]:
            if allocated_fips:
                if not external_ips :
                    self._deallocate_floating_ips(service_id)
                else:
                    if allocated_fips != external_ips:
                        self._deallocate_floating_ips(service_id)
                        self._allocate_floating_ips(service_id,
                                                    specified_fip_pool_fq_name_str,
                                                    service_namespace,
                                                    external_ips)
            else:  #allocated_fip is None
                if external_ips:
                    self._allocate_floating_ips(service_id,
                                                specified_fip_pool_fq_name_str,
                                                service_namespace,
                                                external_ips)
            return

    def _check_service_uuid_change(self, svc_uuid, svc_name,
                                   svc_namespace, ports):
        proj_fq_name = vnc_kube_config.cluster_project_fq_name(svc_namespace)
        lb_fq_name = proj_fq_name + [svc_name]
        lb_uuid = LoadbalancerKM.get_fq_name_to_uuid(lb_fq_name)
        if lb_uuid is None:
            return

        if svc_uuid != lb_uuid:
            self.vnc_service_delete(lb_uuid, svc_name, svc_namespace, ports)
            self.logger.notice("Uuid change detected for service %s. "
                               "Deleteing old service" % lb_fq_name);

    def vnc_service_add(self, service_id, service_name,
                        service_namespace, service_ip, selectors, ports,
                        service_type, externalIps, loadBalancerIp,
                        specified_fip_pool_fq_name_str):
        ingress_update = False
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            ingress_update = True
            self._check_service_uuid_change(service_id, service_name,
                                            service_namespace, ports)

        self._lb_create(service_id, service_name, service_namespace,
                        service_ip, ports)

        # "kubernetes" service needs a link-local service to be created.
        # This link-local service will steer traffic destined for
        # "kubernetes" service from slave (compute) nodes to kube-api server
        # running on master (control) node.
        if service_name == self._kubernetes_service_name:
            self._create_link_local_service(service_name, service_namespace,
                service_ip, ports)

        self._update_service_public_ip(service_id, service_name,
                                       service_namespace, service_type,
                                       externalIps, loadBalancerIp,
                                       specified_fip_pool_fq_name_str)

        if ingress_update:
            self._ingress_mgr.update_ingress_backend(
                service_namespace, service_name, 'ADD')


    def _vnc_delete_pool(self, pool_id):
        self.service_lb_pool_mgr.delete(pool_id)

    def _vnc_delete_listener(self, ll_id):
        self.service_ll_mgr.delete(ll_id)

    def _vnc_delete_listeners(self, lb):
        listeners = lb.loadbalancer_listeners.copy()
        for ll_id in listeners or []:
            ll = LoadbalancerListenerKM.get(ll_id)
            if not ll:
                continue
            pool_id = ll.loadbalancer_pool
            if pool_id:
                pool = LoadbalancerPoolKM.get(pool_id)
                if pool:
                    members = pool.members.copy()
                    for member_id in members or []:
                        member = LoadbalancerMemberKM.get(member_id)
                        if member:
                            self.service_lb_member_mgr.delete(member_id)
                            self.logger.debug("Deleting LB member %s" % member.name)
                            LoadbalancerMemberKM.delete(member_id)

                self._vnc_delete_pool(pool_id)
                self.logger.debug("Deleting LB pool %s" % pool.name)
                LoadbalancerPoolKM.delete(pool_id)

            self.logger.debug("Deleting LB listener %s" % ll.name)
            self._vnc_delete_listener(ll_id)
            LoadbalancerListenerKM.delete(ll_id)

    def _vnc_delete_lb(self, lb_id):
        self.service_lb_mgr.delete(lb_id)

    def _lb_delete(self, service_id, service_name,
                   service_namespace):
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            self.logger.debug("LB doesnot exist for (%s,%s) in cfg db, return" %
                              (service_namespace, service_name))
            return

        self._vnc_delete_listeners(lb)
        self._vnc_delete_lb(service_id)
        LoadbalancerKM.delete(service_id)

    def vnc_service_delete(self, service_id, service_name,
                           service_namespace, ports):
        self._deallocate_floating_ips(service_id)
        self._lb_delete(service_id, service_name, service_namespace)

        # Delete link local service that would have been allocated for
        # kubernetes service.
        if service_name == self._kubernetes_service_name:
            self._delete_link_local_service(service_name, service_namespace,
                ports)
        self._ingress_mgr.update_ingress_backend(
            service_namespace, service_name, 'DELETE')

    def _create_service_event(self, event_type, service_id, lb):
        event = {}
        object = {}
        object['kind'] = 'Service'
        object['spec'] = {}
        object['metadata'] = {}
        object['metadata']['uid'] = service_id
        if event_type == 'delete':
            event['type'] = 'DELETED'
            event['object'] = object
            self._queue.put(event)
        return

    def _sync_service_lb(self):
        lb_uuid_set = set(LoadbalancerKM.keys())
        service_uuid_set = set(ServiceKM.keys())
        deleted_uuid_set = lb_uuid_set - service_uuid_set
        for uuid in deleted_uuid_set:
            lb = LoadbalancerKM.get(uuid)
            if not lb:
                continue
            if not lb.annotations:
                continue
            owner = None
            kind = None
            cluster = None
            for kvp in lb.annotations['key_value_pair'] or []:
                if kvp['key'] == 'cluster':
                    cluster = kvp['value']
                elif kvp['key'] == 'owner':
                    owner = kvp['value']
                elif kvp['key'] == 'kind':
                    kind = kvp['value']

                if cluster == vnc_kube_config.cluster_name() and \
                   owner == 'k8s' and \
                   kind == self._k8s_event_type:
                    self._create_service_event('delete', uuid, lb)
                    break
        return

    def service_timer(self):
        self._sync_service_lb()
        return

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        service_namespace = event['object']['metadata'].get('namespace')
        service_name = event['object']['metadata'].get('name')
        service_id = event['object']['metadata'].get('uid')
        service_ip = event['object']['spec'].get('clusterIP')
        selectors = event['object']['spec'].get('selector', None)
        ports = event['object']['spec'].get('ports')
        service_type  = event['object']['spec'].get('type')
        loadBalancerIp  = event['object']['spec'].get('loadBalancerIP', None)
        externalIps  = event['object']['spec'].get('externalIPs', [])
        annotations = event['object']['metadata'].get('annotations')
        specified_fip_pool_fq_name_str = None
        if annotations:
            if 'opencontrail.org/fip-pool' in annotations:
                specified_fip_pool_fq_name_str = annotations['opencontrail.org/fip-pool']

        print("%s - Got %s %s %s:%s:%s"
              %(self._name, event_type, kind,
              service_namespace, service_name, service_id))
        self.logger.debug("%s - Got %s %s %s:%s:%s"
              %(self._name, event_type, kind,
              service_namespace, service_name, service_id))

        # We dont need to do anything for Headless Service
        if (service_ip == 'None'):
            self.logger.warning("%s - Headless Service %s:%s:%s"
                %(self._name, service_namespace, service_name, service_id))
            return

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            # Add a service label for this service.
            labels = self._labels.get_service_label(service_name)
            self._labels.process(service_id, labels)

            self.vnc_service_add(service_id, service_name,
                service_namespace, service_ip, selectors, ports,
                    service_type, externalIps, loadBalancerIp,
                    specified_fip_pool_fq_name_str)
        elif event['type'] == 'DELETED':
            self.vnc_service_delete(service_id, service_name,
                                    service_namespace, ports)
            self._labels.process(service_id)
        else:
            self.logger.warning(
                'Unknown event type: "{}" Ignoring'.format(event['type']))

