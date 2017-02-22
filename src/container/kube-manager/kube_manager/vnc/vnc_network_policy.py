#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

import uuid

from vnc_api.vnc_api import *
from config_db import *


class VncNetworkPolicy(object):

    def __init__(self, vnc_lib=None, label_cache=None, logger=None):
        self._name = type(self).__name__
        self._vnc_lib = vnc_lib
        self._label_cache = label_cache
        self.logger = logger
        self.logger.info("VncNetworkPolicy init done.")
        self.policy_src_label_cache = {}
        self.policy_dest_label_cache = {}

    def _select_pods(self, labels, pod_set=None):
        result = set()
        for label in labels.items():
            key = self._label_cache._get_key(label)
            pod_ids = self._label_cache.pod_label_cache.get(key)
            if not pod_ids:
                continue
            if not result:
                result = pod_ids.copy()
            else:
                result.intersection_update(pod_ids)

        if pod_set:
            result.intersection_update(pod_set)
        return result

    def _add_sg_rule(self, sg, sg_rule):
        sg_obj = self._vnc_lib.security_group_read(id=sg.uuid)
        rules = sg_obj.get_security_group_entries()
        if rules is None:
            rules = PolicyEntriesType([sg_rule])
        else:
            for sgr in rules.get_policy_rule() or []:
                sgr_copy = copy.copy(sgr)
                sgr_copy.rule_uuid = sg_rule.rule_uuid
                if sg_rule == sgr_copy:
                    self.logger.info("SecurityGroupRuleExists %s" % sgr.rule_uuid)
                    return
            rules.add_policy_rule(sg_rule)

        sg_obj.set_security_group_entries(rules)
        self._vnc_lib.security_group_update(sg_obj)

    def _delete_sg_rule(self, sg_uuid, rule_uuid):
        sg_obj = self._vnc_lib.security_group_read(id=sg_uuid)
        rules = sg_obj.get_security_group_entries()
        if rules is None:
            return

        for sgr in rules.get_policy_rule() or []:
            if sgr.rule_uuid != rule_uuid:
                continue
            rules.delete_policy_rule(sgr)
            update_sg = True

        if update_sg:
            sg_obj.set_security_group_entries(rules)
            self._vnc_lib.security_group_update(sg_obj)

    def _add_src_pod_2_policy(self, pod_id, policy_list):
        for policy_id in policy_list:
            sg = SecurityGroupKM.get(policy_id)
            if not sg:
                continue
            if self._select_pods(sg.src_pod_selector, {pod_id}):
                self._set_sg_rule(sg, pod_id)

    def _add_dst_pod_2_policy(self, pod_id, policy_list):
        for policy_id in policy_list:
            sg = SecurityGroupKM.get(policy_id)
            if not sg:
                continue
            if self._select_pods(sg.dst_pod_selector, {pod_id}):
                self._sg_2_pod_link(pod_id, policy_id, 'ADD')

    def vnc_pod_add(self, event):
        labels = event['object']['metadata'].get('labels', {})
        pod_id = event['object']['metadata']['uid']

        policy_src_list = set()
        policy_dst_list = set()
        for label in labels.items():
            key = self._label_cache._get_key(label)

            policy_ids = self.policy_src_label_cache.get(key, [])
            policy_src_list.update(policy_ids)

            policy_ids = self.policy_dest_label_cache.get(key, [])
            policy_dst_list.update(policy_ids)

        self._add_src_pod_2_policy(pod_id, policy_src_list)
        self._add_dst_pod_2_policy(pod_id, policy_dst_list)

    def _check_deleted_labels(self, deleted_labels, vm):
        if not deleted_labels:
            return

        for vmi_id in vm.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            if not vmi:
                return
            break

        # check if pod link to be deleted
        for sg_id in vmi.security_groups:
            sg = SecurityGroupKM.get(sg_id)
            if not sg or not sg.dst_pod_selector:
                continue
            dst_labels = sg.dst_pod_selector
            if set(deleted_labels.keys()).intersection(set(dst_labels.keys())):
                self._sg_2_pod_link(vm.uuid, sg_id, 'DELETE')

        # check if rule has to be deleted
        for label in deleted_labels.items():
            key = self._label_cache._get_key(label)

            policy_ids = self.policy_src_label_cache.get(key, [])
            for policy_id in policy_ids:
                self._delete_sg_rule(policy_id, vm.uuid)

    def _check_changed_labels(self, changed_labels, vm):
        if not changed_labels:
            return

        for vmi_id in vm.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            if not vmi:
                return
            break

        # check if pod link to be deleted
        for sg_id in vmi.security_groups:
            sg = SecurityGroupKM.get(sg_id)
            if not sg or not sg.dst_pod_selector:
                continue
            dst_labels = sg.dst_pod_selector
            if set(changed_labels.keys()).intersection(set(dst_labels.keys())):
                self._sg_2_pod_link(vm.uuid, sg_id, 'DELETE')

        # check if rule has to be deleted
        for label in changed_labels.items():
            key = self._label_cache._get_key(label)

            policy_ids = self.policy_src_label_cache.get(key, [])
            for policy_id in policy_ids:
                self._delete_sg_rule(policy_id, vm.uuid)

    def vnc_pod_update(self, event, label_diff):
        if not label_diff:
            return

        labels = event['object']['metadata']['labels']
        pod_id = event['object']['metadata']['uid']
        vm = VirtualMachineKM.get(pod_id)
        if not vm:
            return

        self._check_deleted_labels(label_diff['removed'], vm)
        self._check_changed_labels(label_diff['changed'], vm)
        self.vnc_pod_add(event)

    def vnc_pod_delete(self, event):
        labels = event['object']['metadata'].get('labels', {})
        pod_id = event['object']['metadata']['uid']

        for label in labels.items():
            key = self._label_cache._get_key(label)

            policy_ids = self.policy_src_label_cache.get(key, [])
            for policy_id in policy_ids:
                self._delete_sg_rule(policy_id, pod_id)

    def _set_sg_rule(self, sg, src_pod):
        vm = VirtualMachineKM.get(src_pod)
        if not vm:
            return

        ip_addr = None
        for vmi_id in vm.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            if not vmi:
                continue
            for iip_id in vmi.instance_ips:
                iip = InstanceIpKM.get(iip_id)
                if not iip:
                    continue
                ip_addr = iip.address
        if not ip_addr:
            return

        sgr_uuid = src_pod
        src_addr = AddressType(subnet=SubnetType(ip_addr, 32))
        dst_addr = AddressType(security_group='local')
        for port in sg.dst_ports:
            proto = port['protocol'].lower()
            rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                protocol=proto,
                src_addresses=[src_addr],
                src_ports=[PortType(0, 65535)],
                dst_addresses=[dst_addr],
                dst_ports=[PortType(int(port['port']), int(port['port']))],
                ethertype='IPv4')
            self._add_sg_rule(sg, rule)

    def _set_sg_rules(self, sg, event):
        if not sg.src_pod_selector:
            return
        for src_label in sg.src_pod_selector.items():
            key = self._label_cache._get_key(src_label)
            self._label_cache._locate_label(key,
                self.policy_src_label_cache, src_label, sg.uuid)
            src_pods = self._select_pods(sg.src_pod_selector)
            for src_pod in src_pods:
                self._set_sg_rule(sg, src_pod)

    def _sg_2_pod_link(self, pod_id, sg_id, oper):
        vm = VirtualMachineKM.get(pod_id)
        if not vm:
            return

        for vmi_id in vm.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            if not vmi:
                return
            break

        try:
            self._vnc_lib.ref_update('virtual-machine-interface', vmi_id,
                'security-group', sg_id, None, oper)
        except Exception as e:
            self.logger.error("Failed to %s SG %s to pod %s" %
                (oper, pod_id, sg_id))

        if oper == 'ADD':
            default_oper = 'DELETE'
        else:
            default_oper = 'ADD'

        sg_fq_name = ['default-domain', vmi.fq_name[-2], 'default']
        sg_uuid = SecurityGroupKM.get_fq_name_to_uuid(sg_fq_name)
        sg = SecurityGroupKM.get(sg_uuid)
        try:
            self._vnc_lib.ref_update('virtual-machine-interface', vmi_id,
                'security-group', sg_uuid, None, default_oper)
        except Exception as e:
            self.logger.error("Failed to %s SG %s to pod %s" %
                (default_oper, pod_id, sg_uuid)) 

    def _apply_sg_2_pods(self, sg, event):
        if not sg.dst_pod_selector:
            return

        uuid = event['object']['metadata'].get('uid')
        for dest_label in sg.dst_pod_selector.items():
            key = self._label_cache._get_key(dest_label)
            self._label_cache._locate_label(key, self.policy_dest_label_cache,
                dest_label, uuid)

        pod_ids = self._select_pods(sg.dst_pod_selector)
        for pod_id in pod_ids:
            self._sg_2_pod_link(pod_id, sg.uuid, 'ADD')

    def _set_sg_annotations(self, sg_obj, sg, event):
        update_sg = False
        spec_data = json.dumps(event['object']['spec'])
        kvp = KeyValuePair(key='spec', value=spec_data)
        kvps = KeyValuePairs([kvp])
        if not sg:
            sg_obj.set_annotations(kvps)
        elif sg.annotations:
            for kvp in sg.annotations.get('key_value_pair', []):
                if kvp.get('key') != 'spec':
                    continue
                if kvp.get('value') != spec_data:
                    sg_obj.set_annotations(kvps)
                    update_sg = True
                    break
        return update_sg

    def _update_sg(self, event, sg):
        sg_obj = SecurityGroup()
        sg_obj.uuid = sg.uuid
        update_sg = self._set_sg_annotations(sg_obj, sg, event)
        if not update_sg:
            return sg

        try:
            self._vnc_lib.security_group_update(sg_obj)
        except Exception as e:
            self.logger.error("Failed to update SG %s" % sg.uuid)
            return None
        return SecurityGroupKM.locate(sg.uuid)

    def _create_sg(self, event, uuid):
        name = event['object']['metadata'].get('name')
        namespace = event['object']['metadata'].get('namespace')
        sg_fq_name = ['default-domain', namespace, name]
        proj_fq_name = ['default-domain', namespace]
        proj_obj = Project(name=namespace, fq_name=proj_fq_name,
            parent='domain')
        sg_obj = SecurityGroup(name=name, parent_obj=proj_obj)
        sg_obj.uuid = uuid
        self._set_sg_annotations(sg_obj, None, event)
        try:
            self._vnc_lib.security_group_create(sg_obj)
        except Exception as e:
            self.logger.error("Failed to create SG %s" % uuid)
            return None
        sg = SecurityGroupKM.locate(uuid)
        return sg

    def _check_sg_uuid_change(self, event, uuid):
        name = event['object']['metadata'].get('name')
        namespace = event['object']['metadata'].get('namespace')
        sg_fq_name = ['default-domain', namespace, name]
        sg_uuid = SecurityGroupKM.get_fq_name_to_uuid(sg_fq_name)
        if sg_uuid != uuid:
            self.vnc_network_policy_delete(event, sg_uuid)

    def vnc_network_policy_add(self, event):
        uuid = event['object']['metadata'].get('uid')
        sg = SecurityGroupKM.get(uuid)
        if not sg:
            self._check_sg_uuid_change(event, uuid)
            sg = self._create_sg(event, uuid)
        else:
            sg = self._update_sg(event, sg)

        self._set_sg_rules(sg, event)
        self._apply_sg_2_pods(sg, event)

    def vnc_network_policy_delete(self, event, sg_uuid=None):
        if not sg_uuid:
            sg_uuid = event['object']['metadata'].get('uid')
        sg = SecurityGroupKM.get(sg_uuid)
        if not sg:
            return

        for vmi_id in list(sg.virtual_machine_interfaces):
            try:
                self._vnc_lib.ref_update('virtual-machine-interface', vmi_id,
                    'security-group', sg.uuid, None, 'DELETE')
            except Exception as e:
                self.logger.error("Failed to detach SG %s" % str(e))

        self._vnc_lib.security_group_delete(id=sg.uuid)

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        name = event['object']['metadata'].get('name')
        uid = event['object']['metadata'].get('uid')
        namespace = event['object']['metadata'].get('namespace')

        print("%s - Got %s %s %s:%s"
              %(self._name, event_type, kind, namespace, name))
        self.logger.debug("%s - Got %s %s %s:%s"
              %(self._name, event_type, kind, namespace, name))

        if event['object'].get('kind') == 'NetworkPolicy':
            if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
                self.vnc_network_policy_add(event)
            elif event['type'] == 'DELETED':
                self.vnc_network_policy_delete(event)
