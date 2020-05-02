# -*- coding: utf-8 -*-
#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

from __future__ import absolute_import

import unittest
import mock
import six

from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionStatus
from pysandesh.gen_py.process_info.ttypes import ConnectionType as ConnType

from cfgm_common.cassandra import api as cassa_api
from cfgm_common.exceptions import NoIdError, DatabaseUnavailableError, VncError

# Drivers
from cfgm_common.cassandra.drivers import thrift
from cfgm_common.cassandra.drivers import datastax


class FakeDriver(cassa_api.CassandraDriver):
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

    def _Init_Cluster(self):
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
        thrift.pycassa = mock.MagicMock()
        thrift.transport = mock.MagicMock()

        # Mock creating keyspaces
        def _Init_Cluster(self):
            self._cf_dict = {
                cassa_api.OBJ_UUID_CF_NAME: mock.MagicMock(),
                cassa_api.OBJ_FQ_NAME_CF_NAME: mock.MagicMock(),
                cassa_api.OBJ_SHARED_CF_NAME: mock.MagicMock(),
            }
        # Mock handle_exceptions
        def _handle_exceptions(self, func, oper=None):
            def wrapper(*args, **kwargs):
                return func(*args, **kwargs)
            return wrapper
        p = []
        p.append(mock.patch(
            'cfgm_common.cassandra.drivers.thrift.CassandraDriverThrift._Init_Cluster',
            _Init_Cluster))
        p.append(mock.patch(
            'cfgm_common.cassandra.drivers.thrift.CassandraDriverThrift._handle_exceptions',
            _handle_exceptions))
        [x.start() for x in p]

        self.drv = thrift.CassandraDriverThrift(['a', 'b'])

        # Ensure to cleanup mockings
        [self.addCleanup(x.stop) for x in p]

    def test_import_error(self):
        thrift.pycassa = None
        self.assertRaises(ImportError, thrift.CassandraDriverThrift, ['a', 'b'])

    def test_get_count(self):
        self.drv.get_count(
            cassa_api.OBJ_UUID_CF_NAME, '<uuid>', start='a', finish='z')
        self.drv._cf_dict[
            cassa_api.OBJ_UUID_CF_NAME].get_count.assert_called_once_with(
                '<uuid>', column_finish='z', column_start='a')

    def test_xget(self):
        self.drv.xget(
            cassa_api.OBJ_UUID_CF_NAME, '<uuid>', start='a', finish='z')
        self.drv._cf_dict[
            cassa_api.OBJ_UUID_CF_NAME].xget.assert_called_once_with(
                '<uuid>', column_finish='z', column_start='a')

    def test_get_range(self):
        self.drv.get_range(
            cassa_api.OBJ_UUID_CF_NAME, columns=['type', 'fq_name'])
        self.drv._cf_dict[
            cassa_api.OBJ_UUID_CF_NAME].get_range.assert_called_once_with(
                column_count=100000, columns=['type', 'fq_name'])

    def test_remove(self):
        self.drv.remove(
            cf_name=cassa_api.OBJ_FQ_NAME_CF_NAME, key='<uuid>', columns=['fq_name'])
        self.drv._cf_dict[
            cassa_api.OBJ_FQ_NAME_CF_NAME].remove.assert_called_once_with(
                '<uuid>', ['fq_name'])

    def test_insert(self):
        self.drv.insert(
            cf_name=cassa_api.OBJ_FQ_NAME_CF_NAME, key='<uuid>', columns=['fq_name'])
        self.drv._cf_dict[
            cassa_api.OBJ_FQ_NAME_CF_NAME].insert.assert_called_once_with(
                '<uuid>', ['fq_name'])

    def test_get_cf(self):
        self.assertEqual(
            self.drv._cf_dict[cassa_api.OBJ_FQ_NAME_CF_NAME],
            self.drv.get_cf(cassa_api.OBJ_FQ_NAME_CF_NAME))


class TestDataStax(unittest.TestCase):
    def setUp(self):
        # Mock the libraries
        datastax.cassandra = mock.MagicMock()

        # Mock creating keyspaces
        def _Init_Cluster(self):
            self._cf_dict = {
                cassa_api.OBJ_UUID_CF_NAME: mock.MagicMock(),
                cassa_api.OBJ_FQ_NAME_CF_NAME: mock.MagicMock(),
                cassa_api.OBJ_SHARED_CF_NAME: mock.MagicMock(),
            }
            self._cluster = mock.MagicMock()
        p = []
        p.append(mock.patch(
            'cfgm_common.cassandra.drivers.datastax.DataStax._Init_Cluster',
            _Init_Cluster))
        p.append(mock.patch(
            'cfgm_common.cassandra.drivers.datastax.JsonToObject',
            lambda x: x))
        [x.start() for x in p]

        self.drv = datastax.DataStax(['a', 'b'], logger=mock.MagicMock())

        # Ensure to cleanup mockings
        [self.addCleanup(x.stop) for x in p]

    def test_import_error(self):
        datastax.cassandra = None
        self.assertRaises(ImportError, datastax.DataStax, ['a', 'b'])

    def test_default_session(self):
        self.drv.get_default_session()
        self.drv._cluster.connect.assert_called_once_with()

    def assertCql(self, wanted, mocked):
        f = lambda x: x.strip().replace(' ', '').replace('\n', '')
        args, kwargs = mocked.call_args
        self.assertEqual(
            f(wanted), f(args[0]), msg="cql wanted is: " + args[0])

    def test_safe_drop_keyspace(self):
        session = mock.MagicMock()
        self.drv._cluster.connect.return_value = session
        self.drv.safe_drop_keyspace(cassa_api.OBJ_UUID_CF_NAME)
        self.assertCql(
            """
            DROP KEYSPACE "obj_uuid_table"
            """,
            session.execute)

    def test_safe_create_keyspace(self):
        session = mock.MagicMock()
        self.drv._cluster.connect.return_value = session
        self.drv.safe_create_keyspace(cassa_api.OBJ_UUID_CF_NAME)
        self.assertCql(
            """
            CREATE KEYSPACE "obj_uuid_table" 
              WITH replication = { 
                'class': 'SimpleStrategy',
                'replication_factor': '2'
            }""",
            session.execute)

    def test_safe_create_table(self):
        session = self.drv.get_cf(cassa_api.OBJ_UUID_CF_NAME)
        self.drv.safe_create_table(cassa_api.OBJ_UUID_CF_NAME)
        self.assertCql(
            """
            CREATE TABLE "obj_uuid_table" (
              key blob,
              column1 blob,
              value text,
              PRIMARY KEY (key, column1)
            ) WITH CLUSTERING ORDER BY (column1 ASC)
              AND gc_grace_seconds = 864000
            """,
            session.execute)

    def test_get_count(self):
        session = self.drv.get_cf(cassa_api.OBJ_UUID_CF_NAME)
        self.drv.get_count(cassa_api.OBJ_UUID_CF_NAME,
                           'a_key', start='from', finish='end')
        self.assertCql(
            """
            SELECT COUNT(*) FROM "obj_uuid_table"
            WHERE key = textAsBlob(?)
            AND column1 > textAsBlob(?)
            AND column1 < textAsBlob(?)
            """, session.prepare)
        session.prepare().bind.assert_called_once_with(
            ['a_key', 'from', 'end'])

    def test_get_range(self):
        session = self.drv.get_cf(cassa_api.OBJ_UUID_CF_NAME)
        self.drv.get_range(cassa_api.OBJ_UUID_CF_NAME,
                           columns=['a_col1', 'a_col2'])
        self.assertCql(
            """
            SELECT blobAsText(column1), value
            FROM "obj_uuid_table"
            LIMIT ?
            """, session.prepare)
        session.prepare().bind.assert_called_once_with(
            [100000])

    @mock.patch('cfgm_common.cassandra.drivers.datastax.Iter.__next__', return_value={})
    def test_get(self, mock_Iter__next__):
        session = self.drv.get_cf(cassa_api.OBJ_UUID_CF_NAME)
        self.drv.get(cassa_api.OBJ_UUID_CF_NAME,
                     'a_key', columns=['a_col1', 'a_col2'],
                     start='from', finish='end')
        self.assertCql(
            """
            SELECT blobAsText(column1), value
            FROM "obj_uuid_table"
            WHERE key = textAsBlob(?)
            AND column1 > textAsBlob(?)
            AND column1 < textAsBlob(?)
            """, session.prepare)
        session.prepare().bind.assert_called_once_with(
            ['a_key', 'from', 'end'])

    @mock.patch('cfgm_common.cassandra.drivers.datastax.Iter.get_next_items')
    def test_xget(self, mock_Iter_get_next_items):
        mock_Iter_get_next_items.return_value=[
            ('a_col1', 'a_value1'),
            ('a_col2', 'a_value2'),
            ('a_col3', 'a_value3')]
        session = self.drv.get_cf(cassa_api.OBJ_UUID_CF_NAME)
        row = self.drv.xget(cassa_api.OBJ_UUID_CF_NAME,
                            'a_key', columns=['a_col1', 'a_col2'],
                            start='from', finish='end')
        self.assertCql(
            """
            SELECT blobAsText(column1), value
            FROM "obj_uuid_table"
            WHERE key = textAsBlob(?)
            AND column1 > textAsBlob(?)
            AND column1 < textAsBlob(?)
            """, session.prepare)
        session.prepare().bind.assert_called_once_with(
            ['a_key', 'from', 'end'])
        self.assertEqual(
            [('a_col1', 'a_value1'), ('a_col2', 'a_value2')],
            row)

    @mock.patch('cfgm_common.cassandra.drivers.datastax.Iter.get_next_items')
    def test_get_one_col(self, mock_Iter_get_next_items):
        mock_Iter_get_next_items.side_effect=[
            [('a_col1', 'a_value1'),
             ('a_col2', 'a_value2'),
             ('a_col3', 'a_value3')], StopIteration]
        session = self.drv.get_cf(cassa_api.OBJ_UUID_CF_NAME)
        row = self.drv.get_one_col(cassa_api.OBJ_UUID_CF_NAME,
                                   'a_key', column='a_col2')
        self.assertCql(
            """
            SELECT blobAsText(column1), value
            FROM "obj_uuid_table"
            WHERE key = textAsBlob(?)
            """, session.prepare)
        session.prepare().bind.assert_called_once_with(
            ['a_key'])
        self.assertEqual(row, 'a_value2')

    @mock.patch('cfgm_common.cassandra.drivers.datastax.Iter.get_next_items')
    def test_get_one_col_multi_match(self, mock_Iter_get_next_items):
        mock_Iter_get_next_items.side_effect=[
            [('a_col1', 'a_value1'),
             ('a_col2', 'a_value2'),
             ('a_col3', 'a_value3')], [('a_col3', 'a_value3')]]
        session = self.drv.get_cf(cassa_api.OBJ_UUID_CF_NAME)
        self.assertRaises(VncError,
                          self.drv.get_one_col,
                          cassa_api.OBJ_UUID_CF_NAME,
                          'a_key', column='a_col2')

    @mock.patch('cfgm_common.cassandra.drivers.datastax.Iter.get_next_items')
    def test_get_one_col_no_id(self, mock_Iter_get_next_items):
        mock_Iter_get_next_items.side_effect=[]
        session = self.drv.get_cf(cassa_api.OBJ_UUID_CF_NAME)
        self.assertRaises(NoIdError,
                          self.drv.get_one_col,
                          cassa_api.OBJ_UUID_CF_NAME,
                          'a_key', column='a_col1')

    def test_get_count(self):
        session = self.drv.get_cf(cassa_api.OBJ_UUID_CF_NAME)
        session.execute().one.return_value=(8,)
        row = self.drv.get_count(cassa_api.OBJ_UUID_CF_NAME,
                                 'a_key', start='from', finish='end')
        self.assertCql(
            """
            SELECT COUNT(*) FROM "obj_uuid_table"
            WHERE key = textAsBlob(?)
            AND column1 > textAsBlob(?)
            AND column1 < textAsBlob(?) 
            """, session.prepare)
        session.prepare().bind.assert_called_once_with(
            ['a_key', 'from', 'end'])
        self.assertEqual(8, row)

    @mock.patch('cfgm_common.cassandra.drivers.datastax.DataStax._Get_CF_Batch')
    def test_insert(self, mock__Get_CF_Batch):
        batch = mock.MagicMock()
        batch.cf_name = cassa_api.OBJ_UUID_CF_NAME
        mock__Get_CF_Batch.return_value = batch
        self.drv.insert(cf_name=cassa_api.OBJ_UUID_CF_NAME,
                        key='a_key', columns={'a_col1': 'a_val1'})
        self.assertCql(
            """
            INSERT INTO "obj_uuid_table"
            (key, column1, value)
            VALUES (textAsBlob(%s), textAsBlob(%s), %s)
            """, batch.add)
        batch.add.assert_called_once_with(
            mock.ANY, ['a_key', 'a_col1', 'a_val1'])
        batch.send.assert_called_once_with()

    @mock.patch('cfgm_common.cassandra.drivers.datastax.DataStax._Get_CF_Batch')
    def test_insert_multi(self, mock__Get_CF_Batch):
        batch = mock.MagicMock()
        batch.cf_name = cassa_api.OBJ_UUID_CF_NAME
        mock__Get_CF_Batch.return_value = batch
        self.drv.insert(cf_name=cassa_api.OBJ_UUID_CF_NAME,
                        key='a_key', columns={'a_col1': 'a_val1',
                                              'a_col2': 'a_val2'})
        self.assertCql(
            """
            INSERT INTO "obj_uuid_table"
            (key, column1, value)
            VALUES (textAsBlob(%s), textAsBlob(%s), %s)
            """, batch.add)
        batch.add.assert_has_calls([
            mock.call(mock.ANY, ['a_key', 'a_col1', 'a_val1']),
            mock.call(mock.ANY, ['a_key', 'a_col2', 'a_val2'])])
        batch.send.assert_called_once_with()

    @mock.patch('cfgm_common.cassandra.drivers.datastax.DataStax._Get_CF_Batch')
    def test_insert_batch(self, mock__Get_CF_Batch):
        batch = mock.MagicMock()
        batch.cf_name = cassa_api.OBJ_UUID_CF_NAME
        mock__Get_CF_Batch.return_value = batch
        self.drv.insert(cf_name=cassa_api.OBJ_UUID_CF_NAME,
                        key='a_key', columns={'a_col1': 'a_val1'},
                        batch=batch)
        self.assertCql(
            """
            INSERT INTO "obj_uuid_table"
            (key, column1, value)
            VALUES (textAsBlob(%s), textAsBlob(%s), %s)
            """, batch.add)
        batch.add.assert_called_once_with(
            mock.ANY, ['a_key', 'a_col1', 'a_val1'])
        batch.send.assert_not_called()

    @mock.patch('cfgm_common.cassandra.drivers.datastax.DataStax._Get_CF_Batch')
    def test_insert_batch_multi(self, mock__Get_CF_Batch):
        batch = mock.MagicMock()
        batch.cf_name = cassa_api.OBJ_UUID_CF_NAME
        mock__Get_CF_Batch.return_value = batch
        self.drv.insert(cf_name=cassa_api.OBJ_UUID_CF_NAME,
                        key='a_key', columns={'a_col1': 'a_val1',
                                              'a_col2': 'a_val2'},
                        batch=batch)
        self.assertCql(
            """
            INSERT INTO "obj_uuid_table"
            (key, column1, value)
            VALUES (textAsBlob(%s), textAsBlob(%s), %s)
            """, batch.add)
        batch.add.assert_has_calls([
            mock.call(mock.ANY, ['a_key', 'a_col1', 'a_val1']),
            mock.call(mock.ANY, ['a_key', 'a_col2', 'a_val2'])])
        batch.send.assert_not_called()

    @mock.patch('cfgm_common.cassandra.drivers.datastax.DataStax._Get_CF_Batch')
    def test_remove_key(self, mock__Get_CF_Batch):
        batch = mock.MagicMock()
        batch.cf_name = cassa_api.OBJ_UUID_CF_NAME
        mock__Get_CF_Batch.return_value = batch
        self.drv.remove(cf_name=cassa_api.OBJ_UUID_CF_NAME,
                        key='a_key')
        self.assertCql(
            """
            DELETE FROM obj_uuid_table
            WHERE key = textAsBlob(%s)
            """, batch.add)
        batch.add.assert_called_once_with(
            mock.ANY, ['a_key'])
        batch.send.assert_called_once_with()

    @mock.patch('cfgm_common.cassandra.drivers.datastax.DataStax._Get_CF_Batch')
    def test_remove_key_batch(self, mock__Get_CF_Batch):
        batch = mock.MagicMock()
        batch.cf_name = cassa_api.OBJ_UUID_CF_NAME
        mock__Get_CF_Batch.return_value = batch
        self.drv.remove(cf_name=cassa_api.OBJ_UUID_CF_NAME,
                        key='a_key',
                        batch=batch)
        self.assertCql(
            """
            DELETE FROM obj_uuid_table
            WHERE key = textAsBlob(%s)
            """, batch.add)
        batch.add.assert_called_once_with(
            mock.ANY, ['a_key'])
        batch.send.assert_not_called()

    @mock.patch('cfgm_common.cassandra.drivers.datastax.DataStax._Get_CF_Batch')
    def test_remove_col(self, mock__Get_CF_Batch):
        batch = mock.MagicMock()
        batch.cf_name = cassa_api.OBJ_UUID_CF_NAME
        mock__Get_CF_Batch.return_value = batch
        self.drv.remove(cf_name=cassa_api.OBJ_UUID_CF_NAME,
                        key='a_key', columns=['a_col1', 'a_col2'])
        self.assertCql(
            """
            DELETE FROM obj_uuid_table
            WHERE key = textAsBlob(%s)
            AND column1 = textAsBlob(%s)
            """, batch.add)
        batch.add.assert_has_calls([
            mock.call(mock.ANY, ['a_key', 'a_col1']),
            mock.call(mock.ANY, ['a_key', 'a_col2'])])
        batch.send.assert_called_once_with()

    @mock.patch('cfgm_common.cassandra.drivers.datastax.DataStax._Get_CF_Batch')
    def test_remove_col_batch(self, mock__Get_CF_Batch):
        batch = mock.MagicMock()
        batch.cf_name = cassa_api.OBJ_UUID_CF_NAME
        mock__Get_CF_Batch.return_value = batch
        self.drv.remove(cf_name=cassa_api.OBJ_UUID_CF_NAME,
                        key='a_key', columns=['a_col1', 'a_col2'],
                        batch=batch)
        self.assertCql(
            """
            DELETE FROM obj_uuid_table
            WHERE key = textAsBlob(%s)
            AND column1 = textAsBlob(%s)
            """, batch.add)
        batch.add.assert_has_calls([
            mock.call(mock.ANY, ['a_key', 'a_col1']),
            mock.call(mock.ANY, ['a_key', 'a_col2'])])
        batch.send.assert_not_called()
