#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC endpoints management for kubernetes
"""

import uuid

from vnc_api.vnc_api import *
from config_db import *
from cfgm_common import importutils

class VncEndpoints(object):

    def __init__(self, vnc_lib=None, logger=None, kube=None):
        self._name = type(self).__name__
        self._vnc_lib = vnc_lib
        self.logger = logger
        self._kube = kube

        self.service_lb_pool_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbPoolManager', vnc_lib, logger)
        self.service_lb_member_mgr = importutils.import_object(
            'kube_manager.vnc.loadbalancer.ServiceLbMemberManager', vnc_lib, logger)

    def _vnc_create_member(self, pool, pod_id, vmi_id, protocol_port):
        pool_obj = self.service_lb_pool_mgr.read(pool.uuid)
        address = None
        annotations = {}
        annotations['vmi'] = vmi_id
        annotations['vm'] = pod_id
        member_obj = self.service_lb_member_mgr.create(pool_obj,
                          address, protocol_port, annotations)
        return member_obj

    def _is_service_exists(self, service_name, service_namespace):
        name = 'service' + '-' + service_name
        lb_fq_name = ['default-domain', service_namespace, name]
        try:
            lb_obj = self._vnc_lib.loadbalancer_read(fq_name=lb_fq_name)
        except NoIdError:
            return False, None

        if lb_obj is None:
            return False, None
        else:
            return True, lb_obj.uuid

    def _add_pod_to_service(self, service_id, pod_id, port=None):
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            return

        listener_found = True
        for ll_id in list(lb.loadbalancer_listeners):
            ll = LoadbalancerListenerKM.get(ll_id)
            if not ll:
                continue
            if not ll.params['protocol_port']:
                continue

            if port:
                if ll.params['protocol_port'] != port['port'] or \
                   ll.params['protocol'] != port['protocol']:
                    listener_found = False

            if not listener_found:
                continue
            pool_id = ll.loadbalancer_pool
            if not pool_id:
                continue
            pool = LoadbalancerPoolKM.get(pool_id)
            if not pool:
                continue
            vm = VirtualMachineKM.get(pod_id)
            if not vm: 
                continue

            for vmi_id in list(vm.virtual_machine_interfaces):
                vmi = VirtualMachineInterfaceKM.get(vmi_id)
                if not vmi:
                    continue
                member_match = False
                for member_id in pool.members:
                    member = LoadbalancerMemberKM.get(member_id)
                    if member and member.vmi == vmi_id:
                        member_match = True
                        break
                if not member_match:
                    if isinstance(ll.target_port, basestring):
                        target_port = ll.params['protocol_port']
                    else:
                        target_port = ll.target_port
                    self.logger.debug("Create LB member for Pod: %s in LB: %s with target-port: %s" 
                                       % (vm.fq_name, lb.name, target_port))
                    member_obj = self._vnc_create_member(pool, pod_id, vmi_id,
                                                         target_port)
                    LoadbalancerMemberKM.locate(member_obj.uuid)

    def _remove_pod_from_service(self, service_id, pod_id, port=None):
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            return

        listener_found = True
        for ll_id in list(lb.loadbalancer_listeners):
            ll = LoadbalancerListenerKM.get(ll_id)
            if not ll:
                continue
            if not ll.params['protocol_port']:
                continue

            if port:
                if ll.params['protocol_port'] != port['port'] or \
                   ll.params['protocol'] != port['protocol']:
                    listener_found = False

            if not listener_found:
                continue
            pool_id = ll.loadbalancer_pool
            if not pool_id:
                continue
            pool = LoadbalancerPoolKM.get(pool_id)
            if not pool:
                continue

            member_match = False
            for member_id in pool.members:
                member = LoadbalancerMemberKM.get(member_id)
                if member and member.vm == pod_id:
                    member_match = True
                    break
            if member_match:
                self.logger.debug("Delete LB member for Pod: %s from LB: %s" 
                                   % (pod_id, lb.name))
                self.service_lb_member_mgr.delete(member_id)
                LoadbalancerMemberKM.delete(member.uuid)

    def _get_pod_members_from_service_lb(self, service_id):
        pod_members = set()
        lb = LoadbalancerKM.get(service_id)
        if not lb:
            return pod_members

        # No listeners on LB. Error condition. Handle gracefully..
        if len(lb.loadbalancer_listeners) == 0:
            self.logger.warning("No listeners on LB (" + lb.name + ")")
            return pod_members

        for ll_id in list(lb.loadbalancer_listeners):
            ll = LoadbalancerListenerKM.get(ll_id)
            if not ll:
                continue

            pool_id = ll.loadbalancer_pool
            if not pool_id:
                continue
            pool = LoadbalancerPoolKM.get(pool_id)
            if not pool:
                continue

            for member_id in pool.members:
                member = LoadbalancerMemberKM.get(member_id)
                if member.vm:
                    pod_members.add(member.vm)

            return pod_members


    def _get_service_pod_list(self, event):
        pods_in_event = set()
        subsets = event['object']['subsets']
        for subset in subsets:
            endpoints = subset['addresses']
            for endpoint in endpoints:
                pod = endpoint.get('targetRef')
                if pod and pod.get('uid'):
                    pods_in_event.add(pod.get('uid'))

        return pods_in_event


    def vnc_endpoint_add(self, uid, name, namespace, event):
        # Does service exists in contrail-api server,
        # If No, create Service and then continue.
        exists, service_id = self._is_service_exists(name, namespace)
        if exists == False:
            self.logger.warning("Add/Modify endpoint event when service "
                + name + " not existing");
            return

        # Get list of Pods attached to the Service.
        prev_pod_ids = self._get_pod_members_from_service_lb(service_id)

        #Get curr list of Pods matching Service Selector as listed
        # in 'event' elements.
        cur_pod_ids = self._get_service_pod_list(event)

        # Compare. If Pod present in both lists, do nothing

        # If Pod present only in cur_pod list, add 'Pod' to 'Service'
        add_pod_members = cur_pod_ids.difference(prev_pod_ids)
        for pod_id in add_pod_members:
            self._add_pod_to_service(service_id, pod_id)

        # If Pod present only in prev_pod list , delete 'Pod' from 'Service'
        del_pod_members = prev_pod_ids.difference(cur_pod_ids)
        for pod_id in del_pod_members:
            self._remove_pod_from_service(service_id, pod_id)

    def vnc_endpoint_delete(self, uid, name, namespace):
        # Does service exists in contrail-api server,
        # If No, create Service and then continue.
        exists, service_id = self._is_service_exists(name, namespace)
        if exists is False:
            self.logger.warning("Delete endpoint event when service "
                + name + " doesn't exist.. ignore endpoint delete");
            return

        # Get list of Pods attached to the Service.
        prev_pod_ids = self._get_pod_members_from_service_lb(service_id)

        #Get curr list of Pods matching Service Selector as listed
        # in 'event' elements.
        cur_pod_ids = self._get_pod_list(event)

        # Compare 2 lists. Should be same.. any diff is a sign of warning
        pod_diff = set(prev_pod_ids) - set(cur_pod_ids)

        # loadbalancer members source of truth. Delete them'all
        for pod_id in prev_pod_ids:
            remove_pod_from_service(service_id, pod_id)

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        uid = event['object']['metadata'].get('uid')
        name = event['object']['metadata'].get('name')
        namespace = event['object']['metadata'].get('namespace')

        print("%s - Got %s %s %s:%s"
              %(self._name, event_type, kind, namespace, name))
        self.logger.debug("%s - Got %s %s %s:%s"
              %(self._name, event_type, kind, namespace, name))

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            self.vnc_endpoint_add(uid, name, namespace, event)
        elif event['type'] == 'DELETED':
            self.vnc_endpoint_delete(uid, name, namespace)
