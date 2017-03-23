#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

import uuid

from vnc_api.vnc_api import *
from config_db import *
from vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from vnc_common import VncCommon

class VncNetworkPolicy(VncCommon):

    def __init__(self):
        super(VncNetworkPolicy,self).__init__('NetworkPolicy')
        self._name = type(self).__name__
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self._label_cache = vnc_kube_config.label_cache()
        self.logger = vnc_kube_config.logger()
        self.logger.info("VncNetworkPolicy init done.")
        self.policy_src_label_cache = {}
        self.policy_dst_label_cache = {}

    def _find_pods(self, labels, pod_set=None):
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

    def _add_src_pod_2_policy(self, pod_id, policy_list):
        for policy_id in policy_list:
            sg = SecurityGroupKM.get(policy_id)
            if not sg:
                continue
            if self._find_pods(sg.src_pod_selector, {pod_id}):
                sg_name = sg.name + "__ingress"
                sg_name = VncCommon.make_name(sg_name, sg.uuid)
                sg_id = self.get_kube_fq_name_to_uuid(SecurityGroupKM, sg.namespace,
                        sg_name)
                sg_id = SecurityGroupKM.get_kube_fq_name_to_uuid(sg_fq_name)
                if sg_id:
                    self._sg_2_pod_link(pod_id, sg_id, 'ADD')

    def _add_dst_pod_2_policy(self, pod_id, policy_list):
        for policy_id in policy_list:
            sg = SecurityGroupKM.get(policy_id)
            if not sg:
                continue
            if self._find_pods(sg.dst_pod_selector, {pod_id}):
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

            policy_ids = self.policy_dst_label_cache.get(key, [])
            policy_dst_list.update(policy_ids)

        self._add_src_pod_2_policy(pod_id, policy_src_list)
        self._add_dst_pod_2_policy(pod_id, policy_dst_list)

    def _check_deleted_labels(self, label_diff, vm):
        if not label_diff or not label_diff['removed']:
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
                sg = SecurityGroupKM.get(policy_id)
                if not sg:
                    continue
                sg_name = sg.name + "__ingress"
                sg_name = VncCommon.make_name(sg_name, sg.uuid)
                sg_id = self.get_kube_fq_name_to_uuid(SecurityGroupKM, sg.namespace,
                        sg_name)
                if sg_id:
                    self._sg_2_pod_link(pod_id, sg_id, 'DELETE')

    def _check_changed_labels(self, label_diff, vm):
        if not label_diff or not label_diff['changed']:
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
                sg = SecurityGroupKM.get(policy_id)
                if not sg:
                    continue
                sg_name = sg.name + "__ingress"
                sg_name = VncCommon.make_name(sg_name, sg.uuid)
                sg_id = self.get_kube_fq_name_to_uuid(SecurityGroupKM, sg.namespace,
                        sg_name)
                if sg_id:
                    self._sg_2_pod_link(pod_id, sg_id, 'DELETE')

    def vnc_pod_update(self, event, label_diff):
        labels = event['object']['metadata']['labels']
        pod_id = event['object']['metadata']['uid']
        vm = VirtualMachineKM.get(pod_id)
        if not vm:
            return

        self._check_deleted_labels(label_diff, vm)
        self._check_changed_labels(label_diff, vm)
        self.vnc_pod_add(event)

    def vnc_pod_delete(self, event):
        pass

    def _set_sg_rule(self, dst_sg, src_sg):
        sgr_uuid = src_sg.uuid
        src_addr = AddressType(security_group=":".join(src_sg.fq_name))
        dst_addr = AddressType(security_group='local')
        for port in dst_sg.dst_ports:
            proto = port['protocol'].lower()
            rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                protocol=proto,
                src_addresses=[src_addr],
                src_ports=[PortType(0, 65535)],
                dst_addresses=[dst_addr],
                dst_ports=[PortType(int(port['port']), int(port['port']))],
                ethertype='IPv4')
            self._add_sg_rule(dst_sg, rule)

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

    def _apply_dst_sg(self, sg, event):
        if not sg.dst_pod_selector:
            return

        for dst_label in sg.dst_pod_selector.items():
            key = self._label_cache._get_key(dst_label)
            self._label_cache._locate_label(key, self.policy_dst_label_cache,
                dst_label, sg.uuid)

        pod_ids = self._find_pods(sg.dst_pod_selector)
        for pod_id in pod_ids:
            self._sg_2_pod_link(pod_id, sg.uuid, 'ADD')

    def _apply_src_sg(self, sg, event):
        if not sg.src_pod_selector:
            return

        for src_label in sg.src_pod_selector.items():
            key = self._label_cache._get_key(src_label)
            self._label_cache._locate_label(key, self.policy_src_label_cache,
                src_label, sg.uuid)

        pod_ids = self._find_pods(sg.src_pod_selector)
        for pod_id in pod_ids:
            self._sg_2_pod_link(pod_id, sg.uuid, 'ADD')

    def _set_sg_annotations(self, sg_obj, sg, event):
        namespace = event['object']['metadata'].get('namespace')
        spec_data = json.dumps(event['object']['spec'])
        self.add_annotations(sg_obj, SecurityGroupKM.kube_fq_name_key,
            namespace=namespace, name=sg_obj.name, spec=spec_data)
        return

    def _update_sg(self, event, sg):
        sg_obj = SecurityGroup(name=sg.name)
        sg_obj.uuid = sg.uuid
        self._set_sg_annotations(sg_obj, sg, event)
        try:
            self._vnc_lib.security_group_update(sg_obj)
        except Exception as e:
            self.logger.error("Failed to update SG %s" % sg.uuid)
            return None
        return SecurityGroupKM.locate(sg.uuid)

    def _create_sg(self, event, name, uuid=None):
        namespace = event['object']['metadata'].get('namespace')
        proj_fq_name = vnc_kube_config.cluster_project_fq_name(namespace)
        proj_obj = Project(name=proj_fq_name[-1], fq_name=proj_fq_name,
            parent='domain')
        sg_obj = SecurityGroup(name=name, parent_obj=proj_obj)
        if uuid:
            sg_obj.uuid = uuid
        self._set_sg_annotations(sg_obj, None, event)
        try:
            self._vnc_lib.security_group_create(sg_obj)
        except Exception as e:
            self.logger.error("Failed to create SG %s" % uuid)
            return None
        sg = SecurityGroupKM.locate(sg_obj.uuid)
        return sg

    def _check_sg_uuid_change(self, event, uuid):
        namespace = event['object']['metadata'].get('namespace')
        name = event['object']['metadata'].get('name')
        sg_id = self.get_kube_fq_name_to_uuid(SecurityGroupKM, namespace, name)
        if sg_id and sg_id != uuid:
            self.vnc_network_policy_delete(event, sg_uuid)

    def _create_src_sg(self, event, dst_uuid):
        namespace = event['object']['metadata'].get('namespace')
        name = event['object']['metadata'].get('name')
        name = name + '__ingress'
        sg_name = VncCommon.make_name(name, dst_uuid)
        sg_id = self.get_kube_fq_name_to_uuid(SecurityGroupKM, namespace,
            sg_name)
        if not sg_id:
            sg = self._create_sg(event, sg_name)
        else:
            sg = SecurityGroupKM.locate(sg_id)
        return sg

    def _create_dst_sg(self, event):
        name = event['object']['metadata'].get('name')
        uuid = event['object']['metadata'].get('uid')
        sg = SecurityGroupKM.get(uuid)
        if not sg:
            self._check_sg_uuid_change(event, uuid)
            sg_name = VncCommon.make_name(name, uuid)
            sg = self._create_sg(event, sg_name, uuid)
        else:
            sg = self._update_sg(event, sg)
        return sg

    def vnc_network_policy_add(self, event):
        dst_sg = self._create_dst_sg(event)
        src_sg = self._create_src_sg(event, dst_sg.uuid)
        self._set_sg_rule(dst_sg, src_sg)
        self._apply_dst_sg(dst_sg, event)
        self._apply_src_sg(src_sg, event)

    def _delete_sg(self, sg_uuid):
        sg = SecurityGroupKM.get(sg_uuid)
        if not sg:
            return

        for vmi_id in list(sg.virtual_machine_interfaces):
            try:
                self._vnc_lib.ref_update('virtual-machine-interface', vmi_id,
                    'security-group', sg.uuid, None, 'DELETE')
            except Exception as e:
                self.logger.error("Failed to detach SG %s" % str(e))

        try:
            self._vnc_lib.security_group_delete(id=sg.uuid)
        except Exception as e:
            self.logger.error("Failed to delete SG %s %s" % (sg.uuid, str(e)))

    def vnc_network_policy_delete(self, event, sg_uuid):
        self._delete_sg(sg_uuid)
        namespace = event['object']['metadata'].get('namespace')
        name = event['object']['metadata'].get('name')
        name = name + "__ingress"
        src_sg_name = VncCommon.make_name(name, sg_uuid)
        src_sg_uuid = self.get_kube_fq_name_to_uuid(SecurityGroupKM, namespace,
            src_sg_name)
        self._delete_sg(src_sg_uuid)

    def process(self, event):
        event_type = event['type']
        kind = event['object'].get('kind')
        namespace = event['object']['metadata'].get('namespace')
        name = event['object']['metadata'].get('name')
        uid = event['object']['metadata'].get('uid')

        print("%s - Got %s %s %s:%s:%s"
              %(self._name, event_type, kind, namespace, name, uid))
        self.logger.debug("%s - Got %s %s %s:%s:%s"
              %(self._name, event_type, kind, namespace, name, uid))

        if event['object'].get('kind') == 'NetworkPolicy':
            if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
                self.vnc_network_policy_add(event)
            elif event['type'] == 'DELETED':
                self.vnc_network_policy_delete(event, uid)
