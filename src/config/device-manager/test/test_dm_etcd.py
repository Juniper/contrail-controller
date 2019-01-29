# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2013 Juniper Infra, Inc. All rights reserved.
#

import jsonpickle
import socket
import unittest

import mock

# from cfgm_common.zkclient import ZookeeperClient
from device_manager.cassandra import DMCassandraDB
from device_manager.db import DBBaseDM
from device_manager.device_manager import parse_args as dm_parse_args
from device_manager.dm_amqp import DMAmqpHandleEtcd, DMAmqpHandleRabbit
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
        self.manager = MockedManager(self.args)
        if 'host_ip' in self.args:
            self.host_ip = self.args.host_ip
        else:
            self.host_ip = socket.gethostbyname(socket.getfqdn())

    # check for xml conf generation, very basic validation
    # def test_cassandra_generation(self):
    #     db = DMCassandraDB.get_instance(self.manager, self.zkclient)
    #     self.assertIsInstance(db, DMCassandraDB)
    #     DMCassandraDB.clear_instance()

    @mock.patch('etcd3.client', autospec=True)
    def test_etcd_generation(self, etcd_client):
        db = DMEtcdDB.get_instance(self.args, mock.MagicMock())
        self.assertIsInstance(db, DMEtcdDB)
        self.asert
        DMEtcdDB.clear_instance()

    # # test dm instance_etcd
    # def test_dm_instance_etcd(self):
    #     FakeDeviceConnect.reset()
    #     FakeJobHandler.reset()
    #     kill_device_manager(TestInfraDM._dm_greenlet)
    #     self.check_dm_instance()
    #     TestInfraDM._dm_greenlet = gevent.spawn(launch_device_manager_etcd,
    #         self._cluster_id, TestInfraDM._api_server_ip, TestInfraDM._api_server_port)
    #     wait_for_device_manager_up()
    #     # TODO remove this line, mateusz
    #     dm = DeviceManager.get_instance()
    #     self.assertIsNotNone(dm)
    #     self.assertIsInstance(dm, DeviceManager)
    #     self.assertIsInstance(dm._object_db, DMEtcdDB)
    #     self.assertIsInstance(dm._vnc_amqp, DMAmqpHandleEtcd)


if __name__ == '__main__':
    unittest.main()
