#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import mock
import unittest
import uuid
import sys

from gevent.queue import Queue
from test_case import KMTestCase
from kube_manager.vnc.config_db import *
from kube_manager.vnc.loadbalancer import *
from kube_manager.vnc.config_db import *
from kube_manager.kube_manager import *
from kube_manager.vnc import db
from kube_manager import kube_manager
from kube_manager.vnc import vnc_namespace
from kube_manager.vnc import vnc_kubernetes_config as kube_config

class VncNamespaceTest(KMTestCase):
    def setUp(self):
        super(VncNamespaceTest, self).setUp()
    #end setUp

    def tearDown(self):
        super(VncNamespaceTest, self).tearDown()
    #end tearDown

    def test_vnc_namespace_add_delete(self):
        # Add Namespace
        ns_name = "guestbook"
        ns_uuid = str(uuid.uuid4())
        ns_add_event = self.create_add_namespace_event(ns_name, ns_uuid)
        self.enqueue_event(ns_add_event)

        #verify Add
        try:
            proj_obj = self._vnc_lib.project_read(id=ns_uuid)
        except NoIdError:
            pass
        proj_fq_name = ['default-domain', ns_name]
        try:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        except NoIdError:
            return None

        # Delete Namespace
        ns_del_event = self.create_delete_namespace_event(ns_name, ns_uuid)
        self.enqueue_event(ns_del_event)

        #verify delete
        try:
            proj_obj = self._vnc_lib.project_read(id=ns_uuid)
        except NoIdError:
            pass
        proj_fq_name = ['default-domain', ns_name]
        try:
            proj_obj = self._vnc_lib.project_read(fq_name=proj_fq_name)
        except NoIdError:
            pass

    def test_cluster_project_is_defined(self):
        cluster_project = "cluster"
        kube_config.VncKubernetesConfig.args().cluster_project = "{'project':'"+cluster_project+"'}"

        # Add first namespace
        ns1_name = "ns1"
        ns1_uuid = str(uuid.uuid4())
        ns1_add_event = self.create_add_namespace_event(ns1_name, ns1_uuid)
        self.enqueue_event(ns1_add_event)

        # Add second namespace
        ns2_name = "ns2"
        ns2_uuid = str(uuid.uuid4())
        ns2_add_event = self.create_add_namespace_event(ns2_name, ns2_uuid)
        self.enqueue_event(ns2_add_event)

        proj = self._vnc_lib.project_read(fq_name=["default-domain",cluster_project])
        self.assertIsNotNone(proj)
        self.assertEquals(cluster_project, proj.name)
        self.assertRaises(
            NoIdError,
            self._vnc_lib.project_read,
            fq_name=["default-domain",ns1_name])
        self.assertRaises(
            NoIdError,
            self._vnc_lib.project_read,
            fq_name=["default-domain",ns2_name])

    def test_cluster_project_is_undefined(self):
        kube_config.VncKubernetesConfig.args().cluster_project = None

        # Add first namespace
        ns1_name = "ns1"
        ns1_uuid = str(uuid.uuid4())
        ns1_add_event = self.create_add_namespace_event(ns1_name, ns1_uuid)
        self.enqueue_event(ns1_add_event)

        # Add second namespace
        ns2_name = "ns2"
        ns2_uuid = str(uuid.uuid4())
        ns2_add_event = self.create_add_namespace_event(ns2_name, ns2_uuid)
        self.enqueue_event(ns2_add_event)

        proj = self._vnc_lib.project_read(fq_name=["default-domain",ns1_name])
        self.assertIsNotNone(proj)
        self.assertEquals(ns1_name, proj.name)

        proj = self._vnc_lib.project_read(fq_name=["default-domain",ns2_name])
        self.assertIsNotNone(proj)
        self.assertEquals(ns2_name, proj.name)

    def test_namespace_is_isolated(self):
        annotations = {
            'opencontrail.org/isolation': "true"
        }

        # Add first namespace
        ns1_name = "isolated_ns1"
        ns1_uuid = str(uuid.uuid4())
        ns1_add_event = self.create_add_namespace_event(ns1_name, ns1_uuid)
        ns1_add_event['object']['metadata']['annotations'] = annotations
        self.enqueue_event(ns1_add_event)

        # Add second namespace
        ns2_name = "isolated_ns2"
        ns2_uuid = str(uuid.uuid4())
        ns2_add_event = self.create_add_namespace_event(ns2_name, ns2_uuid)
        ns2_annotations = {'isolated': True}
        ns2_add_event['object']['metadata']['annotations'] = annotations
        self.enqueue_event(ns2_add_event)

        vn = self._vnc_lib.virtual_network_read(fq_name=["default-domain","isloated_ns1",ns1_name+"-vn"])
        self.assertIsNotNone(vn)
        self.assertEquals(ns1_name, vn.name)

        vn = self._vnc_lib.virtual_network_read(fq_name=["default-domain","isolated_ns2",ns2_name+"-vn"])
        self.assertIsNotNone(vn)
        self.assertEquals(ns2_name, vn.name)

    def test_namespace_is_not_isolated(self):
        annotations = {
            'opencontrail.org/isolation': "false"
        }

        # Add first namespace
        ns1_name = "isolated_ns1"
        ns1_uuid = str(uuid.uuid4())
        ns1_add_event = self.create_add_namespace_event(ns1_name, ns1_uuid)
        ns1_add_event['object']['metadata']['annotations'] = annotations
        self.enqueue_event(ns1_add_event)

        # Add second namespace
        ns2_name = "isolated_ns2"
        ns2_uuid = str(uuid.uuid4())
        ns2_add_event = self.create_add_namespace_event(ns2_name, ns2_uuid)
        ns2_add_event['object']['metadata']['annotations'] = annotations
        self.enqueue_event(ns2_add_event)

        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_network_read,
            fq_name=["default-domain","isloated_ns1",ns1_name+"-vn"])
        self.assertRaises(
            NoIdError,
            self._vnc_lib.virtual_network_read,
            fq_name=["default-domain", "isloated_ns2",ns2_name+"-vn"])

    def test_namespace_is_not_isolated(self):
        # Add first namespace
        ns1_name = "isolated_ns1"
        ns1_uuid = str(uuid.uuid4())
        ns1_add_event = self.create_add_namespace_event(ns1_name, ns1_uuid)
        ns1_annotations = {
            'opencontrail.org/network': "network1"
        }
        ns1_add_event['object']['metadata']['annotations'] = ns1_annotations
        self.enqueue_event(ns1_add_event)

        # Add second namespace
        ns2_name = "isolated_ns2"
        ns2_uuid = str(uuid.uuid4())
        ns2_add_event = self.create_add_namespace_event(ns2_name, ns2_uuid)
        ns2_annotations = {
            'opencontrail.org/network': "network2"
        }
        ns2_add_event['object']['metadata']['annotations'] = ns2_annotations
        self.enqueue_event(ns2_add_event)

        vn = self._vnc_lib.virtual_network_read(fq_name=["default-domain", "isloated_ns1", "network1"])
        self.assertIsNotNone(vn)
        self.assertEquals(ns1_name, vn.name)

        vn = self._vnc_lib.virtual_network_read(fq_name=["default-domain", "isolated_ns2", "network2"])
        self.assertIsNotNone(vn)
        self.assertEquals(ns2_name, vn.name)