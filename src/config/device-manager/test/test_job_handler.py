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
    NO_LOG_RESPONSE = """
    {
        "value": []
    }
    """
    JOB_LOG_RESPONSE = """
    {
        "value": [
            {
                "MessageTS": 12345,
                "MessageType": "JobLog"
            }
        ]
    }
    """
    TIMEOUT = 15

    def setUp(self):
        super(TestJobHandler, self).setUp()
        self.post_patch = mock.patch(
            'opserver_util.OpServerUtils.post_url_http')
        self.sleep_patch = mock.patch('gevent.sleep')
        self.post_mock = self.post_patch.start()
        self.sleep_mock = self.sleep_patch.start()
        self.vnc_api = mock.Mock()
        self.logger = mock.Mock()

        self.job_type = ['test-job']
        self.job_input = {'key1': 'value1', 'key2': 'value2'}
        self.device_list = ['device1']
        self.analytic_config = {'ip': '1.2.3.4', 'port': '8082'}

        self.job_handler = JobHandler(self.job_type, self.job_input,
                                      self.device_list, self.analytic_config,
                                      self.vnc_api, self.logger)
    # end setUp

    def tearDown(self):
        super(TestJobHandler, self).tearDown()
        self.post_patch.stop()
        self.sleep_patch.stop()
    # end tearDown

    def test_job_executed_successfully(self):
        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api.execute_job.return_value = {'job_execution_id': 'job-1'}
        self.post_mock.return_value = self.JOB_LOG_RESPONSE

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.job_handler.push()

        self.vnc_api.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.assertEqual(self.post_mock.call_args[0][1],
                         'http://1.2.3.4:8082/analytics/query')
        self.assertEqual(self.post_mock.call_count, 1)
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.COMPLETE)
        self.assertEqual(self.sleep_mock.call_args_list, [])
    # end test_job_executed_successfully

    def test_job_stays_in_progress_then_completes(self):
        return_values = [self.NO_LOG_RESPONSE,
                         self.NO_LOG_RESPONSE,
                         self.JOB_LOG_RESPONSE]

        def side_effect(*_):
            self.assertFalse(self.job_handler.is_job_done())
            self.assertEqual(self.job_handler.get_job_status(),
                             JobStatus.IN_PROGRESS)
            side_effect.counter += 1
            return return_values[side_effect.counter - 1]
        # end side_effect
        side_effect.counter = 0

        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api.execute_job.return_value = {'job_execution_id': 'job-1'}
        self.post_mock.side_effect = side_effect

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.job_handler.push(timeout=self.TIMEOUT)

        self.vnc_api.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.assertEqual(self.post_mock.call_count, 3)
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.COMPLETE)
        self.assertEqual(self.sleep_mock.call_args_list,
                         [mock.call(self.TIMEOUT)])
    # end test_job_stays_in_progress_then_completes

    def test_job_failed(self):
        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api.execute_job.return_value = {'job_execution_id': 'job-1'}
        self.post_mock.side_effect = [self.NO_LOG_RESPONSE,
                                      self.JOB_LOG_RESPONSE]

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.assertRaises(Exception, self.job_handler.push)

        self.vnc_api.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.assertEqual(self.post_mock.call_count, 2)
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILED)
        self.assertEqual(self.sleep_mock.call_args_list, [])
    # end test_job_failed

    def test_execute_job_throws(self):
        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api.execute_job.side_effect = \
            cfgm_common.exceptions.HttpError(500, "execute-job failed")

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.assertRaises(Exception, self.job_handler.push)

        self.vnc_api.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.post_mock.assert_not_called()
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILED)
        self.sleep_mock.assert_not_called()
    # end test_execute_job_throws

    def test_check_status_throws(self):
        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api.execute_job.return_value = {'job_execution_id': 'job-1'}
        self.post_mock.side_effect = requests.exceptions.HTTPError()

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.assertRaises(Exception, self.job_handler.push)

        self.vnc_api.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.assertEqual(self.post_mock.call_count, 1)
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILED)
        self.sleep_mock.assert_not_called()
    # end test_check_status_throws

    def test_max_retries_done(self):
        self.assertFalse(self.job_handler.is_job_done())
        self.vnc_api.execute_job.return_value = {'job_execution_id': 'job-1'}
        self.post_mock.side_effect = [self.NO_LOG_RESPONSE,
                                      self.NO_LOG_RESPONSE,
                                      self.NO_LOG_RESPONSE,
                                      self.NO_LOG_RESPONSE,
                                      self.NO_LOG_RESPONSE,
                                      self.NO_LOG_RESPONSE]

        self.assertEqual(self.job_handler.get_job_status(), JobStatus.INIT)
        self.assertRaises(Exception, self.job_handler.push, self.TIMEOUT, 3)

        self.vnc_api.execute_job.assert_called_with(
            job_template_fq_name=self.job_type,
            job_input=self.job_input,
            device_list=self.device_list
        )
        self.assertEqual(self.post_mock.call_count, 6)
        self.assertTrue(self.job_handler.is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILED)
        self.assertEqual(self.sleep_mock.call_args_list,
                         [mock.call(self.TIMEOUT),
                          mock.call(self.TIMEOUT)])
    # end test_max_retries_done
# end class TestJobHandler
