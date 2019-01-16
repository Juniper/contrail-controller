#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
import unittest

import etcd3
import mock

from cfgm_common.vnc_etcd import VncEtcd
from cfgm_common.exceptions import NoIdError
from schema_transformer.etcd import SchemaTransformerEtcd
from schema_transformer.to_bgp import parse_args
from vnc_api.exceptions import RefsExistError

ETCD_HOST = 'etcd-host-01'


def _schema_transformer_etcd_factory(host=ETCD_HOST, vnc_lib=mock.MagicMock(),
                                     logger=mock.MagicMock(), log_response_time=None,
                                     credentials=None):
    """SchemaTransformerEtcd factory function for testing only."""
    args = parse_args('')
    args.etcd_server = ETCD_HOST

    return SchemaTransformerEtcd(args, vnc_lib, logger)


class TestSchemaTransformerEtcd(unittest.TestCase):

    def test_create_schema_transformer_etcd_instance(self):
        schema_transformer_etcd = _schema_transformer_etcd_factory()
        self.assertIsInstance(
            schema_transformer_etcd._object_db, VncEtcd)
        self.assertIsInstance(
            schema_transformer_etcd._object_db._client, etcd3.Etcd3Client)
        self.assertEqual(schema_transformer_etcd._object_db._host, ETCD_HOST)
        self.assertEqual(schema_transformer_etcd._object_db._port, '2379')
        self.assertIsNone(schema_transformer_etcd._object_db._credentials)

    def test_empty_list_service_chain_uuid(self):
        kv_data = []
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        schema_transformer_etcd._object_db._client.get_prefix = mock.MagicMock()
        schema_transformer_etcd._object_db._client.get_prefix.return_value = kv_data
        self.assertEqual(
            sum(1 for i in schema_transformer_etcd.list_service_chain_uuid()), 0)

    @mock.patch('etcd3.client')
    def test_list_service_chain_uuid_len(self, etcd_client):
        kv_data = [('/contrail/schema_transformer/service_chain/k1', 'value1'),
                   ('/contrail/schema_transformer/service_chain/k2', 'value2'),
                   ('/contrail/schema_transformer/service_chain/k3', 'value3'),
                   ]
        schema_transformer_etcd = _schema_transformer_etcd_factory()
        schema_transformer_etcd._object_db._client.get_prefix = mock.MagicMock()
        schema_transformer_etcd._object_db._client.get_prefix.return_value = kv_data

        self.assertEqual(
            sum(1 for i in schema_transformer_etcd.list_service_chain_uuid()), 3)

    @mock.patch('etcd3.client')
    def test_list_service_chain_uuid_elements_encapsulated(self, etcd_client):
        kv_data = [('/contrail/schema_transformer/service_chain/k1', 'value1'),
                   ('/contrail/schema_transformer/service_chain/k2', 'value2'),
                   ('/contrail/schema_transformer/service_chain/k3', 'value3'),
                   ]

        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        schema_transformer_etcd._object_db._client.get_prefix = mock.MagicMock()
        schema_transformer_etcd._object_db._client.get_prefix.return_value = kv_data
        schema_transformer_etcd._object_db._client.put = mock.MagicMock

        for element in schema_transformer_etcd.list_service_chain_uuid():
            self.assertEqual(len(element), 1)
            self.assertIn('value', element)
            self.assertIsInstance(element['value'], str)

    @mock.patch('etcd3.client')
    def test_add_service_chain_uuid(self, etcd_client):
        kv_data = [('/contrail/schema_transformer/service_chain/k1', 'value1'),
                   ('/contrail/schema_transformer/service_chain/k2', 'value2'),
                   ('/contrail/schema_transformer/service_chain/k3', 'value3'),
                   ]

        def etcd3_put(key, value):
            kv_data.append((key, value))

        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        schema_transformer_etcd._object_db._client.put = mock.MagicMock(
            side_effect=etcd3_put)

        schema_transformer_etcd.add_service_chain_uuid('k4', 'value4')
        self.assertEqual(len(kv_data), 4)
        k4_value = None
        for (k, v) in kv_data:
            self.assertEqual(k.startswith(
                '/contrail/schema_transformer/service_chain/k'), True)
            if k == '/contrail/schema_transformer/service_chain/k4':
                k4_value = v
        self.assertEqual(k4_value, 'value4')

    @mock.patch('etcd3.client')
    def test_remove_service_chain_uuid(self, etcd_client):
        kv_data = [('/contrail/schema_transformer/service_chain/k1', 'value1'),
                   ('/contrail/schema_transformer/service_chain/k2', 'value2'),
                   ('/contrail/schema_transformer/service_chain/k3', 'value3'),
                   ]
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())

        def etcd3_delete(key):
            i = -1
            prefixed_key = schema_transformer_etcd._path_key(
                schema_transformer_etcd._ETCD_SERVICE_CHAIN_PATH, key)
            for (k, _) in kv_data:
                i += 1
                if k == prefixed_key:
                    break
            if i > -1:
                del kv_data[i]

        schema_transformer_etcd._object_db._client.delete = mock.MagicMock(
            side_effect=etcd3_delete)

        schema_transformer_etcd.remove_service_chain_uuid('k3')
        self.assertEqual(len(kv_data), 2)
        k3_value = None
        for (k, v) in kv_data:
            if k == '/contrail/schema_transformer/service_chain/k3':
                k3_value = v
        self.assertIsNone(k3_value)


class TestAllocServiceChainVlanSTEtcd(unittest.TestCase):
    def setUp(self):
        super(TestAllocServiceChainVlanSTEtcd, self).setUp()
        # prepare schema transformer with default mocks
        self.st = _schema_transformer_etcd_factory(logger=mock.MagicMock())
        self.st._vnc_lib.create_int_pool = mock.MagicMock(return_value=None)
        self.st._vnc_lib.allocate_int = mock.MagicMock(return_value=None)
        self.st._vnc_lib.deallocate_int = mock.MagicMock(return_value=None)
        self.st._object_db.get_kv = mock.MagicMock(return_value=None)
        self.st._object_db.put_kv = mock.MagicMock(return_value=None)
        self.st._object_db.delete_kv = mock.MagicMock(return_value=None)

        # input data
        self.service_vm = "service_vm-uuid"
        self.service_chain = "firewall"

        # expected data
        self.expected_int = 1
        self.int_pool = "schema_transformer/vlan/service_vm-uuid"
        self.alloc_kv = "schema_transformer/vlan/service_vm-uuid:firewall"
        self.min_vlan = self.st._SC_MIN_VLAN
        self.max_vlan = self.st._SC_MAX_VLAN

    @mock.patch('etcd3.client')
    def test_alloc_sv_vlan_should_return_new_int(self, etcd_client):
        expected_int = 1
        self.st._vnc_lib.allocate_int.return_value = expected_int

        # invoke and check result
        result = self.st.allocate_service_chain_vlan(self.service_vm,
                                                     self.service_chain)
        self.assertEqual(result, self.expected_int)

        # check if mocked methods are called with expected arguments
        self.st._vnc_lib.create_int_pool.assert_called_with(self.int_pool,
                                                            self.min_vlan,
                                                            self.max_vlan)
        self.st._object_db.get_kv.assert_called_with(self.alloc_kv)
        self.st._vnc_lib.allocate_int.assert_called_with(self.int_pool)
        self.st._object_db.put_kv.assert_called_with(self.alloc_kv,
                                                     self.expected_int)

    @mock.patch('etcd3.client')
    def test_alloc_exist_sv_vlan_should_return_old_int(self, etcd_client):
        self.st._vlan_allocators[self.service_vm] = self.int_pool
        self.st._object_db.get_kv.return_value = self.expected_int

        # invoke and check result
        result = self.st.allocate_service_chain_vlan(self.service_vm,
                                                     self.service_chain)
        self.assertEqual(result, self.expected_int)

        # check if mocked methods are called with expected arguments
        self.st._vnc_lib.create_int_pool.assert_not_called()
        self.st._object_db.get_kv.assert_called_with(self.alloc_kv)
        self.st._vnc_lib.allocate_int.assert_not_called()
        self.st._object_db.put_kv.assert_not_called()

    @mock.patch('etcd3.client')
    def test_alloc_exist_not_in_allocdict_should_return_old_int(self, etcd_client):
        self.st._vnc_lib.create_int_pool.side_effect = RefsExistError()
        self.st._object_db.get_kv.return_value = self.expected_int

        # invoke and check result
        result = self.st.allocate_service_chain_vlan(self.service_vm,
                                                     self.service_chain)
        self.assertEqual(result, self.expected_int)

        # check if mocked methods are called with expected arguments
        self.st._vnc_lib.create_int_pool.assert_called_with(self.int_pool,
                                                            self.min_vlan,
                                                            self.max_vlan)
        self.st._object_db.get_kv.assert_called_with(self.alloc_kv)
        self.st._vnc_lib.allocate_int.assert_not_called()
        self.st._object_db.put_kv.assert_not_called()

    @mock.patch('etcd3.client')
    def test_dealloc_sv_vlan(self, etcd_client):
        self.st._vlan_allocators[self.service_vm] = self.int_pool
        self.st._object_db.get_kv.return_value = self.expected_int

        self.st.free_service_chain_vlan(self.service_vm, self.service_chain)
        self.assertNotIn(self.service_vm, self.st._vlan_allocators)

        self.st._object_db.get_kv.assert_called_with(self.alloc_kv)
        self.st._vnc_lib.deallocate_int.assert_called_with(self.int_pool,
                                                           self.expected_int)
        self.st._object_db.delete_kv.assert_called_with(self.alloc_kv)

    @mock.patch('etcd3.client')
    def test_dealloc_not_existing_sv_vlan(self, etcd_client):
        self.st._object_db.get_kv.side_effect = NoIdError(self.expected_int)

        self.st.free_service_chain_vlan(self.service_vm, self.service_chain)

        self.st._object_db.get_kv.assert_called_with(self.alloc_kv)
        self.st._vnc_lib.deallocate_int.assert_not_called()
        self.st._object_db.delete_kv.assert_not_called()

    @mock.patch('etcd3.client')
    def test_dealloc_sv_vlan_which_is_not_in_allocdict(self, etcd_client):
        self.st._object_db.get_kv.return_value = self.expected_int

        self.st.free_service_chain_vlan(self.service_vm, self.service_chain)

        self.st._object_db.get_kv.assert_called_with(self.alloc_kv)
        self.st._vnc_lib.deallocate_int.assert_called_with(self.int_pool,
                                                           self.expected_int)
        self.st._object_db.delete_kv.assert_called_with(self.alloc_kv)
