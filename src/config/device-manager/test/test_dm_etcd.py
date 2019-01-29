# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Infra, Inc. All rights reserved.
#

import etcd3
import jsonpickle
import socket
import unittest

import mock

# from cfgm_common.zkclient import ZookeeperClient
from cfgm_common.vnc_etcd import VncEtcd
from device_manager.cassandra import DMCassandraDB
from device_manager.db import DBBaseDM
from device_manager.device_manager import parse_args as dm_parse_args
from device_manager.dm_amqp import dm_amqp_factory, DMAmqpHandleEtcd, DMAmqpHandleRabbit
from device_manager.etcd import DMEtcdDB
from vnc_api.vnc_api import VncApi

#
# All database related DM test cases should go here
#

ETCD_HOST = 'etcd-host-01'


class MockedManager(object):
    def __init__(self, args):
        self._args = args
        self.logger = mock.MagicMock()


class MockedMeta(object):
    def __init__(self, key):
        self.key = key


class ETCD3Mock:
    def __init__(self):
        self._kv_data = {}

    def put(self, key, value):
        self._kv_data[key] = value

    def get(self, key):
        if key in self._kv_data:
            return self._kv_data[key], MockedMeta(key)
        return None, None

    def get_prefix(self, key):
        return [(self._kv_data[k], MockedMeta(k)) for k in self._kv_data if k.startswith(key)]

    def delete(self, key):
        if key in self._kv_data:
            del self._kv_data[key]


class TestDMEtcdDB(unittest.TestCase):

    def setUp(self):
        self.etcd3 = ETCD3Mock()
        self.etcd3.put('/vnc/device_manager/XXX', 8000100)
        self.etcd3.put('/vnc/device_manager/YYY', 'ala ma kota')
        self.etcd3.put('/vnc/device_manager/ZZZ',
                       jsonpickle.encode({'a': 1, 'b': 2, 'c': 'XYZ'}))
        self.args = dm_parse_args('')
        self.args.etcd_server = ETCD_HOST
        self.manager = MockedManager(self.args)
        if 'host_ip' in self.args:
            self.host_ip = self.args.host_ip
        else:
            self.host_ip = socket.gethostbyname(socket.getfqdn())

    @mock.patch('etcd3.client', autospec=True)
    def test_etcd_db_generation(self, etcd_client):
        db = DMEtcdDB.get_instance(self.args, mock.MagicMock())
        self.assertIsNotNone(db)
        self.assertIsInstance(db, DMEtcdDB)
        self.assertIsInstance(db._object_db, VncEtcd)
        self.assertEqual(db._object_db._host, ETCD_HOST)
        self.assertEqual(db._object_db._port, '2379')
        self.assertIsNone(db._object_db._credentials)
        self.assertTrue('get_one_entry' in dir(db))
        self.assertTrue('get_kv' in dir(db._object_db))
        DMEtcdDB.clear_instance()

    @mock.patch('etcd3.client', autospec=True)
    def test_etcd_amqp_generation(self, etcd_client):
        amqp = dm_amqp_factory(mock.MagicMock(), {}, self.args)
        self.assertIsNotNone(amqp)
        self.assertTrue('establish' in dir(amqp))
        self.assertTrue('evaluate_dependency' in dir(amqp))


if __name__ == '__main__':
    unittest.main()
