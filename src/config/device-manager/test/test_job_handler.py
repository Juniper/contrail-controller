#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import unittest
import mock
import cfgm_common
import requests
from job_handler import JobStatus
from job_handler import JobHandler


class TestJobHandler(unittest.TestCase):
    JOB_SUCCESS = { "job_status": "SUCCESS" }
    JOB_FAILURE = { "job_status": "FAILURE" }
    JOB_UNKNOWN = { "job_status": "UNKNOWN" }
    JOB_IN_PROGRESS = { "job_status": "IN_PROGRESS" }

    TIMEOUT = 15
    MAX_RETRIES = 30

    def setUp(self):
        super(TestJobHandler, self).setUp()
        self.vnc_api_patch = mock.patch('job_handler.VncApi')
        self.sleep_patch = mock.patch('gevent.sleep')
        self.vnc_api_mock = self.vnc_api_patch.start()
        self.sleep_mock = self.sleep_patch.start()
        self.logger = mock.Mock()
        self.vnc_api_obj = mock.Mock()
        self.vnc_api_mock.return_value = self.vnc_api_obj

        self.job_type = ['test-job']
        self.job_input = {'key1': 'value1', 'key2': 'value2'}
        self.device_list = ['device1']
        self.api_server_config = {
            'ips': ["1.2.3.4"],
            'port': "8082",
            'username': "admin",
            'password': "password",
            'tenant': "default",
            'use_ssl': False
        }

        self.job_handler = JobHandler(self.job_type, self.job_input,
                                      self.device_list,
                                      self.api_server_config,
                                      self.logger)
    # end setUp

    def tearDown(self):
        super(TestJobHandler, self).tearDown()
        self.vnc_api_patch.stop()
        self.sleep_patch.stop()
    # end tearDown

    def test_job_executed_successfully(self):
        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api_obj.execute_job.return_value = {'job_execution_id': 'job-1'}
        self.vnc_api_obj.job_status.return_value = self.JOB_SUCCESS

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.job_handler.push(self.TIMEOUT, self.MAX_RETRIES)

        self.vnc_api_obj.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.vnc_api_mock.assert_called_once_with(api_server_host='1.2.3.4',
            api_server_port='8082', username='admin', password='password',
            tenant_name='default', api_server_use_ssl=False)
        self.assertEqual(self.vnc_api_obj.job_status.call_count, 1)
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.COMPLETE)
        self.assertEqual(self.sleep_mock.call_args_list, [])
    # end test_job_executed_successfully

    def test_job_stays_in_progress_then_completes(self):
        return_values = [self.JOB_IN_PROGRESS,
                         self.JOB_IN_PROGRESS,
                         self.JOB_SUCCESS]

        def side_effect(*_):
            self.assertFalse(self.job_handler.is_job_done())
            self.assertEqual(self.job_handler.get_job_status(),
                             JobStatus.IN_PROGRESS)
            side_effect.counter += 1
            return return_values[side_effect.counter - 1]
        # end side_effect
        side_effect.counter = 0

        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api_obj.execute_job.return_value = {'job_execution_id': 'job-1'}
        self.vnc_api_obj.job_status.side_effect = side_effect

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.job_handler.push(self.TIMEOUT, self.MAX_RETRIES)

        self.vnc_api_obj.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.assertEqual(self.vnc_api_obj.job_status.call_count, 3)
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.COMPLETE)
        self.assertEqual(self.sleep_mock.call_args_list,
                         [mock.call(self.TIMEOUT)])
    # end test_job_stays_in_progress_then_completes

    def test_job_failed(self):
        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api_obj.execute_job.return_value = {'job_execution_id': 'job-1'}
        self.vnc_api_obj.job_status.side_effect = [self.JOB_IN_PROGRESS,
                                                   self.JOB_FAILURE]

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.assertRaises(Exception, self.job_handler.push, self.TIMEOUT, self.MAX_RETRIES)

        self.vnc_api_obj.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.assertEqual(self.vnc_api_obj.job_status.call_count, 2)
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILED)
        self.assertEqual(self.sleep_mock.call_args_list, [])
    # end test_job_failed

    def test_unknown_status(self):
        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api_obj.execute_job.return_value = {'job_execution_id': 'job-1'}
        self.vnc_api_obj.job_status.side_effect = [self.JOB_IN_PROGRESS,
                                                   self.JOB_UNKNOWN]

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.assertRaises(Exception, self.job_handler.push, self.TIMEOUT, self.MAX_RETRIES)

        self.vnc_api_obj.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.assertEqual(self.vnc_api_obj.job_status.call_count, 2)
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILED)
        self.assertEqual(self.sleep_mock.call_args_list, [])
    # end test_unknown_status

    def test_execute_job_throws(self):
        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api_obj.execute_job.side_effect = \
            cfgm_common.exceptions.HttpError(500, "execute-job failed")

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.assertRaises(Exception, self.job_handler.push, self.TIMEOUT, self.MAX_RETRIES)

        self.vnc_api_obj.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.vnc_api_obj.job_status.assert_not_called()
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILED)
        self.sleep_mock.assert_not_called()
    # end test_execute_job_throws

    def test_check_status_throws(self):
        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api_obj.execute_job.return_value = {'job_execution_id': 'job-1'}
        self.vnc_api_obj.job_status.side_effect = requests.exceptions.HTTPError()

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.assertRaises(Exception, self.job_handler.push, self.TIMEOUT, self.MAX_RETRIES)

        self.vnc_api_obj.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.assertEqual(self.vnc_api_obj.job_status.call_count, 1)
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILED)
        self.sleep_mock.assert_not_called()
    # end test_check_status_throws

    def test_max_retries_done(self):
        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api_obj.execute_job.return_value = {'job_execution_id': 'job-1'}
        self.vnc_api_obj.job_status.side_effect = [self.JOB_IN_PROGRESS,
                                      self.JOB_IN_PROGRESS,
                                      self.JOB_IN_PROGRESS,
                                      self.JOB_IN_PROGRESS,
                                      self.JOB_IN_PROGRESS,
                                      self.JOB_IN_PROGRESS]

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.assertRaises(Exception, self.job_handler.push, self.TIMEOUT, 3)

        self.vnc_api_obj.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.assertEqual(self.vnc_api_obj.job_status.call_count, 6)
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILED)
        self.assertEqual(self.sleep_mock.call_args_list,
                         [mock.call(self.TIMEOUT),
                          mock.call(self.TIMEOUT)])
    # end test_max_retries_done
# end class TestJobHandler
