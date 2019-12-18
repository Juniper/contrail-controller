#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import unittest
import mock
from cfgm_common.exceptions import TimeOutError
from attrdict import AttrDict
from device_manager.job_handler import JobStatus
from device_manager.job_handler import JobHandler


class TestJobHandler(unittest.TestCase):
    JOB_SUCCESS = AttrDict({ "job_status": "SUCCESS" })
    JOB_FAILURE = AttrDict({ "job_status": "FAILURE" })
    JOB_IN_PROGRESS = AttrDict({ "job_status": "IN_PROGRESS" })

    TIMEOUT = 15
    MAX_RETRIES = 30

    def setUp(self):
        super(TestJobHandler, self).setUp()
        self.sleep_patch = mock.patch('gevent.sleep')
        self.sleep_mock = self.sleep_patch.start()
        self.logger = mock.Mock()
        self.amqp_client = mock.Mock()
        self.message = mock.Mock()

        self.job_template_name = ['test-job']
        self.job_input = {'key1': 'value1', 'key2': 'value2'}
        self.device_list = ['device1']
        self.transaction_id = '1-2-3-4'
        self.transaction_descr = 'test-transaction'
        self.api_server_config = {
            'ips': ["1.2.3.4"],
            'port': "8082",
            'username': "admin",
            'password': "password",
            'tenant': "default",
            'use_ssl': False
        }
        self.args = AttrDict({
            'api_server_ip': '127.0.0.1',
            'admin_user': 'admin',
            'admin_password': 'admin',
            'admin_tenant_name': 'test',
            'api_server_port': 8082,
            'api_server_use_ssl': False,
            'cluster_id': ''
        })

        self.job_handler = JobHandler(self.job_template_name, self.job_input,
                                      self.device_list, self.api_server_config,
                                      self.logger, self.amqp_client,
                                      self.transaction_id,
                                      self.transaction_descr, self.args)
    # end setUp

    def tearDown(self):
        super(TestJobHandler, self).tearDown()
        self.sleep_patch.stop()
    # end tearDown

    def test_job_executed_successfully(self):
        def side_effect(*_):
            self.assertFalse(self.job_handler._is_job_done())
            self.assertEqual(self.job_handler.get_job_status(),
                             JobStatus.STARTING)
            _, kwargs = self.amqp_client.add_consumer.call_args_list[0]
            callback = kwargs['callback']
            callback(self.JOB_SUCCESS, self.message)
        # end side_effect

        self.sleep_mock.side_effect = side_effect
        self.assertFalse(self.job_handler._is_job_done())
        self.job_handler.push(self.TIMEOUT, self.MAX_RETRIES)

        self.amqp_client.add_consumer.assert_called_once()
        self.amqp_client.publish.assert_called_once()

        args, kwargs = self.amqp_client.publish.call_args_list[0]
        job_payload = args[0]
        job_execution_id = job_payload.get('job_execution_id')
        self.assertEqual(args[1], JobHandler.JOB_REQUEST_EXCHANGE)
        self.assertEqual(kwargs['routing_key'], JobHandler.JOB_REQUEST_ROUTING_KEY)

        args, kwargs = self.amqp_client.add_consumer.call_args_list[0]
        self.assertEqual(args[0], JobHandler.JOB_STATUS_CONSUMER + job_execution_id)
        self.assertEqual(args[1], JobHandler.JOB_STATUS_EXCHANGE)
        self.assertEqual(kwargs['routing_key'], JobHandler.JOB_STATUS_ROUTING_KEY + job_execution_id)
        self.assertEqual(kwargs['auto_delete'], True)

        self.message.ack.assert_called_once()
        self.amqp_client.remove_consumer.assert_called_once()

        self.assertTrue(self.job_handler._is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.SUCCESS)
    # end test_job_executed_successfully

    def test_job_stays_in_progress_then_completes(self):
        job_statuses = [self.JOB_IN_PROGRESS,
                        self.JOB_IN_PROGRESS,
                        self.JOB_SUCCESS]

        def side_effect(*_):
            side_effect.counter += 1
            _, kwargs = self.amqp_client.add_consumer.call_args_list[0]
            callback = kwargs['callback']
            callback(job_statuses[side_effect.counter-1], self.message)
        # end side_effect
        side_effect.counter = 0

        self.sleep_mock.side_effect = side_effect

        self.assertFalse(self.job_handler._is_job_done())
        self.job_handler.push(self.TIMEOUT, self.MAX_RETRIES)

        self.amqp_client.publish.assert_called_once()

        self.assertEqual(side_effect.counter, 3)
        self.assertTrue(self.job_handler._is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.SUCCESS)
    # end test_job_stays_in_progress_then_completes

    def test_job_failed(self):
        job_statuses = [self.JOB_IN_PROGRESS,
                        self.JOB_FAILURE]

        def side_effect(*_):
            side_effect.counter += 1
            _, kwargs = self.amqp_client.add_consumer.call_args_list[0]
            callback = kwargs['callback']
            callback(job_statuses[side_effect.counter-1], self.message)
        # end side_effect
        side_effect.counter = 0

        self.sleep_mock.side_effect = side_effect

        self.assertFalse(self.job_handler._is_job_done())
        self.assertRaises(Exception, self.job_handler.push, self.TIMEOUT, self.MAX_RETRIES)

        self.amqp_client.publish.assert_called_once()

        self.assertEqual(side_effect.counter, 2)
        self.assertTrue(self.job_handler._is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILURE)
    # end test_job_failed

    def test_execute_job_throws(self):
        self.assertFalse(self.job_handler._is_job_done())
        self.amqp_client.publish.side_effect = \
            TimeOutError(500)

        self.assertRaises(Exception, self.job_handler.push, self.TIMEOUT, self.MAX_RETRIES)

        self.amqp_client.publish.assert_called_once()

        self.sleep_mock.assert_not_called()
        self.assertTrue(self.job_handler._is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILURE)
    # end test_execute_job_throws

    def test_max_retries_done(self):
        job_statuses = [self.JOB_IN_PROGRESS,
                        self.JOB_IN_PROGRESS,
                        self.JOB_IN_PROGRESS]

        def side_effect(*_):
            side_effect.counter += 1
            _, kwargs = self.amqp_client.add_consumer.call_args_list[0]
            callback = kwargs['callback']
            callback(job_statuses[side_effect.counter-1], self.message)
        # end side_effect
        side_effect.counter = 0

        self.sleep_mock.side_effect = side_effect

        self.assertFalse(self.job_handler._is_job_done())
        self.assertRaises(Exception, self.job_handler.push, self.TIMEOUT, 3)

        self.amqp_client.publish.assert_called_once()

        self.assertEqual(side_effect.counter, 3)
        self.assertTrue(self.job_handler._is_job_done())
        self.assertEqual(self.job_handler.get_job_status(), JobStatus.FAILURE)
    # end test_max_retries_done
# end class TestJobHandler
