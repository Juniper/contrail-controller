#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

import uuid

from vnc_api.vnc_api import *
from config_db import *


class VncNetworkPolicy(object):

    def __init__(self, vnc_lib=None, label_cache=None, logger=None):
        self._vnc_lib = vnc_lib
        self._label_cache = label_cache
        self.logger = logger
        self.logger.info("VncNetworkPolicy init done.")
        self.policy_src_label_cache = {}
        self.policy_dest_label_cache = {}

    def _select_pods(self, labels):
        result = set()
        for label in labels.items():
            key = self._label_cache._get_key(label)
            pod_ids = self._label_cache.pod_label_cache.get(key)
            if not pod_ids:
                continue
            result.update(pod_ids)
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

    def _get_src_labels_from_annotations(self, annotations):
        if not annotations:
            return
        for kvp in annotations.get('key_value_pair', []):
            if kvp.get('key') != 'spec':
                continue
            specjson = json.loads(kvp.get('value'))
            return specjson['ingress']

    def _get_dst_labels_from_annotations(self, annotations):
        if not annotations:
            return
        for kvp in annotations.get('key_value_pair', []):
            if kvp.get('key') != 'spec':
                continue
            specjson = json.loads(kvp.get('value'))
            return specjson['podSelector']['matchLabels']

    def _add_src_pod_2_policy(self, pod_id, policy_id):
        sg = SecurityGroupKM.get(policy_id)
        if not sg:
            return

        rules = self._get_src_labels_from_annotations(sg.annotations)
        for rule in rules:
            ports = rule.get('ports')
            self._set_sg_rule(sg, pod_id, ports)

    def vnc_pod_add(self, event):
        labels = event['object']['metadata']['labels']
        pod_id = event['object']['metadata']['uid']

        for label in labels.items():
            key = self._label_cache._get_key(label)

            policy_ids = self.policy_src_label_cache.get(key, [])
            for policy_id in policy_ids:
                self._add_src_pod_2_policy(pod_id, policy_id)

            policy_ids = self.policy_dest_label_cache.get(key, [])
            for policy_id in policy_ids:
                self._sg_2_pod_link(pod_id, policy_id, 'ADD')

    def _get_label_diff(self, old_labels, new_labels):
        if old_labels == new_labels:
            return None

        diff = dict()
        added = {}
        removed = {}
        changed = {}
        keys = set(old_labels.keys()) | set(new_labels.keys())
        for k in keys:
            if k not in old_labels.keys():
                added[k] = new_labels[k]
                continue
            if k not in new_labels.keys():
                removed[k] = old_labels[k]
                continue
            if old_labels[k] == new_labels[k]:
                continue
            changed[k] = old_labels[k]

        diff['added'] = added
        diff['removed'] = removed
        diff['changed'] = changed
        return diff

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
            if not sg:
                continue
            dst_labels = self._get_dst_labels_from_annotations(sg.annotations)
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
            if not sg:
                continue
            dst_labels = self._get_dst_labels_from_annotations(sg.annotations)
            if set(changed_labels.keys()).intersection(set(dst_labels.keys())):
                self._sg_2_pod_link(vm.uuid, sg_id, 'DELETE')

        # check if rule has to be deleted
        for label in changed_labels.items():
            key = self._label_cache._get_key(label)

            policy_ids = self.policy_src_label_cache.get(key, [])
            for policy_id in policy_ids:
                self._delete_sg_rule(policy_id, vm.uuid)

    def vnc_pod_update(self, event):
        labels = event['object']['metadata']['labels']
        pod_id = event['object']['metadata']['uid']
        vm = VirtualMachineKM.get(pod_id)
        if not vm:
            return
        diff = self._get_label_diff(vm.pod_labels, labels)
        if not diff:
            return

        self._check_deleted_labels(diff['removed'], vm)
        self._check_changed_labels(diff['changed'], vm)
        self.vnc_pod_add(event)

    def vnc_pod_delete(self, event):
        labels = event['object']['metadata']['labels']
        pod_id = event['object']['metadata']['uid']

        for label in labels.items():
            key = self._label_cache._get_key(label)

            policy_ids = self.policy_src_label_cache.get(key, [])
            for policy_id in policy_ids:
                self._delete_sg_rule(policy_id, pod_id)

    def _set_sg_rule(self, sg, src_pod, ports):
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
        for port in ports:
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
        update = False
        rules = event['object']['spec']['ingress']
        for rule in rules:
            ports = rule['ports']
            src_selectors = rule['from']
            for src_selector in src_selectors:
                podSelector = src_selector.get('podSelector', None)
                if not podSelector:
                    continue

                src_labels = podSelector['matchLabels']
                for src_label in src_labels.items():
                    key = self._label_cache._get_key(src_label)
                    self._label_cache._locate_label(key,
                        self.policy_src_label_cache, src_label, sg.uuid)
                src_pods = self._select_pods(src_labels)
                for src_pod in src_pods:
                    self._set_sg_rule(sg, src_pod, ports)

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
        podSelector = event['object']['spec'].get('podSelector', None)
        if not podSelector:
            return

        dest_labels = podSelector['matchLabels']
        if not dest_labels:
            return

        uuid = event['object']['metadata'].get('uid')
        for dest_label in dest_labels.items():
            key = self._label_cache._get_key(dest_label)
            self._label_cache._locate_label(key, self.policy_dest_label_cache,
                dest_label, uuid)

        pod_ids = self._select_pods(dest_labels)
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
        if event['object'].get('kind') == 'NetworkPolicy':
            if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
                self.vnc_network_policy_add(event)
            elif event['type'] == 'DELETED':
                self.vnc_network_policy_delete(event)
