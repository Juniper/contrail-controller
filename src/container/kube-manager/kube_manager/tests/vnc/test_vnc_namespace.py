#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import mock
import unittest
import uuid

from gevent.queue import Queue
from test_case import KMTestCase
from kube_manager.vnc.config_db import *
from kube_manager.vnc.loadbalancer import *
from kube_manager.vnc.config_db import *
from kube_manager.kube_manager import *
from kube_manager.vnc import db
from kube_manager import kube_manager
from kube_manager.vnc import vnc_namespace

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

