#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
import unittest
from collections import namedtuple

import etcd3
import mock

from cfgm_common.vnc_etcd import VncEtcd
from schema_transformer.etcd import SchemaTransformerEtcd

ETCD_HOST = 'etcd-host-01'


def _schema_transformer_etcd_factory(host=ETCD_HOST, port='2379', prefix='/contrail',
                                     logger=None, log_response_time=None, credentials=None):
    """SchemaTransformerEtcd factory function for testing only."""

    Args = namedtuple("Args", ['etcd_server', 'etcd_port',
                               'etcd_prefix', 'etcd_user', 'etcd_password'])
    return SchemaTransformerEtcd(Args(host, port, prefix, "", ""), logger)


class MockedMeta:
    def __init__(self, key):
        self.key = key


class TestSchemaTransformerEtcd(unittest.TestCase):

    def test_create_schema_transformer_etcd_instance(self):
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        self.assertIsInstance(
            schema_transformer_etcd._object_db, VncEtcd)
        self.assertIsInstance(
            schema_transformer_etcd._object_db._client, etcd3.Etcd3Client)
        self.assertEqual(schema_transformer_etcd._object_db._host, ETCD_HOST)
        self.assertEqual(schema_transformer_etcd._object_db._port, '2379')
        self.assertIsNone(schema_transformer_etcd._object_db._credentials)

    def test_empty_get_service_chain_ip(self):
        kv_data = []
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        schema_transformer_etcd._object_db._client.get_prefix = mock.MagicMock()
        schema_transformer_etcd._object_db._client.get_prefix.return_value = kv_data
        self.assertEqual(
            sum(1 for i in schema_transformer_etcd.get_service_chain_ip('test1')), 0)

    @mock.patch('etcd3.client')
    def test_get_service_chain_ip_len(self, etcd_client):
        kv_data = [('127.0.0.1',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ip_address')),
                   ('5f5a8a57-27f1-4e9b-8d11-0bbc4fb01d3e',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ip_uuid')),
                   ('::1',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ipv6_address')),
                   ('dcf3468d-3d30-4d04-a21c-e58f3b9d767a',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ipv6_uuid')),
                   ]
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        schema_transformer_etcd._object_db._client.get_prefix = mock.MagicMock()
        schema_transformer_etcd._object_db._client.get_prefix.return_value = kv_data

        result = schema_transformer_etcd.get_service_chain_ip('test1')
        self.assertEqual(
            sum(1 for i in schema_transformer_etcd.get_service_chain_ip('test1')), 4)

    @mock.patch('etcd3.client')
    def test_add_service_chain_ip(self, etcd_client):
        kv_data = [('127.0.0.1',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ip_address')),
                   ('5f5a8a57-27f1-4e9b-8d11-0bbc4fb01d3e',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ip_uuid')),
                   ('::1',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ipv6_address')),
                   ('dcf3468d-3d30-4d04-a21c-e58f3b9d767a',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ipv6_uuid')),
                   ]

        def etcd3_put(key, value):
            kv_data.append((value, MockedMeta(key)))

        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        schema_transformer_etcd._object_db._client.put = mock.MagicMock(
            side_effect=etcd3_put)

        schema_transformer_etcd.add_service_chain_ip(
            'test2', {'ip_address': '127.0.0.1', 'ip_uuid': '4d21947f-b60d-4faf-b5ed-d867fc0b5d1d'})
        self.assertEqual(len(kv_data), 6)
        test2_value = None
        for (v, k) in kv_data:
            self.assertEqual(k.key.startswith(
                '/contrail/schema_transformer/service_chain_ip/test'), True)
            if k.key == '/contrail/schema_transformer/service_chain_ip/test2/ip_uuid':
                test2_value = v
        self.assertEqual(test2_value, '4d21947f-b60d-4faf-b5ed-d867fc0b5d1d')

    @mock.patch('etcd3.client')
    def test_remove_service_chain_ip(self, etcd_client):
        kv_data = [('127.0.0.1',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ip_address')),
                   ('5f5a8a57-27f1-4e9b-8d11-0bbc4fb01d3e',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ip_uuid')),
                   ('::1',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ipv6_address')),
                   ('dcf3468d-3d30-4d04-a21c-e58f3b9d767a',
                    MockedMeta('/contrail/schema_transformer/service_chain_ip/test1/ipv6_uuid')),
                   ]
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())

        def etcd3_delete_prefix(key):
            i = 0
            d = []
            for (_, k) in kv_data:
                if k.key.startswith(key):
                    d.append(i)
                i += 1
            if d and len(d) > 0:
                d.reverse()
                for i in d:
                    del kv_data[i]

        schema_transformer_etcd._object_db._client.delete_prefix = mock.MagicMock(
            side_effect=etcd3_delete_prefix)

        schema_transformer_etcd.remove_service_chain_ip('test2')
        self.assertEqual(len(kv_data), 4)
        schema_transformer_etcd.remove_service_chain_ip('test1')
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
        kv_data = [('value1', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k1')),
                   ('value2', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k2')),
                   ('value3', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k3')),
                   ]
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())
        schema_transformer_etcd._object_db._client.get_prefix = mock.MagicMock()
        schema_transformer_etcd._object_db._client.get_prefix.return_value = kv_data

        self.assertEqual(
            sum(1 for i in schema_transformer_etcd.list_service_chain_uuid()), 3)

    @mock.patch('etcd3.client')
    def test_list_service_chain_uuid_elements_encapsulated(self, etcd_client):
        kv_data = [('value1', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k1')),
                   ('value2', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k2')),
                   ('value3', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k3')),
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
        kv_data = [('value1', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k1')),
                   ('value2', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k2')),
                   ('value3', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k3')),
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
                '/contrail/schema_transformer/service_chain_uuid/k'), True)
            if k.key == '/contrail/schema_transformer/service_chain_uuid/k4':
                k4_value = v
        self.assertEqual(k4_value, 'value4')

    @mock.patch('etcd3.client')
    def test_remove_service_chain_uuid(self, etcd_client):
        kv_data = [('value1', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k1')),
                   ('value2', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k2')),
                   ('value3', MockedMeta('/contrail/schema_transformer/service_chain_uuid/k3')),
                   ]
        schema_transformer_etcd = _schema_transformer_etcd_factory(
            logger=mock.MagicMock())

        def etcd3_delete(key):
            i = -1
            prefixed_key = schema_transformer_etcd._path_key(
                'service_chain_uuid', key)
            for (_, k) in kv_data:
                i += 1
                if k.key == prefixed_key:
                    break
            if i > -1:
                del kv_data[i]

        schema_transformer_etcd._object_db._client.delete_prefix = mock.MagicMock(
            side_effect=etcd3_delete)

        schema_transformer_etcd.remove_service_chain_uuid('k3')
        self.assertEqual(len(kv_data), 2)
        k3_value = None
        for (v, k) in kv_data:
            if k.key == '/contrail/schema_transformer/service_chain_uuid/k3':
                k3_value = v
        self.assertIsNone(k3_value)
