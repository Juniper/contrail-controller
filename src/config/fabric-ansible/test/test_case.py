#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import sys
from cfgm_common.tests import test_common
sys.path.insert(0, '../../../../build/production/config/fabric-ansible/')
sys.path.insert(0, '../../../../build/debug/config/fabric-ansible/job_manager')

from vnc_api.vnc_api import *


class JobTestCase(test_common.TestCase):

    @classmethod
    def setUpClass(cls, extra_config_knobs=None):
        extra_config = [
            ('DEFAULTS', 'multi_tenancy', 'False'),
            ('DEFAULTS', 'aaa_mode', 'no-auth'),
        ]
        if extra_config_knobs:
            extra_config.append(extra_config_knobs)
        super(JobTestCase, cls).setUpClass(extra_config_knobs=extra_config)

    @classmethod
    def tearDownClass(cls):
        super(JobTestCase, cls).tearDownClass()

    def setUp(self, extra_config_knobs=None):
        super(JobTestCase, self).setUp(extra_config_knobs=extra_config_knobs)
        return

    def tearDown(self):
        super(JobTestCase, self).tearDown()

