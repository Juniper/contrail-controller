#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import sys
import uuid
import subprocess32
import os
import json
import logging
from flexmock import flexmock

from vnc_api.vnc_api import PlaybookInfoType
from vnc_api.vnc_api import PlaybookInfoListType
from vnc_api.vnc_api import JobTemplate
from vnc_api.vnc_api import VncApi

from cfgm_common.exceptions import NoIdError

import test_case
from job_result_handler import JobResultHandler
from job_handler import JobHandler
from job_mgr import JobManager
from job_log_utils import JobLogUtils
from job_utils import JobUtils, JobStatus
from job_exception import JobException
from logger import JobLogger
from sandesh_utils import SandeshUtils
from job_messages import MsgBundle

from sandesh.job.ttypes import JobLog

import gevent
import gevent.monkey
gevent.monkey.patch_all(thread=False)

sys.path.append('../common/tests')

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestJobManagerEC(test_case.JobTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestJobManagerEC, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestJobManagerEC, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    # to test the case when sandesh initialization times out
    def test_sandesh_timeout(self):
        mocked_sandesh_utils = flexmock()
        flexmock(SandeshUtils, __new__=mocked_sandesh_utils)
        mocked_sandesh_utils.should_receive('__init__')
        mocked_sandesh_utils.should_receive('wait_for_connection_establish')\
                            .and_raise(JobException())
        args = {"collectors": ['127.0.0.1:8086']}
        exc_msg = self.assertRaises(
            JobException,
            JobLogUtils,
            "rdm_exc_id",
            json.dumps(args))
        self.assertEqual(str(exc_msg), "JobException in execution" +
                         " (None): " + MsgBundle.getMessage(
                         MsgBundle.SANDESH_INITIALIZATION_TIMEOUT_ERROR))

    # to test the case when job_template_id is missing while initializing the
    # job_manager
    def test_execute_job_no_template_id(self):

        job_input_json = {"no_template_id": "Missing template_id"}
        exc_msg = self.assertRaises(
            Exception,
            JobManager,
            None,
            None,
            job_input_json,
            None)
        self.assertEqual(str(exc_msg), MsgBundle.getMessage(
            MsgBundle.JOB_TEMPLATE_MISSING))

    # to test the case when job_execution_id is missing while initializing the
    # job_manager
    def test_execute_job_no_execution_id(self):

        job_input_json = {"job_template_id": "random_template_id"}
        exc_msg = self.assertRaises(
            Exception,
            JobManager,
            None,
            None,
            job_input_json,
            None)
        self.assertEqual(str(exc_msg), MsgBundle.getMessage(
            MsgBundle.JOB_EXECUTION_ID_MISSING))

    # to test the case when there is no job_template
    def test_execute_job_read_job_template(self):

        execution_id = str(uuid.uuid4())

        job_template_id = str(uuid.uuid4())

        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        job_input_json = {"job_template_id": job_template_id,
                          "job_execution_id": execution_id,
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}}

        mock_vnc = flexmock()
        flexmock(VncApi).new_instances(mock_vnc)

        flexmock(mock_vnc).should_receive('job_template_read')\
            .with_args(id=job_template_id)\
            .and_raise(NoIdError('No such job Template id'))
        jm = JobManager(log_utils.get_config_logger(),
                        mock_vnc, job_input_json, log_utils)
        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(
            sys_exit_msg.code,
            MsgBundle.getMessage(MsgBundle.JOB_EXC_REC_HDR) +
            MsgBundle.getMessage(MsgBundle.READ_JOB_TEMPLATE_ERROR,
                                 job_template_id=job_template_id)
            + " ")

    # to test the generic exception in handle_job
    def test_execute_job_generic_exception_handle_job(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=playbooks_list,
                                   name='handle_job_gen_exc')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        self.mock_sandesh_check()

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"
        exc_msg = 'Job Handler Generic Exception'

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}
                          }

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))

        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)

        # mock the job_handler to raise a general exception
        mock_job_handler = flexmock()
        flexmock(JobHandler).new_instances(mock_job_handler)

        flexmock(mock_job_handler).should_receive('handle_job')
        flexmock(mock_job_handler).should_receive('get_playbook_info')\
                                  .and_raise(
            Exception("Job Handler "
                      "Generic Exception"))

        mock_result_handler = flexmock(job_result_status=JobStatus.FAILURE,
                                       job_summary_message=exc_msg)
        flexmock(JobResultHandler).new_instances(mock_result_handler)
        flexmock(mock_result_handler).should_receive(
            'create_job_summary_message') .and_return(exc_msg)
        flexmock(mock_result_handler).should_receive('create_job_summary_log')
        flexmock(mock_result_handler).should_receive('update_job_status')\
                                     .with_args(JobStatus.FAILURE, exc_msg)

        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(sys_exit_msg.code, exc_msg)

    # to test generic exception from get_playbook_info
    def test_execute_job_handler_gen_exc(self):
        # create job template
        play_info = {}
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])

        fake_job_template = flexmock(job_template_type='device',
                                     job_template_job_runtime='ansible',
                                     job_template_multi_device_job=False,
                                     job_template_playbooks=playbooks_list,
                                     name='single_device_temp_jh_gen_exc',
                                     fq_name=["default-global-system-config",
                                              "single_device_temp_jh_gen_exc"],
                                     uuid='random_uuid')

        mock_vnc = flexmock()
        flexmock(VncApi).new_instances(mock_vnc)

        flexmock(mock_vnc).should_receive('job_template_read')\
            .with_args(id="random_uuid")\
            .and_return(fake_job_template)

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        execution_id = str(uuid.uuid4())
        job_input_json = {"job_template_id": "random_uuid",
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": execution_id,
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}}
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        job_utils_obj = JobUtils(execution_id, "random_uuid",
                                 log_utils.get_config_logger(), mock_vnc)
        jm = JobManager(log_utils.get_config_logger(),
                        mock_vnc, job_input_json, log_utils)

        fake_job_template.should_receive(
            'get_job_template_input_schema').and_return(None)
        fake_job_template.should_receive(
            'get_job_template_multi_device_job').and_return(False)
        fake_job_template.should_receive('get_uuid').and_return('random_uuid')
        # mock the job_handler to raise a general exception
        fake_job_template.should_receive('get_job_template_playbooks')\
            .and_raise(
            Exception("Mock "
                      "Generic Exception in job_handler"))

        e = Exception('Mock Generic Exception in job_handler')

        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(sys_exit_msg.code, MsgBundle.
                         getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
                         MsgBundle.
                         getMessage(MsgBundle.
                                    JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR) +
                         MsgBundle.
                         getMessage(MsgBundle.GET_PLAYBOOK_INFO_ERROR,
                                    job_template_id="random_uuid",
                                    exc_msg=repr(e)))

    # to test single_device_job when playbook is not present in the path
    def test_execute_job_no_pb_file_on_path(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=playbooks_list,
                                   name='single_device_template')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)
        playbook_info = {"uri": play_info.playbook_uri}
        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}}
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)

        # mock the call to invoke the playbook process
        flexmock(os.path).should_receive('exists').and_return(False)

        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(sys_exit_msg.code, MsgBundle.
                         getMessage(MsgBundle.
                                    JOB_SUMMARY_MESSAGE_HDR) +
                         MsgBundle.
                         getMessage(MsgBundle.
                                    JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR) +
                         MsgBundle.
                         getMessage(MsgBundle.PLAYBOOK_NOT_FOUND,
                                    playbook_uri=playbook_info['uri']))

    # to test single_device_job when there is a generic exception
    # from job_handler in job_manager

    def test_execute_job_generic_exception(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=playbooks_list,
                                   name='single_device_template_gen_exc')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}}
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)

        # mock the job_handler to raise a general exception
        mock_job_handler = flexmock()
        flexmock(JobHandler).new_instances(mock_job_handler)

        e = Exception("Mock "
                      "Generic Exception")

        flexmock(mock_job_handler).should_receive('handle_job')\
                                  .and_raise(e)

        exc_msg = repr(e)

        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(sys_exit_msg.code,
                         MsgBundle.getMessage(
                                       MsgBundle.EXC_JOB_ERR_HDR) + 
                         exc_msg + " ")

    # to test generic exception in job_manager
    # Simulate this by omitting (device_id in) job_params

    def test_execute_job_generic_exception_multi_device(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='multi_device_template_gen_exc')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}}
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)

        exc_msg = repr(TypeError(
                           "'NoneType' object has no attribute '__getitem__'"))
        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(sys_exit_msg.code, 
                         MsgBundle.getMessage(
                                       MsgBundle.EXC_JOB_ERR_HDR) +
                         exc_msg + " ")

    # to test the generic exception in handle_device_job
    def test_execute_job_generic_exception_handle_device_job(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='handle_dev_job_gen_exc')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"
        exc_msg = 'Job Device Handler Generic Exception'

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']},
                          "params": {"device_list":
                                     ["aad74e24-a00b-4eb3-8412-f8b9412925c3"]
                                     }
                          }
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)

        # mock the job_handler to raise a general exception
        mock_job_handler = flexmock()
        flexmock(JobHandler).new_instances(mock_job_handler)

        flexmock(mock_job_handler).should_receive('handle_job')
        flexmock(mock_job_handler).should_receive('get_playbook_info')\
                                  .and_raise(
            Exception("Job Device Handler "
                      "Generic Exception"))

        mock_result_handler = flexmock(job_result_status=JobStatus.FAILURE,
                                       job_summary_message=exc_msg)
        flexmock(JobResultHandler).new_instances(mock_result_handler)
        flexmock(mock_result_handler).should_receive(
            'create_job_summary_message') .and_return(exc_msg)
        flexmock(mock_result_handler).should_receive('create_job_summary_log')
        flexmock(mock_result_handler).should_receive('update_job_status')\
                                     .with_args(JobStatus.FAILURE, exc_msg)

        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(sys_exit_msg.code, exc_msg)

    # to test multi_device_job when device_vendor is not found
    def test_execute_job_no_device_vendor(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='multi_device_no_device_vendor')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"
        device_id = "aad74e24-a00b-4eb3-8412-f8b9412925c3"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']},
                          "params": {"device_list":
                                     [device_id]},
                          "device_json": {
                              device_id:
                                  {"device_family": "MX"}
                          }
                          }
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)

        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(sys_exit_msg.code, MsgBundle.getMessage(
                         MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
                         MsgBundle.getMessage(
                         MsgBundle.JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR) +
                         device_id + ",\n" +
                         MsgBundle.getMessage(MsgBundle.
                                              PLAYBOOK_RESULTS_MESSAGE)
                         + device_id + ":" +
                         MsgBundle.getMessage(MsgBundle.
                                              DEVICE_VENDOR_FAMILY_MISSING,
                         device_id=device_id) + " \n"
                         )

    # to test multi_device_job when there is explicit mismatch
    def test_execute_job_explicit_mismatch(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='multi_device_explicit_mismatch')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"
        device_id = "aad74e24-a00b-4eb3-8412-f8b9412925c3"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']},
                          "params": {"device_list":
                                     [device_id]},
                          "device_json": {
                              device_id:
                                  {"device_family": "MX",
                                   "device_vendor": "Arista",
                                   "device_username": "username",
                                   "device_password": "password"}
                          }
                          }
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)

        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(
            sys_exit_msg.code,
            MsgBundle.getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
            MsgBundle.getMessage(MsgBundle.
                                 JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR) +
            device_id + ",\n" +
            MsgBundle.getMessage(MsgBundle.PLAYBOOK_RESULTS_MESSAGE) +
            device_id + ":" +
            MsgBundle.getMessage(MsgBundle.PLAYBOOK_INFO_DEVICE_MISMATCH,
                                 device_vendor="Arista",
                                 device_family="MX") + " \n")

    # to test multi_device_job when credentials are not provided
    def test_execute_job_no_credentials(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='multi_device_no_credentials')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"
        device_id = "aad74e24-a00b-4eb3-8412-f8b9412925c3"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']},
                          "params": {"device_list":
                                     [device_id]},
                          "device_json": {
                              device_id:
                                  {"device_family": "MX",
                                   "device_vendor": "Juniper"}
                          }}
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)

        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(
            sys_exit_msg.code,
            MsgBundle.getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
            MsgBundle.getMessage(MsgBundle.
                                 JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR) +
            device_id +
            ",\n" +
            MsgBundle.getMessage(MsgBundle.PLAYBOOK_RESULTS_MESSAGE) +
            device_id +
            ":" +
            MsgBundle.getMessage(
                                 MsgBundle.
                                 NO_CREDENTIALS_FOUND,
                                 device_id=device_id) +
            " \n")

    # to test multi_device_job when device details for given device
    # cannot be found

    def test_execute_job_no_device_data(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='multi_device_no_data')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"
        device_id = "aad74e24-a00b-4eb3-8412-f8b9412925c3"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']},
                          "params": {"device_list":
                                     [device_id]},
                          "device_json": {
                              "some_random_id":
                                  {"device_family": "MX",
                                   "device_vendor": "Juniper"}
                          }}
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)
        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(
            sys_exit_msg.code,
            MsgBundle.getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
            MsgBundle.getMessage(MsgBundle.
                                 JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR) +
            device_id +
            ",\n" +
            MsgBundle.getMessage(MsgBundle.PLAYBOOK_RESULTS_MESSAGE) +
            device_id +
            ":" +
            MsgBundle.getMessage(MsgBundle.
                                 NO_DEVICE_DATA_FOUND,
                                 device_id=device_id)
            + " \n")

    # to test multi_device_job when no device json
    # can be found

    def test_execute_job_no_device_json(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml',
                                     vendor='Juniper')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=playbooks_list,
                                   name='multi_device_no_json')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"
        device_id = "aad74e24-a00b-4eb3-8412-f8b9412925c3"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']},
                          "params": {"device_list":
                                     [device_id]}
                          }
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))
        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)
        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(
            sys_exit_msg.code,
            MsgBundle.getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
            MsgBundle.getMessage(MsgBundle.
                                 JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR) +
            device_id +
            ",\n" +
            MsgBundle.getMessage(MsgBundle.PLAYBOOK_RESULTS_MESSAGE) +
            device_id +
            ":" +
            MsgBundle.getMessage(MsgBundle.DEVICE_JSON_NOT_FOUND) +
            " \n")

    # to test run_playbook generic exception
    def test_execute_job_run_pb_gen_exc(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=playbooks_list,
                                   name='run_pb_gen_exc')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)
        playbook_info = {"uri": play_info.playbook_uri}
        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}}

        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))

        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)

        e = Exception('some gen exc')

        # mock the call to raise exception
        flexmock(json).should_receive('dumps')\
            .and_raise(e)
        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)

        self.assertEqual(sys_exit_msg.code,
                         MsgBundle.getMessage(MsgBundle.
                                              JOB_SUMMARY_MESSAGE_HDR) +
                         MsgBundle.
                         getMessage(MsgBundle.
                                    JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR) +
                         MsgBundle.getMessage(MsgBundle.RUN_PLAYBOOK_ERROR,
                                              playbook_uri=play_info.
                                              playbook_uri,
                                              exc_msg=repr(e)))

    # to handle run playbook process rc =1 exception

    def test_execute_job_run_pb_process_rc1(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=playbooks_list,
                                   name='run_pb_prc_rc_1')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)
        playbook_info = {"uri": play_info.playbook_uri}
        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}}

        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))

        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)

        flexmock(os.path).should_receive('exists').and_return(True)
        # fake_timeout_exc = flexmock(timeout = 0)
        # mock the call to raise exception

        fake_process = flexmock(returncode=1)
        fake_process.should_receive('wait')
        # flexmock(subprocess).should_receive('TimeoutExpired')
        flexmock(subprocess32).should_receive('Popen').and_return(fake_process)
        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)

        self.assertEqual(sys_exit_msg.code,
                         MsgBundle.getMessage(MsgBundle.
                                              JOB_SUMMARY_MESSAGE_HDR) +
                         MsgBundle.getMessage(
                                       MsgBundle.
                                       JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR) +
                         MsgBundle.getMessage(
                                       MsgBundle.PLAYBOOK_EXIT_WITH_ERROR)
                         )

    # to handle run playbook process generic exception

    def test_execute_job_run_pb_process_gen_exc(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=playbooks_list,
                                   name='run_pb_prc_gen_exc')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)
        playbook_info = {"uri": play_info.playbook_uri}
        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        execution_id = uuid.uuid4()
        job_input_json = {"job_template_id": job_template_uuid,
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": str(execution_id),
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}}

        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))

        jm = JobManager(log_utils.get_config_logger(),
                        self._vnc_lib, job_input_json, log_utils)

        flexmock(os.path).should_receive('exists').and_return(True)
        # fake_timeout_exc = flexmock(timeout = 0)
        # mock the call to raise exception

        fake_process = flexmock(returncode=None)
        fake_process.should_receive('wait')
        # flexmock(subprocess).should_receive('TimeoutExpired')

        e = Exception('mock gen exception in run_playbook_process')
        flexmock(subprocess32).should_receive('Popen').and_raise(e)

        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)

        self.assertEqual(
            sys_exit_msg.code,
            MsgBundle.getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
            MsgBundle.getMessage(MsgBundle.
                                 JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR) +
            MsgBundle.getMessage(MsgBundle.RUN_PLAYBOOK_PROCESS_ERROR,
                                 playbook_uri=playbook_info['uri'],
                                 exc_msg=repr(e)))

    # to test job_input_schema_validations

    def test_execute_job_input_schema(self):
        # create job template
        play_info = {}
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])

        fake_job_template = flexmock(job_template_type='device',
                                     job_template_job_runtime='ansible',
                                     job_template_multi_device_job=False,
                                     job_template_playbooks=playbooks_list,
                                     name='input_schema_template',
                                     fq_name=["default-global-system-config",
                                              "input_schema_template"],
                                     uuid='random_uuid')

        mock_vnc = flexmock()
        flexmock(VncApi).new_instances(mock_vnc)

        flexmock(mock_vnc).should_receive(
                           'job_template_read').with_args(
                           id="random_uuid").and_return(fake_job_template)

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        execution_id = str(uuid.uuid4())
        job_input_json = {"job_template_id": "random_uuid",
                          "input": {"playbook_data": "some playbook data"},
                          "job_execution_id": execution_id,
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}}
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))

        jm = JobManager(log_utils.get_config_logger(),
                        mock_vnc, job_input_json, log_utils)

        e = Exception("u'name' is a required property")

        fake_schema = '{' \
                      '"$schema": ' \
                      '"http://json-schema.org/draft-04/schema#", ' \
                      '"required": ["name", "price"], "type": "object", ' \
                      '"properties": ' \
                      '{' \
                      '"sku": {"type": "integer", "description": ' \
                      '"Stock Keeping Unit"}, ' \
                      '"price": {"minimum": 0, "type": "number"}, ' \
                      '"name": {"type": "string"}' \
                      '},' \
                      '"title": "InventoryItem"' \
                      '}'

        fake_job_template.should_receive(
            'get_job_template_input_schema').and_return(fake_schema)
        fake_job_template.should_receive(
            'get_job_template_multi_device_job').and_return(False)
        fake_job_template.should_receive('get_uuid').and_return('random_uuid')

        # mock the job_handler to raise an exception
        fake_job_template.should_receive('get_job_template_playbooks')
        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(sys_exit_msg.code,
                         MsgBundle.getMessage(MsgBundle.JOB_EXC_REC_HDR) +
                         MsgBundle.getMessage(MsgBundle.INVALID_SCHEMA,
                                              job_template_id="random_uuid",
                                              exc_obj=e) +
                         " ")

    def test_execute_job_input_schema_ip_not_found(self):
        # create job template
        play_info = {}
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])

        fake_job_template = flexmock(job_template_type='device',
                                     job_template_job_runtime='ansible',
                                     job_template_multi_device_job=False,
                                     job_template_playbooks=playbooks_list,
                                     name='input_schema_template_ip',
                                     fq_name=["default-global-system-config",
                                              "input_schema_template_ip"],
                                     uuid='random_uuid')

        mock_vnc = flexmock()
        flexmock(VncApi).new_instances(mock_vnc)

        flexmock(mock_vnc).should_receive('job_template_read')\
            .with_args(id="random_uuid")\
            .and_return(fake_job_template)

        sample_auth_token = "6e7d7f87faa54fac96a2a28ec752336a"

        execution_id = str(uuid.uuid4())
        job_input_json = {"job_template_id": "random_uuid",
                          "job_execution_id": execution_id,
                          "auth_token": sample_auth_token,
                          "args": {"collectors": ['127.0.0.1:8086']}}
        self.mock_sandesh_check()

        args = {"collectors": ['127.0.0.1:8086']}
        log_utils = JobLogUtils(sandesh_instance_id=str(execution_id),
                                config_args=json.dumps(args))

        jm = JobManager(log_utils.get_config_logger(),
                        mock_vnc, job_input_json, log_utils)

        fake_schema = '{' \
                      '"$schema": ' \
                      '"http://json-schema.org/draft-04/schema#", ' \
                      '"required": ["name", "price"], "type": "object", ' \
                      '"properties": ' \
                      '{' \
                      '"sku": {"type": "integer", "description": ' \
                      '"Stock Keeping Unit"}, ' \
                      '"price": {"minimum": 0, "type": "number"}, ' \
                      '"name": {"type": "string"}' \
                      '},' \
                      '"title": "InventoryItem"' \
                      '}'

        fake_job_template.should_receive('get_job_template_input_schema')\
            .and_return(fake_schema)
        fake_job_template.should_receive(
            'get_job_template_multi_device_job')\
            .and_return(False)
        fake_job_template.should_receive('get_uuid').and_return('random_uuid')

        # mock the job_handler to raise an exception
        fake_job_template.should_receive('get_job_template_playbooks')
        sys_exit_msg = self.assertRaises(SystemExit, jm.start_job)
        self.assertEqual(sys_exit_msg.code,
                         MsgBundle.getMessage(MsgBundle.JOB_EXC_REC_HDR) +
                         MsgBundle.getMessage(
                         MsgBundle.INPUT_SCHEMA_INPUT_NOT_FOUND) +
                         " ")

    def mock_sandesh_check(self):
        mocked_sandesh_utils = flexmock()
        flexmock(SandeshUtils, __new__=mocked_sandesh_utils)
        mocked_sandesh_utils.should_receive('__init__')
        mocked_sandesh_utils.should_receive('wait_for_connection_establish')
        mocked_sandesh_utils.should_receive('close_sandesh_connection')
