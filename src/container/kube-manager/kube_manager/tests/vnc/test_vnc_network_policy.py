#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

import uuid
from kube_manager.tests.vnc.test_case import KMTestCase


class VncNetworkPolicyTest(KMTestCase):

    def setUp(self, extra_config_knobs=None):
        super(VncNetworkPolicyTest, self).setUp()

    def tearDown(self):
        super(VncNetworkPolicyTest, self).tearDown()

    def test_vnc_network_policy_add_test(self):
        _, _, network_policy_uuid = self._enqueue_add_network_policy()
        self.wait_for_all_tasks_done()

        sg = self._find_security_group(network_policy_uuid)

        self.assertEquals(1, len(sg.get_security_group_entries().get_policy_rule()))
        policy_rule = sg.get_security_group_entries().get_policy_rule()[0]

        self._assert_policy_rule(policy_rule)

        self._enqueue_delete_network_policy(network_policy_uuid)
        self.wait_for_all_tasks_done()

    def test_vnc_network_policy_add_test_in_custom_namespace(self):
        ns_name, ns_uuid = self._enqueue_add_namespace(namespace_name='ns')
        self.wait_for_all_tasks_done()

        _, _, network_policy_uuid = self._enqueue_add_network_policy(namespace_name='ns')
        self.wait_for_all_tasks_done()

        sg = self._find_security_group(network_policy_uuid)

        policy_rule = sg.get_security_group_entries().get_policy_rule()[0]
        self.assertEquals(1, len(sg.get_security_group_entries().get_policy_rule()))

        self._assert_policy_rule(policy_rule)

        self._enqueue_delete_network_policy(network_policy_uuid, namespace_name='ns')
        self._enqueue_delete_namespace(ns_name, ns_uuid)
        self.wait_for_all_tasks_done()

    def _assert_policy_rule(self, policy_rule):
        dst_ports = policy_rule.get_dst_ports()[0]
        src_ports = policy_rule.get_src_ports()[0]
        self.assertEquals(6379, dst_ports.get_start_port())
        self.assertEquals(6379, dst_ports.get_end_port())
        self.assertEquals('tcp', policy_rule.get_protocol())
        self.assertEquals(0, src_ports.get_start_port())
        self.assertEquals(65535, src_ports.get_end_port())

    def _find_security_group(self, network_policy_uuid):
        sg_uuids = [sg['uuid'] for sg in self._vnc_lib.security_groups_list().values()[0]]
        sgs = [self._vnc_lib.security_group_read(id=sg_uuid) for sg_uuid in sg_uuids]
        network_policy_sg = [sg for sg in sgs if sg.uuid == network_policy_uuid]
        if not network_policy_sg:
            self.fail("Security group of new Network policy did not found")
        return network_policy_sg[0]


    def _enqueue_add_network_policy(self, namespace_name='default'):
        srv_uuid = str(uuid.uuid4())
        srv_meta = {'name': 'test-network-policy', 'uid': srv_uuid, 'namespace': namespace_name}
        srv_spec = {
            "podSelector": {
                "matchLabels": {
                    "role": "db"
                }
            },
            "ingress": [
                {
                    "from": [
                        {
                            "namespaceSelector": {
                                "matchLabels": {
                                    "project": "myproject"
                                }
                            }
                        },
                        {
                            "podSelector": {
                                "matchLabels": {
                                    "role": "frontend"
                                }
                            }
                        }
                    ],
                    "ports": [
                        {
                            "protocol": "tcp",
                            "port": 6379
                        }
                    ]
                }
            ]
        }
        srv_add_event = self.create_event('NetworkPolicy', srv_spec, srv_meta, 'ADDED')
        self.enqueue_event(srv_add_event)
        return srv_meta, srv_spec, srv_uuid

    def _enqueue_delete_network_policy(self, srv_uuid, namespace_name='default'):
        # type: (str, str) -> None
        srv_meta = {'name': 'test-network-policy', 'uid': srv_uuid, 'namespace': namespace_name}
        srv_del_event = self.create_event('NetworkPolicy', {}, srv_meta, 'DELETED')
        self.enqueue_event(srv_del_event)

    def _enqueue_add_namespace(self, namespace_name='default', isolated=False):
        # type: (str, bool) -> (str, str)
        ns_uuid = str(uuid.uuid4())
        namespace_name = namespace_name
        ns_add_event = self.create_add_namespace_event(namespace_name, ns_uuid)
        if isolated:
            annotations = {'opencontrail.org/isolation': 'true'}
            ns_add_event['object']['metadata']['annotations'] = annotations
        self.enqueue_event(ns_add_event)
        return namespace_name, ns_uuid

    def _enqueue_delete_namespace(self, namespace_name, ns_uuid):
        # type: (str, str) -> None
        ns_delete_event = self.create_delete_namespace_event(namespace_name, ns_uuid)
        self.enqueue_event(ns_delete_event)
