#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import gevent
import gevent.monkey
gevent.monkey.patch_all()
import sys
import logging
import subprocess
from flexmock import flexmock
import json
import uuid

from vnc_api.vnc_api import *
from vnc_api.gen.resource_test import *

sys.path.append('../common/tests')
from test_utils import *
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestExecuteJob(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestExecuteJob, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestExecuteJob, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def test_execute_job(self):
        # mock the call to invoke the job manager
        flexmock(subprocess).should_receive('Popen')

        # create the input json
        job_template_id = uuid.uuid4()

        # create test device object
        phy_router_obj = PhysicalRouter(
            parent_type='global-system-config',
            fq_name=["default-global-system-config","test_device"],
            physical_router_management_ip="1.1.1.1",
            physical_router_vendor_name="juniper",
            physical_router_product_name="mx240",
            physical_router_user_credentials={"username": "username",
                                              "password": "password"},
            physical_router_device_family='juniper-mx')
        pr_uuid = self._vnc_lib.physical_router_create(phy_router_obj)

        execute_job_body = json.dumps({'job_template_id': str(job_template_id),
                                       'input':
                                           {'data': 'Playbook input data'},
                                       'params':
                                           {'device_list': [str(pr_uuid)]}})

        (code, msg) = self._http_post('/execute-job', execute_job_body)
        self.assertEqual(code, 200)

