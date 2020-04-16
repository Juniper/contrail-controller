# -*- coding: utf-8 -*-
import unittest
import mock

from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType

from cfgm_common.cassandra import api as cassa_api


class FakeDriver(cassa_api.CassandraDriver):
    def get_cf_batch(keyspace_name, cf_name):
        pass

    def get_range(self, keyspace_name, cf_name):
        pass

    def multiget(self, keyspace_name, cf_name, keys, columns=None, start='',
                 finish='', timestamp=False, num_columns=None):
        pass

    def get(self, keyspace_name, cf_name, key, columns=None, start='',
            finish=''):
        pass

    def get_one_col(self, keyspace_name, cf_name, key, column):
        pass

    def insert(self, key, columns, keyspace_name=None, cf_name=None,
               batch=None):
        pass

    def remove(self, key, columns=None, keyspace_name=None, cf_name=None,
               batch=None):
        pass


class TestOptions(unittest.TestCase):

    def test_keyspace_wipe(self):
        drv = FakeDriver([])
        self.assertEqual(
            "my_keyspace", drv.keyspace("my_keyspace"))

        drv = FakeDriver([], db_prefix='a_prefix')
        self.assertEqual(
            "a_prefix_my_keyspace", drv.keyspace("my_keyspace"))

    def test_reset_config_wipe(self):
        drv = FakeDriver([])
        self.assertFalse(drv.options.reset_config)

        drv = FakeDriver([], reset_config=True)
        self.assertTrue(drv.options.reset_config)

    def test_server_list(self):
        drv = FakeDriver(['a', 'b', 'c'])
        self.assertEqual(['a', 'b', 'c'], drv._server_list)

    def test_pool_size(self):
        drv = FakeDriver(['a', 'b', 'c'])
        self.assertEqual(6, drv.pool_size())

        drv = FakeDriver(['a', 'b', 'c'], pool_size=8)
        self.assertEqual(8, drv.pool_size())

    def test_nodes(self):
        drv = FakeDriver(['a', 'b', 'c'])
        self.assertEqual(3, drv.nodes())

    def test_logger_wipe(self):
        drv = FakeDriver([])
        self.assertIsNone(drv.options.logger)

        drv = FakeDriver([], logger='<something>')
        self.assertEqual('<something>', drv.options.logger)

    def test_credential_wipe(self):
        drv = FakeDriver([])
        self.assertIsNone(drv.options.credential)

        drv = FakeDriver([], credential='<creds>')
        self.assertEqual('<creds>', drv.options.credential)

    def test_ssl_enabled(self):
        drv = FakeDriver([])
        self.assertFalse(drv.options.ssl_enabled)

        drv = FakeDriver([], ssl_enabled=True)
        self.assertTrue(drv.options.ssl_enabled)

    def test_ca_certs(self):
        drv = FakeDriver([])
        self.assertIsNone(drv.options.ca_certs)

        drv = FakeDriver([], ca_certs='<certificats>')
        self.assertEqual('<certificats>', drv.options.ca_certs)


class TestStatus(unittest.TestCase):

    def test_status(self):
        drv = FakeDriver(['a', 'b', 'c'])
        self.assertEqual(ConnectionStatus.INIT, drv.get_status())

    @mock.patch.object(ConnectionState, 'update')
    def test_status_up(self, mock_state):
        drv = FakeDriver(['a', 'b', 'c'])

        drv.report_status_up()
        mock_state.assert_called_once_with(
            conn_type=ConnType.DATABASE,
            name='Cassandra',
            status=ConnectionStatus.UP,
            message='',
            server_addrs=['a', 'b', 'c'])
        self.assertEqual(ConnectionStatus.UP, drv.get_status())

    @mock.patch.object(ConnectionState, 'update')
    def test_status_down(self, mock_state):
        drv = FakeDriver(['a', 'b', 'c'])

        drv.report_status_down('i want chocolate!')
        mock_state.assert_called_once_with(
            conn_type=ConnType.DATABASE,
            name='Cassandra',
            status=ConnectionStatus.DOWN,
            message='i want chocolate!',
            server_addrs=['a', 'b', 'c'])
        self.assertEqual(ConnectionStatus.DOWN, drv.get_status())

    @mock.patch.object(ConnectionState, 'update')
    def test_status_init(self, mock_state):
        drv = FakeDriver(['a', 'b', 'c'])

        drv.report_status_init()
        mock_state.assert_called_once_with(
            conn_type=ConnType.DATABASE,
            name='Cassandra',
            status=ConnectionStatus.INIT,
            message='',
            server_addrs=['a', 'b', 'c'])
        self.assertEqual(ConnectionStatus.INIT, drv.get_status())


