#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

import mock
import unittest
from mock import patch

from gevent.queue import Queue
from test_case import KMTestCase
from kube_manager.vnc.config_db import *
from kube_manager.vnc.loadbalancer import *
from kube_manager.vnc.config_db import *
from kube_manager.kube_manager import *
from kube_manager.vnc import db
from kube_manager import kube_manager
from kube_manager.vnc import vnc_namespace
from kube_manager.vnc import vnc_pod

class VncPodTest(KMTestCase):
    def setUp(self):
        super(VncPodTest, self).setUp()
    #end setUp

    def tearDown(self):
        super(VncPodTest, self).tearDown()
    #end tearDown

    def test_vnc_pod_add(self):
        pass

    def test_vnc_pod_delete(self):
        pass
