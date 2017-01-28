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

    def _append_sg_rule(self, sg_obj, sg_rule):
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

    def _remove_sg_rule_by_id(self, sg_obj, rule_uuid):
        rules = sg_obj.get_security_group_entries()
        if rules is None:
            self.logger.info("No SecurityGroupRule matching rule_uuid %s, no rules found" % rule_uuid)
            return

        for sgr in rules.get_policy_rule() or []:
            if sgr.rule_uuid == rule_uuid:
                self.logger.info("Deleting SecurityGroupRule %s matching rule_uuid %s: " %
                                 sgr.rule_uuid, rule_uuid)
                rules.delete_policy_rule(sgr)
                sg_obj.set_security_group_entries(rules)
                update_needed = True

        if update_needed:
            sg_obj.set_security_group_entries(rules)
        else:
            self.logger.info("No SecurityGroupRule matching rule_uuid %s found" % rule_uuid)

    def _remove_sg_rule_by_src_address(self, sg_obj, src_address):
        rules = sg_obj.get_security_group_entries()
        if rules is None:
            self.logger.error("No SecurityGroupRules matching src_addr %s, no rules found" % src_address)
            return

        for sgr in rules.get_policy_rule() or []:
            if sgr.src_addresses == src_address:
                self.logger.info("Deleting SecurityGroupRule %s matching src_addr %s: " %
                                 sgr.rule_uuid, src_address)
                rules.delete_policy_rule(sgr)
                update_needed = True
        if update_needed:
            sg_obj.set_security_group_entries(rules)
        else:
            self.logger.error("No SecurityGroupRule matching src_addr %s found" % src_address)

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

        sgr_uuid = str(uuid.uuid4())
        src_addr = AddressType(subnet=SubnetType(ip_addr, 32))
        dst_addr = AddressType(security_group='local')
        for port in ports:
            proto = port['protocol'].lower()
            rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                protocol=proto,
                src_addresses=[src_addr],
                src_ports=[PortType(0, 65535)],
                dst_addresses=[dst_addr],
                dst_ports=[PortType(0, int(port['port']))],
                ethertype='IPv4')
            self._append_sg_rule(sg, rule)

    def _set_sg_rules(self, sg, event):
        rules = event['object']['spec']['ingress']
        uuid = event['object']['metadata'].get('uid')
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
                    self._label_cache._locate_label(key, self.policy_src_label_cache, src_label, uuid)
                src_pods = self._select_pods(src_labels)
                for src_pod in src_pods:
                    self._set_sg_rule(sg, src_pod, ports)

        self._vnc_lib.security_group_update(sg)

    def _apply_sg_2_pod(self, sg, pod):
        vm = VirtualMachineKM.get(pod)
        if not vm:
            return

        for vmi_id in vm.virtual_machine_interfaces:
#            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
            if not vmi:
                continue

            vmi.add_security_group(sg)
            self._vnc_lib.virtual_machine_interface_update(vmi)

    def _delete_sg_from_pod(self, sg, pod):
        vm = VirtualMachineKM.get(pod)
        if not vm:
            return

        for vmi_id in vm.virtual_machine_interfaces:
            #            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_id)
            if not vmi:
                continue

            vmi_sg = vmi.get_security_group()
            if vmi_sg['uuid'] == sg['uuid']:
                vmi.delete_security_group(sg)
                self._vnc_lib.virtual_machine_interface_update(vmi)

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
            self._label_cache._locate_label(key, self.policy_dest_label_cache, dest_label, uuid)

        dest_pods = self._select_pods(dest_labels)

        for pod in dest_pods:
            self._apply_sg_2_pod(sg, pod)

    def _sg_create_annotations(self, event):
        kvp = KeyValuePair(key='spec',
            value=json.dumps(event['object']['spec']))
        return KeyValuePairs([kvp])

    def _vnc_create_sg(self, uid, name, namespace, event):
        sg_fq_name = ['default-domain', namespace, name]
        sg_obj = SecurityGroup(name=name, fq_name=sg_fq_name)
        sg_obj.uuid = uid
        kvps = self._sg_create_annotations(event)
        sg_obj.set_annotations(kvps)
        try:
            self._vnc_lib.security_group_create(sg_obj)
        except Exception as e:
            print str(e)
        return self._vnc_lib.security_group_read(id=sg_obj.uuid)
#        return SecurityGroupKM.locate(uid)

    def _sg_create(self, event):
        uuid = event['object']['metadata'].get('uid')
        name = event['object']['metadata'].get('name')
        namespace = event['object']['metadata'].get('namespace')

#        sg = SecurityGroupKM.get(uid)
        try:
            sg = self._vnc_lib.security_group_read(id=uuid)
        except Exception as e:
            sg = self._vnc_create_sg(uuid, name, namespace, event)
        return sg

    def vnc_network_policy_add(self, event):
        sg = self._sg_create(event)
        self._set_sg_rules(sg, event)
        self._apply_sg_2_pods(sg, event)

    def _vnc_sg_clear_vmis(self, sg):
        vmi_ids = sg.get_virtual_machine_interface_back_refs()
        if not vmi_ids:
            return
        for vmi_id in vmi_ids:
#            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            vmi = self._vnc_lib.virtual_machine_interface_read(id=vmi_id['uuid'])
            vmi.del_security_group(sg)
            self._vnc_lib.virtual_machine_interface_update(vmi)

    def vnc_network_policy_delete(self, event):
        uuid = event['object']['metadata'].get('uid')
        sg = self._vnc_lib.security_group_read(id=uuid)
        self._vnc_sg_clear_vmis(sg)
        self._vnc_lib.security_group_delete(id=uuid)

    def _get_rules_from_annotations(self, annotations):
        if not annotations:
            return
        for kvp in annotations.key_value_pair or []:
            if kvp.get_key() == 'spec':
                spec = kvp.get_value()
                if not spec:
                    return None
                specjson = json.loads(spec)
                return specjson['ingress']

    def _add_src_pod_2_policy(self, pod_id, policy_id):
        try:
            sg = self._vnc_lib.security_group_read(id=policy_id)
        except Exception as e:
            self.logger.error("Cannot read security group with UUID " + id)

        rules = self._get_rules_from_annotations(sg.get_annotations())
        for rule in rules:
            ports = rule.get('ports')
            self._set_sg_rule(sg, pod_id, ports)

    def _add_dest_pod_2_policy(self, pod_id, policy_id):
        try:
            sg = self._vnc_lib.security_group_read(id=policy_id)
        except Exception as e:
            self.logger.error("Cannot read security group with UUID " + id)
        self._apply_sg_2_pod(sg, pod_id)

    def vnc_pod_add(self, event):
        labels = event['object']['metadata'].get('labels', {})
        pod_id = event['object']['metadata']['uid']

        for label in labels.items():
            key = self._label_cache._get_key(label)

            policy_ids = self.policy_src_label_cache.get(key, [])
            for policy_id in policy_ids:
                self._add_src_pod_2_policy(pod_id, policy_id)

            policy_ids = self.policy_dest_label_cache.get(key, [])
            for policy_id in policy_ids:
                self._add_dest_pod_2_policy(pod_id, policy_id)

    def _delete_src_pod_from_policy(self, pod_id, policy_id):
        try:
            sg = self._vnc_lib.security_group_read(id=policy_id)
        except Exception as e:
            self.logger.error("Cannot read security group with UUID " + id)

        vm = VirtualMachineKM.get(pod_id)
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

        sgr_uuid = str(uuid.uuid4())
        src_addr = AddressType(subnet=SubnetType(ip_addr, 32))
        dst_addr = AddressType(security_group='local')
        for port in ports:
            proto = port['protocol'].lower()
            rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                                  protocol=proto,
                                  src_addresses=[src_addr],
                                  src_ports=[PortType(0, 65535)],
                                  dst_addresses=[dst_addr],
                                  dst_ports=[PortType(0, int(port['port']))],
                                  ethertype='IPv4')
            self._append_sg_rule(sg, rule)



    def _delete_dest_pod_from_policy(self, pod_id, policy_id):
        try:
            sg = self._vnc_lib.security_group_read(id=policy_id)
        except Exception as e:
            self.logger.error("Cannot read security group with UUID " + id)
        self._delete_sg_from_pod(sg, pod_id)


    def vnc_pod_delete(self, event):
        labels = event['object']['metadata']['labels']
        pod_id = event['object']['metadata']['uid']

        for label in labels.items():
            key = self._label_cache._get_key(label)

            policy_ids = self.policy_src_label_cache.get(key, [])
            for policy_id in policy_ids:
                self._delete_src_pod_from_policy(pod_id, policy_id)

            policy_ids = self.policy_dest_label_cache.get(key, [])
            for policy_id in policy_ids:
                self._delete_dest_pod_from_policy(pod_id, policy_id)

    def process(self, event):
        if event['object'].get('kind') == 'NetworkPolicy':
            if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
                self.vnc_network_policy_add(event)
            elif event['type'] == 'DELETED':
                self.vnc_network_policy_delete(event)
        if event['object'].get('kind') == 'Pod':
            if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
                self.vnc_pod_add(event)
            elif event['type'] == 'DELETED':
                self.vnc_pod_delete(event)

    def _select_pods(self, labels):
        result = set()
        for label in labels.items():
            key = self._label_cache._get_key(label)
            pod_ids = self._label_cache.pod_label_cache.get(key)
            if not pod_ids:
                continue
            result.update(pod_ids)
        return result
