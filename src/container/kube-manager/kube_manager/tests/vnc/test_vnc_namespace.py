#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

import uuid

from kube_manager.tests.vnc.test_case import KMTestCase
from kube_manager.kube_manager import NoIdError
from kube_manager.vnc import vnc_kubernetes_config as kube_config
from kube_manager.vnc.vnc_namespace import NamespaceKM

class VncNamespaceTest(KMTestCase):
    def setUp(self, extra_config_knobs=None):
        super(VncNamespaceTest, self).setUp(
            extra_config_knobs=extra_config_knobs)
    #end setUp

    def tearDown(self):
        super(VncNamespaceTest, self).tearDown()
    #end tearDown

    def test_add_namespace_when_cluster_project_is_defined(self):
        cluster_project = "cluster"
        kube_config.VncKubernetesConfig.args().cluster_project = \
            "{'%s': '%s'}" % ('project', cluster_project)

        # Add first namespace
        ns1_name = "ns1"
        ns1_uuid = str(uuid.uuid4())
        ns1_add_event = self.create_add_namespace_event(ns1_name, ns1_uuid)
        self.enqueue_event(ns1_add_event)
        self.wait_for_all_tasks_done()

        # Check for project
        proj = self._vnc_lib.project_read(fq_name=["default-domain",
                                                   cluster_project])
        self.assertIsNotNone(proj)
        self.assertEquals(cluster_project, proj.name)
        self.assertRaises(
            NoIdError,
            self._vnc_lib.project_read,
            fq_name=["default-domain", ns1_name])

    def test_add_namespace_when_cluster_project_is_undefined(self):
        kube_config.VncKubernetesConfig.args().cluster_project = None

        # Add first namespace
        ns1_name = "ns1"
        ns1_uuid = str(uuid.uuid4())
        ns1_add_event = self.create_add_namespace_event(ns1_name, ns1_uuid)
        self.enqueue_event(ns1_add_event)
        self.wait_for_all_tasks_done()

        proj = self._vnc_lib.project_read(fq_name=["default-domain", ns1_name])
        self.assertIsNotNone(proj)
        self.assertEquals(ns1_name, proj.name)

    def test_add_namespace_with_isolation_annotation_true(self):
        kube_config.VncKubernetesConfig.args().cluster_project = None
        annotations = {
            'opencontrail.org/isolation': "true"
        }

        # Add first namespace
        ns1_name = "isolated_ns1"
        ns1_uuid = str(uuid.uuid4())
        ns1_add_event = self.create_add_namespace_event(ns1_name, ns1_uuid)
        ns1_add_event['object']['metadata']['annotations'] = annotations
        NamespaceKM.locate(ns1_uuid, ns1_add_event['object'])
        self.enqueue_event(ns1_add_event)
        self.wait_for_all_tasks_done()

        proj = self._vnc_lib.project_read(fq_name=["default-domain", ns1_name])
        self.assertIsNotNone(proj)
        self.assertEquals(ns1_name, proj.name)

        fqname = ['default-domain', ns1_name, ns1_name+'-vn']
        vn = self._vnc_lib.virtual_network_read(fq_name=fqname)
        self.assertIsNotNone(vn)
        self.assertEquals(ns1_name+'-vn', vn.name)

    def test_add_namespace_with_isolation_annotation_false(self):
        kube_config.VncKubernetesConfig.args().cluster_project = None
        annotations = {
            'opencontrail.org/isolation': 'false'
        }

        # Add first namespace
        ns1_name = 'isolated_ns1'
        ns1_uuid = str(uuid.uuid4())
        ns1_add_event = self.create_add_namespace_event(ns1_name, ns1_uuid)
        ns1_add_event['object']['metadata']['annotations'] = annotations
        self.enqueue_event(ns1_add_event)
        self.wait_for_all_tasks_done()

        proj = self._vnc_lib.project_read(fq_name=["default-domain", ns1_name])
        self.assertIsNotNone(proj)
        self.assertEquals(ns1_name, proj.name)

        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_network_read,
            fq_name=['default-domain', 'isloated_ns1', ns1_name+'-vn'])

    def test_add_namespace_with_custom_network_annotation(self):
        kube_config.VncKubernetesConfig.args().cluster_project = None
        # Create network for Pod
        proj_fq_name = ['default-domain', 'default']
        proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        self.create_network('network1', proj_obj)
        # Add first namespace
        eval_vn_dict = \
            '{"domain":"default-domain","project":"default","name":"network1"}'
        ns1_name = 'namespace1'
        ns1_uuid = str(uuid.uuid4())
        ns1_add_event = self.create_add_namespace_event(ns1_name, ns1_uuid)
        ns1_annotations = {
            'opencontrail.org/network': eval_vn_dict
        }
        ns1_add_event['object']['metadata']['annotations'] = ns1_annotations
        NamespaceKM.locate(ns1_uuid, ns1_add_event['object'])
        self.enqueue_event(ns1_add_event)
        self.wait_for_all_tasks_done()

        proj = self._vnc_lib.project_read(fq_name=["default-domain", ns1_name])
        self.assertIsNotNone(proj)
        self.assertEquals(ns1_name, proj.name)

        fqname = ['default-domain', 'default', 'network1']
        vn = self._vnc_lib.virtual_network_read(fq_name=fqname)
        self.assertIsNotNone(vn)
        self.assertEquals('network1', vn.name)
