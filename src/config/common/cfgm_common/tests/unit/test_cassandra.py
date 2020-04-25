# -*- coding: utf-8 -*-

#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import unittest
import mock

from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType

from cfgm_common.datastore import api as datastore_api

# Drivers
from cfgm_common.datastore.drivers import cassandra_thrift


class FakeDriver(datastore_api.CassandraDriver):
    def _Get_CF_Batch(self, cf_name, keyspace_name=None):
        pass

    def _Get_Range(self, cf_name, columns=None, column_count=100000):
        pass

    def _Multiget(self, cf_name, keys, columns=None, start='', finish='',
                  timestamp=False, num_columns=None):
        pass

    def _Get(self, keyspace_name, cf_name, key, columns=None, start='',
             finish=''):
        pass

    def _XGet(self, cf_name, key, columns=None, start='', finish=''):
        pass

    def _Get_Count(self, cf_name, key, start='', finish='', keyspace_name=None):
        pass

    def _Get_One_Col(self, cf_name, key, column):
        pass

    def _Insert(self, key, columns, keyspace_name=None, cf_name=None,
                batch=None, column_family=None):
        pass

    def _Remove(self, key, columns=None, keyspace_name=None, cf_name=None,
                batch=None, column_family=None):
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

    def test_ro_keyspaces(self):
        drv = FakeDriver([])
        self.assertEqual({
            'config_db_uuid': {
                'obj_fq_name_table': {
                    'cf_args': {'autopack_values': False}},
                'obj_shared_table': {},
                'obj_uuid_table': {
                    'cf_args': {'autopack_names': False,
                                'autopack_values': False}}
            }}, drv.options.ro_keyspaces)

        drv = FakeDriver([], ro_keyspaces={'a': 'b'})
        self.assertEqual({
            'a': 'b',
            'config_db_uuid': {
                'obj_fq_name_table': {
                    'cf_args': {'autopack_values': False}},
                'obj_shared_table': {},
                'obj_uuid_table': {
                    'cf_args': {'autopack_names': False,
                                'autopack_values': False}}
            }}, drv.options.ro_keyspaces)

    def test_rw_keyspaces(self):
        drv = FakeDriver([])
        self.assertEqual({}, drv.options.rw_keyspaces)

        drv = FakeDriver([], rw_keyspaces={'c': 'd'})
        self.assertEqual({'c': 'd'}, drv.options.rw_keyspaces)

    def test_log_response_time(self):
        drv = FakeDriver([])
        self.assertIsNone(drv.options.log_response_time)
        # TODO(sahid): Should be removed
        self.assertIsNone(drv.log_response_time)

        drv = FakeDriver([], log_response_time='<time>')
        self.assertEqual('<time>', drv.options.log_response_time)
        # TODO(sahid): Should be removed
        self.assertEqual('<time>', drv.log_response_time)

    def test_genereate_url(self):
        drv = FakeDriver([])
        self.assertIsNotNone(drv.options.generate_url)
        # TODO(sahid): Should be removed
        self.assertIsNotNone(drv._generate_url)

        drv = FakeDriver([], generate_url='<url>')
        self.assertEqual('<url>', drv.options.generate_url)
        # TODO(sahid): Should be removed
        self.assertEqual('<url>', drv._generate_url)


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


class TestCassandraDriverThrift(unittest.TestCase):
    # The aim here is not to test the legacy driver which already runs
    # in production, but test new methods updated to avoid
    # regressions.

    def setUp(self):
        # Mock the libraries
        cassandra_thrift.pycassa = mock.MagicMock()
        cassandra_thrift.transport = mock.MagicMock()

        # Mock creating keyspaces
        def _cassandra_init(self, server_list):
            self._cf_dict = {
                datastore_api.OBJ_UUID_CF_NAME: mock.MagicMock(),
                datastore_api.OBJ_FQ_NAME_CF_NAME: mock.MagicMock(),
                datastore_api.OBJ_SHARED_CF_NAME: mock.MagicMock(),
            }
        # Mock handle_exceptions
        def _handle_exceptions(self, func, oper=None):
            def wrapper(*args, **kwargs):
                return func(*args, **kwargs)
            return wrapper
        p = []
        p.append(mock.patch(
            'cfgm_common.datastore.drivers.cassandra_thrift.CassandraDriverThrift._cassandra_init',
            _cassandra_init))
        p.append(mock.patch(
            'cfgm_common.datastore.drivers.cassandra_thrift.CassandraDriverThrift._handle_exceptions',
            _handle_exceptions))
        [x.start() for x in p]

        self.drv = cassandra_thrift.CassandraDriverThrift(['a', 'b'])

        # Ensure to cleanup mockings
        [self.addCleanup(x.stop) for x in p]

    def test_import_error(self):
        cassandra_thrift.pycassa = None
        self.assertRaises(ImportError, cassandra_thrift.CassandraDriverThrift, ['a', 'b'])

    def test_get_count(self):
        self.drv.get_count(
            datastore_api.OBJ_UUID_CF_NAME, '<uuid>', start='a', finish='z')
        self.drv._cf_dict[
            datastore_api.OBJ_UUID_CF_NAME].get_count.assert_called_once_with(
                '<uuid>', column_finish='z', column_start='a')

    def test_xget(self):
        self.drv.xget(
            datastore_api.OBJ_UUID_CF_NAME, '<uuid>', start='a', finish='z')
        self.drv._cf_dict[
            datastore_api.OBJ_UUID_CF_NAME].xget.assert_called_once_with(
                '<uuid>', column_finish='z', column_start='a')

    def test_get_range(self):
        self.drv.get_range(
            datastore_api.OBJ_UUID_CF_NAME, columns=['type', 'fq_name'])
        self.drv._cf_dict[
            datastore_api.OBJ_UUID_CF_NAME].get_range.assert_called_once_with(
                column_count=100000, columns=['type', 'fq_name'])

    def test_remove(self):
        self.drv.remove(
            cf_name=datastore_api.OBJ_FQ_NAME_CF_NAME, key='<uuid>', columns=['fq_name'])
        self.drv._cf_dict[
            datastore_api.OBJ_FQ_NAME_CF_NAME].remove.assert_called_once_with(
                '<uuid>', ['fq_name'])

    def test_insert(self):
        self.drv.insert(
            cf_name=datastore_api.OBJ_FQ_NAME_CF_NAME, key='<uuid>', columns=['fq_name'])
        self.drv._cf_dict[
            datastore_api.OBJ_FQ_NAME_CF_NAME].insert.assert_called_once_with(
                '<uuid>', ['fq_name'])
