#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
VNC Security Policy management for kubernetes
"""

from vnc_api.vnc_api import *
from kube_manager.vnc.vnc_kubernetes_config import (
    VncKubernetesConfig as vnc_kube_config)
from kube_manager.vnc.config_db import (ApplicationPolicySetKM,
    PolicyManagementKM)
from enum import Enum
from kube_manager.vnc.label_cache import XLabelCache
from kube_manager.vnc.config_db import (FirewallPolicyKM, FirewallRuleKM,
                                        AddressGroupKM)
from kube_manager.vnc.vnc_common import VncCommon
import collections
import json

class FWSimpleAction(Enum):
    PASS = ActionListType(simple_action='pass')
    DENY = ActionListType(simple_action='deny')

class FWService(object):
    @classmethod
    def get(cls, protocol='any', dst_start_port=-1, dst_end_port=None):
        if not dst_end_port:
            dst_end_port = dst_start_port
        dst_ports = PortType(dst_start_port, dst_end_port)
        return FirewallServiceType(protocol=protocol,
                                   dst_ports=dst_ports)

class FWRuleEndpoint(object):
    @classmethod
    def get(cls, tags=[], address_group=None):
        if tags:
            return FirewallRuleEndpointType(tags=tags)

        if address_group:
            ep = FirewallRuleEndpointType()
            ep.set_address_group(":".join(address_group.get_fq_name()))
            return ep

        return FirewallRuleEndpointType(any=True)

class FWDirection(Enum):
    BIDRECT = "<>"
    FROM = "<"
    TO = ">"

class FWDefaultProtoPort(Enum):
    PROTOCOL = "any"
    START_PORT = "0"
    END_PORT = "65535"

FWProtoMap = {
    "TCP": "tcp",
    "UDP": "udp",
    "any": "any"
}

class FWRule(object):
    default_policy_management_name = None
    vnc_lib = None

    @classmethod
    def create_address_group(cls, name, cidr, parent_obj=None):
        if not name:
            name = cidr

        if not parent_obj:
            pm_obj = PolicyManagement(cls.default_policy_management_name)
            try:
                parent_uuid = cls.vnc_lib.policy_management_create(pm_obj)
            except RefsExistError:
                pass
            pm_obj = cls.vnc_lib.policy_management_read(
                fq_name=pm_obj.get_fq_name())
            PolicyManagementKM.locate(pm_obj.get_uuid())
        else:
            pm_obj = parent_obj

        ip_prefix =  cidr.split('/')
        subnet_list = SubnetListType()
        subnet = SubnetType(ip_prefix=ip_prefix[0],
                            ip_prefix_len=ip_prefix[1])
        subnet_list.add_subnet(subnet)

        addr_grp_obj = AddressGroup(name=name, parent_obj=pm_obj,
                                address_group_prefix=subnet_list)
        try:
            addr_grp_uuid = cls.vnc_lib.address_group_create(addr_grp_obj)
        except RefsExistError:
            cls.vnc_lib.address_group_update(addr_grp_obj)
            addr_grp_uuid = addr_grp_obj.get_uuid()

        # Update application policy set in our cache.
        AddressGroupKM.locate(addr_grp_uuid)
        addr_grp_obj = cls.vnc_lib.address_group_read(id=addr_grp_uuid)

        return addr_grp_obj

    @classmethod
    def delete_address_group(cls, uuid):
        try:
            # Delete the address group.
            cls.vnc_lib.address_group_delete(id=uuid)
        except RefsExistError:
           # It is possible that this address group is shared across
           # multiple firwall rules or even by user rules.
           # So if ref exists, we are fine with it.
            pass

    @classmethod
    def _get_np_pod_selector(cls, spec, namespace):
        labels = {}
        pod_selector = spec.get('podSelector')

        if pod_selector and 'matchLabels' in pod_selector:
            labels = pod_selector.get('matchLabels')

        if namespace:
            ns_key, ns_value = XLabelCache.get_namespace_label_kv(namespace)
            labels[ns_key] = ns_value

        return labels

    @classmethod
    def spec_parser(cls, from_rule, from_rule_index,
                    rule_name_prefix, namespace = None):
        ep_list = []
        name = None
        tags = []

        if 'namespaceSelector' in from_rule:
            name = 'namespaceSelector'
            ns_selector = from_rule.get('namespaceSelector')
            if ns_selector:
                ns_selector_labels_dict =\
                    dict(ns_selector.get('matchLabels', {}))
                if ns_selector_labels_dict:
                    tags = VncSecurityPolicy.get_tags_fn(
                               ns_selector_labels_dict, True)

                    rule_name = '-'.join([rule_name_prefix,
                                          name,
                                          str(from_rule_index)])

                    ep_list.append([rule_name,
                                    FWRuleEndpoint.get(tags),
                                    FWSimpleAction.PASS.value])

        if 'podSelector' in from_rule:
            name = 'podSelector'
            pod_selector = from_rule.get('podSelector')
            pod_selector_labels_dict =\
                dict(pod_selector.get('matchLabels', {}))
            if pod_selector_labels_dict:
                if namespace:
                    pod_selector_labels_dict.update(
                        XLabelCache.get_namespace_label(namespace))
                tags = VncSecurityPolicy.get_tags_fn(pod_selector_labels_dict,
                                                     True)
                rule_name = '-'.join([rule_name_prefix,
                                      name,
                                      str(from_rule_index)])
                ep_list.append([rule_name,
                                FWRuleEndpoint.get(tags),
                                FWSimpleAction.PASS.value])

        if 'ipBlock' in from_rule:

            name = "ipBlock"
            ip_block = from_rule.get('ipBlock')
            if 'except' in ip_block:
                for except_cidr in ip_block.get('except'):
                    rule_name = '-'.join([rule_name_prefix,
                                        name,
                                        str(from_rule_index),
                                        except_cidr])
                    addr_grp_obj = cls.create_address_group(name=None,
                                                            cidr=except_cidr)
                    ep_list.append([rule_name,
                                FWRuleEndpoint.get(address_group=addr_grp_obj),
                                FWSimpleAction.DENY.value])

            if 'cidr' in ip_block:
                rule_name = '-'.join([rule_name_prefix,
                                      name,
                                      str(from_rule_index),
                                      "cidr",
                                      ip_block.get('cidr')])
                addr_grp_obj = cls.create_address_group(name=None,
                                                     cidr=ip_block.get('cidr'))

                ep_list.append([rule_name,
                                FWRuleEndpoint.get(address_group=addr_grp_obj),
                                FWSimpleAction.PASS.value])

        return ep_list

    @classmethod
    def ports_parser(cls, spec):
        service_list = []
        ports = spec.get('ports', [])
        if not ports:
            protocol = FWDefaultProtoPort.PROTOCOL.value
            port_start = FWDefaultProtoPort.START_PORT.value
            port_end = FWDefaultProtoPort.END_PORT.value
            service_list.append([FWService.get(protocol,
                                              dst_start_port=port_start,
                                              dst_end_port=port_end)])
        else:
            for port in ports:
                protocol = port.get('protocol',
                                    FWDefaultProtoPort.PROTOCOL.value)
                port_start = port.get('port',
                                      FWDefaultProtoPort.START_PORT.value)
                port_end = port.get('port',
                                    FWDefaultProtoPort.END_PORT.value)

                service_list.append([FWService.get(FWProtoMap[protocol],
                                                  dst_start_port=port_start,
                                                  dst_end_port=port_end)])
        return service_list

    @classmethod
    def ingress_parser(cls, name, namespace, pobj, tags, ingress, ingress_index):

        if not namespace:
            rule_name = "-".join(["ingress", name, str(ingress_index)])
        else:
            rule_name = "-".join([namespace, "ingress", name, str(ingress_index)])

        #
        # Endpoint 2 deduction.
        #
        ep2 = FWRuleEndpoint.get(tags)

        #
        # Endpoint 1 deduction.
        #
        ep1_list = []

        if ingress:
            from_rules = ingress.get('from', [])
            if from_rules:
                for from_rule in from_rules:
                    ep_list = cls.spec_parser(from_rule,
                                        from_rules.index(from_rule),
                                        rule_name, namespace)
                    if ep_list:
                        ep1_list.extend(ep_list)

        if not ep1_list:
            ep1_list.append([
                                '-'.join([rule_name, "allow-all"]),
                                 FWRuleEndpoint.get(),
                                 FWSimpleAction.PASS.value
                            ])

        service_list = cls.ports_parser(ingress)

        rule_list = []
        for ep1 in ep1_list:
            for service in service_list:
                rule_obj = FirewallRule(
                               name='%s' % "-".join([ep1[0], str(service_list.index(service))]),
                               parent_obj=pobj,
                               action_list=ep1[2],
                               service=service[0],
                               endpoint_1=ep1[1],
                               endpoint_2=ep2,
                               direction=FWDirection.TO.value
                            )
                rule_list.append(rule_obj)

        return rule_list

    @classmethod
    def get_egress_rule_name(cls, name, namespace):
        if namespace:
            rule_name = namespace + "-egress-" + name
        else:
            rule_name = "egress-" + name
        return rule_name

    @classmethod
    def egress_parser(cls, name, namespace, pobj, tags, egress):

        rule_list = []
        rule_name = cls.get_egress_rule_name(name, namespace)

        # Endpoint 1 is self.
        ep1 = FWRuleEndpoint.get(tags)

        # Endpoint 2 is derived from the spec.
        ep2_list = []

        if egress:
            to_rules = egress.get('to', [])
            if to_rules:
                for to_rule in to_rules:
                    ep_list = cls.spec_parser(to_rule,
                                          to_rules.index(to_rule),
                                          rule_name, namespace)
                    if ep_list:
                        ep2_list.extend(ep_list)

        # if no explicit spec is specified, then allow all egress.
        if not ep2_list:
            ep2_list.append([
                                '-'.join([rule_name, "allow-all"]),
                                 FWRuleEndpoint.get(),
                                 FWSimpleAction.PASS.value
                            ])

        # Get port config from spec.
        service_list = cls.ports_parser(egress)

        rule_list = []
        for ep2 in ep2_list:
            for service in service_list:
                rule_obj = FirewallRule(
                    name='%s' % "-".join([ep2[0], str(service_list.index(service))]),
                    parent_obj=pobj,
                    action_list=ep2[2],
                    service=service[0],
                    endpoint_1=ep1,
                    endpoint_2=ep2[1],
                    direction=FWDirection.TO.value
                    )
                rule_list.append(rule_obj)

        return rule_list

    @classmethod
    def get_egress_rule_deny_all(cls, name, namespace, pobj, tags):

        rule_name = "-".join([cls.get_egress_rule_name(name, namespace),
                              "default-deny-all"])

        protocol = FWDefaultProtoPort.PROTOCOL.value
        port_start = FWDefaultProtoPort.START_PORT.value
        port_end = FWDefaultProtoPort.END_PORT.value
        action = FWSimpleAction.DENY.value
        ep1 = FWRuleEndpoint.get(tags)
        ep2 = FWRuleEndpoint.get()
        service=FWService.get(protocol,
                          dst_start_port=port_start, dst_end_port=port_end)

        rule = FirewallRule(
            name='%s' % rule_name,
            parent_obj=pobj,
            action_list=action,
            service=service,
            endpoint_1=ep1,
            endpoint_2=ep2,
            direction=FWDirection.TO.value
        )

        return rule


    @classmethod
    def parser(cls, name, namespace, pobj, spec):

        fw_rules = []

        # Get pod selectors.
        podSelector_dict = cls._get_np_pod_selector(spec, namespace)
        tags = VncSecurityPolicy.get_tags_fn(podSelector_dict, True)

        deny_all_rule_uuid = None
        egress_deny_all_rule_uuid = None
        policy_types = spec.get('policyTypes', ['Ingress'])
        for policy_type in policy_types:
            if policy_type == 'Ingress':
                # Get ingress spec.
                ingress_spec_list = spec.get("ingress", [])
                for ingress_spec in ingress_spec_list:
                    fw_rules +=\
                        cls.ingress_parser(name, namespace, pobj, tags,
                           ingress_spec, ingress_spec_list.index(ingress_spec))

                # Add ingress deny-all for all other non-explicit traffic.
                deny_all_rule_name = namespace+"-ingress-"+name+"-denyall"
                deny_all_rule_uuid =\
                    VncSecurityPolicy.create_firewall_rule_deny_all(
                                                      deny_all_rule_name, tags)

            if policy_type == 'Egress':
                # Get egress spec.
                egress_spec_list = spec.get("egress", [])
                for egress_spec in egress_spec_list:
                    fw_rules +=\
                        cls.egress_parser(name, namespace, pobj, tags,
                                          egress_spec)
                # Add egress deny-all for all other non-explicit traffic.
                egress_deny_all_rule_uuid =\
                    VncSecurityPolicy.create_firewall_rule_egress_deny_all(
                        name, namespace, tags)

        return fw_rules, deny_all_rule_uuid, egress_deny_all_rule_uuid

class VncSecurityPolicy(VncCommon):
    default_policy_management_name = 'default-policy-management'
    vnc_lib = None
    cluster_aps_uuid = None
    get_tags_fn = None
    vnc_security_policy_instance = None
    allow_all_fw_policy_uuid = None
    deny_all_fw_policy_uuid = None
    ingress_svc_fw_policy_uuid = None

    def __init__(self, vnc_lib, get_tags_fn):
        self._k8s_event_type = 'VncSecurityPolicy'
        VncSecurityPolicy.vnc_lib = vnc_lib
        self._labels = XLabelCache(self._k8s_event_type)
        self.reset_resources()

        # Init FW Rule constructs.
        FWRule.default_policy_management_name = self.default_policy_management_name
        FWRule.vnc_lib = vnc_lib

        VncSecurityPolicy.get_tags_fn = get_tags_fn
        super(VncSecurityPolicy, self).__init__(self._k8s_event_type)
        VncSecurityPolicy.vnc_security_policy_instance = self

    def reset_resources(self):
        self._labels.reset_resource()
        VncSecurityPolicy.allow_all_fw_policy_uuid = None
        VncSecurityPolicy.deny_all_fw_policy_uuid = None
        VncSecurityPolicy.ingress_svc_fw_policy_uuid = None

    @staticmethod
    def construct_sequence_number(seq_num):
        snum_list = str(float(seq_num)).split('.')
        constructed_snum = "%s.%s" % (snum_list[0].zfill(5), snum_list[1])
        return FirewallSequence(sequence=constructed_snum)

    @classmethod
    def create_application_policy_set(cls, name, parent_obj=None):
        if not parent_obj:
            pm_obj = PolicyManagement(cls.default_policy_management_name)
            try:
                parent_uuid = cls.vnc_lib.policy_management_create(pm_obj)
            except RefsExistError:
                pass
            pm_obj = cls.vnc_lib.policy_management_read(
                fq_name=pm_obj.get_fq_name())
            PolicyManagementKM.locate(pm_obj.get_uuid())
        else:
            pm_obj = parent_obj

        aps_obj = ApplicationPolicySet(name=name, parent_obj=pm_obj)
        try:
            aps_uuid = cls.vnc_lib.application_policy_set_create(aps_obj)
        except RefsExistError:
            cls.vnc_lib.application_policy_set_update(aps_obj)
            aps_uuid = aps_obj.get_uuid()

        # Update application policy set in our cache.
        ApplicationPolicySetKM.locate(aps_uuid)
        cls.cluster_aps_uuid = aps_uuid
        return aps_uuid

    @classmethod
    def tag_cluster_application_policy_set(cls):
        aps_uuid = cls.cluster_aps_uuid
        aps_obj = cls.vnc_lib.application_policy_set_read(id=aps_uuid)
        cls.vnc_security_policy_instance._labels.process(aps_uuid,
            cls.vnc_security_policy_instance._labels.get_cluster_label(
                vnc_kube_config.cluster_name()))
        cls.vnc_lib.set_tags(aps_obj,
            cls.vnc_security_policy_instance._labels.get_labels_dict(aps_uuid))

    @classmethod
    def get_firewall_policy_name(cls, name, namespace, is_global):
        if is_global:
            policy_name = name
        else:
            policy_name = "-".join([namespace, name])

        # Always prepend firewall policy name with cluster name.
        return "-".join([vnc_kube_config.cluster_name(), policy_name])

    @classmethod
    def create_firewall_policy(cls, name, namespace, spec, tag_last=False,
                               is_global=False):

        if not cls.cluster_aps_uuid:
            raise Exception("Cluster Application Policy Set not available.")

        # Get parent object for this firewall policy.
        aps_obj = cls.vnc_lib.application_policy_set_read(
            id=cls.cluster_aps_uuid)

        try:
            pm_obj = cls.vnc_lib.policy_management_read(
                fq_name=aps_obj.get_parent_fq_name())
        except NoIdError:
            raise

        fw_policy_obj = FirewallPolicy(
            cls.get_firewall_policy_name(name, namespace, is_global), pm_obj)

        custom_ann_kwargs = {}
        curr_fw_policy = None
        fw_rules_del_candidates = set()

        # If this firewall policy already exists, get its uuid.
        fw_policy_uuid = VncSecurityPolicy.get_firewall_policy_uuid(
                             name, namespace, is_global)
        if fw_policy_uuid:
            #
            # FW policy exists.
            # Check for modidifcation to its spec.
            # If not modifications are found, return the uuid of policy.
            #
            curr_fw_policy = FirewallPolicyKM.locate(fw_policy_uuid)
            if curr_fw_policy and curr_fw_policy.spec:
                if curr_fw_policy.spec == json.dumps(spec):
                    # Input spec is same as existing spec. Nothing to do.
                    # Just return the uuid.
                    return fw_policy_uuid

                # Get the current firewall rules on this policy.
                # All rules are delete candidates as any of them could have
                # changed.
                fw_rules_del_candidates = curr_fw_policy.firewall_rules

        # Annotate the FW policy object with input spec.
        # This will be used later to identify and validate subsequent modify
        # or add (i.e post restart) events.
        custom_ann_kwargs['spec'] = json.dumps(spec)

        # Check if we are being asked to place this firewall policy in the end
        # of fw policy list in its Application Policy Set.
        # If yes, tag accordingly.
        if tag_last:
            custom_ann_kwargs['tail'] = "True"

        # Parse input spec and construct the list of rules for this FW policy.
        fw_rules = []
        deny_all_rule_uuid = None
        egress_deny_all_rule_uuid = None

        if spec is not None:
            fw_rules, deny_all_rule_uuid, egress_deny_all_rule_uuid =\
                FWRule.parser(name, namespace, pm_obj, spec)

        for rule in fw_rules:
            try:
                rule_uuid = cls.vnc_lib.firewall_rule_create(rule)
            except RefsExistError:
                cls.vnc_lib.firewall_rule_update(rule)
                rule_uuid = rule.get_uuid()

                # The rule is in use and needs to stay.
                # Remove it from delete candidate collection.
                if fw_rules_del_candidates and\
                   rule_uuid in fw_rules_del_candidates:
                    fw_rules_del_candidates.remove(rule_uuid)


            rule_obj = cls.vnc_lib.firewall_rule_read(id=rule_uuid)
            FirewallRuleKM.locate(rule_uuid)

            fw_policy_obj.add_firewall_rule(rule_obj,
                cls.construct_sequence_number(fw_rules.index(rule)))

        if deny_all_rule_uuid:
            VncSecurityPolicy.add_firewall_rule(
                VncSecurityPolicy.deny_all_fw_policy_uuid, deny_all_rule_uuid)
            custom_ann_kwargs['deny_all_rule_uuid'] = deny_all_rule_uuid

        if egress_deny_all_rule_uuid:
            VncSecurityPolicy.add_firewall_rule(
                VncSecurityPolicy.deny_all_fw_policy_uuid,
                egress_deny_all_rule_uuid)
            custom_ann_kwargs['egress_deny_all_rule_uuid'] =\
                egress_deny_all_rule_uuid

        FirewallPolicyKM.add_annotations(
            VncSecurityPolicy.vnc_security_policy_instance,
            fw_policy_obj, namespace, name, None, **custom_ann_kwargs)

        try:
            fw_policy_uuid = cls.vnc_lib.firewall_policy_create(fw_policy_obj)
        except RefsExistError:

            # Remove existing firewall rule refs on this fw policy.
            # Once existing firewall rules are remove, firewall policy will
            # be updated with rules correspoinding to current input spec.
            for rule in fw_rules_del_candidates:
                cls.delete_firewall_rule(fw_policy_uuid, rule)

            cls.vnc_lib.firewall_policy_update(fw_policy_obj)
            fw_policy_uuid = fw_policy_obj.get_uuid()

        fw_policy_obj = cls.vnc_lib.firewall_policy_read(id=fw_policy_uuid)
        FirewallPolicyKM.locate(fw_policy_uuid)

        return fw_policy_uuid

    @classmethod
    def delete_firewall_policy(cls, name, namespace, is_global=False):

        if not cls.cluster_aps_uuid:
            raise Exception("Cluster Application Policy Set not available.")

        # Get parent object for this firewall policy.
        aps_obj = cls.vnc_lib.application_policy_set_read(
            id=cls.cluster_aps_uuid)

        try:
            pm_obj = cls.vnc_lib.policy_management_read(
                fq_name=aps_obj.get_parent_fq_name())
        except NoIdError:
            raise
        fw_policy_fq_name = pm_obj.get_fq_name() +\
            [cls.get_firewall_policy_name(name, namespace, is_global)]
        fw_policy_uuid = FirewallPolicyKM.get_fq_name_to_uuid(fw_policy_fq_name)

        if not fw_policy_uuid:
            # We are not aware of this firewall policy.
            return


        fw_policy = FirewallPolicyKM.locate(fw_policy_uuid)
        fw_policy_rules = fw_policy.firewall_rules

        # Remove deny all firewall rule, if any.
        if fw_policy.deny_all_rule_uuid:
            VncSecurityPolicy.delete_firewall_rule(
                VncSecurityPolicy.deny_all_fw_policy_uuid,
                fw_policy.deny_all_rule_uuid)

        # Remove egress deny all firewall rule, if any.
        if fw_policy.egress_deny_all_rule_uuid:
            VncSecurityPolicy.delete_firewall_rule(
                VncSecurityPolicy.deny_all_fw_policy_uuid,
                fw_policy.egress_deny_all_rule_uuid)

        for rule_uuid in fw_policy_rules:
            try:
                VncSecurityPolicy.delete_firewall_rule(fw_policy_uuid,
                                                       rule_uuid)
            except:
                raise

        cls.remove_firewall_policy(name, namespace)
        try:
            cls.vnc_lib.firewall_policy_delete(id=fw_policy_uuid)
            FirewallPolicyKM.delete(fw_policy_uuid)
        except:
            raise

    @classmethod
    def create_firewall_rule_allow_all(cls, rule_name, labels_dict,
                                       src_labels_dict=None):

        if not cls.cluster_aps_uuid:
            raise Exception("Cluster Application Policy Set not available.")

        # Get parent object for this firewall policy.
        aps_obj = cls.vnc_lib.application_policy_set_read(
            id=cls.cluster_aps_uuid)

        try:
            pm_obj = cls.vnc_lib.policy_management_read(
                fq_name=aps_obj.get_parent_fq_name())
        except NoIdError:
            raise

        tags = VncSecurityPolicy.get_tags_fn(labels_dict, True)

        if src_labels_dict:
            src_tags = VncSecurityPolicy.get_tags_fn(src_labels_dict, True)
        else:
            src_tags = None

        protocol = FWDefaultProtoPort.PROTOCOL.value
        port_start = FWDefaultProtoPort.START_PORT.value
        port_end = FWDefaultProtoPort.END_PORT.value
        action = FWSimpleAction.PASS.value
        ep1 = FWRuleEndpoint.get(src_tags)
        ep2 = FWRuleEndpoint.get(tags)
        service=FWService.get(protocol,
                          dst_start_port=port_start, dst_end_port=port_end)

        rule = FirewallRule(
            name='%s' % rule_name,
            parent_obj=pm_obj,
            action_list=action,
            service=service,
            endpoint_1=ep1,
            endpoint_2=ep2,
            direction=FWDirection.TO.value
        )

        try:
            rule_uuid = cls.vnc_lib.firewall_rule_create(rule)
        except RefsExistError:
            cls.vnc_lib.firewall_rule_update(rule)
            rule_uuid = rule.get_uuid()
        rule_obj = cls.vnc_lib.firewall_rule_read(id=rule_uuid)
        FirewallRuleKM.locate(rule_uuid)

        return rule_uuid

    @classmethod
    def create_firewall_rule_deny_all(cls, rule_name, tags):

        if not cls.cluster_aps_uuid:
            raise Exception("Cluster Application Policy Set not available.")

        # Get parent object for this firewall policy.
        aps_obj = cls.vnc_lib.application_policy_set_read(
            id=cls.cluster_aps_uuid)

        try:
            pm_obj = cls.vnc_lib.policy_management_read(
                fq_name=aps_obj.get_parent_fq_name())
        except NoIdError:
            raise

        protocol = FWDefaultProtoPort.PROTOCOL.value
        port_start = FWDefaultProtoPort.START_PORT.value
        port_end = FWDefaultProtoPort.END_PORT.value
        action = FWSimpleAction.DENY.value
        ep1 = FWRuleEndpoint.get()
        ep2 = FWRuleEndpoint.get(tags)
        service=FWService.get(protocol,
                          dst_start_port=port_start, dst_end_port=port_end)

        rule = FirewallRule(
            name='%s' % rule_name,
            parent_obj=pm_obj,
            action_list=action,
            service=service,
            endpoint_1=ep1,
            endpoint_2=ep2,
            direction=FWDirection.TO.value
        )

        try:
            rule_uuid = cls.vnc_lib.firewall_rule_create(rule)
        except RefsExistError:
            cls.vnc_lib.firewall_rule_update(rule)
            rule_uuid = rule.get_uuid()
        FirewallRuleKM.locate(rule_uuid)

        return rule_uuid

    @classmethod
    def create_firewall_rule_egress_deny_all(cls, name, namespace, tags):

        if not cls.cluster_aps_uuid:
            raise Exception("Cluster Application Policy Set not available.")

        # Get parent object for this firewall policy.
        aps_obj = cls.vnc_lib.application_policy_set_read(
            id=cls.cluster_aps_uuid)

        try:
            pm_obj = cls.vnc_lib.policy_management_read(
                fq_name=aps_obj.get_parent_fq_name())
        except NoIdError:
            raise

        rule_name = "-".join([FWRule.get_egress_rule_name(name, namespace),
                                 "default-deny-all"])

        protocol = FWDefaultProtoPort.PROTOCOL.value
        port_start = FWDefaultProtoPort.START_PORT.value
        port_end = FWDefaultProtoPort.END_PORT.value
        action = FWSimpleAction.DENY.value
        ep1 = FWRuleEndpoint.get(tags)
        ep2 = FWRuleEndpoint.get()
        service=FWService.get(protocol,
                              dst_start_port=port_start,
                              dst_end_port=port_end)

        rule = FirewallRule(
            name='%s' % rule_name,
            parent_obj=pm_obj,
            action_list=action,
            service=service,
            endpoint_1=ep1,
            endpoint_2=ep2,
            direction=FWDirection.TO.value
        )

        try:
            rule_uuid = cls.vnc_lib.firewall_rule_create(rule)
        except RefsExistError:
            cls.vnc_lib.firewall_rule_update(rule)
            rule_uuid = rule.get_uuid()
        FirewallRuleKM.locate(rule_uuid)

        return rule_uuid

    @classmethod
    def _move_trailing_firewall_policies(cls, aps_obj, tail_sequence):
        sequence_num = float(tail_sequence.get_sequence())
        if cls.deny_all_fw_policy_uuid:
            sequence = cls.construct_sequence_number(sequence_num)
            try:
                fw_policy_obj = cls.vnc_lib.firewall_policy_read(
                                    id=cls.deny_all_fw_policy_uuid)
            except NoIdError:
                raise
            aps_obj.add_firewall_policy(fw_policy_obj, sequence)
            sequence_num += 1

        if cls.allow_all_fw_policy_uuid:
            sequence = cls.construct_sequence_number(sequence_num)
            try:
                fw_policy_obj = cls.vnc_lib.firewall_policy_read(
                                    id=cls.allow_all_fw_policy_uuid)
            except NoIdError:
                raise
            aps_obj.add_firewall_policy(fw_policy_obj, sequence)
            sequence_num += 1

        cls.vnc_lib.application_policy_set_update(aps_obj)
        return cls.construct_sequence_number(sequence_num)

    @classmethod
    def add_firewall_policy(cls, fw_policy_uuid, append_after_tail=False):
        if not cls.cluster_aps_uuid:
            raise Exception("Cluster Application Policy Set not available.")

        aps_obj = cls.vnc_lib.application_policy_set_read(
            id=cls.cluster_aps_uuid)

        try:
            new_fw_policy_obj = cls.vnc_lib.firewall_policy_read(
                                    id=fw_policy_uuid)
        except NoIdError:
            raise

        last_obj = False
        last_entry_sequence = None
        last_k8s_obj = None
        last_k8s_obj_sequence = None
        fw_policy_refs = aps_obj.get_firewall_policy_refs()
        for fw_policy in fw_policy_refs if fw_policy_refs else []:
            try:
                fw_policy_obj = cls.vnc_lib.firewall_policy_read(
                                    id=fw_policy['uuid'])
            except NoIdError:
                # TBD Error handling.
                pass

            # Return if the firewall policy is already found on this APS.
            if new_fw_policy_obj.get_fq_name() == fw_policy_obj.get_fq_name():
                return

            k8s_obj = False
            last_obj = False

            annotations = fw_policy_obj.get_annotations()
            if annotations:
                for kvp in annotations.get_key_value_pair() or []:
                    if kvp.key == 'owner' and kvp.value == 'k8s':
                        k8s_obj = True
                    elif kvp.key == 'tail' and kvp.value == 'True':
                        last_obj = True

            if k8s_obj and last_obj:
                last_k8s_obj = fw_policy_obj
                last_k8s_obj_sequence = fw_policy['attr'].get_sequence()

            if not last_entry_sequence:
                last_entry_sequence = fw_policy['attr'].get_sequence()
            elif float(last_entry_sequence) < float(fw_policy['attr'].get_sequence()):
                   last_entry_sequence = fw_policy['attr'].get_sequence()


        #
        # Determine the sequence number.
        #

        # Start with presumption that this is the first.
        sequence = cls.construct_sequence_number('1.0')
        if len(fw_policy_refs if fw_policy_refs else []):
            last_k8s_fw_policy_sequence = \
                cls.construct_sequence_number(
                    float(last_entry_sequence) + float('1.0'))
            if last_k8s_obj_sequence:
                tail_sequence = cls._move_trailing_firewall_policies(aps_obj,
                                    last_k8s_fw_policy_sequence)
                if append_after_tail:
                    sequence = cls.construct_sequence_number(
                        float(tail_sequence.get_sequence()))
                else:
                    sequence = FirewallSequence(sequence=last_k8s_obj_sequence)
                # Move the existing last k8s FW policy to the end of the list.
            else:
                sequence = last_k8s_fw_policy_sequence

        aps_obj.add_firewall_policy(new_fw_policy_obj, sequence)
        cls.vnc_lib.application_policy_set_update(aps_obj)

    @classmethod
    def remove_firewall_policy(cls, name, namespace, is_global=False):
        if not cls.cluster_aps_uuid:
            raise Exception("Cluster Application Policy Set not available.")

        aps_obj = cls.vnc_lib.application_policy_set_read(
            id=cls.cluster_aps_uuid)

        try:
            pm_obj = cls.vnc_lib.policy_management_read(
                fq_name=aps_obj.get_parent_fq_name())
        except NoIdError:
            raise

        fw_policy_fq_name = pm_obj.get_fq_name() +\
            [cls.get_firewall_policy_name(name, namespace, is_global)]

        fw_policy_uuid = FirewallPolicyKM.get_fq_name_to_uuid(fw_policy_fq_name)

        if not fw_policy_uuid:
            # We are not aware of this firewall policy.
            return

        try:
            fw_policy_obj = cls.vnc_lib.firewall_policy_read(id=fw_policy_uuid)
        except NoIdError:
            raise

        aps_obj.del_firewall_policy(fw_policy_obj)
        cls.vnc_lib.application_policy_set_update(aps_obj)

    @classmethod
    def add_firewall_rule(cls, fw_policy_uuid, fw_rule_uuid):

        try:
            fw_policy_obj = cls.vnc_lib.firewall_policy_read(id=fw_policy_uuid)
        except NoIdError:
            raise

        try:
            fw_rule_obj = cls.vnc_lib.firewall_rule_read(id=fw_rule_uuid)
        except NoIdError:
            raise

        last_entry_sequence = None
        rule_refs = fw_policy_obj.get_firewall_rule_refs()
        for rule in rule_refs if rule_refs else []:

            if  fw_rule_uuid == rule['uuid']:
                return

            if last_entry_sequence < rule['attr'].get_sequence():
                last_entry_sequence = rule['attr'].get_sequence()

        # Start with presumption that this is the first.
        sequence = cls.construct_sequence_number('1.0')
        if last_entry_sequence:
            sequence = cls.construct_sequence_number(
                float(last_entry_sequence) + float('1.0'))

        fw_policy_obj.add_firewall_rule(fw_rule_obj, sequence)
        cls.vnc_lib.firewall_policy_update(fw_policy_obj)
        FirewallPolicyKM.locate(fw_policy_obj.get_uuid())

    @classmethod
    def delete_firewall_rule(cls, fw_policy_uuid, fw_rule_uuid):

        # If policy or rule info is not provided, then there is nothing to do.
        if not fw_policy_uuid or not fw_rule_uuid:
            return

        try:
            fw_policy_obj = cls.vnc_lib.firewall_policy_read(id=fw_policy_uuid)
        except NoIdError:
            raise

        try:
            fw_rule_obj = cls.vnc_lib.firewall_rule_read(id=fw_rule_uuid)
        except NoIdError:
            return

        addr_grp_refs = fw_rule_obj.get_address_group_refs()

        fw_policy_obj.del_firewall_rule(fw_rule_obj)

        cls.vnc_lib.firewall_policy_update(fw_policy_obj)
        FirewallPolicyKM.locate(fw_policy_obj.get_uuid())

        # Delete the rule.
        cls.vnc_lib.firewall_rule_delete(id=fw_rule_uuid)

        # Try to delete address groups allocated for this FW rule.
        for addr_grp in addr_grp_refs if addr_grp_refs else []:
            FWRule.delete_address_group(addr_grp['uuid'])

    @classmethod
    def create_allow_all_security_policy(cls):
        if not cls.allow_all_fw_policy_uuid:
            allow_all_fw_policy_uuid =\
                VncSecurityPolicy.create_firewall_policy("allowall",
                 None, None, is_global=True)
            VncSecurityPolicy.add_firewall_policy(allow_all_fw_policy_uuid,
                                                  append_after_tail=True)
            cls.allow_all_fw_policy_uuid = allow_all_fw_policy_uuid

    @classmethod
    def create_deny_all_security_policy(cls):
        if not cls.deny_all_fw_policy_uuid:
            cls.deny_all_fw_policy_uuid =\
                VncSecurityPolicy.create_firewall_policy("denyall",
                 None, None, tag_last=True, is_global=True)
            VncSecurityPolicy.add_firewall_policy(cls.deny_all_fw_policy_uuid)

    @classmethod
    def get_firewall_rule_uuid(cls, rule_name):

        if not cls.cluster_aps_uuid:
            raise Exception("Cluster Application Policy Set not available.")

        aps = ApplicationPolicySetKM.locate(cls.cluster_aps_uuid)
        pm = PolicyManagementKM.locate(aps.parent_uuid)
        rule_fq_name = pm.fq_name + [rule_name]
        rule_uuid = FirewallRuleKM.get_fq_name_to_uuid(rule_fq_name)
        return rule_uuid

    @classmethod
    def get_firewall_policy_uuid(cls, name, namespace, is_global=False):

        if not cls.cluster_aps_uuid:
            raise Exception("Cluster Application Policy Set not available.")
        aps = ApplicationPolicySetKM.locate(cls.cluster_aps_uuid)

        if not aps or not aps.parent_uuid:
            return None

        pm = PolicyManagementKM.locate(aps.parent_uuid)
        fw_policy_fq_name = pm.fq_name +\
            [cls.get_firewall_policy_name(name, namespace, is_global)]
        fw_policy_uuid = FirewallPolicyKM.get_fq_name_to_uuid(fw_policy_fq_name)
        return fw_policy_uuid
