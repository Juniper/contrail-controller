#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
VNC Ingress management for kubernetes
"""

import uuid

from config_db import *
from loadbalancer import *
from vnc_api.vnc_api import *

from cfgm_common import importutils

class VncIngress(object):
    def __init__(self, args=None, vnc_lib=None, \
                 label_cache=None, logger=None, kube=None):
        self._args = args
        self._kube = kube
        self._vnc_lib = vnc_lib
        self._vn_obj = None
        self._fip_pool_obj = None
        self.service_lb_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbManager', vnc_lib, logger)
        self.service_ll_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbListenerManager', vnc_lib, logger)
        self.service_lb_pool_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbPoolManager', vnc_lib, logger)
        self.service_lb_member_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbMemberManager', vnc_lib, logger)

    def _get_project(self, namespace):
        proj_fq_name = ['default-domain', namespace]
        try:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        except NoIdError:
            return None
        return proj_obj

    def _get_network(self):
        if self._vn_obj:
            return self._vn_obj
        vn_fq_name = ['default-domain', 'default', 'cluster-network']
        try:
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        except NoIdError:
            return None
        self._vn_obj = vn_obj
        return vn_obj

    def _get_public_fip_pool(self):
        if self._fip_pool_obj:
            return self._fip_pool_obj
        fip_pool_fq_name = ['default-domain', 'default', '__public__', '__fip_pool_public__']
        try:
            fip_pool_obj = self._vnc_lib.floating_ip_pool_read(fq_name=fip_pool_fq_name)
        except NoIdError:
            return None
        self._fip_pool_obj = fip_pool_obj
        return fip_pool_obj

    def _get_floating_ip(self, name, proj_obj, vmi_obj=None):
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
        fip_obj = FloatingIp(name + "-public-ip", fip_pool)
        fip_obj.set_project(proj_obj)
        if vmi_obj:
            fip_obj.set_virtual_machine_interface(vmi_obj)
        self._vnc_lib.floating_ip_create(fip_obj)
        fip = FloatingIpKM.locate(fip_obj.uuid)
        return fip

    def _allocate_floating_ip(self, lb_obj, name, proj_obj):
        vmi_id = lb_obj.virtual_machine_interface_refs[0]['uuid']
        vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
        if vmi_obj is None:
            return None
        fip = self._get_floating_ip(name, proj_obj, vmi_obj)
        return fip

    def _deallocate_floating_ip(self, lb):
        vmi_id = lb.virtual_machine_interfaces.pop()
        vmi = VirtualMachineInterfaceKM.get(vmi_id)
        if vmi is None:
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

    def _vnc_create_pool(self, namespace, ll, port, lb_algorithm, annotations):
        proj_obj = self._get_project(namespace)
        ll_obj = self.service_ll_mgr.read(ll.uuid)
        pool_obj = self.service_lb_pool_mgr.create(ll_obj, proj_obj, \
                                            port, lb_algorithm, annotations)
        return pool_obj

    def _vnc_create_listeners(self, namespace, lb, port):
        proj_obj = self._get_project(namespace)
        lb_obj = self.service_lb_mgr.read(lb.uuid)
        ll_obj = self.service_ll_mgr.create(lb_obj, proj_obj, port)
        return ll_obj

    def _vnc_create_lb(self, uid, name, namespace):
        lb_provider = 'opencontrail'
        proj_obj = self._get_project(namespace)
        vn_obj = self._get_network()
        if proj_obj is None or vn_obj is None:
            return None

        vip_address = None
        lb_obj = self.service_lb_mgr.create(lb_provider, vn_obj, uid,
                      name, proj_obj, vip_address)
        if lb_obj:
            self._allocate_floating_ip(lb_obj, name, proj_obj)

        return lb_obj

    def _vnc_delete_member(self, member_id):
        self.service_lb_member_mgr.delete(member_id)

    def _vnc_delete_pool(self, pool_id):
        self.service_lb_pool_mgr.delete(pool_id)

    def _vnc_delete_listener(self, ll_id):
        self.service_ll_mgr.delete(ll_id)

    def _vnc_delete_lb(self, lb_obj):
        self._deallocate_floating_ip(lb_obj)
        self.service_lb_mgr.delete(lb_obj.uuid)

    def _get_backend_list(self, spec):
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

    def _create_member(self, namespace, backend_member, pool):
        resource_type = "services"
        service_name = backend_member['serviceName']
        service_port = backend_member['servicePort']
        service_info = self._kube.get_resource(resource_type, service_name, namespace)
        if 'clusterIP' in service_info['spec']:
            service_ip = service_info['spec']['clusterIP']
            member_match = False
            for member_id in pool.members:
                member = LoadbalancerMemberKM.get(member_id)
                if member and member.params['address'] == service_ip \
                   and member.params['protocol_port'] == service_port:
                    member_match = True
                    break
            if not member_match:
                member_obj = self._vnc_create_member(pool, \
                                  service_ip, service_port, None)
                member = LoadbalancerMemberKM.locate(member_obj.uuid)
        return member

    def _create_pool(self, namespace, ll, port, lb_algorithm, annotations):
        pool_id = ll.loadbalancer_pool
        pool = LoadbalancerPoolKM.get(pool_id)
        if pool is None:
            pool_obj = self._vnc_create_pool(namespace, ll, \
                            port, lb_algorithm, annotations)
            pool_id = pool_obj.uuid
            pool = LoadbalancerPoolKM.locate(pool_id)
        return pool

    def _create_listener(self, namespace, lb, port):
        ll_obj = self._vnc_create_listeners(namespace, lb, port)
        ll = LoadbalancerListenerKM.locate(ll_obj.uuid)
        return ll

    def _create_lb(self, uid, name, namespace, event):
        lb = LoadbalancerKM.get(uid)
        if not lb:
            lb_obj = self._vnc_create_lb(uid, name, namespace)
            if lb_obj is None:
                return
            lb = LoadbalancerKM.locate(uid)

        self._delete_all_listeners(lb)

        port = {}
        lb_algorithm = "ROUND_ROBIN"
        port['protocol'] = 'HTTP'
        port['port'] = '80'

        spec = event['object']['spec']
        backend_list = self._get_backend_list(spec)
        for backend in backend_list:
            ll = self._create_listener(namespace, lb, port)
            annotations = {}
            for key in backend['annotations']:
                annotations[key] = backend['annotations'][key]
            port['protocol'] = backend['protocol']
            pool = self._create_pool(namespace, ll, port, lb_algorithm, annotations)
            backend_member = backend['member']
            member = self._create_member(namespace, backend_member, pool)

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

    def _delete_lb(self, uid):
        lb_obj = LoadbalancerKM.get(uid)
        if not lb_obj:
            return
        self._delete_all_listeners(lb_obj)
        self._vnc_delete_lb(lb_obj)
        LoadbalancerKM.delete(uid)

    def _update_ingress(self, name, uid, event):
        namespace = event['object']['metadata'].get('namespace')
        self._create_lb(uid, name, namespace, event)

    def _delete_ingress(self, uid):
        self._delete_lb(uid)

    def process(self, event):
        name = event['object']['metadata'].get('name')
        uid = event['object']['metadata'].get('uid')

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            self._update_ingress(name, uid, event)
        elif event['type'] == 'DELETED':
            self._delete_ingress(uid)
