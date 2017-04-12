#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC Ingress management for kubernetes
"""

import uuid

from config_db import *
from vnc_api.vnc_api import *

from kube_manager.common.kube_config_db import IngressKM
from kube_manager.common.kube_config_db import NamespaceKM
from kube_manager.vnc.loadbalancer import ServiceLbManager
from kube_manager.vnc.loadbalancer import ServiceLbListenerManager
from kube_manager.vnc.loadbalancer import ServiceLbPoolManager
from kube_manager.vnc.loadbalancer import ServiceLbMemberManager
from vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from vnc_common import VncCommon

from cStringIO import StringIO
from cfgm_common.utils import cgitb_hook

class VncIngress(VncCommon):
    def __init__(self):
        self._k8s_event_type = 'Ingress'
        super(VncIngress,self).__init__(self._k8s_event_type)
        self._name = type(self).__name__
        self._args = vnc_kube_config.args()
        self._queue = vnc_kube_config.queue()
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self._logger = vnc_kube_config.logger()
        self._kube = vnc_kube_config.kube()
        self._default_vn_obj = None
        self._fip_pool_obj = None
        self.service_lb_mgr = ServiceLbManager()
        self.service_ll_mgr = ServiceLbListenerManager()
        self.service_lb_pool_mgr = ServiceLbPoolManager()
        self.service_lb_member_mgr = ServiceLbMemberManager()

    def _get_namespace(self, ns_name):
        return NamespaceKM.find_by_name_or_uuid(ns_name)

    def _get_project(self, ns_name):
        proj_fq_name = vnc_kube_config.cluster_project_fq_name(ns_name)
        try:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        except NoIdError:
            self._logger.error("%s - %s Not Found" %(self._name, proj_fq_name))
            return None
        return proj_obj

    def _get_network(self, ns_name):
        ns = self._get_namespace(ns_name)
        if ns.is_isolated():
            vn_fq_name = ns.get_network_fq_name()
        else:
            if self._default_vn_obj:
                return self._default_vn_obj
            proj_fq_name = vnc_kube_config.cluster_default_project_fq_name()
            vn_fq_name = proj_fq_name +\
                [vnc_kube_config.cluster_default_network_name()]
        try:
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        except NoIdError:
            self._logger.error("%s - %s Not Found" %(self._name, vn_fq_name))
            return None
        if not ns.is_isolated():
            self._default_vn_obj = vn_obj
        return vn_obj

    def _get_pod_ipam_subnet_uuid(self, vn_obj):
        pod_ipam_subnet_uuid = None
        fq_name = vnc_kube_config.pod_ipam_fq_name()
        vn = VirtualNetworkKM.find_by_name_or_uuid(vn_obj.get_uuid())
        pod_ipam_subnet_uuid = vn.get_ipam_subnet_uuid(fq_name)
        if pod_ipam_subnet_uuid is None:
            self._logger.error("%s - %s Not Found" %(self._name, fq_name))
        return pod_ipam_subnet_uuid

    def _get_public_fip_pool(self):
        if self._fip_pool_obj:
            return self._fip_pool_obj
        fip_pool_fq_name = [vnc_kube_config.cluster_domain(),
                            self._args.public_network_project,
                            self._args.public_network,
                            self._args.public_fip_pool]
        try:
            fip_pool_obj = self._vnc_lib. \
                           floating_ip_pool_read(fq_name=fip_pool_fq_name)
        except NoIdError:
            self._logger.error("%s - %s Not Found" \
                 %(self._name, fip_pool_fq_name))
            return None
        self._fip_pool_obj = fip_pool_obj
        return fip_pool_obj

    def _get_floating_ip(self, name,
            proj_obj, external_ip=None, vmi_obj=None):
        if vmi_obj:
            fip_refs = vmi_obj.get_floating_ip_back_refs()
            for ref in fip_refs or []:
                fip = FloatingIpKM.get(ref['uuid'])
                if fip:
                    return fip
                else:
                    break
        fip_pool = self._get_public_fip_pool()
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
        except Exception as e:
            string_buf = StringIO()
            cgitb_hook(file=string_buf, format="text")
            err_msg = string_buf.getvalue()
            self._logger.error("%s - %s" %(self._name, err_msg))
            return None
        return fip

    def _allocate_floating_ip(self, lb_obj, name, proj_obj, external_ip):
        vmi_id = lb_obj.virtual_machine_interface_refs[0]['uuid']
        vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
        if vmi_obj is None:
            self._logger.error("%s - %s Vmi %s Not Found" \
                 %(self._name, lb_obj.name, vmi_id))
            return None
        fip = self._get_floating_ip(name, proj_obj, external_ip, vmi_obj)
        return fip

    def _deallocate_floating_ip(self, lb):
        vmi_id = list(lb.virtual_machine_interfaces)[0]
        vmi = VirtualMachineInterfaceKM.get(vmi_id)
        if vmi is None:
            self._logger.error("%s - %s Vmi %s Not Found" \
                 %(self._name, lb.name, vmi_id))
            return
        fip_list = vmi.floating_ips.copy()
        for fip_id in fip_list or []:
            fip_obj = self._vnc_lib.floating_ip_read(id=fip_id)
            fip_obj.set_virtual_machine_interface_list([])
            self._vnc_lib.floating_ip_update(fip_obj)
            self._vnc_lib.floating_ip_delete(id=fip_obj.uuid)
            FloatingIpKM.delete(fip_obj.uuid)

    def _vnc_create_member(self, pool, address, port, annotations):
        pool_obj = self.service_lb_pool_mgr.read(pool.uuid)
        member_obj = self.service_lb_member_mgr.create(pool_obj,
                          address, port, annotations)
        return member_obj

    def _vnc_update_member(self, member_id, address, port, annotations):
        member_obj = self.service_lb_member_mgr.update(member_id,
                          address, port, annotations)
        return member_obj

    def _vnc_create_pool(self, ns_name, ll, port, lb_algorithm, annotations):
        proj_obj = self._get_project(ns_name)
        ll_obj = self.service_ll_mgr.read(ll.uuid)
        pool_obj = self.service_lb_pool_mgr.create(ll_obj, proj_obj,
                                            port, lb_algorithm, annotations)
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
        pod_ipam_subnet_uuid = self._get_pod_ipam_subnet_uuid(vn_obj)
        lb_obj = self.service_lb_mgr.create(self._k8s_event_type, ns_name,
                            uid, name, proj_obj, vn_obj, vip_address,
                            pod_ipam_subnet_uuid)
        if lb_obj:
            vip_info = {}
            external_ip = None
            vip_info['clusterIP'] = lb_obj._loadbalancer_properties.vip_address
            if annotations and 'externalIP' in annotations:
                external_ip = annotations['externalIP']
            fip_obj = self._allocate_floating_ip(lb_obj,
                        name, proj_obj, external_ip)
            if fip_obj:
                vip_info['externalIP'] = fip_obj.address
            patch = {'metadata': {'annotations': vip_info}}
            self._kube.patch_resource("ingresses", name,
                                      patch, ns_name, beta=True)
        else:
            self._logger.error("%s - %s LB Not Created" %(self._name, name))

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
                backend['member'] = {}
                if len(pool.members) == 0:
                    continue
                member_id = list(pool.members)[0]
                member = LoadbalancerMemberKM.get(member_id)
                if member.annotations is None:
                    annotations = {}
                    kvps = []
                    member_obj = self._vnc_lib. \
                                 loadbalancer_member_read(id=member_id)
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

    def _get_new_backend_list(self, spec):
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
                    backend['member'] = {}
                    backend['protocol'] = 'HTTP'
                    if 'host' in rule:
                        backend['annotations']['host'] = rule['host']
                    if 'path' in path:
                        backend['annotations']['path'] = path['path']
                    service = path['backend']
                    backend['annotations']['type'] = 'acl'
                    backend['member']['serviceName'] = service['serviceName']
                    backend['member']['servicePort'] = service['servicePort']
                    backend_list.append(backend)
        if 'backend' in spec:
            service = spec['backend']
            backend = {}
            backend['annotations'] = {}
            backend['member'] = {}
            backend['protocol'] = 'HTTP'
            backend['annotations']['type'] = 'default'
            backend['member']['serviceName'] = service['serviceName']
            backend['member']['servicePort'] = service['servicePort']
            backend_list.append(backend)
        return backend_list

    def _create_member(self, ns_name, backend_member, pool):
        resource_type = "services"
        service_name = backend_member['serviceName']
        service_port = backend_member['servicePort']
        service_info = self._kube.get_resource(resource_type,
                       service_name, ns_name)
        member = None
        if service_info and 'clusterIP' in service_info['spec']:
            service_ip = service_info['spec']['clusterIP']
            self._logger.debug("%s - clusterIP for service %s - %s" \
                 %(self._name, service_name, service_ip))
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
                member_obj = self._vnc_create_member(pool,
                                  service_ip, service_port, annotations)
                if member_obj:
                    member = LoadbalancerMemberKM.locate(member_obj.uuid)
                else:
                    self._logger.error(
                         "%s - (%s %s) Member Not Created for Pool %s" \
                         %(self._name, service_name,
                         str(service_port), pool.name))
        else:
            self._logger.error("%s - clusterIP for Service %s Not Found" \
                 %(self._name, service_name))
            self._logger.error(
                 "%s - (%s %s) Member Not Created for Pool %s" \
                 %(self._name, service_name,
                 str(service_port), pool.name))
        return member

    def _update_member(self, ns_name, backend_member, pool):
        resource_type = "services"
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
            service_info = self._kube.get_resource(resource_type,
                           new_service_name, ns_name)
            if service_info and 'clusterIP' in service_info['spec']:
                service_ip = service_info['spec']['clusterIP']
            else:
                self._logger.error("%s - clusterIP for Service %s Not Found" \
                     %(self._name, new_service_name))
                self._logger.error(
                     "%s - (%s %s) Member Not Updated for Pool %s" \
                     %(self._name, new_service_name,
                     str(new_service_port), pool.name))
                self._vnc_delete_member(member_id)
                LoadbalancerMemberKM.delete(member_id)
                self._logger.error(
                     "%s - (%s %s) Member Deleted for Pool %s" \
                     %(self._name, old_service_name,
                     str(old_service_port), pool.name))
                return None
        else:
            service_ip = member.params['address']
        annotations = {}
        annotations['serviceName'] = new_service_name
        member_obj = self._vnc_update_member(member_id,
                     service_ip, new_service_port, annotations)
        member = LoadbalancerMemberKM.update(member)
        return member

    def _create_pool(self, ns_name, ll, port, lb_algorithm, annotations):
        pool_id = ll.loadbalancer_pool
        pool = LoadbalancerPoolKM.get(pool_id)
        if pool is None:
            pool_obj = self._vnc_create_pool(ns_name, ll,
                            port, lb_algorithm, annotations)
            pool_id = pool_obj.uuid
            pool = LoadbalancerPoolKM.locate(pool_id)
        else:
            self._logger.error("%s - %s Pool Not Created" \
                 %(self._name, ll.name))
        return pool

    def _create_listener(self, ns_name, lb, port):
        ll_obj = self._vnc_create_listeners(ns_name, lb, port)
        if ll_obj:
            ll = LoadbalancerListenerKM.locate(ll_obj.uuid)
        else:
            self._logger.error("%s - %s Listener for Port %s Not Created" \
                 %(self._name, lb.name, str(port)))
        return ll

    def _create_lb(self, uid, name, ns_name, event):
        lb = LoadbalancerKM.get(uid)
        if not lb:
            annotations = event['object']['metadata'].get('annotations')
            lb_obj = self._vnc_create_lb(uid, name, ns_name, annotations)
            if lb_obj is None:
                return
            lb = LoadbalancerKM.locate(uid)

        spec = event['object']['spec']
        new_backend_list = self._get_new_backend_list(spec)
        old_backend_list = self._get_old_backend_list(lb)

        # find the unchanged backends
        for new_backend in new_backend_list[:] or []:
            for old_backend in old_backend_list[:] or []:
                if new_backend['annotations'] == old_backend['annotations'] \
                    and new_backend['member'] == old_backend['member']:
                    old_backend_list.remove(old_backend)
                    new_backend_list.remove(new_backend)
        if len(old_backend_list) == 0 and len(new_backend_list) == 0:
            return lb

        # find the updated backends and update
        backend_update_list = []
        for new_backend in new_backend_list[:] or []:
            for old_backend in old_backend_list[:] or []:
                if new_backend['annotations'] == old_backend['annotations']:
                    backend = old_backend
                    backend['member']['member_id'] = \
                                     old_backend['member_id']
                    backend['member']['serviceName'] = \
                                     new_backend['member']['serviceName']
                    backend['member']['servicePort'] = \
                                     new_backend['member']['servicePort']
                    backend_update_list.append(backend)
                    old_backend_list.remove(old_backend)
                    new_backend_list.remove(new_backend)
        for backend in backend_update_list or []:
            ll = LoadbalancerListenerKM.get(backend['listener_id'])
            pool = LoadbalancerPoolKM.get(backend['pool_id'])
            backend_member = backend['member']
            member = self._update_member(ns_name, backend_member, pool)
            if member is None:
                self._logger.error("%s - Deleting Listener %s and Pool %s" \
                     %(self._name, ll.name, pool.name))
                self._vnc_delete_pool(pool.uuid)
                LoadbalancerPoolKM.delete(pool.uuid)
                self._vnc_delete_listener(ll.uuid)
                LoadbalancerListenerKM.delete(ll.uuid)
        if len(old_backend_list) == 0 and len(new_backend_list) == 0:
            return lb

        # delete the old backends
        for backend in old_backend_list or []:
            self._delete_listener(backend['listener_id'])

        # create the new backends
        port = {}
        lb_algorithm = "ROUND_ROBIN"
        port['protocol'] = 'HTTP'
        port['port'] = '80'
        for backend in new_backend_list:
            ll = self._create_listener(ns_name, lb, port)
            annotations = {}
            for key in backend['annotations']:
                annotations[key] = backend['annotations'][key]
            port['protocol'] = backend['protocol']
            pool = self._create_pool(ns_name, ll, port, lb_algorithm, annotations)
            backend_member = backend['member']
            member = self._create_member(ns_name, backend_member, pool)
            if member is None:
                self._logger.error("%s - Deleting Listener %s and Pool %s" \
                     %(self._name, ll.name, pool.name))
                self._vnc_delete_pool(pool.uuid)
                LoadbalancerPoolKM.delete(pool.uuid)
                self._vnc_delete_listener(ll.uuid)
                LoadbalancerListenerKM.delete(ll.uuid)

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
        self._delete_all_listeners(lb)
        self._vnc_delete_lb(lb)
        LoadbalancerKM.delete(uid)

    def _update_ingress(self, name, uid, event):
        ns_name = event['object']['metadata'].get('namespace')
        self._create_lb(uid, name, ns_name, event)

    def _delete_ingress(self, uid):
        self._delete_lb(uid)

    def _create_ingress_event(self, event_type, ingress_id, lb):
        event = {}
        object = {}
        object['kind'] = 'Ingress'
        object['spec'] = {}
        object['metadata'] = {}
        object['metadata']['uid'] = ingress_id
        if event_type == 'delete':
            event['type'] = 'DELETED'
            event['object'] = object
            self._queue.put(event)
        return

    def _sync_ingress_lb(self):
        lb_uuid_set = set(LoadbalancerKM.keys())
        ingress_uuid_set = set(IngressKM.keys())
        deleted_ingress_set = lb_uuid_set - ingress_uuid_set
        for uuid in deleted_ingress_set:
            lb = LoadbalancerKM.get(uuid)
            if not lb:
                continue
            if not lb.annotations:
                continue
            owner = None
            kind = None
            for kvp in lb.annotations['key_value_pair'] or []:
                if kvp['key'] == 'owner':
                    owner = kvp['value']
                elif kvp['key'] == 'kind':
                    kind = kvp['value']
                if owner == 'k8s' and kind == self._k8s_event_type:
                    self._create_ingress_event('delete', uuid, lb)
                    break
        return

    def ingress_timer(self):
        self._sync_ingress_lb()

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        ns_name = event['object']['metadata'].get('namespace')
        name = event['object']['metadata'].get('name')
        uid = event['object']['metadata'].get('uid')

        print("%s - Got %s %s %s:%s:%s"
              %(self._name, event_type, kind, ns_name, name, uid))
        self._logger.debug("%s - Got %s %s %s:%s:%s"
              %(self._name, event_type, kind, ns_name, name, uid))

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            self._update_ingress(name, uid, event)
        elif event['type'] == 'DELETED':
            self._delete_ingress(uid)
