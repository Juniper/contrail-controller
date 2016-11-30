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

    def __init__(self, vnc_lib=None, label_cache=None):
        self._vnc_lib = vnc_lib
        self._label_cache = label_cache

    def _append_sg_rule(self, sg_obj, sg_rule):
        rules = sg_obj.get_security_group_entries()
        if rules is None:
            rules = PolicyEntriesType([sg_rule])
        else:
            for sgr in rules.get_policy_rule() or []:
                sgr_copy = copy.copy(sgr)
                sgr_copy.rule_uuid = sg_rule.rule_uuid
                if sg_rule == sgr_copy:
                    raise Exception('SecurityGroupRuleExists %s' % sgr.rule_uuid)
            rules.add_policy_rule(sg_rule)

        sg_obj.set_security_group_entries(rules)

    def _remove_sg_rule(self, sg_obj, sg_rule):
        rules = sg_obj.get_security_group_entries()
        if rules is None:
            raise Exception('SecurityGroupRuleNotExists %s' % sg_rule.rule_uuid)
        else:
            for sgr in rules.get_policy_rule() or []:
                if sgr.rule_uuid == sg_rule.rule_uuid:
                    rules.delete_policy_rule(sgr)
                    sg_obj.set_security_group_entries(rules)
                    return
            raise Exception('SecurityGroupRuleNotExists %s' % sg_rule.rule_uuid)

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
        for rule in rules:
            ports = rule['ports']
            src_selectors = rule['from']
            for src_selector in src_selectors:
                podSelector = src_selector.get('podSelector', None)
                if not podSelector:
                    continue

                src_labels = podSelector['matchLabels']
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

    def _apply_sg_2_pods(self, sg, event):
        podSelector = event['object']['spec'].get('podSelector', None)
        if not podSelector:
            return

        labels = podSelector['matchLabels']
        if not labels:
            return
        dest_pods = self._select_pods(labels)

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

    def process(self, event):

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            self.vnc_network_policy_add(event)
        elif event['type'] == 'DELETED':
            self.vnc_network_policy_delete(event)

    def _select_pods(self, labels):
        result = set()
        for label in labels.items():
            key = self._label_cache._get_key(label)
            pod_ids = self._label_cache.pod_label_cache.get(key)
            if not pod_ids:
                continue
            result.update(pod_ids)
        return result
