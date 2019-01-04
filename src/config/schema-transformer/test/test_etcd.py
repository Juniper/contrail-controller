#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

import unittest

# import sys
import etcd3
import mock

# from cfgm_common.db_etcd import SchemaTransformerEtcd
from etcd import EtcdClient, SchemaTransformerEtcd

ETCD_HOST = 'etcd-host-01'


def _schema_transformer_etcd_factory(host=ETCD_HOST, port='2379', prefix=None,
                                     logger=None, log_response_time=None, credentials=None):
    """SchemaTransformerEtcd factory function for testing only."""
    return SchemaTransformerEtcd(host, port, prefix, logger=logger, log_response_time=log_response_time, credentials=credentials)


class SchemaTransformerEtcdTest(unittest.TestCase):

    def test_create_schema_transformer_etcd_instance(self):
        schema_transformer_etcd = _schema_transformer_etcd_factory()
        self.assertIsInstance(schema_transformer_etcd._client, EtcdClient)
        self.assertIsInstance(
            schema_transformer_etcd._client, etcd3.Etcd3Client)
        self.assertEqual(schema_transformer_etcd._client._host, ETCD_HOST)
        self.assertEqual(schema_transformer_etcd._client._port, '2379')
        self.assertIsNone(schema_transformer_etcd._client._credentials)

    def test_key_prefix(self):
        schema_transformer_etcd = _schema_transformer_etcd_factory()
        result = schema_transformer_etcd._key_prefix(obj_type='test_object')
        expected = '/contrail/schema_transformer/test_object/'
        self.assertEqual(result, expected)

    def test_key_obj(self):
        schema_transformer_etcd = _schema_transformer_etcd_factory()
        result = schema_transformer_etcd._key_obj(
            obj_type='test_object', obj_id='qwerty')
        expected = '/contrail/schema_transformer/test_object/qwerty'
        self.assertEqual(result, expected)

    def test_empty_list_service_chain_uuid(self):
        schema_transformer_etcd = _schema_transformer_etcd_factory()
        self.assertEqual(
            sum(1 for i in schema_transformer_etcd.list_service_chain_uuid()), 0)

    @mock.patch('etcd3.client')
    def test_list_service_chain_uuid_len(self, etcd_client):
        kv_data = [('/contrail/schema_transformer/service_chain/k1', 'value1'),
                   ('/contrail/schema_transformer/service_chain/k2', 'value2'),
                   ('/contrail/schema_transformer/service_chain/k3', 'value3'),
                   ]
        schema_transformer_etcd = _schema_transformer_etcd_factory()
        schema_transformer_etcd._client.get_prefix = mock.MagicMock()
        schema_transformer_etcd._client.get_prefix.return_value = kv_data

        self.assertEqual(
            sum(1 for i in schema_transformer_etcd.list_service_chain_uuid()), 3)

    @mock.patch('etcd3.client')
    def test_list_service_chain_uuid_elements_encapsulated(self, etcd_client):
        kv_data = [('/contrail/schema_transformer/service_chain/k1', 'value1'),
                   ('/contrail/schema_transformer/service_chain/k2', 'value2'),
                   ('/contrail/schema_transformer/service_chain/k3', 'value3'),
                   ]

        schema_transformer_etcd = _schema_transformer_etcd_factory()
        schema_transformer_etcd._client.get_prefix = mock.MagicMock()
        schema_transformer_etcd._client.get_prefix.return_value = kv_data
        schema_transformer_etcd._client.put = mock.MagicMock

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

        schema_transformer_etcd = _schema_transformer_etcd_factory()
        schema_transformer_etcd._client.put = mock.MagicMock(
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
        schema_transformer_etcd = _schema_transformer_etcd_factory()

        def etcd3_delete(key):
            i = 0
            for (k, _) in kv_data:
                if k == key:
                    break
                i += 1
            del kv_data[i]
        
        schema_transformer_etcd._client.delete = mock.MagicMock(
            side_effect=etcd3_delete)

        schema_transformer_etcd.remove_service_chain_uuid('k3')
        self.assertEqual(len(kv_data), 2)
        k3_value = None
        for (k, v) in kv_data:
            if k == '/contrail/schema_transformer/service_chain/k3':
                k3_value = v
        self.assertIsNone(k3_value)
