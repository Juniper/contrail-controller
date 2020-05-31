#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import unittest
import uuid

from kube_manager.tests.vnc.test_case import KMTestCase
from kube_manager.common.kube_config_db import (NetworkPolicyKM)
from kube_manager.vnc.vnc_namespace import NamespaceKM
from kube_manager.vnc.config_db import FirewallPolicyKM
from kube_manager.vnc.config_db import ApplicationPolicySetKM
from kube_manager.vnc.vnc_security_policy import VncSecurityPolicy


class VncNetworkPolicyTest(KMTestCase):
    def setUp(self, extra_config_knobs=None):
        super(VncNetworkPolicyTest, self).setUp(
            extra_config_knobs=extra_config_knobs)
    # end setUp

    def tearDown(self):
        super(VncNetworkPolicyTest, self).tearDown()
    # end tearDown

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        super(VncNetworkPolicyTest, cls).setUpClass(
            extra_config_knobs=extra_config_knobs)
        cls.domain = 'default-domain'
        cls.cluster_project = 'test-project'
        cls.ns_name = 'test-namespace'

        prj_dict = {}
        prj_dict['project'] = cls.cluster_project

    @classmethod
    def tearDownClass(cls):
        super(VncNetworkPolicyTest, cls).tearDownClass()

    def _create_namespace(self, ns_name, ns_eval_vn_dict, is_isolated=False, labels={}):
        ns_uuid = str(uuid.uuid4())
        ns_add_event = self.create_add_namespace_event(ns_name, ns_uuid)
        ns_object = ns_add_event['object']
        ns_object['spec'] = {}
        ns_meta = ns_object['metadata']
        ns_meta['annotations'] = {}

        ns_meta['name'] = ns_name
        ns_meta['uid'] = ns_uuid
        ns_meta['namespace'] = ns_name
        ns_meta['labels'] = labels

        if ns_eval_vn_dict:
            ns_meta['annotations']['opencontrail.org/network'] = \
                ns_eval_vn_dict
        if is_isolated:
            ns_meta['annotations']['opencontrail.org/isolation'] = 'true'

        NamespaceKM.delete(ns_name)
        ns = NamespaceKM.locate(ns_name, ns_object)

        self.enqueue_event(ns_add_event)
        self.wait_for_all_tasks_done()

        return ns.uuid

    def _add_update_network_policy(self, np_name, np_spec={},
                                   labels=None, locate=False):
        np_uuid = str(uuid.uuid4())
        np_meta = {'name': np_name,
                   'uid': np_uuid,
                   'namespace': self.ns_name}
        if labels:
            np_meta['labels'] = labels

        np_add_event = self.create_event('NetworkPolicy', np_spec,
                                         np_meta, 'ADDED')
        NetworkPolicyKM.locate(np_uuid, np_add_event['object'])
        self.enqueue_event(np_add_event)
        self.wait_for_all_tasks_done()

        return np_uuid

    def _delete_network_policy(self, name, uuid, np_spec={}):
        np_meta = {'name': name,
                   'uid': uuid,
                   'namespace': self.ns_name}

        NetworkPolicyKM.delete(uuid)
        np_del_event = self.create_event('NetworkPolicy', np_spec,
                                         np_meta, 'DELETED')
        self.enqueue_event(np_del_event)
        self.wait_for_all_tasks_done()

    def _get_spec_number_of_rules(self, spec):
        num_rules = 0
        if not spec:
            return

        policy_types = spec.get('policyTypes', ['Ingress'])

        ingress_spec_list = spec.get("ingress", [])
        for ingress_spec in ingress_spec_list:
            num_from_rules = 0
            from_rules = ingress_spec.get('from', [])
            if from_rules:
                for from_rule in from_rules:
                    if all(k in from_rule for k in ('namespaceSelector',
                                                    'podSelector')):
                        num_from_rules += 1
                    else:
                        if 'podSelector' in from_rule:
                            num_from_rules += 1
                        if 'namespaceSelector' in from_rule:
                            num_from_rules += 1
                    if 'ipBlock' in from_rule:
                        if 'except' in from_rule['ipBlock']:
                            num_from_rules += len(from_rule['ipBlock']['except'])
                        if 'cidr' in from_rule['ipBlock']:
                            num_from_rules += 1
            else:
                # Implicit allow all rule.
                num_from_rules += 1

            ports = ingress_spec.get('ports', [])
            if ports:
                num_from_rules = len(ports) * num_from_rules

            num_rules += num_from_rules

        if "Egress" not in policy_types:
            return num_rules

        egress_spec_list = spec.get("egress", [])

        for egress_spec in egress_spec_list:
            num_to_rules = 0
            to_rules = egress_spec.get('to', [])
            if to_rules:
                for to_rule in to_rules:
                    if all(k in to_rule for k in ('namespaceSelector',
                                                  'podSelector')):
                        num_to_rules += 1
                    else:
                        if 'podSelector' in to_rule:
                            num_to_rules += 1
                        if 'namespaceSelector' in to_rule:
                            num_to_rules += 1
                    if 'ipBlock' in to_rule:
                        if 'except' in to_rule['ipBlock']:
                            num_to_rules += len(from_rule['ipBlock']['except'])
                        if 'cidr' in to_rule['ipBlock']:
                            num_to_rules += 1
            else:
                # Allow to any.
                num_to_rules += 1

            ports = egress_spec.get('ports', [])
            if ports:
                num_to_rules = len(ports) * num_to_rules

            num_rules += num_to_rules

        return num_rules

    def _validate_spec(self, spec={}, fw_policy=None):
        if not spec or not fw_policy:
            return

        # Validate that number of rules is as expected.
        num_spec_rules = self._get_spec_number_of_rules(spec)
        num_fw_rules = len(fw_policy.firewall_rules)
        self.assertEqual(num_spec_rules, num_fw_rules)

    def _validate_network_policy_resources(self, name, uuid, spec={},
                                           validate_delete=False,
                                           namespace=None):
        ns_name = namespace if namespace else self.ns_name
        np_event_obj = NetworkPolicyKM.find_by_name_or_uuid(uuid)
        if validate_delete:
            self.assertIsNone(np_event_obj)
        elif not spec:
            fw_policy_uuid = VncSecurityPolicy.get_firewall_policy_uuid(name, ns_name)
            fw_policy = FirewallPolicyKM.locate(fw_policy_uuid)
            self.assertIsNotNone(np_event_obj)
            self.assertIsNone(fw_policy)
        else:
            fw_policy_uuid = VncSecurityPolicy.get_firewall_policy_uuid(name, ns_name)
            fw_policy = FirewallPolicyKM.locate(fw_policy_uuid)
            self.assertIsNotNone(np_event_obj)
            self.assertIsNotNone(fw_policy)

            # Validate network policy spec.
            self._validate_spec(spec, fw_policy)

    def _create_application_policy_set(self, name, parent_obj=None):
        return VncSecurityPolicy.create_application_policy_set(name, parent_obj)

    def _get_default_application_policy_set(self):
        _ = [VncSecurityPolicy.default_policy_management_name, self.cluster_name()]
        return self._vnc_lib.application_policy_set_read(
            id=VncSecurityPolicy.cluster_aps_uuid)

    def test_add_network_policy(self):
        np_name = unittest.TestCase.id(self)
        np_spec = {'podSelector': {}}
        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec)

        self._delete_network_policy(unittest.TestCase.id(self), np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True)

    def test_add_np_allow_all(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {'podSelector': {}, 'ingress': [{}]}
        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True)

    def test_add_np_allow_app_to_web_in_same_namespace(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'ingress': [{'from': [{'podSelector': {'matchLabels': {'tier': 'app'}}}]}],
            'podSelector': {'matchLabels': {'tier': 'web'}},
            'policyTypes': ['Ingress', 'Egress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True)

    def test_add_np_allow_app_to_web_in_different_namespace(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'ingress': [
                {'from': [
                    {'ipBlock': {'cidr': '172.17.0.0/16',
                                 'except': ['172.17.1.0/24']}},
                    {'namespaceSelector': {
                        'matchLabels': {
                            'deployment': 'HR',
                            'site': 'SVL'
                        }
                    }},
                    {'podSelector': {'matchLabels': {'tier': 'app'}}}
                ]}
            ],
            'podSelector': {'matchLabels': {'tier': 'web'}},
            'policyTypes': ['Ingress', 'Egress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_np_allow_app_to_web_with_egress_cidr(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'ingress': [{
                'from': [
                    {'ipBlock': {'cidr': '172.17.0.0/16',
                                 'except': ['172.17.1.0/24']}},
                    {'namespaceSelector': {
                        'matchLabels': {
                            'deployment': 'HR',
                            'site': 'SVL'
                        }
                    }},
                    {'podSelector': {'matchLabels': {'tier': 'app'}}}],
                'ports': [{
                    'port': 5978,
                    'protocol': 'TCP'
                }]
            }],
            'egress': [
                {
                    'ports': [
                        {
                            'port': 5978,
                            'protocol': 'TCP'
                        }
                    ],
                    'to': [
                        {
                            'ipBlock': {'cidr': u'10.0.0.0/24'}
                        }
                    ]
                }
            ],
            'podSelector': {'matchLabels': {'tier': 'web'}},
            'policyTypes': ['Ingress', 'Egress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_np_allow_app_to_web_with_egress_cidr_multiple_ipblocks(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'ingress': [{
                'from': [
                    {'ipBlock': {'cidr': '172.17.0.0/16',
                                 'except': ['172.17.1.0/24']}},
                    {'namespaceSelector': {
                        'matchLabels': {
                            'deployment': 'HR',
                            'site': 'SVL'
                        }
                    }},
                    {'podSelector': {'matchLabels': {'tier': 'app'}}}
                ],
                'ports': [
                    {
                        'port': "5978",
                        'protocol': 'TCP'
                    }
                ]}
            ],
            'egress': [
                {
                    'ports': [
                        {
                            'port': '0',
                            'protocol': 'TCP'
                        }
                    ],
                    'to': [
                        {
                            'ipBlock': {'cidr': u'10.0.0.0/24'}
                        },
                        {
                            'ipBlock': {'cidr': u'20.0.0.0/24'}
                        }
                    ]
                }
            ],
            'podSelector': {'matchLabels': {'tier': 'web'}},
            'policyTypes': ['Ingress', 'Egress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_np_allow_app_to_web_with_egress_cidr_multiple_ports(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'ingress': [{
                'from': [
                    {'ipBlock': {'cidr': '172.17.0.0/16',
                                 'except': ['172.17.1.0/24']}},
                    {'namespaceSelector': {
                        'matchLabels': {
                            'deployment': 'HR',
                            'site': 'SVL'
                        }
                    }},
                    {'podSelector': {'matchLabels': {'tier': 'app'}}}
                ],
                'ports': [
                    {
                        'port': '3978',
                        'protocol': 'TCP'
                    },
                    {
                        'port': '4978',
                        'protocol': 'TCP'
                    }
                ]}
            ],
            'egress': [
                {
                    'ports': [
                        {
                            'port': '5978',
                            'protocol': 'TCP'
                        },
                        {
                            'port': '6978',
                            'protocol': 'TCP'
                        }
                    ],
                    'to': [
                        {
                            'ipBlock': {'cidr': u'10.0.0.0/24'}
                        }
                    ]
                }
            ],
            'podSelector': {'matchLabels': {'tier': 'web'}},
            'policyTypes': ['Ingress', 'Egress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_np_default_allow_all_ingress(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'ingress': [{}],
            'podSelector': {},
            'policyTypes': ['Ingress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_np_default_deny_all_ingress(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'podSelector': {},
            'policyTypes': ['Ingress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_np_default_deny_all_egress(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {'podSelector': {}, 'policyTypes': ['Egress']}

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_np_default_allow_all_egress(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {'egress': [{}],
                   'podSelector': {},
                   'policyTypes': ['Egress']
                   }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_np_default_deny_all_ingress_egress(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'podSelector': {},
            'policyTypes': ['Ingress', 'Egress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_network_policy_scaling(self):
        np_uuid_dict = {}
        test_range = list(range(1, 10))
        for i in test_range:
            np_spec = {
                'podSelector': {},
                'ingress': [{}]
            }
            np_name = "-".join([unittest.TestCase.id(self), str(i)])
            np_uuid_dict[i] = self._add_update_network_policy(np_name, np_spec)
            self._validate_network_policy_resources(np_name, np_uuid_dict[i],
                                                    np_spec)

        previous_sequence = None
        _ = VncSecurityPolicy.cluster_aps_uuid
        aps_obj = self._get_default_application_policy_set()
        fw_policy_refs = aps_obj.get_firewall_policy_refs()
        for i in test_range:
            np_name = "-".join([unittest.TestCase.id(self), str(i)])
            fw_policy_name = VncSecurityPolicy.get_firewall_policy_name(
                np_name, self.ns_name, False)
            for fw_policy in fw_policy_refs if fw_policy_refs else []:
                if fw_policy_name == fw_policy['to'][-1]:
                    if previous_sequence:
                        self.assertTrue(
                            previous_sequence < fw_policy['attr'].get_sequence())
                    previous_sequence = fw_policy['attr'].get_sequence()
                    break

        for i in test_range:
            self._delete_network_policy(unittest.TestCase.id(self), np_uuid_dict[i])
            self._validate_network_policy_resources(
                np_name, np_uuid_dict[i],
                np_spec, validate_delete=True)

    def test_ingress_policy_periodic_validate(self):
        """
        Validate network policy periodic self-healing when
        ingress-svc firewall policy is detached from APS.
        """

        # Check if we have a valid config to start with.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        # Create a user network policy.
        np_name = unittest.TestCase.id(self)
        np_spec = {
            'podSelector': {},
            'policyTypes': ['Ingress', 'Egress']
        }
        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        # Validate that config is sane after user policy add.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        # Get some basic object handles.
        self.assertIsNotNone(VncSecurityPolicy.ingress_svc_fw_policy_uuid)
        fw_policy_obj = self._vnc_lib.firewall_policy_read(
            id=VncSecurityPolicy.ingress_svc_fw_policy_uuid)
        aps_obj = self._get_default_application_policy_set()
        self.assertIsNotNone(fw_policy_obj)
        self.assertIsNotNone(aps_obj)

        # Detach ingress policy from APS to introduce error.
        aps_obj.del_firewall_policy(fw_policy_obj)
        self._vnc_lib.application_policy_set_update(aps_obj)

        # Verify that validation of APS will fail.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertFalse(valid)

        # Fix the inconsisteny in APS.
        VncSecurityPolicy.recreate_cluster_security_policy()

        # Verify that validation of APS will succeed now.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        # Cleanup user created network policy.
        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_deny_all_policy_periodic_validate(self):
        """
        Validate network policy periodic self-healing when
        deny-all firewall policy is detached from APS.
        """

        # Check if we have a valid config to start with.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        # Create a network policy.
        np_name = unittest.TestCase.id(self)
        np_spec = {
            'podSelector': {},
            'policyTypes': ['Ingress', 'Egress']
        }

        # Create a user network policy.
        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        # Validate that config is sane after user policy add.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        # Get some basic object handles.
        self.assertIsNotNone(VncSecurityPolicy.deny_all_fw_policy_uuid)
        fw_policy_obj = self._vnc_lib.firewall_policy_read(
            id=VncSecurityPolicy.deny_all_fw_policy_uuid)
        aps_obj = self._get_default_application_policy_set()
        self.assertIsNotNone(fw_policy_obj)
        self.assertIsNotNone(aps_obj)

        # Detach deny-all policy from APS to introduce error.
        aps_obj.del_firewall_policy(fw_policy_obj)
        self._vnc_lib.application_policy_set_update(aps_obj)

        # Verify that validation of APS will fail.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertFalse(valid)

        # Fix the inconsisteny in APS.
        VncSecurityPolicy.recreate_cluster_security_policy()

        # Verify that validation of APS will succeed now.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        # Cleanup user created network policy.
        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_allow_all_policy_periodic_validate(self):
        """
        Validate network policy periodic self-healing when
        ingress-svc firewall policy is detached from APS.
        """

        # Check if we have a valid config to start with.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        # Create a network policy.
        np_name = unittest.TestCase.id(self)
        np_spec = {
            'podSelector': {},
            'policyTypes': ['Ingress', 'Egress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        # Validate that config is sane after user policy add.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        # Get some basic object handles.
        self.assertIsNotNone(VncSecurityPolicy.allow_all_fw_policy_uuid)
        fw_policy_obj = self._vnc_lib.firewall_policy_read(
            id=VncSecurityPolicy.allow_all_fw_policy_uuid)
        aps_obj = self._get_default_application_policy_set()
        self.assertIsNotNone(fw_policy_obj)
        self.assertIsNotNone(aps_obj)

        # Detach allow-all policy from APS to introduce error.
        aps_obj.del_firewall_policy(fw_policy_obj)
        self._vnc_lib.application_policy_set_update(aps_obj)

        # Verify that validation of APS will fail.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertFalse(valid)

        # Fix the inconsisteny in APS.
        VncSecurityPolicy.recreate_cluster_security_policy()

        # Verify that validation of APS will succeed now.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        # Cleanup user created network policy.
        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_periodic_validate_with_user_policies(self):
        """
        Validate network policy periodic self-healing when
        multiple user created policies are present.
        """

        np_uuid_dict = {}
        test_range = list(range(1, 10))
        for i in test_range:
            np_spec = {
                'podSelector': {},
                'ingress': [{}]
            }
            np_name = "-".join([unittest.TestCase.id(self), str(i)])
            np_uuid_dict[i] = self._add_update_network_policy(np_name, np_spec)
            self._validate_network_policy_resources(np_name, np_uuid_dict[i],
                                                    np_spec)

        # Check if we have a valid config to start with.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        # Get some basic object handles.
        self.assertIsNotNone(VncSecurityPolicy.allow_all_fw_policy_uuid)
        fw_policy_obj = self._vnc_lib.firewall_policy_read(
            id=VncSecurityPolicy.allow_all_fw_policy_uuid)
        aps_obj = self._get_default_application_policy_set()
        self.assertIsNotNone(fw_policy_obj)
        self.assertIsNotNone(aps_obj)

        # Detach allow-all policy from APS to introduce error.
        aps_obj.del_firewall_policy(fw_policy_obj)
        self._vnc_lib.application_policy_set_update(aps_obj)

        # Verify that validation of APS will fail.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertFalse(valid)

        # Fix the inconsisteny in APS.
        VncSecurityPolicy.recreate_cluster_security_policy()

        # Verify that validation of APS will succeed now.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        #
        # After self-healing, verify that the first on the APS, the FW policies
        # are ordered as follows:
        # - Ingress-svc fw policy
        # - User created policies
        # - Deny-all fw policy
        # - Allow-all fw policy
        #
        previous_sequence = None
        aps = ApplicationPolicySetKM.locate(aps_obj.get_uuid())
        aps.update()

        fw_policy_refs = aps.get_firewall_policy_refs_sorted()
        ingress_fw_policy_idx = None
        for index, fw_policy_ref in enumerate(fw_policy_refs):
            fw_policy = FirewallPolicyKM.locate(fw_policy_ref['uuid'])
            if fw_policy.owner and\
               fw_policy.cluster_name == self.cluster_name():
                self.assertTrue(fw_policy.uuid == VncSecurityPolicy.ingress_svc_fw_policy_uuid)
                ingress_fw_policy_idx = index
                break

        last_user_policy_index = None
        loop_start_index = ingress_fw_policy_idx + 1
        for i in test_range:
            np_name = "-".join([unittest.TestCase.id(self), str(i)])
            fw_policy_name = VncSecurityPolicy.get_firewall_policy_name(
                np_name, self.ns_name, False)
            for index, fw_policy in enumerate(fw_policy_refs[loop_start_index:]):
                if fw_policy_name == fw_policy['to'][-1]:
                    if previous_sequence:
                        self.assertTrue(
                            previous_sequence < fw_policy['attr']['sequence'])
                    previous_sequence = fw_policy['attr']['sequence']
                    last_user_policy_index = loop_start_index + index
                    break

        deny_all_policy_index = None
        loop_start_index = last_user_policy_index + 1
        for index, fw_policy_ref in enumerate(fw_policy_refs[loop_start_index:]):
            fw_policy = FirewallPolicyKM.locate(fw_policy_ref['uuid'])
            if fw_policy.cluster_name and\
               fw_policy.cluster_name == self.cluster_name():
                self.assertTrue(fw_policy.uuid == VncSecurityPolicy.deny_all_fw_policy_uuid)
                deny_all_policy_index = loop_start_index + index
                break

        loop_start_index = deny_all_policy_index + 1
        for fw_policy_ref in fw_policy_refs[loop_start_index:]:
            fw_policy = FirewallPolicyKM.locate(fw_policy_ref['uuid'])
            if fw_policy.cluster_name and\
               fw_policy.cluster_name == self.cluster_name():
                self.assertTrue(fw_policy.uuid == VncSecurityPolicy.allow_all_fw_policy_uuid)
                break

        for i in test_range:
            self._delete_network_policy(unittest.TestCase.id(self), np_uuid_dict[i])
            self._validate_network_policy_resources(
                np_name, np_uuid_dict[i],
                np_spec, validate_delete=True)

    def test_network_policy_ordering_resolve_during_modify(self):

        # Check if we have a valid config to start with.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

        # Get deny-all object handle.
        self.assertIsNotNone(VncSecurityPolicy.deny_all_fw_policy_uuid)
        fw_policy_obj = self._vnc_lib.firewall_policy_read(
            id=VncSecurityPolicy.deny_all_fw_policy_uuid)
        aps_obj = self._get_default_application_policy_set()
        self.assertIsNotNone(fw_policy_obj)
        self.assertIsNotNone(aps_obj)

        # Detach deny-all policy from APS to introduce error.
        aps_obj.del_firewall_policy(fw_policy_obj)
        self._vnc_lib.application_policy_set_update(aps_obj)

        # Verify that validation of APS will fail.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertFalse(valid)

        # Add deny-all policy to TAIL.
        # This is essentially an error condition where there is already
        # post-tail objects in the APS, but the deny-all gets added after
        # post-tail objects.
        VncSecurityPolicy.add_firewall_policy(
            VncSecurityPolicy.deny_all_fw_policy_uuid,
            tail=True)

        # Verify that validation of APS will fail.
        # Validation will fail because "tail" object is found after objects
        # that are marked as post-tail.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertFalse(valid)

        # Get allow-all object handle.
        self.assertIsNotNone(VncSecurityPolicy.allow_all_fw_policy_uuid)
        fw_policy_obj = self._vnc_lib.firewall_policy_read(
            id=VncSecurityPolicy.allow_all_fw_policy_uuid)
        aps_obj = self._get_default_application_policy_set()
        self.assertIsNotNone(fw_policy_obj)
        self.assertIsNotNone(aps_obj)

        # Re-add attempt of object marked post-tail should cause the post-tail
        # object to be re-arranged after tail, even though the object is already
        # present on the APS.
        VncSecurityPolicy.add_firewall_policy(
            VncSecurityPolicy.allow_all_fw_policy_uuid,
            append_after_tail=True)

        # Validation of APS should now succeed.
        valid = VncSecurityPolicy.validate_cluster_security_policy()
        self.assertTrue(valid)

    def test_add_np_allow_app_to_web_with_ingress_podnsselector(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'ingress': [{
                'from': [
                    {'ipBlock': {'cidr': '172.17.0.0/16',
                                 'except': ['172.17.1.0/24']}},
                    {
                        'namespaceSelector': {
                            'matchLabels': {
                                'deployment': 'HR',
                                'site': 'SVL'
                            }
                        },
                        'podSelector': {'matchLabels': {'tier': 'app'}}
                    }
                ],
                'ports': [
                    {
                        'port': 5978,
                        'protocol': 'TCP'
                    }
                ]}
            ],
            'egress': [
                {
                    'ports': [
                        {
                            'port': 5978,
                            'protocol': 'TCP'
                        }
                    ],
                    'to': [
                        {
                            'ipBlock': {'cidr': u'10.0.0.0/24'}
                        }
                    ]
                }
            ],
            'podSelector': {'matchLabels': {'tier': 'web'}},
            'policyTypes': ['Ingress', 'Egress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_np_allow_app_to_web_with_ingress_podnsselector_allowany(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'ingress': [{
                'from': [
                    {'ipBlock': {'cidr': '172.17.0.0/16',
                                 'except': ['172.17.1.0/24']}},
                    {'namespaceSelector': {
                        'matchLabels': {}
                    }, 'podSelector': {'matchLabels': {}}}
                ],
                'ports': [
                    {
                        'port': 5978,
                        'protocol': 'TCP'
                    }
                ]}
            ],
            'egress': [
                {
                    'ports': [
                        {
                            'port': 5978,
                            'protocol': 'TCP'
                        }
                    ],
                    'to': [
                        {
                            'ipBlock': {'cidr': u'10.0.0.0/24'}
                        }
                    ]
                }
            ],
            'podSelector': {'matchLabels': {'tier': 'web'}},
            'policyTypes': ['Ingress', 'Egress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_np_allow_app_to_web_with_egress_podnsselector(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'ingress': [{
                'from': [
                    {'ipBlock': {'cidr': '172.17.0.0/16',
                                 'except': ['172.17.1.0/24']}},
                    {'namespaceSelector': {
                        'matchLabels': {
                            'deployment': 'HR',
                            'site': 'SVL'
                        }
                    }},
                    {'podSelector': {'matchLabels': {'tier': 'app'}}}
                ],
                'ports': [
                    {
                        'port': 5978,
                        'protocol': 'TCP'
                    }
                ]
            }],
            'egress': [
                {
                    'ports': [
                        {
                            'port': 5978,
                            'protocol': 'TCP'
                        }
                    ],
                    'to': [
                        {'namespaceSelector': {
                            'matchLabels': {
                                'deployment': 'HR',
                                'site': 'SVL'
                            }
                        }, 'podSelector': {'matchLabels': {'tier': 'app'}}
                        }
                    ]
                }
            ],
            'podSelector': {'matchLabels': {'tier': 'web'}},
            'policyTypes': ['Ingress', 'Egress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)

    def test_add_np_allow_app_to_web_with_egress_podnsselector_allowany(self):
        # Create namespace.
        self._create_namespace(self.ns_name, None, True)

        np_name = unittest.TestCase.id(self)
        np_spec = {
            'ingress': [{
                'from': [
                    {'ipBlock': {'cidr': '172.17.0.0/16',
                                 'except': ['172.17.1.0/24']}},
                    {'namespaceSelector': {
                        'matchLabels': {
                            'deployment': 'HR',
                            'site': 'SVL'
                        }
                    }},
                    {'podSelector': {'matchLabels': {'tier': 'app'}}}
                ],
                'ports': [
                    {
                        'port': 5978,
                        'protocol': 'TCP'
                    }
                ]
            }],
            'egress': [
                {
                    'ports': [
                        {
                            'port': 5978,
                            'protocol': 'TCP'
                        }
                    ],
                    'to': [
                        {'namespaceSelector': {
                            'matchLabels': {}
                        }, 'podSelector': {'matchLabels': {}}
                        }
                    ]
                }
            ],
            'podSelector': {'matchLabels': {'tier': 'web'}},
            'policyTypes': ['Ingress', 'Egress']
        }

        np_uuid = self._add_update_network_policy(np_name, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                namespace=self.ns_name)

        self._delete_network_policy(np_name, np_uuid, np_spec)
        self._validate_network_policy_resources(np_name, np_uuid, np_spec,
                                                validate_delete=True,
                                                namespace=self.ns_name)
