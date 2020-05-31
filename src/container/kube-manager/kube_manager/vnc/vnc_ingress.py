#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC Ingress management for kubernetes.
"""
from __future__ import print_function

from builtins import str
import copy
from six import StringIO
import uuid

from cfgm_common.utils import cgitb_hook
from cfgm_common.exceptions import NoIdError
from vnc_api.gen.resource_xsd import KeyValuePair
from vnc_api.gen.resource_client import FloatingIp

from kube_manager.common.kube_config_db import IngressKM
from kube_manager.common.kube_config_db import NamespaceKM
from kube_manager.vnc.config_db import (
    LoadbalancerKM, LoadbalancerListenerKM, VirtualNetworkKM,
    LoadbalancerMemberKM, LoadbalancerPoolKM, VirtualMachineInterfaceKM,
    FloatingIpKM
)
from kube_manager.vnc.loadbalancer import ServiceLbManager
from kube_manager.vnc.loadbalancer import ServiceLbListenerManager
from kube_manager.vnc.loadbalancer import ServiceLbPoolManager
from kube_manager.vnc.loadbalancer import ServiceLbMemberManager
from kube_manager.vnc.vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from kube_manager.vnc.vnc_security_policy import VncSecurityPolicy
from kube_manager.vnc.vnc_common import VncCommon
from kube_manager.common.utils import get_fip_pool_fq_name_from_dict_string
from kube_manager.vnc.label_cache import XLabelCache


class VncIngress(VncCommon):
    def __init__(self, tag_mgr=None):
        self._k8s_event_type = 'Ingress'
        super(VncIngress, self).__init__(self._k8s_event_type)
        self._name = type(self).__name__
        self._args = vnc_kube_config.args()
        self._queue = vnc_kube_config.queue()
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self._logger = vnc_kube_config.logger()
        self._kube = vnc_kube_config.kube()
        self._label_cache = vnc_kube_config.label_cache()
        self._labels = XLabelCache(self._k8s_event_type)
        self.tag_mgr = tag_mgr
        self._ingress_label_cache = {}
        self._default_vn_obj = None
        self._fip_pool_obj = None
        self.service_lb_mgr = ServiceLbManager()
        self.service_ll_mgr = ServiceLbListenerManager()
        self.service_lb_pool_mgr = ServiceLbPoolManager()
        self.service_lb_member_mgr = ServiceLbMemberManager()

    def _get_project(self, ns_name):
        proj_fq_name = vnc_kube_config.cluster_project_fq_name(ns_name)
        try:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        except NoIdError:
            self._logger.error("%s - %s Not Found" % (self._name, proj_fq_name))
            return None
        return proj_obj

    def _get_namespace(self, ns_name):
        return NamespaceKM.find_by_name_or_uuid(ns_name)

    def _is_network_isolated(self, ns_name):
        return self._get_namespace(ns_name).is_isolated()

    def _get_ip_fabric_forwarding(self, ns_name):
        ns = self._get_namespace(ns_name)
        if ns:
            return ns.get_ip_fabric_forwarding()
        return None

    def _is_ip_fabric_forwarding_enabled(self, ns_name):
        ip_fabric_forwarding = self._get_ip_fabric_forwarding(ns_name)
        if ip_fabric_forwarding is not None:
            return ip_fabric_forwarding
        else:
            return self._args.ip_fabric_forwarding

    def _get_network(self, ns_name):
        set_default_vn = False
        ns = self._get_namespace(ns_name)
        vn_fq_name = ns.get_annotated_network_fq_name()

        if not vn_fq_name:
            if ns.is_isolated():
                vn_fq_name = ns.get_isolated_pod_network_fq_name()

        if not vn_fq_name:
            if self._default_vn_obj:
                return self._default_vn_obj
            set_default_vn = True
            vn_fq_name = vnc_kube_config.cluster_default_pod_network_fq_name()

        try:
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        except NoIdError:
            self._logger.error("%s - %s Not Found" % (self._name, vn_fq_name))
            return None

        if set_default_vn:
            self._default_vn_obj = vn_obj

        return vn_obj

    def _get_pod_ipam_subnet_uuid(self, ns_name, vn_obj):
        pod_ipam_subnet_uuid = None
        if self._is_network_isolated(ns_name):
            vn_namespace = ns_name
        else:
            vn_namespace = 'default'
        if self._is_ip_fabric_forwarding_enabled(vn_namespace):
            ipam_fq_name = vnc_kube_config.ip_fabric_ipam_fq_name()
        else:
            ipam_fq_name = vnc_kube_config.pod_ipam_fq_name()
        vn = VirtualNetworkKM.find_by_name_or_uuid(vn_obj.get_uuid())
        pod_ipam_subnet_uuid = vn.get_ipam_subnet_uuid(ipam_fq_name)
        if pod_ipam_subnet_uuid is None:
            self._logger.error("%s - %s Not Found" % (self._name, ipam_fq_name))
        return pod_ipam_subnet_uuid

    def _get_specified_fip_pool(self, specified_fip_pool_fq_name_str):
        if specified_fip_pool_fq_name_str is None:
            return None
        fip_pool_fq_name = get_fip_pool_fq_name_from_dict_string(specified_fip_pool_fq_name_str)
        try:
            fip_pool_obj = self._vnc_lib.floating_ip_pool_read(fq_name=fip_pool_fq_name)
        except NoIdError:
            return None
        return fip_pool_obj

    def _get_fip_pool_obj(self, fip_pool_fq_name):
        try:
            fip_pool_obj = self._vnc_lib.floating_ip_pool_read(fq_name=fip_pool_fq_name)
        except NoIdError:
            self._logger.error(
                "%s - %s Not Found" % (self._name, fip_pool_fq_name))
            return None
        self._fip_pool_obj = fip_pool_obj
        return fip_pool_obj

    def _get_floating_ip(
            self, name, ns_name,
            proj_obj, external_ip=None, vmi_obj=None, specified_fip_pool_fq_name_str=None):
        fip_pool_fq_name = None
        if specified_fip_pool_fq_name_str is not None:
            fip_pool_fq_name = get_fip_pool_fq_name_from_dict_string(specified_fip_pool_fq_name_str)
        if fip_pool_fq_name is None:
            ns = self._get_namespace(ns_name)
            fip_pool_fq_name = ns.get_annotated_ns_fip_pool_fq_name()
        if fip_pool_fq_name is None:
            if not vnc_kube_config.is_public_fip_pool_configured():
                return None
            try:
                fip_pool_fq_name = get_fip_pool_fq_name_from_dict_string(
                    self._args.public_fip_pool)
            except Exception:
                string_buf = StringIO()
                cgitb_hook(file=string_buf, format="text")
                err_msg = string_buf.getvalue()
                self._logger.error("%s - %s" % (self._name, err_msg))
                return None

        if vmi_obj:
            fip_refs = vmi_obj.get_floating_ip_back_refs()
            for ref in fip_refs or []:
                fip = FloatingIpKM.get(ref['uuid'])
                if fip and fip.fq_name[:-1] == fip_pool_fq_name:
                    return fip
                else:
                    break
        fip_pool = self._get_fip_pool_obj(fip_pool_fq_name)
        if fip_pool is None:
            return None
        fip_uuid = str(uuid.uuid4())
        fip_name = VncCommon.make_name(name, fip_uuid)
        fip_obj = FloatingIp(fip_name, fip_pool)
        fip_obj.uuid = fip_uuid
        fip_obj.set_project(proj_obj)
        if vmi_obj:
            fip_obj.set_virtual_machine_interface(vmi_obj)
        if external_ip:
            fip_obj.floating_ip_address = external_ip
        try:
            self._vnc_lib.floating_ip_create(fip_obj)
            fip = FloatingIpKM.locate(fip_obj.uuid)
        except Exception:
            string_buf = StringIO()
            cgitb_hook(file=string_buf, format="text")
            err_msg = string_buf.getvalue()
            self._logger.error("%s - %s" % (self._name, err_msg))
            return None
        return fip

    def _allocate_floating_ip(self, lb_obj, name, ns_name, proj_obj, external_ip, specified_fip_pool_fq_name_str=None):
        vmi_id = lb_obj.virtual_machine_interface_refs[0]['uuid']
        vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
        if vmi_obj is None:
            self._logger.error(
                "%s - %s Vmi %s Not Found" % (self._name, lb_obj.name, vmi_id))
            return None
        fip = self._get_floating_ip(name, ns_name, proj_obj, external_ip, vmi_obj, specified_fip_pool_fq_name_str)
        return fip

    def _deallocate_floating_ip(self, lb):
        vmi_id = list(lb.virtual_machine_interfaces)[0]
        vmi = VirtualMachineInterfaceKM.get(vmi_id)
        if vmi is None:
            self._logger.error(
                "%s - %s Vmi %s Not Found" % (self._name, lb.name, vmi_id))
            return
        fip_list = vmi.floating_ips.copy()
        for fip_id in fip_list or []:
            fip_obj = self._vnc_lib.floating_ip_read(id=fip_id)
            fip_obj.set_virtual_machine_interface_list([])
            self._vnc_lib.floating_ip_update(fip_obj)
            self._vnc_lib.floating_ip_delete(id=fip_obj.uuid)
            FloatingIpKM.delete(fip_obj.uuid)

    def _update_floating_ip(self, name, ns_name, external_ip, lb_obj, specified_fip_pool_fq_name_str=None):
        proj_obj = self._get_project(ns_name)
        fip = self._allocate_floating_ip(
            lb_obj, name, ns_name, proj_obj, external_ip, specified_fip_pool_fq_name_str)
        if fip:
            lb_obj.add_annotations(
                KeyValuePair(key='externalIP', value=external_ip))
            self._vnc_lib.loadbalancer_update(lb_obj)
        return fip

    def _update_kube_api_server(self, name, ns_name, lb_obj, fip):
        vip_dict_list = []
        if fip:
            vip_dict = {}
            vip_dict['ip'] = fip.address
            vip_dict_list.append(vip_dict)
        vip_dict = {}
        vip_dict['ip'] = lb_obj._loadbalancer_properties.vip_address
        vip_dict_list.append(vip_dict)
        patch = {'status': {'loadBalancer': {'ingress': vip_dict_list}}}
        self._kube.patch_resource(
            "ingress", name, patch,
            ns_name, sub_resource_name='status')

    def _find_ingress(self, ingress_cache, ns_name, service_name):
        if not ns_name or not service_name:
            return
        key = 'service'
        value = '-'.join([ns_name, service_name])
        labels = {key: value}
        result = set()
        for label in list(labels.items()):
            key = self._label_cache._get_key(label)
            ingress_ids = ingress_cache.get(key, set())
            # no matching label
            if not ingress_ids:
                return ingress_ids
            if not result:
                result = ingress_ids.copy()
            else:
                result.intersection_update(ingress_ids)
        return result

    def _clear_ingress_cache_uuid(self, ingress_cache, ingress_uuid):
        if not ingress_uuid:
            return
        key_list = [k for k, v in list(ingress_cache.items()) if ingress_uuid in v]
        for key in key_list or []:
            label = tuple(key.split(':'))
            self._label_cache._remove_label(key, ingress_cache, label, ingress_uuid)

    def _clear_ingress_cache(
            self, ingress_cache,
            ns_name, service_name, ingress_uuid):
        if not ns_name or not service_name:
            return
        key = 'service'
        value = '-'.join([ns_name, service_name])
        labels = {key: value}
        for label in list(labels.items()) or []:
            key = self._label_cache._get_key(label)
            self._label_cache._remove_label(
                key, ingress_cache, label, ingress_uuid)

    def _update_ingress_cache(
            self, ingress_cache,
            ns_name, service_name, ingress_uuid):
        if not ns_name or not service_name:
            return
        key = 'service'
        value = '-'.join([ns_name, service_name])
        labels = {key: value}
        for label in list(labels.items()) or []:
            key = self._label_cache._get_key(label)
            self._label_cache._locate_label(
                key, ingress_cache, label, ingress_uuid)

    def _vnc_create_member(self, pool, address, port, annotations):
        pool_obj = self.service_lb_pool_mgr.read(pool.uuid)
        member_obj = self.service_lb_member_mgr.create(
            pool_obj, address, port, annotations)
        return member_obj

    def _vnc_update_member(self, member_id, address, port, annotations):
        member_obj = self.service_lb_member_mgr.update(
            member_id, address, port, annotations)
        return member_obj

    def _vnc_create_pool(self, ns_name, ll, port, lb_algorithm, annotations):
        proj_obj = self._get_project(ns_name)
        ll_obj = self.service_ll_mgr.read(ll.uuid)
        pool_obj = self.service_lb_pool_mgr.create(
            ll_obj, proj_obj, port, lb_algorithm, annotations)
        return pool_obj

    def _vnc_create_listeners(self, ns_name, lb, port):
        proj_obj = self._get_project(ns_name)
        lb_obj = self.service_lb_mgr.read(lb.uuid)
        ll_obj = self.service_ll_mgr.create(lb_obj, proj_obj, port)
        return ll_obj

    def _vnc_create_lb(self, uid, name, ns_name, annotations):
        proj_obj = self._get_project(ns_name)
        vn_obj = self._get_network(ns_name)
        if proj_obj is None or vn_obj is None:
            return None

        vip_address = None
        pod_ipam_subnet_uuid = self._get_pod_ipam_subnet_uuid(ns_name, vn_obj)
        lb_obj = self.service_lb_mgr.create(
            self._k8s_event_type, ns_name, uid,
            name, proj_obj, vn_obj, vip_address, pod_ipam_subnet_uuid,
            tags=self._labels.get_labels_dict(uid))
        if lb_obj:
            external_ip = None
            if annotations and 'externalIP' in annotations:
                external_ip = annotations['externalIP']
            specified_fip_pool_fq_name_str = None
            if annotations and 'opencontrail.org/fip-pool' in annotations:
                specified_fip_pool_fq_name_str = annotations['opencontrail.org/fip-pool']
            fip = self._update_floating_ip(
                name, ns_name, external_ip, lb_obj, specified_fip_pool_fq_name_str)
            self._update_kube_api_server(name, ns_name, lb_obj, fip)
        else:
            self._logger.error("%s - %s LB Not Created" % (self._name, name))

        return lb_obj

    def _vnc_delete_member(self, member_id):
        self.service_lb_member_mgr.delete(member_id)

    def _vnc_delete_pool(self, pool_id):
        self.service_lb_pool_mgr.delete(pool_id)

    def _vnc_delete_listener(self, ll_id):
        self.service_ll_mgr.delete(ll_id)

    def _vnc_delete_lb(self, lb):
        self._deallocate_floating_ip(lb)
        self.service_lb_mgr.delete(lb.uuid)

    def _get_old_backend_list(self, lb):
        backend_list = []
        listener_list = lb.loadbalancer_listeners
        for ll_id in listener_list:
            backend = {}
            backend['listener_id'] = ll_id
            ll = LoadbalancerListenerKM.get(ll_id)
            backend['listener'] = {}
            backend['listener']['protocol'] = ll.params['protocol']
            if backend['listener']['protocol'] == 'TERMINTED_HTTPS':
                if ll.params['default_tls_container']:
                    backend['listener']['default_tls_container'] = \
                        ll.params['default_tls_container']
                if ll.params['sni_containers']:
                    backend['listener']['sni_containers'] = \
                        ll.params['sni_containers']
            pool_id = ll.loadbalancer_pool
            if pool_id:
                pool = LoadbalancerPoolKM.get(pool_id)
                if pool.annotations is None:
                    annotations = {}
                    kvps = []
                    pool_obj = self._vnc_lib.loadbalancer_pool_read(id=pool_id)
                    pool_obj_kvp = pool_obj.annotations.key_value_pair
                    kvps_len = len(pool_obj_kvp)
                    for count in range(0, kvps_len):
                        kvp = {}
                        kvp['key'] = pool_obj_kvp[count].key
                        kvp['value'] = pool_obj_kvp[count].value
                        kvps.append(kvp)
                    annotations['key_value_pair'] = kvps
                else:
                    annotations = pool.annotations
                backend['pool_id'] = pool_id
                backend['annotations'] = {}
                for kvp in annotations['key_value_pair'] or []:
                    key = kvp['key']
                    value = kvp['value']
                    backend['annotations'][key] = value
                backend['pool'] = {}
                backend['pool']['protocol'] = pool.params['protocol']
                backend['member'] = {}
                if len(pool.members) == 0:
                    continue
                member_id = list(pool.members)[0]
                member = LoadbalancerMemberKM.get(member_id)
                if member.annotations is None:
                    annotations = {}
                    kvps = []
                    member_obj = self._vnc_lib.loadbalancer_member_read(id=member_id)
                    member_obj_kvp = member_obj.annotations.key_value_pair
                    kvps_len = len(member_obj_kvp)
                    for count in range(0, kvps_len):
                        kvp = {}
                        kvp['key'] = member_obj_kvp[count].key
                        kvp['value'] = member_obj_kvp[count].value
                        kvps.append(kvp)
                    annotations['key_value_pair'] = kvps
                else:
                    annotations = member.annotations
                backend['member_id'] = member_id
                protocol_port = member.params['protocol_port']
                for kvp in annotations['key_value_pair'] or []:
                    if kvp['key'] == 'serviceName':
                        backend['member']['serviceName'] = kvp['value']
                        backend['member']['servicePort'] = protocol_port
                        break
            backend_list.append(backend)
        return backend_list

    def _get_tls_dict(self, spec, ns_name):
        tls_dict = {}
        if 'tls' in spec:
            tls_list = spec['tls']
            for tls in tls_list:
                if 'secretName' not in tls:
                    continue
                if 'hosts' in tls:
                    hosts = tls['hosts']
                else:
                    hosts = ['ALL']
                for host in hosts:
                    tls_dict[host] = ns_name + '__' + tls['secretName']
        return tls_dict

    def _get_new_backend_list(self, spec, ns_name):
        tls_dict = self._get_tls_dict(spec, ns_name)
        backend_list = []
        rules = []
        if 'rules' in spec:
            rules = spec['rules']
            for rule in rules:
                if 'http' not in rule:
                    continue
                paths = rule['http']['paths']
                for path in paths or []:
                    backend = {}
                    backend['annotations'] = {}
                    backend['listener'] = {}
                    backend['pool'] = {}
                    backend['member'] = {}
                    backend['listener']['protocol'] = 'HTTP'
                    backend['pool']['protocol'] = 'HTTP'
                    secretname = ""
                    virtual_host = False
                    if 'host' in rule:
                        host = rule['host']
                        backend['annotations']['host'] = host
                        if host in list(tls_dict.keys()):
                            secretname = tls_dict[host]
                            virtual_host = True
                    if 'path' in path:
                        backend['annotations']['path'] = path['path']
                        if not virtual_host and 'ALL' in list(tls_dict.keys()):
                            secretname = 'ALL'
                    service = path['backend']
                    backend['annotations']['type'] = 'acl'
                    backend['member']['serviceName'] = service['serviceName']
                    backend['member']['servicePort'] = service['servicePort']
                    backend_list.append(backend)
                    if secretname:
                        backend_https = copy.deepcopy(backend)
                        backend_https['listener']['protocol'] = 'TERMINATED_HTTPS'
                        if virtual_host:
                            backend_https['listener']['sni_containers'] = [secretname]
                        else:
                            backend_https['listener']['default_tls_container'] = tls_dict['ALL']
                        backend_list.append(backend_https)
        if 'backend' in spec:
            service = spec['backend']
            backend = {}
            backend['annotations'] = {}
            backend['listener'] = {}
            backend['pool'] = {}
            backend['member'] = {}
            backend['listener']['protocol'] = 'HTTP'
            backend['pool']['protocol'] = 'HTTP'
            backend['annotations']['type'] = 'default'
            backend['member']['serviceName'] = service['serviceName']
            backend['member']['servicePort'] = service['servicePort']
            backend_list.append(backend)
            if 'ALL' in list(tls_dict.keys()):
                backend_https = copy.deepcopy(backend)
                backend_https['listener']['protocol'] = 'TERMINATED_HTTPS'
                backend_https['listener']['default_tls_container'] = tls_dict['ALL']
                backend_list.append(backend_https)
        return backend_list

    def _create_member(self, ns_name, backend_member, pool):
        resource_type = "service"
        service_name = backend_member['serviceName']
        service_port = backend_member['servicePort']
        service_info = self._kube.get_resource(
            resource_type, service_name, ns_name)
        member = None
        if service_info and 'clusterIP' in service_info['spec']:
            service_ip = service_info['spec']['clusterIP']
            self._logger.debug(
                "%s - clusterIP for service %s - %s"
                % (self._name, service_name, service_ip))
            member_match = False
            annotations = {}
            annotations['serviceName'] = service_name
            for member_id in pool.members:
                member = LoadbalancerMemberKM.get(member_id)
                if member and member.params['address'] == service_ip \
                   and member.params['protocol_port'] == service_port:
                    member_match = True
                    break
            if not member_match:
                member_obj = self._vnc_create_member(
                    pool, service_ip, service_port, annotations)
                if member_obj:
                    member = LoadbalancerMemberKM.locate(member_obj.uuid)
                else:
                    self._logger.error(
                        "%s - (%s %s) Member Not Created for Pool %s"
                        % (self._name, service_name, str(service_port), pool.name))
        else:
            self._logger.error(
                "%s - clusterIP for Service %s Not Found"
                % (self._name, service_name))
            self._logger.error(
                "%s - (%s %s) Member Not Created for Pool %s"
                % (self._name, service_name, str(service_port), pool.name))
        return member

    def _update_member(self, ns_name, backend_member, pool):
        resource_type = "service"
        member_id = backend_member['member_id']
        new_service_name = backend_member['serviceName']
        new_service_port = backend_member['servicePort']
        member = LoadbalancerMemberKM.get(member_id)
        annotations = member.annotations
        for kvp in annotations['key_value_pair'] or []:
            if kvp['key'] == 'serviceName':
                old_service_name = kvp['value']
                break
        old_service_port = member.params['protocol_port']
        service_ip = None
        if new_service_name != old_service_name:
            service_info = self._kube.get_resource(
                resource_type, new_service_name, ns_name)
            if service_info and 'clusterIP' in service_info['spec']:
                service_ip = service_info['spec']['clusterIP']
            else:
                self._logger.error(
                    "%s - clusterIP for Service %s Not Found"
                    % (self._name, new_service_name))
                self._logger.error(
                    "%s - (%s %s) Member Not Updated for Pool %s"
                    % (self._name, new_service_name, str(new_service_port), pool.name))
                self._vnc_delete_member(member_id)
                LoadbalancerMemberKM.delete(member_id)
                self._logger.error(
                    "%s - (%s %s) Member Deleted for Pool %s"
                    % (self._name, old_service_name, str(old_service_port), pool.name))
                return None
        else:
            service_ip = member.params['address']
        annotations = {}
        annotations['serviceName'] = new_service_name
        self._vnc_update_member(
            member_id, service_ip, new_service_port, annotations)
        member = LoadbalancerMemberKM.update(member)
        return member

    def _create_pool(self, ns_name, ll, port, lb_algorithm, annotations):
        pool_id = ll.loadbalancer_pool
        pool = LoadbalancerPoolKM.get(pool_id)
        if pool is None:
            pool_obj = self._vnc_create_pool(
                ns_name, ll, port, lb_algorithm, annotations)
            pool_id = pool_obj.uuid
            pool = LoadbalancerPoolKM.locate(pool_id)
        else:
            self._logger.error(
                "%s - %s Pool Not Created" % (self._name, ll.name))
        return pool

    def _create_listener(self, ns_name, lb, port):
        ll_obj = self._vnc_create_listeners(ns_name, lb, port)
        if ll_obj:
            ll = LoadbalancerListenerKM.locate(ll_obj.uuid)
        else:
            self._logger.error(
                "%s - %s Listener for Port %s Not Created"
                % (self._name, lb.name, str(port)))
        return ll

    def _create_listener_pool_member(self, ns_name, lb, backend):
        pool_port = {}
        listener_port = {}
        listener_port['port'] = '80'
        listener_port['protocol'] = backend['listener']['protocol']
        if listener_port['protocol'] == 'TERMINATED_HTTPS':
            listener_port['port'] = '443'
            if 'default_tls_container' in backend['listener']:
                listener_port['default_tls_container'] = backend['listener']['default_tls_container']
            if 'sni_containers' in backend['listener']:
                listener_port['sni_containers'] = backend['listener']['sni_containers']
        ll = self._create_listener(ns_name, lb, listener_port)
        annotations = {}
        for key in backend['annotations']:
            annotations[key] = backend['annotations'][key]
        lb_algorithm = "ROUND_ROBIN"
        pool_port['port'] = '80'
        pool_port['protocol'] = backend['pool']['protocol']
        pool = self._create_pool(ns_name, ll, pool_port, lb_algorithm, annotations)
        backend_member = backend['member']
        member = self._create_member(ns_name, backend_member, pool)
        if member is None:
            self._logger.error(
                "%s - Deleting Listener %s and Pool %s"
                % (self._name, ll.name, pool.name))
            self._vnc_delete_pool(pool.uuid)
            LoadbalancerPoolKM.delete(pool.uuid)
            self._vnc_delete_listener(ll.uuid)
            LoadbalancerListenerKM.delete(ll.uuid)

    def update_ingress_backend(self, ns_name, service_name, oper):
        ingress_ids = self._find_ingress(
            self._ingress_label_cache, ns_name, service_name)
        for ingress_id in ingress_ids or []:
            ingress = IngressKM.get(ingress_id)
            lb = LoadbalancerKM.get(ingress_id)
            if not ingress or not lb:
                continue
            if oper == 'ADD':
                new_backend_list = self._get_new_backend_list(ingress.spec, ns_name)
                for new_backend in new_backend_list[:] or []:
                    if new_backend['member']['serviceName'] == service_name:

                        # Create a firewall rule for ingress to this service.
                        fw_uuid = VncIngress.add_ingress_to_service_rule(
                            ns_name, ingress.name, service_name)
                        lb.add_firewall_rule(fw_uuid)

                        self._create_listener_pool_member(
                            ns_name, lb, new_backend)
            else:
                old_backend_list = self._get_old_backend_list(lb)
                for old_backend in old_backend_list[:] or []:
                    if old_backend['member']['serviceName'] == service_name:
                        self._delete_listener(old_backend['listener_id'])

                        # Delete rules created for this ingress to service.
                        deleted_fw_rule_uuid =\
                            VncIngress.delete_ingress_to_service_rule(
                                ns_name, ingress.name, service_name)
                        lb.remove_firewall_rule(deleted_fw_rule_uuid)

    def _create_lb(self, uid, name, ns_name, event):
        annotations = event['object']['metadata'].get('annotations')
        ingress_controller = 'opencontrail'
        if annotations:
            if 'kubernetes.io/ingress.class' in annotations:
                ingress_controller = annotations['kubernetes.io/ingress.class']
        if ingress_controller != 'opencontrail':
            self._logger.warning(
                "%s - ingress controller is not opencontrail for ingress %s"
                % (self._name, name))
            self._delete_ingress(uid)
            return
        lb = LoadbalancerKM.get(uid)
        if not lb:
            lb_obj = self._vnc_create_lb(uid, name, ns_name, annotations)
            if lb_obj is None:
                return
            lb = LoadbalancerKM.locate(uid)
        else:
            external_ip = None
            if annotations and 'externalIP' in annotations:
                external_ip = annotations['externalIP']
            specified_fip_pool_fq_name_str = None
            if annotations and 'opencontrail.org/fip-pool' in annotations:
                specified_fip_pool_fq_name_str = annotations['opencontrail.org/fip-pool']
            if external_ip != lb.external_ip:
                self._deallocate_floating_ip(lb)
                lb_obj = self._vnc_lib.loadbalancer_read(id=lb.uuid)
                fip = self._update_floating_ip(
                    name, ns_name, external_ip, lb_obj, specified_fip_pool_fq_name_str)
                if fip:
                    lb.external_ip = external_ip
                self._update_kube_api_server(name, ns_name, lb_obj, fip)

        self._clear_ingress_cache_uuid(self._ingress_label_cache, uid)

        spec = event['object']['spec']
        new_backend_list = self._get_new_backend_list(spec, ns_name)
        old_backend_list = self._get_old_backend_list(lb)

        # find the unchanged backends
        for new_backend in new_backend_list[:] or []:
            self._update_ingress_cache(
                self._ingress_label_cache,
                ns_name, new_backend['member']['serviceName'], uid)
            for old_backend in old_backend_list[:] or []:
                if (new_backend['annotations'] == old_backend['annotations'] and
                        new_backend['listener'] == old_backend['listener'] and
                        new_backend['pool'] == old_backend['pool'] and
                        new_backend['member'] == old_backend['member']):
                    # Create a firewall rule for this member.
                    fw_uuid = VncIngress.add_ingress_to_service_rule(
                        ns_name, name, new_backend['member']['serviceName'])
                    lb.add_firewall_rule(fw_uuid)

                    old_backend_list.remove(old_backend)
                    new_backend_list.remove(new_backend)
                    break
        if len(old_backend_list) == 0 and len(new_backend_list) == 0:
            return lb

        # find the updated backends and update
        backend_update_list = []
        for new_backend in new_backend_list[:] or []:
            for old_backend in old_backend_list[:] or []:
                if (new_backend['annotations'] == old_backend['annotations'] and
                        new_backend['listener'] == old_backend['listener'] and
                        new_backend['pool'] == old_backend['pool']):
                    backend = old_backend
                    backend['member']['member_id'] = old_backend['member_id']
                    backend['member']['serviceName'] = new_backend['member']['serviceName']
                    backend['member']['servicePort'] = new_backend['member']['servicePort']
                    backend_update_list.append(backend)
                    old_backend_list.remove(old_backend)
                    new_backend_list.remove(new_backend)
        for backend in backend_update_list or []:
            ll = LoadbalancerListenerKM.get(backend['listener_id'])
            pool = LoadbalancerPoolKM.get(backend['pool_id'])
            backend_member = backend['member']
            member = self._update_member(ns_name, backend_member, pool)
            if member is None:
                self._logger.error(
                    "%s - Deleting Listener %s and Pool %s"
                    % (self._name, ll.name, pool.name))
                self._vnc_delete_pool(pool.uuid)
                LoadbalancerPoolKM.delete(pool.uuid)
                self._vnc_delete_listener(ll.uuid)
                LoadbalancerListenerKM.delete(ll.uuid)
        if len(old_backend_list) == 0 and len(new_backend_list) == 0:
            return lb

        # delete the old backends
        for backend in old_backend_list or []:
            self._delete_listener(backend['listener_id'])

            deleted_fw_rule_uuid =\
                VncIngress.delete_ingress_to_service_rule(
                    ns_name, name, backend['member']['serviceName'])
            lb.remove_firewall_rule(deleted_fw_rule_uuid)

        # create the new backends
        for backend in new_backend_list:

            # Create a firewall rule for this member.
            fw_uuid = VncIngress.add_ingress_to_service_rule(
                ns_name, name, backend['member']['serviceName'])
            lb.add_firewall_rule(fw_uuid)

            self._create_listener_pool_member(ns_name, lb, backend)

        return lb

    def _delete_all_listeners(self, lb):
        listener_list = lb.loadbalancer_listeners.copy()
        for ll_id in listener_list:
            ll = LoadbalancerListenerKM.get(ll_id)
            pool_id = ll.loadbalancer_pool
            if pool_id:
                pool = LoadbalancerPoolKM.get(pool_id)
                member_list = pool.members.copy()
                for member_id in member_list:
                    self._vnc_delete_member(member_id)
                    LoadbalancerMemberKM.delete(member_id)
                self._vnc_delete_pool(pool_id)
                LoadbalancerPoolKM.delete(pool_id)
            self._vnc_delete_listener(ll_id)
            LoadbalancerListenerKM.delete(ll_id)

    def _delete_listener(self, ll_id):
        ll = LoadbalancerListenerKM.get(ll_id)
        pool_id = ll.loadbalancer_pool
        if pool_id:
            pool = LoadbalancerPoolKM.get(pool_id)
            member_list = pool.members.copy()
            for member_id in member_list:
                self._vnc_delete_member(member_id)
                LoadbalancerMemberKM.delete(member_id)
            self._vnc_delete_pool(pool_id)
            LoadbalancerPoolKM.delete(pool_id)
        self._vnc_delete_listener(ll_id)
        LoadbalancerListenerKM.delete(ll_id)

    def _delete_lb(self, uid):
        lb = LoadbalancerKM.get(uid)
        if not lb:
            return
        # Delete rules created for this member.
        firewall_rules = set(lb.get_firewall_rules())
        for fw_rule_uuid in firewall_rules:
            VncIngress.delete_ingress_to_service_rule_by_id(fw_rule_uuid)
            lb.remove_firewall_rule(fw_rule_uuid)

        self._delete_all_listeners(lb)
        self._vnc_delete_lb(lb)
        LoadbalancerKM.delete(uid)

    def _update_ingress(self, name, uid, event):
        ns_name = event['object']['metadata'].get('namespace')
        self._create_lb(uid, name, ns_name, event)

    def _delete_ingress(self, uid):
        self._delete_lb(uid)
        self._clear_ingress_cache_uuid(self._ingress_label_cache, uid)

    def _create_ingress_event(self, event_type, ingress_id, lb):
        event = {}
        object_ = {}
        object_['kind'] = 'Ingress'
        object_['spec'] = {}
        object_['metadata'] = {}
        object_['metadata']['uid'] = ingress_id
        if event_type == 'delete':
            event['type'] = 'DELETED'
            event['object'] = object_
            self._queue.put(event)
        return

    def _sync_ingress_lb(self):
        lb_uuid_set = set(LoadbalancerKM.keys())
        ingress_uuid_set = set(IngressKM.keys())
        deleted_ingress_set = lb_uuid_set - ingress_uuid_set
        for _id in deleted_ingress_set:
            lb = LoadbalancerKM.get(_id)
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
                    self._create_ingress_event('delete', _id, lb)
                    break
        return

    def ingress_timer(self):
        self._sync_ingress_lb()

    @classmethod
    def get_ingress_label_name(cls, ns_name, name):
        return "-".join([vnc_kube_config.cluster_name(), ns_name, name])

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        ns_name = event['object']['metadata'].get('namespace')
        name = event['object']['metadata'].get('name')
        uid = event['object']['metadata'].get('uid')

        print(
            "%s - Got %s %s %s:%s:%s"
            % (self._name, event_type, kind, ns_name, name, uid))
        self._logger.debug(
            "%s - Got %s %s %s:%s:%s"
            % (self._name, event_type, kind, ns_name, name, uid))

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':

            #
            # Construct and add labels for this ingress.
            # Following labels are added by infra:
            #
            # 1. A label for the ingress object.
            # 2. A label for the namespace of ingress object.
            #
            labels = self._labels.get_ingress_label(
                self.get_ingress_label_name(ns_name, name))
            labels.update(self._labels.get_namespace_label(ns_name))
            self._labels.process(uid, labels)

            self._update_ingress(name, uid, event)

        elif event['type'] == 'DELETED':
            # Dis-associate infra labels from refernced VMI's.
            self.remove_ingress_labels(ns_name, name)

            self._delete_ingress(uid)

            # Delete labels added by infra for this ingress.
            self._labels.process(uid)
        else:
            self._logger.warning(
                'Unknown event type: "{}" Ignoring'.format(event['type']))

    def remove_ingress_labels(self, ns_name, name):
        """
        Remove ingress infra label/tag from VMI's corresponding to the services of
        this ingress.

        For each ingress service, kube-manager will create a infra label to add
        rules that allow traffic from ingress VMI to backend service VMI's.

        Ingress is a special case where tags created by kube-manager are attached
        to VMI's that are not created/managed by kube-manager. Since the ingress
        label/tag is being deleted, dis-associate this tag from all VMI's on which
        it is referred.
        """
        if not self.tag_mgr or not ns_name or not name:
            return

        # Get labels for this ingress service.
        labels = self._labels.get_ingress_label(
            self.get_ingress_label_name(ns_name, name))
        for type_, value in labels.items():
            tag_obj = self.tag_mgr.read(type_, value)
            if tag_obj:
                vmi_refs = tag_obj.get_virtual_machine_interface_back_refs()
                for vmi in vmi_refs if vmi_refs else []:
                    vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi['uuid'])
                    self._vnc_lib.unset_tag(vmi_obj, type_)

    def create_ingress_security_policy(self):
        """
        Create a FW policy to house all ingress-to-service rules.
        """
        if not VncSecurityPolicy.ingress_svc_fw_policy_uuid:
            ingress_svc_fw_policy_uuid =\
                VncSecurityPolicy.create_firewall_policy(
                    self._k8s_event_type,
                    None, None, is_global=True)
            VncSecurityPolicy.add_firewall_policy(ingress_svc_fw_policy_uuid)
            VncSecurityPolicy.ingress_svc_fw_policy_uuid =\
                ingress_svc_fw_policy_uuid

    @classmethod
    def _get_ingress_firewall_rule_name(cls, ns_name, ingress_name, svc_name):
        return "-".join([vnc_kube_config.cluster_name(),
                         "Ingress",
                         ns_name,
                         ingress_name,
                         svc_name])

    @classmethod
    def add_ingress_to_service_rule(cls, ns_name, ingress_name, service_name):
        """
        Add a ingress-to-service allow rule to ingress firewall policy.
        """
        if VncSecurityPolicy.ingress_svc_fw_policy_uuid:

            ingress_labels = XLabelCache.get_ingress_label(
                cls.get_ingress_label_name(ns_name, ingress_name))
            service_labels = XLabelCache.get_service_label(service_name)

            rule_name = VncIngress._get_ingress_firewall_rule_name(
                ns_name, ingress_name, service_name)

            fw_rule_uuid = VncSecurityPolicy.create_firewall_rule_allow_all(
                rule_name, service_labels, ingress_labels)

            VncSecurityPolicy.add_firewall_rule(
                VncSecurityPolicy.ingress_svc_fw_policy_uuid, fw_rule_uuid)

            return fw_rule_uuid

    @classmethod
    def delete_ingress_to_service_rule(cls, ns_name, ingress_name,
                                       service_name):
        """
        Delete the ingress-to-service allow rule added to ingress firewall
        policy.
        """
        rule_uuid = None
        if VncSecurityPolicy.ingress_svc_fw_policy_uuid:
            rule_name = VncIngress._get_ingress_firewall_rule_name(
                ns_name, ingress_name, service_name)

            # Get the rule id of the rule to be deleted.
            rule_uuid = VncSecurityPolicy.get_firewall_rule_uuid(rule_name)
            if rule_uuid:
                # Delete the rule.
                VncSecurityPolicy.delete_firewall_rule(
                    VncSecurityPolicy.ingress_svc_fw_policy_uuid, rule_uuid)

        return rule_uuid

    @classmethod
    def delete_ingress_to_service_rule_by_id(cls, rule_uuid):
        if VncSecurityPolicy.ingress_svc_fw_policy_uuid:
            # Delete the rule.
            VncSecurityPolicy.delete_firewall_rule(
                VncSecurityPolicy.ingress_svc_fw_policy_uuid, rule_uuid)
