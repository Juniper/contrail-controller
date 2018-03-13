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
from ansible.executor.playbook_executor import PlaybookExecutor

from vnc_api.vnc_api import *

sys.path.append('../common/tests')
from test_utils import *
import test_case
from job_mgr import JobManager
from logger import JobLogger

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

    def test_execute_job(self):
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

        # mock the play book executor call
        self.mock_play_book_executor()
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
                          "auth_token": sample_auth_token}
        logger = JobLogger()
        jm = JobManager(logger, self._vnc_lib, job_input_json)
        jm.start_job()

    # to test the case when only device vendor is passed in job_template_input
    def test_execute_job_02(self):
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
        self.mock_play_book_executor()
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
                                         }
                         }
        logger = JobLogger()
        jm = JobManager(logger, self._vnc_lib, job_input_json)
        jm.start_job()

    # to test the case when device vendor and multiple device families are
    # passed in job_template_input
    def test_execute_job_03(self):
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
        self.mock_play_book_executor()
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
                                         }
                         }
        logger = JobLogger()
        jm = JobManager(logger, self._vnc_lib, job_input_json)
        jm.start_job()

    # to test the case when multiple device vendors are
    # passed in job_template_input
    def test_execute_job_04(self):
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
        self.mock_play_book_executor()
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
                                         }
                         }
        logger = JobLogger()
        jm = JobManager(logger, self._vnc_lib, job_input_json)
        jm.start_job()

    # to test the case when no vendor is passed in job_template_input
    def test_execute_job_05(self):
        play_info_juniper_qfx = PlaybookInfoType(
                                     playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper', device_family='QFX')
        play_info_vendor_agnostic = PlaybookInfoType(playbook_uri=
                                                     'job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(playbook_info=
                                              [play_info_juniper_qfx,
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
        self.mock_play_book_executor()
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
                                          }
                         }
        logger = JobLogger()
        jm = JobManager(logger, self._vnc_lib, job_input_json)
        jm.start_job()

    def mock_play_book_executor(self):
        facts_dict = {"localhost":
                         {"output":
                             {"status": "Success",
                              "message": "Playbook executed successfully",
                              "results": {"discovered_pr_id": ["123"]}}}}
        mocked_var_mgr = flexmock(_nonpersistent_fact_cache=facts_dict)
        mocked_task_q_mgr = flexmock(_variable_manager=mocked_var_mgr)
        mocked_playbook_executor = flexmock(_tqm=mocked_task_q_mgr)
        flexmock(PlaybookExecutor, __new__=mocked_playbook_executor)
        mocked_playbook_executor.should_receive('__init__')
        mocked_playbook_executor.should_receive('run')

        flexmock(os.path).should_receive('exists').and_return(True)

