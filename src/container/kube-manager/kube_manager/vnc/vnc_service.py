#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

from vnc_api.vnc_api import *
from config_db import *
from loadbalancer import *
from cfgm_common import importutils

class VncService(object):

    def __init__(self, vnc_lib=None, label_cache=None):
        self._vnc_lib = vnc_lib
        self._label_cache = label_cache

        self.service_lb_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbManager', vnc_lib)
        self.service_ll_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbListenerManager', vnc_lib)
        self.service_lb_pool_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbPoolManager', vnc_lib)
        self.service_lb_member_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbMemberManager', vnc_lib)

    def _get_project(self, service_namespace):
        proj_fq_name = ['default-domain', service_namespace]
        try:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
            return proj_obj
        except NoIdError:
            return None

    def _get_network(self):
        vn_fq_name = ['default-domain', 'default', 'cluster-network']
        try:
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        except NoIdError:
            return None
        return vn_obj

    def _get_virtualmachine(self, id):
        try:
            vm_obj = self._vnc_lib.virtual_machine_read(id=id)
        except NoIdError:
            return None
        return vm_obj

    def check_service_label_actions(self, labels, service_id, ports):
        for label in labels.items():
            key = self._label_cache._get_key(label)
            self._label_cache._locate_label(key,
                self._label_cache.service_label_cache, label, service_id)
            pod_ids = self._label_cache.pod_label_cache.get(key, [])
            if len(pod_ids):
                self.update_service(service_id, pod_ids, ports)

    def remove_service_labels_from_cache(self, labels, service_id, ports):
        for label in labels.items():
            key = self._label_cache._get_key(label)
            self._label_cache._remove_label(key,
                self._label_cache.service_label_cache, label, service_id)

    def update_service(self, service_id, pod_list, ports=None):
        #lb = LoadbalancerKM.get(service_id)
        lb_obj = self.service_lb_mgr.read(service_id)
        if lb_obj is None:
            return

        ll_back_refs = self.service_lb_mgr.get_loadbalancer_listener_back_refs(service_id)
        if ports is not None:
            for port in ports:
                ll_obj = None
                for ll_back_ref in ll_back_refs or []:
                    ll_obj = self._vnc_lib.loadbalancer_listener_read(id=ll_back_ref['uuid'])
                    if ll_obj and ll_obj.get_loadbalancer_listener_properties().get_protocol_port() == port['port']:
                        break
                target_port = self.service_ll_mgr.get_target_port_from_annotations(id=ll_back_ref['uuid']);

                pool_refs = ll_obj.get_loadbalancer_pool_back_refs()
                if pool_refs is None:
                    return
                pool_obj = self._vnc_lib.loadbalancer_pool_read(id=pool_refs[0]['uuid'])
                member_refs = pool_obj.get_loadbalancer_members()

                for pod_id in pod_list:
                    vm = self._get_virtualmachine(pod_id)
                    vmi_back_refs = vm.get_virtual_machine_interface_back_refs()
                    member_match = False
                    for member_ref in member_refs or []:
                        annotations = self.service_lb_member_mgr.get_member_annotations(member_ref['uuid'])
                        if annotations:
                            for kvp in annotations.key_value_pair or []:
                                if kvp.key == 'vmi' and kvp.value == vmi_back_refs[0]['uuid']:
                                    member_match = True
                                    break
                    if member_match == False:
                        self.service_lb_member_mgr.create(pool_obj, vmi_back_refs[0]['uuid'], target_port)
        else:
                ll_obj = None
                for ll_back_ref in ll_back_refs or []:
                    ll_obj = self._vnc_lib.loadbalancer_listener_read(id=ll_back_ref['uuid'])
                    if ll_obj is not None:
                        pool_refs = ll_obj.get_loadbalancer_pool_back_refs()
                        if pool_refs is None:
                            continue;

                        pool_obj = self._vnc_lib.loadbalancer_pool_read(id=pool_refs[0]['uuid'])
                        member_refs = pool_obj.get_loadbalancer_members()

                        for pod_id in pod_list:
                            vm = self._get_virtualmachine(pod_id)
                            vmi_back_refs = vm.get_virtual_machine_interface_back_refs()
                            member_match = False
                            for member_ref in member_refs or []:
                                annotations = self.service_lb_member_mgr.get_member_annotations(member_ref['uuid'])
                                if annotations:
                                    for kvp in annotations.key_value_pair or []:
                                        if kvp.key == 'vmi' and kvp.value == vmi_back_refs[0]['uuid']:
                                            member_match = True
                                            break
                            if member_match == False:
                                self.service_lb_member_mgr.create(pool_obj, vmi_back_refs[0]['uuid'],
                                    ll_obj.get_loadbalancer_listener_properties().get_protocol_port())

    def vnc_service_add(self, service_id, service_name,
            service_namespace, service_ip, selectors, ports):
        #import pdb; pdb.set_trace()
        proj_obj = self._get_project(service_namespace)
        vn_obj = self._get_network()

        lb_obj = self.service_lb_mgr.read(service_id)
        if lb_obj is None:
            lb_obj = self.service_lb_mgr.create(vn_obj, service_id, service_name, 
                                proj_obj, service_ip, selectors)
        ll_back_refs = self.service_lb_mgr.get_loadbalancer_listener_back_refs(service_id)

        for port in ports:
            ll_obj = None
            for ll_back_ref in ll_back_refs or []:
               ll_obj = self._vnc_lib.loadbalancer_listener_read(id=ll_back_ref['uuid'])
               if ll_obj and ll_obj.get_loadbalancer_listener_properties().get_protocol_port() == port['port']:
                   break
            if ll_obj is None:
                ll_obj = self.service_ll_mgr.create(lb_obj, proj_obj, port)

            pool_refs = ll_obj.get_loadbalancer_pool_back_refs()
            if pool_refs is None:
                pool_obj = self.service_lb_pool_mgr.create(ll_obj, proj_obj, port)

        if selectors:
            self.check_service_label_actions(selectors, service_id, ports)

    def vnc_service_delete(self, service_id, service_name, selectors, ports):
        lb_obj = self.service_lb_mgr.read(service_id)
        if lb_obj is None:
            return

        ll_back_refs = self.service_lb_mgr.get_loadbalancer_listener_back_refs(service_id)
        for ll_back_ref in ll_back_refs or []:
            ll_obj = self.service_ll_mgr.read(ll_back_ref['uuid'])
            pool_refs = ll_obj.get_loadbalancer_pool_back_refs()
            for pool_ref in pool_refs or []:
                pool_obj = self.service_lb_pool_mgr.read(pool_ref['uuid'])
                lb_members = pool_obj.get_loadbalancer_members()
                for lb_member in lb_members or []:
                    self.service_lb_member_mgr.delete(lb_member['uuid'])
                self.service_lb_pool_mgr.delete(pool_ref['uuid'])
            self.service_ll_mgr.delete(ll_back_ref['uuid'])

        try:
            self.service_lb_mgr.delete(id=service_id)
        except NoIdError:
            pass

        if selectors:
            self.remove_service_labels_from_cache(selectors, service_id, ports)

    def process(self, event):
        service_id = event['object']['metadata'].get('uid')
        service_name = event['object']['metadata'].get('name')
        service_namespace = event['object']['metadata'].get('namespace')
        service_ip = event['object']['spec'].get('clusterIP')
        selectors = event['object']['spec'].get('selector', None)
        ports = event['object']['spec'].get('ports')

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            self.vnc_service_add(service_id, service_name,
                service_namespace, service_ip, selectors, ports)
        elif event['type'] == 'DELETED':
            self.vnc_service_delete(service_id, service_name, selectors, ports)
