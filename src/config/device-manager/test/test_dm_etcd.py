# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Infra, Inc. All rights reserved.
#
import socket
import unittest

import jsonpickle
import mock
from cfgm_common.vnc_etcd import VncEtcd
from device_manager.db import (PortTupleDM, ServiceInstanceDM,
                               VirtualMachineInterfaceDM)
from device_manager.device_manager import parse_args as dm_parse_args
from device_manager.dm_amqp import dm_amqp_factory
from device_manager.etcd import DMEtcdDB
from vnc_api.vnc_api import VncApi

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


@mock.patch('etcd3.client', autospec=True)
class TestDMEtcdDBSimulation(unittest.TestCase):
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

    def test_etcd_amqp_generation(self, etcd_client):
        amqp = dm_amqp_factory(mock.MagicMock(), {}, self.args)
        self.assertIsNotNone(amqp)
        self.assertTrue('establish' in dir(amqp))
        self.assertTrue('evaluate_dependency' in dir(amqp))


@mock.patch('cfgm_common.vnc_etcd.VncEtcd', autospec=True)
class TestDMEtcdDB(unittest.TestCase):
    def setUp(self):
        self.vnc_lib = mock.MagicMock(autospec=VncApi)
        self.vmi = mock.MagicMock(autospec=VirtualMachineInterfaceDM)
        self.si = mock.MagicMock(autospec=ServiceInstanceDM)
        self.pt = mock.MagicMock(autospec=PortTupleDM)

        self.etcd_args = mock.MagicMock()
        self.etcd_args.etcd_server = 'localhost'
        self.etcd_args.etcd_port = '2379'
        self.etcd_args.etcd_kv_store = '/vnc'
        self.etcd_args.etcd_prefix = '/contrail'
        self.etcd_args.etcd_user = None
        self.etcd_args.etcd_password = None
        self.etcd_args.pnf_network_start = 1
        self.etcd_args.pnf_network_end = 10

        self.default_pnf = {"network_id": "1", "vlan_id": "1", "unit_id": "1"}

    def tearDown(self):
        """
        Unfortunately DMEtcdDB is a Singleton.
        It needs to be manually destroyed.
        """
        DMEtcdDB.dm_object_db_instance = None

    def test_db_instance_singleton(self, *args):
        dm_etcd_1 = DMEtcdDB.get_instance(self.etcd_args, vnc_lib=self.vnc_lib)
        dm_etcd_2 = DMEtcdDB.get_instance(self.etcd_args, vnc_lib=self.vnc_lib)
        self.assertEqual(dm_etcd_1, dm_etcd_2)

    def test_clear_instance(self, *args):
        dm_etcd_1 = DMEtcdDB.get_instance(self.etcd_args, vnc_lib=self.vnc_lib)
        self.assertIsNotNone(DMEtcdDB.dm_object_db_instance)
        DMEtcdDB.clear_instance()
        self.assertIsNone(DMEtcdDB.dm_object_db_instance)

        dm_etcd_2 = DMEtcdDB.get_instance(self.etcd_args, vnc_lib=self.vnc_lib)
        self.assertNotEqual(dm_etcd_1, dm_etcd_2)

    def test_get_pnf_resources_empty_vmi_should_return_none(self, *args):
        self.vmi.service_instance = None
        self.vmi.physical_interface = None
        dm_etcd = DMEtcdDB.get_instance(self.etcd_args, vnc_lib=self.vnc_lib)
        self.assertIsNone(dm_etcd.get_pnf_resources(self.vmi, None))

    def test_get_pnf_resources_should_return_from_cache(self, *args):
        self.vmi.service_instance = 'si-blue-uuid'

        dm_etcd = DMEtcdDB.get_instance(self.etcd_args, vnc_lib=self.vnc_lib)
        dm_etcd.pnf_resources_map = {
            self.vmi.service_instance: self.default_pnf
        }

        result = dm_etcd.get_pnf_resources(self.vmi, None)
        self.assertEqual(result, self.default_pnf)

    def test_get_pnf_resources(self, vnc_api):
        self.vnc_lib.allocate_int.return_value = 1
        self.vnc_lib.set_int.return_value = None

        self.vmi.service_instance = 'si-blue-uuid'
        self.vmi.physical_interface = 'pi-blue-uuid'

        dm_etcd = DMEtcdDB.get_instance(self.etcd_args, vnc_lib=self.vnc_lib)
        dm_etcd._object_db = vnc_api
        dm_etcd.get_si_pr_set = mock.MagicMock(
            return_value={"pr-blue-uuid", "pr-red-uuid"})
        res = dm_etcd.get_pnf_resources(self.vmi, "pr-blue-uuid")
        dm_etcd.get_si_pr_set.assert_called_with(self.vmi.service_instance)

        expected = {'network_id': '1', 'vlan_id': '1', 'unit_id': '1'}
        self.assertDictEqual(res, expected)

    def test_delete_pnf_resources_should_return_if_not_found(self, *args):
        dm_etcd = DMEtcdDB.get_instance(self.etcd_args, vnc_lib=self.vnc_lib)
        self.assertIsNone(dm_etcd.delete_pnf_resources('si-blue-uuid'))

    @mock.patch('device_manager.db.ServiceInstanceDM.get')
    @mock.patch('device_manager.db.PortTupleDM.get')
    @mock.patch('device_manager.db.VirtualMachineInterfaceDM.get')
    def test_delete_pnf_resources(self, vnc_api, vmi_get, pt_get, si_get):
        vmi_get.return_value = self.vmi
        pt_get.return_value = self.pt
        si_get.return_value = self.si

        self.si.uuid = 'si-blue-uuid'
        self.si.port_tuples = ['pt-blue-uuid']

        self.pt.uuid = 'pt-blue-uuid'
        self.pt.virtual_machine_interfaces = ['vmi-blue-uuid']

        self.vmi.uuid = 'vmi-blue-uuid'

        dm_etcd = DMEtcdDB.get_instance(self.etcd_args, vnc_lib=self.vnc_lib)
        dm_etcd.pnf_resources_map = {self.si.uuid: self.default_pnf}
        dm_etcd._object_db = vnc_api
        dm_etcd._object_db.get_value.return_value = self.default_pnf
        dm_etcd.get_si_pr_set = mock.MagicMock(return_value={"pr-blue-uuid",
                                                             "pr-red-uuid"})

        dm_etcd.delete_pnf_resources(self.si.uuid)
        self.assertNotIn(self.si.uuid, dm_etcd.pnf_resources_map)

    def test_handle_pnf_resource_deletes(self, *args):
        dm_etcd = DMEtcdDB.get_instance(self.etcd_args, vnc_lib=self.vnc_lib)
        dm_etcd.delete_pnf_resources = mock.MagicMock()
        dm_etcd.pnf_resources_map = {
            'si-blue-uuid': self.default_pnf,
            'si-red-uuid': self.default_pnf,
        }
        dm_etcd.handle_pnf_resource_deletes(['si-blue-uuid'])
        dm_etcd.delete_pnf_resources.assert_called_with('si-red-uuid')
