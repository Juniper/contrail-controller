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

    def _set_sg_rule(self, sg, pod_id, ports):
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
        dst_addr = AddressType(subnet=SubnetType(ip_addr, 32))
        src_addr = AddressType(security_group='local')
        for port in ports:
            proto = port['protocol'].lower()
            rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                protocol=proto,
                src_addresses=[src_addr],
                src_ports=[PortType(0, 65535)],
                dst_addresses=[dst_addr],
                dst_ports=[PortType(0, int(port['port']))],
                ethertype='IPv4')

    def _set_sg_rules(self, sg):
        if not sg.annotations:
            return

        ingress = None
        for kvp in sg.annotations.get('key_value_pair', []):
            if kvp['key'] == 'ingress':
                ingress = kvp['value']
                break
        if not ingress:
            return

        sources = json.loads(kvp['value'])
        for source in sources:
            ports = source.get('ports', [])
            for selectors in source.get('from', []):
                labels = selectors['namespaceSelector']['matchLabels']
                pod_ids = set()
                for label in labels.items():
                    key = self._label_cache._get_key(label)
                    pod_ids = pod_ids.union(
                        self._label_cache.pod_label_cache.get(key, []))
            for pod_id in pod_ids:
                self._set_sg_rule(sg, pod_id, ports)

    def _apply_sg(self, sg):
        pass

    def _sg_create_annotations(self, event):
        spec = event['object']['spec']
        kvp1 = KeyValuePair(key='ingress',
            value=json.dumps(spec['ingress']))
        kvp2 = KeyValuePair(key='podSelector',
            value=json.dumps(spec['podSelector']))
        return KeyValuePairs([kvp1, kvp2])

    def _vnc_create_sg(self, uid, namespace, event):
        sg_fq_name = ['default-domain', namespace, name]
        sg_obj = SecurityGroup(name=name, fq_name=sg_fq_name)
        sg_obj.uuid = uid
        kvps = self._sg_create_annotations(event)
        sg_obj.set_annotations(kvps)
        try:
            self._vnc_lib.security_group_create(sg_obj)
        except Exception as e:
            print str(e)
        return SecurityGroupKM.locate(uid)

    def _sg_create(self, uid, name, namespace, event):
        sg = SecurityGroupKM.get(uid)
        if not sg:
            sg = self._vnc_create_sg(uid, name, namespace, event)
       
        self._set_sg_rules(sg)
        self._apply_sg(sg)

    def vnc_network_policy_add(self, uid, name, namespace, event):
        self._sg_create(uid, name, namespace, event)

    def vnc_network_policy_delete(self, uid, name):
        pass

    def process(self, event):
        uid = event['object']['metadata'].get('uid')
        name = event['object']['metadata'].get('name')
        namespace = event['object']['metadata'].get('namespace')

        if event['type'] == 'ADDED' or event['type'] == 'MODIFIED':
            self.vnc_network_policy_add(uid, name, namespace, event)
        elif event['type'] == 'DELETED':
            self.vnc_network_policy_delete(uid, name)
