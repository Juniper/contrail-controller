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
from job_manager.job_result_handler import JobResultHandler
from job_manager.job_handler import JobHandler
from job_manager.job_mgr import WFManager
from job_manager.job_log_utils import JobLogUtils
from job_manager.job_utils import JobUtils, JobStatus
from job_manager.job_exception import JobException
from job_manager.sandesh_utils import SandeshUtils
from job_manager.job_messages import MsgBundle

from test_job_manager_utils import TestJobManagerUtils

import gevent
import gevent.monkey
gevent.monkey.patch_all(thread=False)

sys.path.append('../common/tests')

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)
loop_var = 0


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

    #to test the case when sandesh initialization times out
    def test_sandesh_timeout(self):
        import pdb; pdb.set_trace()
        mocked_sandesh_utils = flexmock()
        flexmock(SandeshUtils, __new__=mocked_sandesh_utils)
        mocked_sandesh_utils.should_receive('__init__')
        mocked_sandesh_utils.should_receive('wait_for_connection_establish') \
            .and_raise(JobException())
        args = {"collectors": ['127.0.0.1:8086']}
        exc_msg = self.assertRaises(
            JobException,
            JobLogUtils,
            "rdm_exc_id",
            json.dumps(args))
        print str(exc_msg)
        self.assertEqual(str(exc_msg), "JobException in execution" +
                         " (None): " + MsgBundle.getMessage(
            MsgBundle.SANDESH_INITIALIZATION_TIMEOUT_ERROR))

    # to test the case when job_template_id is missing while initializing the
    # workflow_manager
    def test_execute_job_no_template_id(self):
        job_input_json = {"no_template_id": "Missing template_id"}
        exc_msg = self.assertRaises(
            Exception,
            WFManager,
            None,
            None,
            job_input_json,
            None)
        self.assertEqual(str(exc_msg), MsgBundle.getMessage(
            MsgBundle.JOB_TEMPLATE_MISSING))

    #to test the case when job_execution_id is missing while initializing the
    #workflow_manager
    def test_execute_job_no_execution_id(self):
        job_input_json = {"job_template_id": "template_id"}
        exc_msg = self.assertRaises(
            Exception,
            WFManager,
            None,
            None,
            job_input_json,
            None)
        self.assertEqual(str(exc_msg), MsgBundle.getMessage(
            MsgBundle.JOB_EXECUTION_ID_MISSING))

    #to test the case when there is no job_template
    def test_execute_job_read_job_template(self):
        job_template_id = str(uuid.uuid4())

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_id)

        mock_vnc = flexmock()
        flexmock(VncApi).new_instances(mock_vnc)

        flexmock(mock_vnc).should_receive('job_template_read') \
            .with_args(id=job_template_id) \
            .and_raise(Exception('No such job Template id'))
        wm = WFManager(log_utils.get_config_logger(),
                          mock_vnc, job_input_json, log_utils)
        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
        self.assertEqual(
            sys_exit_msg.code,
            MsgBundle.getMessage(MsgBundle.JOB_EXC_REC_HDR) +
            MsgBundle.getMessage(MsgBundle.READ_JOB_TEMPLATE_ERROR,
                                 job_template_id=job_template_id)
            + " ")

    #to test the generic exception in handle_job for single device
    def test_execute_job_generic_exception_handle_job(self):
        # create job template
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=TestJobManagerUtils.
                                   playbooks_list,
                                   name='handle_job_gen_exc')

        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        TestJobManagerUtils.mock_sandesh_check()

        exc_msg = 'Job Handler Generic Exception'

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(),
                          self._vnc_lib, job_input_json, log_utils)

        # mock the job_handler to raise a general exception
        mock_job_handler = flexmock()
        flexmock(JobHandler).new_instances(mock_job_handler)

        flexmock(mock_job_handler).should_receive('handle_job')
        flexmock(mock_job_handler).should_receive('get_playbook_info') \
           .and_raise(
           Exception("Job Handler "
                     "Generic Exception"))
        mock_result_handler = flexmock(job_result_status=JobStatus.FAILURE,
                                       job_summary_message=exc_msg)
        flexmock(JobResultHandler).new_instances(mock_result_handler)
        flexmock(mock_result_handler).should_receive(
            'create_job_summary_message').and_return(exc_msg)
        flexmock(mock_result_handler).should_receive(
            'create_job_summary_log')
        flexmock(mock_result_handler).should_receive('update_job_status') \
            .with_args(JobStatus.FAILURE, exc_msg)
        flexmock(mock_result_handler).should_receive(
            'get_device_data')
        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
        self.assertEqual(sys_exit_msg.code, exc_msg)


    #to test generic exception from get_playbook_info
    #TODO - THIS test case it not longer necessary. GET_PLAYBOOK_INFO_ERROR
    # def test_execute_job_handler_gen_exc(self):
    #     # create job template
    #     fake_job_template = flexmock(
    #         job_template_type='device',
    #         job_template_job_runtime='ansible',
    #         job_template_multi_device_job=False,
    #         job_template_playbooks=TestJobManagerUtils.
    #             playbooks_list,
    #         name='single_device_temp_jh_gen_exc',
    #         fq_name=["default-global-system-config",
    #                  "single_device_temp_jh_gen_exc"],
    #         uuid='random_uuid')
    #
    #     mock_vnc = flexmock()
    #     flexmock(VncApi).new_instances(mock_vnc)
    #
    #     flexmock(mock_vnc).should_receive('job_template_read') \
    #         .with_args(id="random_uuid") \
    #         .and_return(fake_job_template)
    #
    #     TestJobManagerUtils.mock_sandesh_check()
    #
    #     job_input_json, log_utils = TestJobManagerUtils.get_min_details(
    #         "random_uuid")
    #
    #     wm = WFManager(log_utils.get_config_logger(),
    #                       mock_vnc, job_input_json, log_utils)
    #
    #     fake_job_template.should_receive(
    #         'get_job_template_input_schema').and_return(None)
    #
    #     fake_job_template.should_receive('get_uuid').and_return(
    #         'random_uuid')
    #
    #     # mock the job_handler to raise a general exception
    #     fake_job_template.should_receive('get_job_template_playbooks') \
    #         .and_raise(
    #         Exception("Mock "
    #                   "Generic Exception in job_handler"))
    #
    #     exc_msg = repr(Exception('Mock Generic Exception in job_handler'))
    #
    #     sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
    #     self.assertEqual(sys_exit_msg.code, MsgBundle.
    #                      getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
    #                      MsgBundle.
    #                      getMessage(MsgBundle.
    #                                 JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR) +
    #                      MsgBundle.
    #                      getMessage(MsgBundle.GET_PLAYBOOK_INFO_ERROR,
    #                                 job_template_id="random_uuid",
    #                                 exc_msg = exc_msg + " "))
    #

    # #to test single_device_job when playbook is not present in the path
    def test_execute_job_no_pb_file_on_path(self):
        # create job template
        play_info = PlaybookInfoType(playbook_uri='job_manager_test.yaml')
        playbooks_list = PlaybookInfoListType(playbook_info=[play_info])
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=TestJobManagerUtils.
                                   playbooks_list,
                                   name='single_device_template')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        playbook_info = {"uri": "job_manager_test.yaml"}

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(),
                          self._vnc_lib, job_input_json, log_utils)

        # mock the call to invoke the playbook process
        flexmock(os.path).should_receive('exists').and_return(False)

        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
        self.assertEqual(sys_exit_msg.code, MsgBundle.
                         getMessage(MsgBundle.
                                    JOB_SUMMARY_MESSAGE_HDR) +
                         MsgBundle.
                         getMessage(MsgBundle.
                                    JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR) +
                         MsgBundle.
                         getMessage(MsgBundle.PLAYBOOK_NOT_FOUND,
                                    playbook_uri=playbook_info['uri']))

    #To test generic job exception in handle_job
    def test_execute_job_generic_exception(self):
        # create job template
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=TestJobManagerUtils.
                                   playbooks_list,
                                   name='single_device_template_gen_exc')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(),
                          self._vnc_lib, job_input_json, log_utils)

        # mock the job_handler to raise a general exception
        mock_job_handler = flexmock()
        flexmock(JobHandler).new_instances(mock_job_handler)

        exc = Exception("Mock "
                        "Generic Exception")

        flexmock(mock_job_handler).should_receive('handle_job') \
            .and_raise(exc)

        exc_msg = repr(exc)

        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
        self.assertEqual(sys_exit_msg.code,
                         MsgBundle.getMessage(
                             MsgBundle.EXC_JOB_ERR_HDR) +
                         exc_msg + " ")


    #TODO this test case can be removed becuase id device id is omitted,
    # to test generic exception in job_manager
    # Simulate this by omitting (device_id in) job_params
    # def test_execute_job_generic_exception_multi_device(self):
    #     # create job template
    #     job_template = JobTemplate(job_template_type='device',
    #                                job_template_job_runtime='ansible',
    #                                job_template_multi_device_job=True,
    #                                job_template_playbooks=TestJobManagerUtils.
    #                                playbooks_list,
    #                                name='multi_device_template_gen_exc')
    #     job_template_uuid = self._vnc_lib.job_template_create(job_template)
    #
    #     TestJobManagerUtils.mock_sandesh_check()
    #
    #     job_input_json, log_utils = TestJobManagerUtils.get_min_details(
    #         job_template_uuid)
    #
    #     wm = WFManager(log_utils.get_config_logger(),
    #                       self._vnc_lib, job_input_json, log_utils)
    #
    #     exc_msg = repr(TypeError(
    #         "'NoneType' object has no attribute '__getitem__'"))
    #     sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
    #     self.assertEqual(sys_exit_msg.code,
    #                      MsgBundle.getMessage(
    #                          MsgBundle.EXC_JOB_ERR_HDR) +
    #                      exc_msg + " ")

    #to test the generic exception in handle_device_job when executing for
    #multiple devices.
    def test_execute_job_generic_exception_handle_device_job(self):
        # create job template
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=TestJobManagerUtils.
                                   playbooks_list,
                                   name='handle_dev_job_gen_exc')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        exc_msg = 'Job Device Handler Generic Exception'

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)
        job_input_json["params"] = {"device_list":
                                        [
                                            "aad74e24-a00b-4eb3-8412-f8b9412925c3"]}

        wm = WFManager(log_utils.get_config_logger(),
                          self._vnc_lib, job_input_json, log_utils)

        # mock the job_handler to raise a general exception
        mock_job_handler = flexmock()
        flexmock(JobHandler).new_instances(mock_job_handler)

        flexmock(mock_job_handler).should_receive('handle_job')
        flexmock(mock_job_handler).should_receive('get_playbook_info') \
            .and_raise(
            Exception("Job Device Handler "
                      "Generic Exception"))

        mock_result_handler = flexmock(job_result_status=JobStatus.FAILURE,
                                       job_summary_message=exc_msg)
        flexmock(JobResultHandler).new_instances(mock_result_handler)
        flexmock(mock_result_handler).should_receive(
            'create_job_summary_message').and_return(exc_msg)
        flexmock(mock_result_handler).should_receive(
            'create_job_summary_log')
        flexmock(mock_result_handler).should_receive('get_device_data')
        flexmock(mock_result_handler).should_receive('update_job_status') \
            .with_args(JobStatus.FAILURE, exc_msg)

        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
        self.assertEqual(sys_exit_msg.code, exc_msg)


    #to test multi_device_job when device_vendor is not found.
    #get_playbook_info fails.
    def test_execute_job_no_device_vendor(self):
        # create job template
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_playbooks=TestJobManagerUtils.
                                   playbooks_list,
                                   name='multi_device_no_device_vendor')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        device_id = "aad74e24-a00b-4eb3-8412-f8b9412925c3"

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)
        job_input_json.update({
            "params": {"device_list":
                           [device_id]},
            "device_json": {
                device_id:
                    {"device_family": "MX"}}})

        wm = WFManager(log_utils.get_config_logger(),
                          self._vnc_lib, job_input_json, log_utils)

        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
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



    # # to test multi_device_job when there is explicit mismatch, between
    # device vendor and playbok vendor
    # #TODO this is not necessary because depending on the device vendor family, the right playbook will be chosen
    # def test_execute_job_explicit_mismatch(self):
    #     # create job template
    #     job_template = JobTemplate(job_template_type='device',
    #                                job_template_job_runtime='ansible',
    #                                job_template_playbooks=TestJobManagerUtils.
    #                                playbooks_list,
    #                                name='multi_device_explicit_mismatch')
    #     job_template_uuid = self._vnc_lib.job_template_create(job_template)
    #
    #     TestJobManagerUtils.mock_sandesh_check()
    #
    #     device_id = "aad74e24-a00b-4eb3-8412-f8b9412925c3"
    #     job_input_json, log_utils = TestJobManagerUtils.get_min_details(
    #         job_template_uuid)
    #     job_input_json.update({"params": {"device_list":
    #                                           [device_id]},
    #                            "device_json": {
    #                                device_id:
    #                                    {"device_family": "MX",
    #                                     "device_vendor": "Arista",
    #                                     "device_username": "username",
    #                                     "device_password": "password"}}})
    #
    #     wm = WFManager(log_utils.get_config_logger(),
    #                       self._vnc_lib, job_input_json, log_utils)
    #
    #     sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
    #     self.assertEqual(
    #         sys_exit_msg.code,
    #         MsgBundle.getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
    #         MsgBundle.getMessage(MsgBundle.
    #                              JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR) +
    #         device_id + ",\n" +
    #         MsgBundle.getMessage(MsgBundle.PLAYBOOK_RESULTS_MESSAGE) +
    #         device_id + ":" +
    #         MsgBundle.getMessage(MsgBundle.PLAYBOOK_INFO_DEVICE_MISMATCH,
    #                              device_vendor="Arista",
    #                              device_family="MX") + " \n")


    #to test multi_device_job when credentials are not provided
    def test_execute_job_no_credentials(self):
        # create job template
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=True,
                                   job_template_playbooks=TestJobManagerUtils.
                                   playbooks_list,
                                   name='multi_device_no_credentials')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        device_id = "aad74e24-a00b-4eb3-8412-f8b9412925c3"

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)
        job_input_json.update({"params": {"device_list":
                                              [device_id]},
                               "device_json": {
                                   device_id:
                                       {"device_family": "MX",
                                        "device_vendor": "Juniper",
                                        "device_product": "Some random product"}}})

        wm = WFManager(log_utils.get_config_logger(),
                          self._vnc_lib, job_input_json, log_utils)

        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
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


    # # to test multi_device_job when device details for given device
    # # cannot be found
    # #TODO - not required since we always check if the device id is present.
    # def test_execute_job_no_device_data(self):
    #     # create job template
    #     job_template = JobTemplate(job_template_type='device',
    #                                job_template_job_runtime='ansible',
    #                                job_template_multi_device_job=True,
    #                                job_template_playbooks=TestJobManagerUtils.
    #                                playbooks_list,
    #                                name='multi_device_no_data')
    #     job_template_uuid = self._vnc_lib.job_template_create(job_template)
    #
    #     device_id = "aad74e24-a00b-4eb3-8412-f8b9412925c3"
    #
    #     TestJobManagerUtils.mock_sandesh_check()
    #
    #     job_input_json, log_utils = TestJobManagerUtils.get_min_details(
    #         job_template_uuid)
    #     job_input_json.update({"params": {"device_list":
    #                                           [device_id]},
    #                            "device_json": {
    #                                "some_random_id":
    #                                    {"device_family": "MX",
    #                                     "device_vendor": "Juniper"}
    #                            }})
    #
    #     wm = WFManager(log_utils.get_config_logger(),
    #                       self._vnc_lib, job_input_json, log_utils)
    #     sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
    #     self.assertEqual(
    #         sys_exit_msg.code,
    #         MsgBundle.getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
    #         MsgBundle.getMessage(MsgBundle.
    #                              JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR) +
    #         device_id +
    #         ",\n" +
    #         MsgBundle.getMessage(MsgBundle.PLAYBOOK_RESULTS_MESSAGE) +
    #         device_id +
    #         ":" +
    #         MsgBundle.getMessage(MsgBundle.
    #                              NO_DEVICE_DATA_FOUND,
    #                              device_id=device_id)
    #         + " \n")

    # to test multi_device_job when no device json does not contain any details.
    #TODO device_json cannot be empty for multi job, so not necessary.
    # def test_execute_job_no_device_json(self):
    #     # create job template
    #     job_template = JobTemplate(job_template_type='device',
    #                                job_template_job_runtime='ansible',
    #                                job_template_multi_device_job=True,
    #                                job_template_playbooks=TestJobManagerUtils.
    #                                playbooks_list,
    #                                name='multi_device_no_json')
    #     job_template_uuid = self._vnc_lib.job_template_create(job_template)
    #
    #     device_id = "aad74e24-a00b-4eb3-8412-f8b9412925c3"
    #
    #     TestJobManagerUtils.mock_sandesh_check()
    #
    #     job_input_json, log_utils = TestJobManagerUtils.get_min_details(
    #         job_template_uuid)
    #     # job_input_json.update({
    #     #     "params": {"device_list":
    #     #                    [device_id]},
    #     # })
    #     job_input_json.update({"params": {"device_list":
    #                                           [device_id]},
    #                            "device_json": {
    #
    #                            }})
    #
    #     wm = WFManager(log_utils.get_config_logger(),
    #                       self._vnc_lib, job_input_json, log_utils)
    #     sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
    #     self.assertEqual(
    #         sys_exit_msg.code,
    #         MsgBundle.getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
    #         MsgBundle.getMessage(MsgBundle.
    #                              JOB_MULTI_DEVICE_FAILED_MESSAGE_HDR) +
    #         device_id +
    #         ",\n" +
    #         MsgBundle.getMessage(MsgBundle.PLAYBOOK_RESULTS_MESSAGE) +
    #         device_id +
    #         ":" +
    #         MsgBundle.getMessage(MsgBundle.DEVICE_JSON_NOT_FOUND) +
    #         " \n")
    #

    #to test run_playbook generic exception- fail json.
    def test_execute_job_run_pb_gen_exc(self):
        # create job template
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=TestJobManagerUtils.
                                   playbooks_list,
                                   name='run_pb_gen_exc')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(),
                          self._vnc_lib, job_input_json, log_utils)

        exc = Exception('some gen exc')

        # mock the call to raise exception
        flexmock(json).should_receive('dumps') \
            .and_raise(exc)
        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)

        self.assertEqual(sys_exit_msg.code,
                         MsgBundle.getMessage(MsgBundle.
                                              JOB_SUMMARY_MESSAGE_HDR) +
                         MsgBundle.
                         getMessage(MsgBundle.
                                    JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR) +
                         MsgBundle.getMessage(MsgBundle.RUN_PLAYBOOK_ERROR,
                                              playbook_uri=TestJobManagerUtils.
                                              play_info.
                                              playbook_uri,
                                              exc_msg=repr(exc)))


    #to handle run playbook process rc =1 exception
    def test_execute_job_run_pb_process_rc1(self):
        # create job template
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=TestJobManagerUtils.
                                   playbooks_list,
                                   name='run_pb_prc_rc_1')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(),
                          self._vnc_lib, job_input_json, log_utils)
        loop_var = 0

        def mock_readline():
            global loop_var

            loop_var += 1
            if loop_var == 1:
                with open("tests/test.txt", 'r') as f:
                    line = f.readline()
                    return line
            if loop_var == 2:
                fake_process.should_receive("poll").and_return(123)
                loop_var = 0
                return ""

        stdout = flexmock(readline=mock_readline)
        flexmock(json).should_receive("loads")
        flexmock(os.path).should_receive('exists').and_return(True)
        # mock the call to raise exception

        fake_process = flexmock(returncode=1,stdout=stdout)
        fake_process.should_receive('wait')
        # flexmock(subprocess).should_receive('TimeoutExpired')
        flexmock(subprocess32).should_receive('Popen').and_return(
            fake_process)
        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)

        self.assertEqual(sys_exit_msg.code,
                         MsgBundle.getMessage(MsgBundle.
                                              JOB_SUMMARY_MESSAGE_HDR) +
                         MsgBundle.getMessage(
                             MsgBundle.
                                 JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR) +
                         MsgBundle.getMessage(
                             MsgBundle.PLAYBOOK_EXIT_WITH_ERROR,
                             playbook_uri=TestJobManagerUtils.
                                 play_info.
                                 playbook_uri, )
                         )

    #to handle run playbook process generic exception
    def test_execute_job_run_pb_process_gen_exc(self):
        # create job template
        job_template = JobTemplate(job_template_type='device',
                                   job_template_job_runtime='ansible',
                                   job_template_multi_device_job=False,
                                   job_template_playbooks=TestJobManagerUtils.
                                   playbooks_list,
                                   name='run_pb_prc_gen_exc')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(),
                          self._vnc_lib, job_input_json, log_utils)

        flexmock(os.path).should_receive('exists').and_return(True)
        # mock the call to raise exception

        fake_process = flexmock(returncode=None)
        fake_process.should_receive('wait')
        exc = Exception('mock gen exception in run_playbook_process')
        flexmock(subprocess32).should_receive('Popen').and_raise(exc)

        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)

        self.assertEqual(
            sys_exit_msg.code,
            MsgBundle.getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
            MsgBundle.getMessage(MsgBundle.
                                 JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR) +
            MsgBundle.getMessage(MsgBundle.RUN_PLAYBOOK_PROCESS_ERROR,
                                 playbook_uri=TestJobManagerUtils.play_info.
                                 playbook_uri,
                                 exc_msg=repr(exc)))

    #Test run_playbook_process for TimeoutExpired
    def test_execute_job_run_pb_process_timeout_expired(self):
        # create job template
        job_template = JobTemplate(
            job_template_type='device',
            job_template_job_runtime='ansible',
            job_template_multi_device_job=False,
            job_template_playbooks=TestJobManagerUtils.
                playbooks_list,
            name='run_pb_prc_rc_timeout')
        job_template_uuid = self._vnc_lib.job_template_create(job_template)

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            job_template_uuid)

        wm = WFManager(log_utils.get_config_logger(),
                       self._vnc_lib, job_input_json, log_utils)

        loop_var = 0
        def mock_readline():
            global loop_var

            loop_var += 1
            if loop_var == 1:
                with open("tests/test.txt", 'r') as f:
                    line = f.readline()
                    return line
            if loop_var == 2:
                fake_process.should_receive("poll").and_return(123)
                loop_var = 0
                return ""

        stdout = flexmock(readline=mock_readline)
        flexmock(json).should_receive("loads")
        flexmock(os.path).should_receive('exists').and_return(True)
        flexmock(os).should_receive('kill')

        # mock the call to raise exception, using return code = 1
        fake_process = flexmock(returncode=1, pid=1234, stdout=stdout)
        exc = subprocess32.TimeoutExpired(cmd='Mock timeout exc cmd',
                                          timeout=3600)
        fake_process.should_receive('wait').and_raise(exc)
        flexmock(subprocess32).should_receive('Popen').and_return(
            fake_process)
        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
        self.assertEqual(
            sys_exit_msg.code,
            MsgBundle.getMessage(MsgBundle.JOB_SUMMARY_MESSAGE_HDR) +
            MsgBundle.getMessage(MsgBundle.
                                 JOB_SINGLE_DEVICE_FAILED_MESSAGE_HDR) +
            MsgBundle.getMessage(MsgBundle.RUN_PLAYBOOK_PROCESS_TIMEOUT,
                                 playbook_uri=TestJobManagerUtils.play_info.
                                 playbook_uri,
                                 exc_msg=repr(exc)))

    #to test job_input_schema_validation
    def test_execute_job_input_schema(self):
        # create job template
        fake_job_template = flexmock(
            job_template_type='device',
            job_template_job_runtime='ansible',
            job_template_multi_device_job=False,
            job_template_playbooks=TestJobManagerUtils.
                playbooks_list,
            name='input_schema_template',
            fq_name=["default-global-system-config",
                     "input_schema_template"],
            uuid='random_uuid')

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json, log_utils = TestJobManagerUtils.get_min_details(
            "random_uuid")

        mock_vnc = flexmock()
        flexmock(VncApi).new_instances(mock_vnc)

        flexmock(mock_vnc).should_receive(
            'job_template_read').with_args(
            id="random_uuid").and_return(fake_job_template)

        wm = WFManager(log_utils.get_config_logger(),
                          mock_vnc, job_input_json, log_utils)

        exc = Exception("'name' is a required property")

        fake_schema = TestJobManagerUtils.fake_schema

        fake_job_template.should_receive(
            'get_job_template_input_schema').and_return(fake_schema)
        fake_job_template.should_receive('get_uuid').and_return(
            'random_uuid')

        # mock the job_handler to raise an exception
        fake_job_template.should_receive('get_job_template_playbooks')
        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
        self.assertEqual(sys_exit_msg.code,
                         MsgBundle.getMessage(MsgBundle.JOB_EXC_REC_HDR) +
                         MsgBundle.getMessage(MsgBundle.INVALID_SCHEMA,
                                              job_template_id="random_uuid",
                                              exc_obj=exc) +
                         " ")

    #Test when input is not found when input schema is specified.
    def test_execute_job_input_schema_ip_not_found(self):
        # create job template
        fake_job_template = flexmock(
            job_template_type='device',
            job_template_job_runtime='ansible',
            job_template_multi_device_job=False,
            job_template_playbooks=TestJobManagerUtils.
                playbooks_list,
            name='input_schema_template_ip',
            fq_name=["default-global-system-config",
                     "input_schema_template_ip"],
            uuid='random_uuid')

        mock_vnc = flexmock()
        flexmock(VncApi).new_instances(mock_vnc)

        flexmock(mock_vnc).should_receive('job_template_read') \
            .with_args(id="random_uuid") \
            .and_return(fake_job_template)

        TestJobManagerUtils.mock_sandesh_check()

        job_input_json = {
            "job_template_id": "random_uuid",
            "job_execution_id": TestJobManagerUtils.execution_id,
            "fabric_fq_name": "Global-system-config:fabric:1",
            "auth_token": "6e7d7f87faa54fac96a2a28ec752336a",
            "args": TestJobManagerUtils.args
        }
        log_utils = JobLogUtils(
            sandesh_instance_id=TestJobManagerUtils.execution_id,
            config_args=json.dumps(TestJobManagerUtils.args))

        wm = WFManager(log_utils.get_config_logger(),
                          mock_vnc, job_input_json, log_utils)

        fake_schema = TestJobManagerUtils.fake_schema

        fake_job_template.should_receive('get_job_template_input_schema') \
        .and_return(fake_schema)
        fake_job_template.should_receive(
        'get_job_template_multi_device_job') \
        .and_return(False)
        fake_job_template.should_receive('get_uuid').and_return('random_uuid')

        # mock the job_handler to raise an exception
        fake_job_template.should_receive('get_job_template_playbooks')
        sys_exit_msg = self.assertRaises(SystemExit, wm.start_job)
        self.assertEqual(sys_exit_msg.code,
                     MsgBundle.getMessage(MsgBundle.JOB_EXC_REC_HDR) +
                     MsgBundle.getMessage(
                         MsgBundle.INPUT_SCHEMA_INPUT_NOT_FOUND) +
                     " ")
