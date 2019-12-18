#!/usr/bin/python
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
from builtins import str
import gevent
import gevent.monkey
gevent.monkey.patch_all(thread=False)
import sys
import os
import io
import logging
from flexmock import flexmock
import subprocess32
import requests
import uuid
from vnc_api.vnc_api import PlaybookInfoType
from vnc_api.vnc_api import PlaybookInfoListType
from vnc_api.vnc_api import JobTemplate

from . import test_case
from job_manager.job_mgr import WFManager
from job_manager.job_utils import JobStatus
from job_manager.job_utils import PLAYBOOK_EOL_PATTERN
from cfgm_common.tests.test_utils import FakeKazooClient
from .test_job_manager_utils import TestJobManagerUtils

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestJobManager(test_case.JobTestCase):
    fake_zk_client = FakeKazooClient()
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestJobManager, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestJobManager, cls).tearDownClass(*args, **kwargs)

    # end tearDownClass

    # Test for a single playbook in the workflow template
    def test_execute_job_success(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yml',
                                     vendor='Juniper',
                                     device_family='MX', sequence_no=0)
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='workflow',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mocking creation of a process
        self.mock_play_book_execution()
        # mocking Sandesh
        TestJobManagerUtils.mock_sandesh_check()
        # getting details required for job manager execution
        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(), self._vnc_lib,
                       job_input_json, log_utils, self.fake_zk_client)
        wm.start_job()
        self.assertEqual(wm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    # Test for job success with multiple playbooks in the workflow template
    def test_execute_job_success_multiple_templates(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yml',
                                     vendor='Juniper',
                                     device_family='MX', sequence_no=0)
        play_info1 = PlaybookInfoType(
            playbook_uri='job_manager_test_multiple.yml',
            vendor='Juniper',
            device_family='QFX', sequence_no=1)
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info,
                                                             play_info1])
        job_template = JobTemplate(job_template_type='workflow',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template1')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mocking creation of a process
        self.mock_play_book_execution()
        # mocking Sandesh
        TestJobManagerUtils.mock_sandesh_check()
        # getting details required for job manager execution
        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(), self._vnc_lib,
                       job_input_json, log_utils, self.fake_zk_client)
        wm.start_job()
        self.assertEqual(wm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    # to test the case when only device vendor is passed in job_template_input
    def test_execute_job_with_vendor_only(self):
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper', sequence_no=0)
        play_info1 = PlaybookInfoType(
            playbook_uri='job_manager_test_multiple.yml',
            vendor='Juniper', sequence_no=1)

        playbooks_list = PlaybookInfoListType(
            playbook_info=[play_info, play_info1])
        job_template = JobTemplate(job_template_type='workflow',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template_multidevice')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mock the play book executor call
        self.mock_play_book_execution()
        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = self.get_details(job_template_uuid)
        wm = WFManager(log_utils.get_config_logger(), self._vnc_lib,
                       job_input_json, log_utils, self.fake_zk_client)

        wm.start_job()
        self.assertEqual(wm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    # to test the case when device vendor and multiple device families are
    # passed in job_template_input
    def test_execute_job_multiple_device_families(self):
        play_info_mx = PlaybookInfoType(
            playbook_uri='job_manager_test.yaml',
            vendor='Juniper', device_family='MX', sequence_no=0)
        play_info_qfx = PlaybookInfoType(
            playbook_uri='job_manager_test.yaml',
            vendor='Juniper', device_family='QFX', sequence_no=1)

        playbooks_list = PlaybookInfoListType(
            playbook_info=[play_info_qfx, play_info_mx])
        job_template = JobTemplate(job_template_type='workflow',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template_multidevfamilies')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mock the play book executor call
        self.mock_play_book_execution()

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = self.get_details(job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(), self._vnc_lib,
                       job_input_json, log_utils, self.fake_zk_client)

        wm.start_job()
        self.assertEqual(wm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    # to test the case when device vendor and multiple device families are
    # passed in workflow_template_input along with multiple playbooks
    # TODO - The task weightage array from be gotten from the
    # TODO - workflow_template. Currently the test case
    # TODO - fails beacuse the task weightage array is hard coded.
    def test_execute_job_multiple_device_families_multiple_playbooks(self):
        play_info_mx = PlaybookInfoType(
            playbook_uri='job_manager_test.yaml',
            vendor='Juniper', device_family='MX', sequence_no=0)
        play_info_qfx = PlaybookInfoType(
            playbook_uri='job_manager_test.yaml',
            vendor='Juniper', device_family='QFX', sequence_no=1)
        play_info_2 = PlaybookInfoType(
            playbook_uri='job_manager_test2.yaml',
            vendor='Juniper', sequence_no=2)

        playbooks_list = PlaybookInfoListType(
            playbook_info=[play_info_qfx, play_info_mx, play_info_2])
        job_template = JobTemplate(job_template_type='workflow',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template_multi_devfamilies')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mock the play book executor call
        self.mock_play_book_execution()

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = self.get_details(job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(), self._vnc_lib,
                       job_input_json, log_utils, self.fake_zk_client)

        wm.start_job()
        self.assertEqual(wm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    # to test the case when multiple device vendors and multiple_playbooks are
    # passed in job_template_input
    # TODO - Test case fails for the same reason as above. The
    # TODO - hardcoded array only considers chaining of two playbooks.
    def test_execute_job_multiple_vendors_multiple_playbooks(self):
        play_info_juniper_mx = PlaybookInfoType(
            playbook_uri='job_manager_test.yaml',
            vendor='Juniper', device_family='MX', sequence_no=0)
        play_info_juniper_qfx = PlaybookInfoType(
            playbook_uri='job_manager_test.yaml',
            vendor='Juniper', device_family='QFX', sequence_no=1)
        play_info_arista_df = PlaybookInfoType(
            playbook_uri='job_manager_test2.yaml',
            vendor='Arista', device_family='df', sequence_no=2)
        playbooks_list = PlaybookInfoListType(
            playbook_info=[play_info_arista_df,
                           play_info_juniper_qfx,
                           play_info_juniper_mx])
        job_template = JobTemplate(job_template_type='workflow',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template_multivendors')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mock the play book executor call
        self.mock_play_book_execution()

        TestJobManagerUtils.mock_sandesh_check()
        job_input_json, log_utils = self.get_details(job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(), self._vnc_lib,
                       job_input_json, log_utils, self.fake_zk_client)

        wm.start_job()
        self.assertEqual(wm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    # to test the case when no vendor is passed in workflow_template_input for
    # second playbook_info - Depending on the device vendor, the right
    # playbooks has to be picked.
    def test_execute_job_no_vendor(self):
        play_info_juniper_qfx = PlaybookInfoType(
            playbook_uri='job_manager_test.yaml',
            vendor='Juniper', device_family='QFX', sequence_no=0)
        play_info_vendor_agnostic = PlaybookInfoType(
            playbook_uri='job_manager_test2.yaml', sequence_no=1)
        playbooks_list = PlaybookInfoListType(
            playbook_info=[play_info_juniper_qfx,
                           play_info_vendor_agnostic])
        job_template = JobTemplate(job_template_type='workflow',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template_no_vendor')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mock the play book executor call
        # check if the playbook is getting executed after job_manager
        # validation
        self.mock_play_book_execution()

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = self.get_details(job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(), self._vnc_lib,
                       job_input_json, log_utils, self.fake_zk_client)

        wm.start_job()
        self.assertEqual(wm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    def get_details(self, job_template_uuid):
        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)
        job_input_json.update({
            "params": {"device_list": [
                "aad74e24-a00b-4eb3-8412-f8b9412925c3"]},
            "device_json": {
                "aad74e24-a00b-4eb3-8412-f8b9412925c3":
                    {'device_vendor': 'Juniper',
                     'device_family': 'MX',
                     'device_username': 'username',
                     'device_password': 'pswd',
                     'device_product': 'QFX-10002',
                     'device_fqname': ['default-global-system-config',
                                       'random_device_fqname']}}
        })

        return job_input_json, log_utils

    def mock_play_book_execution(self):
        mock_subprocess32 = flexmock(subprocess32)
        playbook_id = uuid.uuid4()
        mock_unique_pb_id = flexmock(uuid)
        fake_process = flexmock(returncode=0, pid=123)
        fake_process.should_receive('wait')
        mock_subprocess32.should_receive('Popen').and_return(
            fake_process)
        mock_unique_pb_id.should_receive('uuid4').and_return(playbook_id)

        fake_process.should_receive('poll').and_return(123)
        # mock the call to invoke the playbook process
        flexmock(os.path).should_receive('exists').and_return(True)

        # mock the call to write an END to the file
        with open("/tmp/"+TestJobManagerUtils.execution_id, "a") as f:
            f.write(str(playbook_id) + 'END' + PLAYBOOK_EOL_PATTERN)

        # mock sys exit call
        flexmock(sys).should_receive('exit')
        fake_resp = flexmock(status_code=123)
        fake_request = flexmock(requests).should_receive(
                           'post').and_return(fake_resp)

