# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Infra, Inc. All rights reserved.
#
import jsonpickle
import mock
import socket
import unittest

from cfgm_common.kombu_amqp import KombuAmqpClient
from cfgm_common.zkclient import ZookeeperClient
from pysandesh.sandesh_base import Sandesh

from device_manager.cassandra import DMCassandraDB
from device_manager.device_job_manager import DeviceJobManager
from device_manager.device_ztp_manager import DeviceZtpManager
from device_manager.dm_server import initialize_amqp_client as dm_initialize_amqp_client
from device_manager.dm_server_args import parse_args as dm_parse_args
from device_manager.etcd import DMEtcdDB
from device_manager.logger import DeviceManagerLogger
from vnc_api.vnc_api import VncApi

ETCD_HOST = 'etcd-host-01'


class AbstractTestDMManagers(unittest.TestCase):

    def setUp(self):
        self.args = dm_parse_args('')
        self.args.etcd_server = ETCD_HOST
        if 'host_ip' in self.args:
            self.host_ip = self.args.host_ip
        else:
            self.host_ip = socket.gethostbyname(socket.getfqdn())
            self.args.host_ip = self.host_ip
        self.logger = mock.Mock(
            name='DeviceManagerLogger', spec=DeviceManagerLogger)
        self.logger._sandesh = mock.Mock(name='Sandesh', spec=Sandesh)
        self.amqp_mocked_calls = []

    def _mock_amqp_mock(self, amqp_client):
        # As KombuAmqpClient class is usually mocked in tests (see
        # common/tests/test_common.py).  This is approach to 'mock a mocker'
        # and verify if certain methods are used.
        def amqp_add_consumer(name, exchange, routing_key='', **kwargs):
            self.amqp_mocked_calls.append(('add_consumer', (name, exchange,
                                                            routing_key)))

        def amqp_add_exchange(name, type='direct', durable=False, **kwargs):
            self.amqp_mocked_calls.append(('add_exchange', (name, type,
                                                            durable)))

        def amqp_publish(message, exchange, routing_key=None, **kwargs):
            self.amqp_mocked_calls.append(('publish', (message, exchange,
                                                       routing_key)))

        if amqp_client is None:
            amqp_client = mock.Mock(
                name='KombuAmqpClient', spec=KombuAmqpClient)
        amqp_client.add_consumer = amqp_add_consumer
        amqp_client.add_exchange = amqp_add_exchange
        amqp_client.publish = amqp_publish
        return amqp_client


class TestDeviceJobManager(AbstractTestDMManagers):

    def setUp(self):
        super(TestDeviceJobManager, self).setUp()
        self.zk_client = mock.Mock(
            name='ZookeeperClient', spec=ZookeeperClient)

    def test_job_manager_cassandra_n_rabbit(self):
        self.args.notification_driver = 'rabbit'
        self.args.db_driver = 'cassandra'
        self.amqp_mocked_calls = []
        amqp_client = self._mock_amqp_mock(
            dm_initialize_amqp_client(self.logger, self.args))
        db_conn = mock.Mock(name='DMCassandraDB', spec=DMCassandraDB)
        job_mgr = DeviceJobManager(db_conn, amqp_client, self.zk_client,
                                   self.args, self.logger)
        self.assertIsInstance(job_mgr, DeviceJobManager)
        self._verify_amqp_mocked_calls()
        self.amqp_mocked_calls = []

    def test_job_manager_etcd(self):
        self.args.notification_driver = 'etcd'
        self.args.db_driver = 'etcd'
        self.amqp_mocked_calls = []
        amqp_client = self._mock_amqp_mock(
            dm_initialize_amqp_client(self.logger, self.args))
        db_conn = mock.Mock(name='DMEtcdDB', spec=DMEtcdDB)
        job_mgr = DeviceJobManager(db_conn, amqp_client, self.zk_client,
                                   self.args, self.logger)
        self.assertIsInstance(job_mgr, DeviceJobManager)
        self._verify_amqp_mocked_calls()
        self.amqp_mocked_calls = []

    def _verify_amqp_mocked_calls(self, amqp_mocked_calls=None):
        if amqp_mocked_calls is None:
            amqp_mocked_calls = self.amqp_mocked_calls
        self.assertEqual(len(amqp_mocked_calls), 4)
        self.assertEqual(amqp_mocked_calls[0][0], 'add_exchange')
        self.assertTupleEqual(amqp_mocked_calls[0][1],
                              ('job_status_exchange', 'direct', False))
        self.assertEqual(amqp_mocked_calls[1][0], 'add_consumer')
        self.assertTupleEqual(amqp_mocked_calls[1][1],
                              ('job_status_consumer.dummy',
                               'job_status_exchange', 'job.status.dummy'))
        self.assertEqual(amqp_mocked_calls[2][0], 'add_exchange')
        self.assertTupleEqual(amqp_mocked_calls[2][1],
                              ('job_request_exchange', 'direct', False))
        self.assertEqual(amqp_mocked_calls[3][0], 'add_consumer')
        self.assertTupleEqual(
            amqp_mocked_calls[3][1],
            ('job_request_consumer', 'job_request_exchange', 'job.request'))


class TestDeviceZtpManager(AbstractTestDMManagers):

    def setUp(self):
        super(TestDeviceZtpManager, self).setUp()
        self.args.dnsmasq_conf_dir = '/tmp'
        self.args.tftp_dir = '/tmp'
        self.args.dhcp_leases_file = '/dev/null'

    def test_ztp_manager_rabbit(self):
        self.args.notification_driver = 'rabbit'
        self.args.db_driver = 'cassandra'
        self.amqp_mocked_calls = []
        amqp_client = self._mock_amqp_mock(
            dm_initialize_amqp_client(self.logger, self.args))
        ztp_mgr = DeviceZtpManager(amqp_client, self.args, self.host_ip,
                                   self.logger)
        self.assertIsInstance(ztp_mgr, DeviceZtpManager)
        self._verify_amqp_mocked_calls()
        self.amqp_mocked_calls = []

    def test_ztp_manager_etcd(self):
        self.args.notification_driver = 'etcd'
        self.args.db_driver = 'etcd'
        self.amqp_mocked_calls = []
        amqp_client = self._mock_amqp_mock(
            dm_initialize_amqp_client(self.logger, self.args))
        ztp_mgr = DeviceZtpManager(amqp_client, self.args, self.host_ip,
                                   self.logger)
        self.assertIsInstance(ztp_mgr, DeviceZtpManager)
        self._verify_amqp_mocked_calls()
        self.amqp_mocked_calls = []

    def _verify_amqp_mocked_calls(self, amqp_mocked_calls=None):
        if amqp_mocked_calls is None:
            amqp_mocked_calls = self.amqp_mocked_calls
        self.assertEqual(len(amqp_mocked_calls), 3)
        self.assertEqual(amqp_mocked_calls[0][0], 'add_exchange')
        self.assertTupleEqual(amqp_mocked_calls[0][1],
                              ('device_ztp_exchange', 'direct', False))
        self.assertEqual(amqp_mocked_calls[1][0], 'add_consumer')
        self.assertEqual(len(amqp_mocked_calls[1][1]), 3)
        self.assertRegexpMatches(amqp_mocked_calls[1][1][0],
                                 r"device_manager_ztp\.\S+\.config_queue")
        self.assertEqual(amqp_mocked_calls[1][1][1], 'device_ztp_exchange')
        self.assertEqual(amqp_mocked_calls[1][1][2], 'device_ztp.config.file')
        self.assertEqual(amqp_mocked_calls[2][0], 'add_consumer')
        self.assertEqual(len(amqp_mocked_calls[2][1]), 3)
        self.assertRegexpMatches(amqp_mocked_calls[2][1][0],
                                 r"device_manager_ztp\.\S+\.tftp_queue")
        self.assertEqual(amqp_mocked_calls[2][1][1], 'device_ztp_exchange')
        self.assertEqual(amqp_mocked_calls[2][1][2], 'device_ztp.tftp.file')


if __name__ == "__main__":
    unittest.main()
