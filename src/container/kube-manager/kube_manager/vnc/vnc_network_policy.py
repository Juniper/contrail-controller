#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
VNC service management for kubernetes
"""

import uuid

from vnc_api.vnc_api import *
from config_db import *
from kube_manager.common.kube_config_db import NamespaceKM
from kube_manager.common.kube_config_db import NetworkPolicyKM
from vnc_kubernetes_config import VncKubernetesConfig as vnc_kube_config
from vnc_common import VncCommon

class VncNetworkPolicy(VncCommon):

    def __init__(self):
        super(VncNetworkPolicy,self).__init__('NetworkPolicy')
        self._name = type(self).__name__
        self._queue = vnc_kube_config.queue()
        self._ingress_ns_label_cache = {}
        self._ingress_pod_label_cache = {}
        self._np_pod_label_cache = {}
        self._default_ns_sgs = {}
        self._vnc_lib = vnc_kube_config.vnc_lib()
        self._label_cache = vnc_kube_config.label_cache()
        self._build_np_cache()
        self.logger = vnc_kube_config.logger()
        self.logger.info("VncNetworkPolicy init done.")

    def _build_np_cache(self):
        ns_uuid_set = set(NamespaceKM.keys())
        ns_sg_name_set = set()
        for ns_uuid in ns_uuid_set or []:
            ns = NamespaceKM.get(ns_uuid)
            if not ns:
                continue
            ns_name = ns.name
            ns_sg = "-".join(
                [vnc_kube_config.cluster_name(), ns_name, 'sg'])
            ns_sg_name_set.add(ns_sg)
            default_sg = "-".join(
                [vnc_kube_config.cluster_name(), ns_name, 'default'])
            ns_sg_name_set.add(default_sg)
            self._default_ns_sgs[ns_name] = {}
        sg_uuid_set = set(SecurityGroupKM.keys())
        for sg_uuid in sg_uuid_set or []:
            sg = SecurityGroupKM.get(sg_uuid)
            if not sg or not sg.namespace:
                continue
            if sg.name in ns_sg_name_set:
                sg_dict = {}
                sg_dict[sg.name] = sg_uuid
                self._default_ns_sgs[sg.namespace].update(sg_dict)
            elif sg.np_pod_selector:
                self._update_sg_cache(self._np_pod_label_cache,
                            sg.np_pod_selector, sg.uuid)
            elif sg.ingress_pod_selector:
                self._update_sg_cache(self._ingress_pod_label_cache,
                            sg.ingress_pod_selector, sg.uuid)
            if sg.np_spec:
                #_get_ingress_rule_list update _ingress_ns_label_cache
                self._get_ingress_rule_list(sg.np_spec,
                            sg.namespace, sg.name, sg.uuid)

    def _get_ns_allow_all_label(self):
        label = {'NS-SG':'ALLOW-ALL'}
        return label

    def _get_namespace_label(self, namespace):
        label = {'namespace': namespace}
        return label

    def _find_namespaces(self, labels, ns_set=None):
        result = set()
        for label in labels.items():
            key = self._label_cache._get_key(label)
            ns_ids = self._label_cache.ns_label_cache.get(key, set())
            #no matching label
            if not ns_ids:
                return ns_ids
            if not result:
                result = ns_ids.copy()
            else:
                result.intersection_update(ns_ids)

        if ns_set:
            result.intersection_update(ns_set)
        return result

    def _find_pods(self, labels, pod_set=None):
        result = set()
        for label in labels.items():
            key = self._label_cache._get_key(label)
            pod_ids = self._label_cache.pod_label_cache.get(key, set())
            #no matching label
            if not pod_ids:
                return pod_ids
            if not result:
                result = pod_ids.copy()
            else:
                result.intersection_update(pod_ids)

        if pod_set:
            result.intersection_update(pod_set)
        return result

    def _find_sg(self, sg_cache, labels):
        result = set()
        for label in labels.items():
            key = self._label_cache._get_key(label)
            sg_ids = sg_cache.get(key, set())
            #no matching label
            if not sg_ids:
                continue
            if not result:
                result = sg_ids.copy()
            else:
                result.update(sg_ids)
        return result

    def _clear_sg_cache_uuid(self, sg_cache, sg_uuid):
        if not sg_uuid:
            return
        key_list = [k for k,v in sg_cache.items() if sg_uuid in v]
        for key in key_list or []:
            label = tuple(key.split(':'))
            self._label_cache._remove_label(key, sg_cache, label, sg_uuid)

    def _clear_sg_cache(self, sg_cache, labels, sg_uuid):
        if not labels or not sg_uuid:
            return
        for label in labels.items() or []:
            key = self._label_cache._get_key(label)
            self._label_cache._remove_label(key, sg_cache, label, sg_uuid)

    def _update_sg_cache(self, sg_cache, labels, sg_uuid):
        if not labels or not sg_uuid:
            return
        for label in labels.items() or []:
            key = self._label_cache._get_key(label)
            self._label_cache._locate_label(key, sg_cache, label, sg_uuid)

    def _set_sg_annotations(self, namespace, name, sg_obj, **kwargs):
        self.add_annotations(sg_obj, SecurityGroupKM.kube_fq_name_key,
            namespace=namespace, name=sg_obj.name, **kwargs)
        return

    def _vnc_create_sg(self, np_spec, namespace, name,
            uuid=None, **kwargs_annotations):
        proj_fq_name = vnc_kube_config.cluster_project_fq_name(namespace)
        proj_obj = Project(name=proj_fq_name[-1], fq_name=proj_fq_name,
            parent='domain')
        sg_obj = SecurityGroup(name=name, parent_obj=proj_obj)
        if uuid:
            sg_obj.uuid = uuid
        if np_spec:
            kwargs_annotations.update({'np_spec': json.dumps(np_spec)})
        self._set_sg_annotations(namespace, name,
            sg_obj, **kwargs_annotations)
        try:
            self._vnc_lib.security_group_create(sg_obj)
        except Exception as e:
            self.logger.error("Failed to create SG %s" % uuid)
            return None
        sg = SecurityGroupKM.locate(sg_obj.uuid)
        return sg

    def _create_ingress_sg(self, namespace, sg_name, ingress_pod_selector):
        sg = self._vnc_create_sg(None, namespace,
                sg_name, ingress_pod_selector=ingress_pod_selector)
        return sg

    def _create_np_sg(self, spec, namespace, name, uuid, np_pod_selector):
        sg_name = VncCommon.make_name(name, uuid)
        sg = self._vnc_create_sg(spec, namespace,
                sg_name, uuid, np_pod_selector=np_pod_selector)
        return sg

    def _get_rule_list(self, address_list, port_list, ingress=True):
        rule_list = []
        if ingress:
            target_address = 'src_address'
            target_port = 'dst_port'
        else:
            target_address = 'dst_address'
            target_port = 'src_port'
        for address in address_list or []:
            for port in port_list or []:
                rule = {}
                rule[target_address] = address
                rule[target_port] = port
                rule_list.append(rule)
        return rule_list

    def _get_ns_address(self, ns_name):
        address = {}
        ns_sg = "-".join([vnc_kube_config.cluster_name(), ns_name, 'sg'])
        address['security_group'] = ns_sg
        return address

    def _get_ns_address_list(self, np_sg_uuid, labels=None):
        address_list = []
        if not labels:
            ns_uuid_list = NamespaceKM.keys()
            labels = self._get_ns_allow_all_label()
        else:
            ns_uuid_set = self._find_namespaces(labels)
            ns_uuid_list = list(ns_uuid_set)
        for ns_uuid in ns_uuid_list or []:
            address = {}
            ns = NamespaceKM.get(ns_uuid)
            if not ns:
                continue
            ns_sg = "-".join([vnc_kube_config.cluster_name(), ns.name, 'sg'])
            address['security_group'] = ns_sg
            address['ns_selector'] = labels
            if ns_sg in self._default_ns_sgs[ns.name]:
                address['ns_sg_uuid'] = self._default_ns_sgs[ns.name][ns_sg]
                address_list.append(address)
        for label in labels.items():
            key = self._label_cache._get_key(label)
            self._label_cache._locate_label(key,
                    self._ingress_ns_label_cache, label, np_sg_uuid)
        return address_list

    def _get_ports(self, port_info=None):
        port = {}
        if not port_info:
            port['start_port'] = 0
            port['end_port'] = 65535
            port['protocol'] = 'any'
            return port
        if 'port' in port_info:
            port['start_port'] = port_info['port']
            port['end_port'] = port_info['port']
        else:
            port['start_port'] = 0
            port['end_port'] = 65535
        if 'protocol' in port_info:
            port['protocol'] = port_info['protocol']
        else:
            port['protocol'] = 'TCP'
        return port

    def _get_ingress_rule_list(self, spec, namespace, np_sg_name, np_sg_uuid):
        ingress_rule_list = []
        ingress_acl_rules = spec.get('ingress')
        if not ingress_acl_rules or not len(ingress_acl_rules):
            return ingress_rule_list
        ingress_pod_sg_index = 0
        for ingress_acl_rule in ingress_acl_rules:
            dst_port_list = []
            src_address_list = []
            ports = ingress_acl_rule.get('ports')
            if not ports:
                ports = []
                dst_port = self._get_ports()
                dst_port_list.append(dst_port)
            for port in ports:
                dst_port = self._get_ports(port)
                dst_port_list.append(dst_port)
            from_rules = ingress_acl_rule.get('from')
            if not from_rules:
                from_rules = []
                # allow-all-ns-sg
                ns_address_list = self._get_ns_address_list(np_sg_uuid)
                src_address_list.extend(ns_address_list)
                # allow-all-pods
                src_address = self._get_ns_address(namespace)
                src_address_list.append(src_address)
            for from_rule in from_rules:
                src_address = {}
                if 'namespaceSelector' in from_rule:
                    ns_address_list = []
                    ns_selector = from_rule.get('namespaceSelector')
                    ns_selector_labels = ns_selector.get('matchLabels')
                    if not ns_selector_labels:
                        ns_address_list = self._get_ns_address_list(np_sg_uuid)
                    else:
                        ns_address_list = \
                            self._get_ns_address_list(np_sg_uuid, ns_selector_labels)
                    if len(ns_address_list):
                        src_address_list.extend(ns_address_list)
                if 'podSelector' in from_rule:
                    pod_selector = from_rule.get('podSelector')
                    pod_selector_labels = pod_selector.get('matchLabels')
                    if not pod_selector_labels:
                        # allow-all-pods
                        src_address = self._get_ns_address(namespace)
                    else:
                        ingress_pod_sg_index += 1
                        src_sg_name = VncCommon.make_name(np_sg_name,
                                    'ingress', ingress_pod_sg_index)
                        src_address['pod_selector'] = pod_selector_labels
                        src_address['src_sg_name'] = src_sg_name
                    src_address_list.append(src_address)
            rule_list = self._get_rule_list(src_address_list, dst_port_list)
            ingress_rule_list.extend(rule_list)
        return ingress_rule_list

    def _get_ingress_sg_rule(self, src_sg_fq_name, dst_port):
        sgr_uuid = 1
        src_addr = AddressType(security_group=':'.join(src_sg_fq_name))
        dst_addr = AddressType(security_group='local')
        proto = dst_port['protocol'].lower()
        rule = PolicyRuleType(rule_uuid=sgr_uuid, direction='>',
                protocol=proto,
                src_addresses=[src_addr],
                src_ports=[PortType(0, 65535)],
                dst_addresses=[dst_addr],
                dst_ports=[PortType(int(dst_port['start_port']),
                    int(dst_port['end_port']))],
                ethertype='IPv4')
        return rule

    def _update_sg_pod_link(self, namespace, pod_id,
            sg_id, oper, validate_vm=True, validate_sg=False):
        vm = VirtualMachineKM.get(pod_id)
        if not vm or vm.owner != 'k8s':
            return

        if validate_vm and vm.pod_namespace != namespace:
            return

        if validate_sg:
            sg = SecurityGroupKM.get(sg_id)
            if not sg or sg.namespace != namespace:
                return
            match_found = False
            sg_labels = sg.np_pod_selector.copy()
            sg_labels.update(sg.ingress_pod_selector)
            if set(sg_labels.items()).issubset(set(vm.pod_labels.items())):
                match_found = True
            if oper == 'ADD' and not match_found:
                return
            elif oper == 'DELETE' and match_found:
                return

        for vmi_id in vm.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            if not vmi:
                return
            try:
                self._vnc_lib.ref_update('virtual-machine-interface',
                    vmi_id, 'security-group', sg_id, None, oper)
            except RefsExistError:
                self.logger.error("%s -  %s SG Exists %s for pod %s" %
                            (self._name, sg_id, pod_id))
            except Exception as e:
                self.logger.error("%s - Failed to %s SG %s to pod %s" %
                            (self._name, oper, pod_id, sg_id))

    def _update_rule_uuid(self, sg_rule_set):
        for sg_rule in sg_rule_set or []:
            sg_rule.rule_uuid = str(uuid.uuid4())

    def _update_np_sg(self, namespace, sg, sg_rule_set, **annotations):
        sg_obj = self._vnc_lib.security_group_read(id=sg.uuid)
        if sg_rule_set:
            rules = PolicyEntriesType(list(sg_rule_set))
            sg_obj.set_security_group_entries(rules)
        self._set_sg_annotations(namespace, sg.name,
            sg_obj, **annotations)
        self._vnc_lib.security_group_update(sg_obj)

    def _update_ns_sg(self, ns_sg_uuid, np_sg_uuid, oper):
        ns_sg = SecurityGroupKM.get(ns_sg_uuid)
        if not ns_sg:
            return
        match_found = False
        if np_sg_uuid in ns_sg.np_sgs:
            match_found = True
        if oper == 'ADD' and not match_found:
            ns_sg.np_sgs.add(np_sg_uuid)
        elif oper == 'DELETE' and match_found:
            ns_sg.np_sgs.remove(np_sg_uuid)
        else:
            return
        sg_obj = self._vnc_lib.security_group_read(id=ns_sg.uuid)
        annotations = {}
        annotations['np_sgs'] = json.dumps(list(ns_sg.np_sgs))
        self._set_sg_annotations(ns_sg.namespace, ns_sg.name, sg_obj, **annotations)
        self._vnc_lib.security_group_update(sg_obj)

    def _get_ingress_sg_rule_list(self, namespace, name,
            ingress_rule_list, ingress_pod_sg_create=True):
        ingress_pod_sgs = set()
        ingress_ns_sgs = set()
        ingress_sg_rule_list = []
        ingress_pod_sg_dict = {}
        ingress_pod_sg_index = 0
        for ingress_rule in ingress_rule_list or []:
            proj_fq_name = vnc_kube_config.cluster_project_fq_name(namespace)
            src_sg_fq_name = proj_fq_name[:]
            dst_port = ingress_rule['dst_port']
            src_address = ingress_rule['src_address']
            if 'pod_selector' in src_address:
                pod_sg_created = False
                src_sg_name = src_address['src_sg_name']
                pod_selector = src_address['pod_selector']
                if src_sg_name in ingress_pod_sg_dict:
                    pod_sg_created = True
                if ingress_pod_sg_create and not pod_sg_created:
                    pod_sg = self._create_ingress_sg(
                            namespace, src_sg_name, json.dumps(pod_selector))
                    if not pod_sg:
                        continue
                    ingress_pod_sg_dict[src_sg_name] = pod_sg.uuid
                    pod_sg.ingress_pod_selector = pod_selector
                    ingress_pod_sgs.add(pod_sg.uuid)
                    self._update_sg_cache(self._ingress_pod_label_cache,
                            pod_selector, pod_sg.uuid)
                    pod_ids = self._find_pods(pod_selector)
                    for pod_id in pod_ids:
                        self._update_sg_pod_link(namespace,
                            pod_id, pod_sg.uuid, 'ADD', validate_vm=True)
            else:
                src_sg_name = src_address['security_group']
                if 'ns_selector' in src_address:
                    ns_sg_uuid = src_address['ns_sg_uuid']
                    ingress_ns_sgs.add(ns_sg_uuid)
            src_sg_fq_name.append(src_sg_name)
            ingress_sg_rule = self._get_ingress_sg_rule(
                    src_sg_fq_name, dst_port)
            ingress_sg_rule_list.append(ingress_sg_rule)
        return ingress_sg_rule_list, ingress_pod_sgs, ingress_ns_sgs

    def update_pod_np(self, pod_namespace, pod_id, labels):
        vm = VirtualMachineKM.get(pod_id)
        if not vm or vm.owner != 'k8s':
            return

        namespace_label = self._get_namespace_label(pod_namespace)
        np_sg_uuid_set = self._find_sg(
                self._np_pod_label_cache, namespace_label)
        np_sg_uuid_set = np_sg_uuid_set | \
                self._find_sg(self._np_pod_label_cache, labels)
        ingress_sg_uuid_set = self._find_sg(
                self._ingress_pod_label_cache, labels)
        new_sg_uuid_set  = np_sg_uuid_set | ingress_sg_uuid_set

        vmi_sg_uuid_set = []
        for vmi_id in vm.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceKM.get(vmi_id)
            if not vmi:
                continue
            vmi_sg_uuid_set = vmi.security_groups
            default_ns_sgs = set()
            for sg_name in self._default_ns_sgs[pod_namespace].keys() or []:
                sg_uuid = self._default_ns_sgs[pod_namespace][sg_name]
                default_ns_sgs.add(sg_uuid)
            vmi_sg_uuid_set = vmi_sg_uuid_set - default_ns_sgs
        old_sg_uuid_set = vmi_sg_uuid_set

        removed_sg_uuid_set = old_sg_uuid_set
        for sg_uuid in removed_sg_uuid_set or []:
            self._update_sg_pod_link(pod_namespace,
                    pod_id, sg_uuid, 'DELETE', validate_sg=True)
        added_sg_uuid_set = new_sg_uuid_set - old_sg_uuid_set
        for sg_uuid in added_sg_uuid_set or []:
            self._update_sg_pod_link(pod_namespace,
                    pod_id, sg_uuid, 'ADD', validate_sg=True)

    def update_ns_np(self, ns_name, ns_id, labels, sg_dict):
        self._default_ns_sgs[ns_name] = sg_dict
        ns_sg_name = "-".join(
            [vnc_kube_config.cluster_name(), ns_name, 'sg'])
        for sg_name in sg_dict.keys() or []:
            if sg_name == ns_sg_name:
                break
        sg_uuid = sg_dict[sg_name]
        ns_sg = SecurityGroupKM.get(sg_uuid)
        if not ns_sg:
            return
        np_sgs = list(ns_sg.np_sgs)
        for np_sg in np_sgs[:] or []:
            self._update_ns_sg(sg_uuid, np_sg, 'DELETE')

        ns_allow_all_label = self._get_ns_allow_all_label()
        ingress_ns_allow_all_sg_set = self._find_sg(
                self._ingress_ns_label_cache, ns_allow_all_label)
        ingress_ns_sg_uuid_set = self._find_sg(
                self._ingress_ns_label_cache, labels)
        sg_uuid_set = set(np_sgs) | \
                ingress_ns_allow_all_sg_set | ingress_ns_sg_uuid_set

        for sg_uuid in sg_uuid_set or []:
            np_sg = SecurityGroupKM.get(sg_uuid)
            if not np_sg or not np_sg.np_spec or not np_sg.namespace:
                continue
            ingress_rule_list = \
                self._get_ingress_rule_list(
                        np_sg.np_spec, np_sg.namespace, np_sg.name, np_sg.uuid)
            ingress_sg_rule_list, ingress_pod_sgs, \
                ingress_ns_sgs = self._get_ingress_sg_rule_list(
                        np_sg.namespace, np_sg.name, ingress_rule_list, False)
            for ns_sg in ingress_ns_sgs or []:
                self._update_ns_sg(ns_sg, np_sg.uuid, 'ADD')
            annotations = {}
            annotations['ingress_ns_sgs'] = json.dumps(list(ingress_ns_sgs))
            ingress_sg_rule_set = set(ingress_sg_rule_list)
            self._update_rule_uuid(ingress_sg_rule_set)
            self._update_np_sg(np_sg.namespace, np_sg,
                    ingress_sg_rule_set, **annotations)

    def _get_np_pod_selector(self, spec, namespace):
        pod_selector = spec.get('podSelector')
        if not pod_selector:
            return
        if not 'matchLabels' in pod_selector:
            namespace_label = self._get_namespace_label(namespace)
        else:
            labels = pod_selector.get('matchLabels')
        return labels

    def vnc_network_policy_add(self, event, namespace, name, uid):
        spec = event['object']['spec']
        if not spec:
            return

        np_sg = SecurityGroupKM.get(uid)
        if np_sg:
            return

        np_pod_selector = self._get_np_pod_selector(spec, namespace)
        if not np_pod_selector:
            return

        np_sg = self._create_np_sg(spec, namespace,
                name, uid, json.dumps(np_pod_selector))
        if not np_sg:
            return
        np_sg.np_pod_selector = np_pod_selector
        self._update_sg_cache(self._np_pod_label_cache,
                np_pod_selector, np_sg.uuid)

        ingress_rule_list = \
            self._get_ingress_rule_list(spec, namespace, np_sg.name, np_sg.uuid)

        ingress_sg_rule_list, ingress_pod_sgs, ingress_ns_sgs = \
            self._get_ingress_sg_rule_list(namespace, np_sg.name, ingress_rule_list)

        np_sg.ingress_pod_sgs = ingress_pod_sgs
        np_sg.ingress_ns_sgs = ingress_ns_sgs
        for ns_sg in ingress_ns_sgs or []:
            self._update_ns_sg(ns_sg, np_sg.uuid, 'ADD')

        annotations = {}
        annotations['np_pod_selector'] = json.dumps(np_pod_selector)
        annotations['ingress_pod_sgs'] = json.dumps(list(ingress_pod_sgs))
        annotations['ingress_ns_sgs'] = json.dumps(list(ingress_ns_sgs))
        ingress_sg_rule_set = set(ingress_sg_rule_list)
        self._update_rule_uuid(ingress_sg_rule_set)
        self._update_np_sg(namespace, np_sg,
                ingress_sg_rule_set, **annotations)
        np_pod_ids = self._find_pods(np_pod_selector)
        for np_pod_id in np_pod_ids or []:
            self._update_sg_pod_link(namespace, np_pod_id,
                    np_sg.uuid, 'ADD', validate_vm=True)

    def _vnc_delete_sg(self, sg):
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

    def vnc_network_policy_delete(self, namespace, name, uuid):
        np_sg = SecurityGroupKM.get(uuid)
        if not np_sg:
            return
        ingress_ns_sgs = np_sg.ingress_ns_sgs
        for ns_sg in ingress_ns_sgs or []:
            self._update_ns_sg(ns_sg, np_sg.uuid, 'DELETE')
        ingress_pod_sgs = np_sg.ingress_pod_sgs
        for ingress_pod_sg in ingress_pod_sgs or []:
            pod_sg = SecurityGroupKM.get(ingress_pod_sg)
            if not pod_sg:
                continue
            ingress_pod_selector = pod_sg.ingress_pod_selector
            if ingress_pod_selector:
                self._clear_sg_cache(self._ingress_pod_label_cache,
                        ingress_pod_selector, ingress_pod_sg)
            self._vnc_delete_sg(pod_sg)
        self._clear_sg_cache_uuid(self._ingress_ns_label_cache, np_sg.uuid)
        np_pod_selector = np_sg.np_pod_selector
        if np_pod_selector:
            self._clear_sg_cache(self._np_pod_label_cache,
                        np_pod_selector, np_sg.uuid)
        self._vnc_delete_sg(np_sg)

    def _create_network_policy_event(self, event_type, np_id):
        event = {}
        object = {}
        object['kind'] = 'NetworkPolicy'
        object['metadata'] = {}
        object['metadata']['uid'] = np_id
        if event_type == 'delete':
            event['type'] = 'DELETED'
            event['object'] = object
            self._queue.put(event)
        return

    def _sync_np_sg(self):
        sg_uuid_set = set(SecurityGroupKM.keys())
        np_uuid_set = set(NetworkPolicyKM.keys())
        deleted_np_set = sg_uuid_set - np_uuid_set
        for uuid in deleted_np_set:
            sg = SecurityGroupKM.get(uuid)
            if not sg or sg.owner != 'k8s':
                continue
            if not sg.np_spec:
                continue
            self._create_network_policy_event('delete', sg.uuid)
        return

    def network_policy_timer(self):
        self._sync_np_sg()
        return

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
            if event['type'] == 'ADDED':
                self.vnc_network_policy_add(event, namespace, name, uid)
            elif event['type'] == 'MODIFIED':
                # spec modification is restricted in k8s
                pass
            elif event['type'] == 'DELETED':
                self.vnc_network_policy_delete(namespace, name, uid)
