#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
import unittest
from collections import namedtuple

import etcd3
import mock
import jsonpickle

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


class MockedMeta:
    def __init__(self, key):
        self.key = key


class TestSchemaTransformerEtcd(unittest.TestCase):
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

    def test_create_schema_transformer_etcd_instance(self):
        schema_transformer_etcd = _schema_transformer_etcd_factory()
        self.assertIsInstance(
            schema_transformer_etcd._object_db, VncEtcd)
        self.assertIsInstance(
            schema_transformer_etcd._object_db._client, etcd3.Etcd3Client)
        self.assertEqual(schema_transformer_etcd._object_db._host, ETCD_HOST)
        self.assertEqual(schema_transformer_etcd._object_db._port, 2379)
        self.assertIsNone(schema_transformer_etcd._object_db._credentials)

    def test_empty_get_service_chain_ip(self):
        # kv_data = [None, MockedMeta('/vnc/schema_transformer/service_chain_ip/test1')]
        kv_data = [(None, None)]
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        schema_transformer_etcd._object_db._client.get = mock.MagicMock()
        schema_transformer_etcd._object_db._client.get.return_value = kv_data[0]
        self.assertEqual(
            sum(1 for i in schema_transformer_etcd.get_service_chain_ip('test1')), 0)

    @mock.patch('etcd3.client')
    def test_get_service_chain_ip_len(self, etcd_client):
        kv_data = [('{"ip_uuid": "5f5a8a57-27f1-4e9b-8d11-0bbc4fb01d3e", "ipv6_address": "::1", "ip_address": "127.0.0.1", "ipv6_uuid": "dcf3468d-3d30-4d04-a21c-e58f3b9d767a"}',
                    MockedMeta('/vnc/schema_transformer/service_chain_ip/test1')),
                   ('{"ip_uuid": "69ab63ca-7eeb-41d5-a7c5-7762466fd445", "ipv6_address": "2001:1a68:10:1200:215a:fb68:2705:3677", "ip_address": "172.17.16.82", "ipv6_uuid": "508f8294-b2ef-4c13-aaea-8c1e642b0831"}',
                    MockedMeta('/vnc/schema_transformer/service_chain_ip/test3')),
                   ]
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        schema_transformer_etcd._object_db._client.get = mock.MagicMock()
        schema_transformer_etcd._object_db._client.get.return_value = kv_data[0]

        self.assertEqual(
            sum(1 for i in schema_transformer_etcd.get_service_chain_ip('test1')), 4)

    @mock.patch('etcd3.client')
    def test_add_service_chain_ip(self, etcd_client):
        kv_data = [('{"ip_uuid": "5f5a8a57-27f1-4e9b-8d11-0bbc4fb01d3e", "ipv6_address": "::1", "ip_address": "127.0.0.1", "ipv6_uuid": "dcf3468d-3d30-4d04-a21c-e58f3b9d767a"}',
                    MockedMeta('/vnc/schema_transformer/service_chain_ip/test1')),
                   ('{"ip_uuid": "69ab63ca-7eeb-41d5-a7c5-7762466fd445", "ipv6_address": "2001:1a68:10:1200:215a:fb68:2705:3677", "ip_address": "172.17.16.82", "ipv6_uuid": "508f8294-b2ef-4c13-aaea-8c1e642b0831"}',
                    MockedMeta('/vnc/schema_transformer/service_chain_ip/test3')),
                   ]

        def etcd3_put(key, value):
            kv_data.append((value, MockedMeta(key)))
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        schema_transformer_etcd._object_db._client.put = mock.MagicMock(
            side_effect=etcd3_put)

        schema_transformer_etcd.add_service_chain_ip(
            'test2', {'ip_address': '127.0.0.1', 'ip_uuid': '4d21947f-b60d-4faf-b5ed-d867fc0b5d1d'})
        self.assertEqual(len(kv_data), 3)
        test2_value = None
        for (v, k) in kv_data:
            self.assertEqual(k.key.startswith(
                '/vnc/schema_transformer/service_chain_ip/test'), True)
            if k.key == '/vnc/schema_transformer/service_chain_ip/test2':
                test2_value = v
        self.assertEqual(
            test2_value,
            '{"ip_uuid": "4d21947f-b60d-4faf-b5ed-d867fc0b5d1d", "ip_address": "127.0.0.1"}')

    @mock.patch('etcd3.client')
    def test_remove_service_chain_ip(self, etcd_client):
        kv_data = [('{"ip_uuid": "5f5a8a57-27f1-4e9b-8d11-0bbc4fb01d3e", "ipv6_address": "::1", "ip_address": "127.0.0.1", "ipv6_uuid": "dcf3468d-3d30-4d04-a21c-e58f3b9d767a"}',
                    MockedMeta('/vnc/schema_transformer/service_chain_ip/test1')),
                   ('{"ip_uuid": "69ab63ca-7eeb-41d5-a7c5-7762466fd445", "ipv6_address": "2001:1a68:10:1200:215a:fb68:2705:3677", "ip_address": "172.17.16.82", "ipv6_uuid": "508f8294-b2ef-4c13-aaea-8c1e642b0831"}',
                    MockedMeta('/vnc/schema_transformer/service_chain_ip/test3')),
                   ]
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())

        def etcd3_delete(key):
            i, d = 0, []
            for (_, k) in kv_data:
                if k.key.endswith(key):
                    d = [i] + d
                i += 1
            if d and len(d) > 0:
                for i in d:
                    del kv_data[i]

        schema_transformer_etcd._object_db._client.delete = mock.MagicMock(
            side_effect=etcd3_delete)

        schema_transformer_etcd.remove_service_chain_ip('test1')
        self.assertEqual(len(kv_data), 1)
        schema_transformer_etcd.remove_service_chain_ip('test3')
        self.assertEqual(len(kv_data), 0)

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
        kv_data = [('value1',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k1')),
                   ('value2',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k2')),
                   ('value3',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k3')),
                   ]
        schema_transformer_etcd = _schema_transformer_etcd_factory()
        schema_transformer_etcd._object_db._client.get_prefix = mock.MagicMock()
        schema_transformer_etcd._object_db._client.get_prefix.return_value = kv_data

        self.assertEqual(
            sum(1 for i in schema_transformer_etcd.list_service_chain_uuid()), 3)

    @mock.patch('etcd3.client')
    def test_list_service_chain_uuid_elements_encapsulated(self, etcd_client):
        kv_data = [('value1',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k1')),
                   ('value2',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k2')),
                   ('value3',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k3')),
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
        kv_data = [('value1',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k1')),
                   ('value2',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k2')),
                   ('value3',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k3')),
                   ]

        def etcd3_put(key, value):
            kv_data.append((value, MockedMeta(key)))

        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        schema_transformer_etcd._object_db._client.put = mock.MagicMock(
            side_effect=etcd3_put)

        schema_transformer_etcd.add_service_chain_uuid('k4', 'value4')
        self.assertEqual(len(kv_data), 4)
        k4_value = None
        for (v, k) in kv_data:
            self.assertEqual(k.key.startswith(
                '/vnc/schema_transformer/service_chain_uuid/k'), True)
            if k.key == '/vnc/schema_transformer/service_chain_uuid/k4':
                k4_value = v
        self.assertEqual(k4_value, 'value4')

    @mock.patch('etcd3.client')
    def test_remove_service_chain_uuid(self, etcd_client):
        kv_data = [('value1',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k1')),
                   ('value2',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k2')),
                   ('value3',
                    MockedMeta('/vnc/schema_transformer/service_chain_uuid/k3')),
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
        for (v, k) in kv_data:
            if k.key == '/vnc/schema_transformer/service_chain_uuid/k3':
                k3_value = v
        self.assertIsNone(k3_value)


class TestSchemaTransformerEtcdRouteTarget(unittest.TestCase):
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

    def setUp(self, extra_config_knobs=None):
        etcd3 = TestSchemaTransformerEtcdRouteTarget.ETCD3Mock()
        etcd3.put('/vnc/schema_transformer/route_target/test_a', 8000100)
        etcd3.put('/vnc/schema_transformer/route_target/test_b', 8000101)
        etcd3.put('/vnc/schema_transformer/route_target/test_c', 8000102)
        self._etcd3 = etcd3
        self._schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        self._schema_transformer_etcd._object_db._client = self._etcd3

    def test_alloc_route_target(self):
        self._schema_transformer_etcd._vnc_lib.allocate_int = mock.MagicMock()
        self._schema_transformer_etcd._vnc_lib.allocate_int.return_value = 8000103

        # Allocate a new route target
        rt_num = self._schema_transformer_etcd.alloc_route_target('test_d')
        self.assertEqual(rt_num, 8000103)
        self.assertEqual(len(self._etcd3._kv_data), 4)
        self.assertEqual(self._etcd3.get(
            '/vnc/schema_transformer/route_target/test_d')[0], '8000103')

        # Try to allocate an existing route target
        self._schema_transformer_etcd._vnc_lib.allocate_int.return_value = 8000104
        rt_num = self._schema_transformer_etcd.alloc_route_target('test_b')
        self.assertEqual(rt_num, 8000101)

        # Try to allocate an existing route target using alloc_only
        self._schema_transformer_etcd._vnc_lib.allocate_int.return_value = 8000104
        rt_num = self._schema_transformer_etcd.alloc_route_target(
            'test_c', True)
        self.assertEqual(rt_num, 8000104)
        self.assertEqual(len(self._etcd3._kv_data), 4)

    def test_get_route_target(self):
        # Read existing value
        rt_num = self._schema_transformer_etcd.get_route_target('test_b')
        self.assertEqual(rt_num, 8000101)

        # Read non-existing value
        rt_num = self._schema_transformer_etcd.get_route_target('test_d')
        self.assertEqual(rt_num, 0)

    def test_get_route_target_range(self):
        self._etcd3.put('/vnc/svc_monitor/route_target/test_c', 8000103)

        # Read existing value
        rt_dict = dict(self._schema_transformer_etcd.get_route_target_range())
        self.assertEqual(sum(1 for i in rt_dict), 3)
        self.assertEqual(rt_dict['test_a'], {'rtgt_num': 8000100})
        self.assertEqual(rt_dict['test_b'], {'rtgt_num': 8000101})
        self.assertEqual(rt_dict['test_c'], {'rtgt_num': 8000102})

    def test_free_route_target(self):
        # Free route target
        self._schema_transformer_etcd.free_route_target("test_c")
        self.assertEqual(len(self._etcd3._kv_data), 2)


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
        self.st._vnc_lib.allocate_int.return_value = self.expected_int

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
