#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import gevent
import gevent.monkey
gevent.monkey.patch_all(thread=False)
import sys
import uuid
import os
import logging
from flexmock import flexmock
import json
import subprocess

from vnc_api.vnc_api import PlaybookInfoType
from vnc_api.vnc_api import PlaybookInfoListType
from vnc_api.vnc_api import JobTemplate

sys.path.append('../common/cfgm_common/tests')
from test_utils import *
import test_case
from job_mgr import JobManager
from job_log_utils import JobLogUtils
from job_utils import JobStatus
from sandesh_utils import SandeshUtils

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestJobManager(test_case.JobTestCase):
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

    def test_execute_job_success(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper',
                                     device_family='MX')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        self.mock_play_book_execution()
        self.mock_sandesh_check()

        # Hardcoding a sample auth token since its a madatory value passed
        # by the api server while invoking the job manager. Here since we
        # are directly invoking the job manager, we are passing a dummy
        # auth token for testing. This value is not used internally since
        # the calls that use the auth token are mocked
        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}}
        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)
        jm.start_job()
        self.assertEqual(jm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    # to test the case when only device vendor is passed in job_template_input
    def test_execute_job_with_vendor_only(self):
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template_multidevice')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mock the play book executor call
        self.mock_play_book_execution()
        self.mock_sandesh_check()
        execution_id = uuid.uuid4()

        # Hardcoding a sample auth token since its a madatory value passed
        # by the api server while invoking the job manager. Here since we
        # are directly invoking the job manager, we are passing a dummy
        # auth token for testing. This value is not used internally since
        # the calls that use the auth token are mocked
        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "params": {"device_list":
                                     ["aad74e24-a00b-4eb3-8412-f8b9412925c3"]},
                          "device_json": {
                              "aad74e24-a00b-4eb3-8412-f8b9412925c3":
                                  {'device_vendor': 'Juniper',
                                   'device_family': 'MX'}
                          },
                          "args": {"collectors": ['127.0.0.1:8086']}}
        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(), self._vnc_lib,
                        job_input_json, log_utils)
        jm.start_job()
        self.assertEqual(jm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    # to test the case when device vendor and multiple device families are
    # passed in job_template_input
    def test_execute_job_multiple_device_families(self):
        play_info_mx = PlaybookInfoType(playbook_uri='job_manager_test.yaml',
                                        vendor='Juniper', device_family='MX')
        play_info_qfx = PlaybookInfoType(playbook_uri='job_manager_test.yaml',
                                         vendor='Juniper', device_family='QFX')

        playbooks_list = PlaybookInfoListType(
                                   playbook_info=[play_info_qfx, play_info_mx])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template_multidevfamilies')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mock the play book executor call
        self.mock_play_book_execution()
        self.mock_sandesh_check()
        execution_id = uuid.uuid4()

        # Hardcoding a sample auth token since its a madatory value passed
        # by the api server while invoking the job manager. Here since we
        # are directly invoking the job manager, we are passing a dummy
        # auth token for testing. This value is not used internally since
        # the calls that use the auth token are mocked
        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "params": {"device_list":
                                     ["aad74e24-a00b-4eb3-8412-f8b9412925c3"]},
                          "device_json": {
                              "aad74e24-a00b-4eb3-8412-f8b9412925c3":
                                  {'device_vendor': 'Juniper',
                                   'device_family': 'MX'}
                          },
                          "args": {"collectors": ['127.0.0.1:8086']}}
        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(), self._vnc_lib,
                        job_input_json, log_utils)
        jm.start_job()
        self.assertEqual(jm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    # to test the case when multiple device vendors are
    # passed in job_template_input
    def test_execute_job_multiple_vendors(self):
        play_info_juniper_mx = PlaybookInfoType(
                                     playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper', device_family='MX')
        play_info_juniper_qfx = PlaybookInfoType(
                                     playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper', device_family='QFX')
        play_info_arista_df = PlaybookInfoType(
                                     playbook_uri='job_manager_test.yaml',
                                     vendor='Arista', device_family='df')
        playbooks_list = PlaybookInfoListType(
            playbook_info=[play_info_arista_df,
                           play_info_juniper_qfx,
                           play_info_juniper_mx])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template_multivendors')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mock the play book executor call
        self.mock_play_book_execution()
        self.mock_sandesh_check()
        execution_id = uuid.uuid4()

        # Hardcoding a sample auth token since its a madatory value passed
        # by the api server while invoking the job manager. Here since we
        # are directly invoking the job manager, we are passing a dummy
        # auth token for testing. This value is not used internally since
        # the calls that use the auth token are mocked
        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "params": {"device_list":
                                     ["aad74e24-a00b-4eb3-8412-f8b9412925c3"]},
                          "device_json": {
                              "aad74e24-a00b-4eb3-8412-f8b9412925c3":
                                  {'device_vendor': 'Juniper',
                                   'device_family': 'MX'
                                   }
                              },
                          "args": {"collectors": ['127.0.0.1:8086']}}
        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(), self._vnc_lib,
                        job_input_json, log_utils)
        jm.start_job()
        self.assertEqual(jm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    # to test the case when no vendor is passed in job_template_input
    def test_execute_job_no_vendor(self):
        play_info_juniper_qfx = PlaybookInfoType(
                                     playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper', device_family='QFX')
        play_info_vendor_agnostic = PlaybookInfoType(
            playbook_uri='job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(
            playbook_info=[play_info_juniper_qfx,
                           play_info_vendor_agnostic])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='Test_template_no_vendor')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        # mock the play book executor call
        # check if the playbook is getting executed after job_manager
        # validation
        self.mock_play_book_execution()
        self.mock_sandesh_check()
        execution_id = uuid.uuid4()

        # Hardcoding a sample auth token since its a madatory value passed
        # by the api server while invoking the job manager. Here since we
        # are directly invoking the job manager, we are passing a dummy
        # auth token for testing. This value is not used internally since
        # the calls that use the auth token are mocked
        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "params": {
                              "device_list":
                                  ["aad74e24-a00b-4eb3-8412-f8b9412925c3"]},
                          "device_json": {
                              "aad74e24-a00b-4eb3-8412-f8b9412925c3":
                                  {'device_vendor': 'Juniper',
                                   'device_family': 'MX'
                                   }
                              },
                          "args": {"collectors": ['127.0.0.1:8086']}}
        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(), self._vnc_lib,
                        job_input_json, log_utils)
        jm.start_job()
        self.assertEqual(jm.result_handler.job_result_status,
                         JobStatus.SUCCESS)

    def mock_play_book_execution(self):
        # mock the call to invoke the playbook
        fake_process = flexmock(returncode=0)
        fake_process.should_receive('wait')
        flexmock(subprocess).should_receive('Popen').and_return(fake_process)

        # mock the call to invoke the playbook process
        flexmock(os.path).should_receive('exists').and_return(True)

        # mock sys exit call
        flexmock(sys).should_receive('exit')

    def mock_sandesh_check(self):
        mocked_sandesh_utils = flexmock()
        flexmock(SandeshUtils, __new__=mocked_sandesh_utils)
        mocked_sandesh_utils.should_receive('__init__')
        mocked_sandesh_utils.should_receive('wait_for_connection_establish')

