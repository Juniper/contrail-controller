# -*- coding: utf-8 -*-
import json
import unittest

import etcd3
import mock
from vnc_etcd import EtcdCache, VncEtcd, NoIdError


class VncEtcdTest(unittest.TestCase):
    def test_create_etcd_client_instance(self):
        vnc_etcd = VncEtcd(host='etcd-host-01', port='2379',
                           prefix='/contrail', logger=None,
                           obj_cache_exclude_types=['tag_type'],
                           credential=None, ssl_enabled=False,
                           ca_certs=None)
        self.assertEqual(vnc_etcd._host, 'etcd-host-01')
        self.assertEqual(vnc_etcd._port, '2379')
        self.assertEqual(vnc_etcd._prefix, '/contrail')
        self.assertIsNone(vnc_etcd._logger)
        self.assertIsNone(vnc_etcd._credential)
        self.assertFalse(vnc_etcd._ssl_enabled)
        self.assertIsNone(vnc_etcd._ca_certs)
        self.assertIsInstance(vnc_etcd._client, etcd3.Etcd3Client)
        self.assertIsInstance(vnc_etcd._obj_cache, EtcdCache)

    def test_key_prefix(self):
        vnc_etcd = VncEtcd(host='etcd-host-01', port='2379',
                           prefix='/contrail', logger=None,
                           obj_cache_exclude_types=[],
                           credential=None, ssl_enabled=False,
                           ca_certs=None)
        result = vnc_etcd._key_prefix(obj_type='test_object')
        expected = '/contrail/test_object'
        self.assertEqual(result, expected)

    def test_key_obj(self):
        vnc_etcd = VncEtcd(host='etcd-host-01', port='2379',
                           prefix='/contrail', logger=None,
                           obj_cache_exclude_types=[],
                           credential=None, ssl_enabled=False,
                           ca_certs=None)
        result = vnc_etcd._key_obj(obj_type='test_object', obj_id='qwerty')
        expected = '/contrail/test_object/qwerty'
        self.assertEqual(result, expected)

    @mock.patch('etcd3.client')
    def test_object_read(self, etcd_client):
        example_resource = str('{"uuid":"ba3442c8a3ec",'
                               '"parent_uuid":"beefbeef0003",'
                               '"parent_type":"project"}')

        vnc_etcd = VncEtcd(host='etcd-host-01', port='2379',
                           prefix='/contrail', logger=None,
                           obj_cache_exclude_types=None,
                           credential=None, ssl_enabled=False,
                           ca_certs=None)

        etcd_client.get = mock.MagicMock()
        etcd_client.get.return_value = (example_resource, None)
        vnc_etcd._client = etcd_client
        ok, results = vnc_etcd.object_read('virtual_network', ['ba3442c8a3ec'])
        self.assertTrue(ok)
        self.assertDictEqual(results[0], json.loads(example_resource))

        key = '/contrail/virtual_network/ba3442c8a3ec'
        self.assertIn(key, vnc_etcd._obj_cache)

    @mock.patch('etcd3.client')
    def test_object_read_field_filtered(self, etcd_client):
        example_resource = str('{"uuid":"ba3442c8a3ec",'
                               '"parent_uuid":"beefbeef0003",'
                               '"parent_type":"project"}')

        vnc_etcd = VncEtcd(host='etcd-host-01', port='2379',
                           prefix='/contrail', logger=None,
                           obj_cache_exclude_types=None,
                           credential=None, ssl_enabled=False,
                           ca_certs=None)

        etcd_client.get = mock.MagicMock()
        etcd_client.get.return_value = (example_resource, None)
        vnc_etcd._client = etcd_client
        ok, results = vnc_etcd.object_read('virtual_network', ['ba3442c8a3ec'],
                                           field_names=['uuid', 'parent_type'])
        self.assertTrue(ok)
        self.assertDictEqual(results[0], {'uuid': 'ba3442c8a3ec',
                                          'parent_type': 'project'})

    @mock.patch('etcd3.client')
    def test_object_read_raise_no_id_error(self, etcd_client):
        vnc_etcd = VncEtcd(host='etcd-host-01', port='2379',
                           prefix='/contrail', logger=None,
                           obj_cache_exclude_types=None,
                           credential=None, ssl_enabled=False,
                           ca_certs=None)
        etcd_client.get = mock.MagicMock()
        etcd_client.get.return_value = (None, None)
        vnc_etcd._client = etcd_client
        self.assertRaises(NoIdError, vnc_etcd.object_read,
                          'virtual_network', ['ba3442c8a3ec'])

    @mock.patch('etcd3.client')
    def test_object_read_return_empty_result(self, etcd_client):
        vnc_etcd = VncEtcd(host='etcd-host-01', port='2379',
                           prefix='/contrail', logger=None,
                           obj_cache_exclude_types=None,
                           credential=None, ssl_enabled=False,
                           ca_certs=None)
        etcd_client.get = mock.MagicMock()
        etcd_client.get.return_value = (None, None)
        vnc_etcd._client = etcd_client
        ok, results = vnc_etcd.object_read('virtual_network', obj_uuids=[])
        self.assertTrue(ok)
        self.assertListEqual(results, [])

    @mock.patch('etcd3.client')
    def test_object_read_return_from_cache(self, etcd_client):
        example_resource = str('{"uuid":"ba3442c8a3ec",'
                               '"parent_uuid":"beefbeef0003",'
                               '"parent_type":"project"}')

        vnc_etcd = VncEtcd(host='etcd-host-01', port='2379',
                           prefix='/contrail', logger=None,
                           obj_cache_exclude_types=None,
                           credential=None, ssl_enabled=False,
                           ca_certs=None)
        etcd_client.get = mock.MagicMock()
        etcd_client.get.return_value = (example_resource, None)
        vnc_etcd._client = etcd_client

        # first read should be from etcd_client.get and store to cache
        vnc_etcd.object_read('virtual_network',
                             obj_uuids=['ba3442c8a3ec'], ret_readonly=True)
        # should read from cache
        vnc_etcd.object_read('virtual_network',
                             obj_uuids=['ba3442c8a3ec'], ret_readonly=True)

        # client get should be called only once
        etcd_client.get.assert_called_once()

    @mock.patch('etcd3.client')
    def test_object_all_should_return_resources(self, etcd_client):
        example_resource = [('{"uuid":"aa3442c8a3dd"}', None),
                            ('{"uuid":"bb3442c8a3ee"}', None),
                            ('{"uuid":"cc3442c8a3ff"}', None)]

        vnc_etcd = VncEtcd(host='etcd-host-01', port='2379',
                           prefix='/contrail', logger=None,
                           obj_cache_exclude_types=None,
                           credential=None, ssl_enabled=False,
                           ca_certs=None)
        etcd_client.get_prefix = mock.MagicMock()
        etcd_client.get_prefix.return_value = example_resource
        vnc_etcd._client = etcd_client

        result = vnc_etcd.object_all('test_resource')
        expected = [json.loads(res) for res, kv in example_resource]
        self.assertListEqual(result, expected)

    @mock.patch('etcd3.client')
    def test_object_all_shoud_return_from_cache(self, etcd_client):
        example_resource = [('{"uuid":"aa3442c8a3dd"}', None),
                            ('{"uuid":"bb3442c8a3ee"}', None),
                            ('{"uuid":"cc3442c8a3ff"}', None)]

        vnc_etcd = VncEtcd(host='etcd-host-01', port='2379',
                           prefix='/contrail', logger=None,
                           obj_cache_exclude_types=None,
                           credential=None, ssl_enabled=False,
                           ca_certs=None)
        etcd_client.get_prefix = mock.MagicMock()
        etcd_client.get_prefix.return_value = example_resource
        vnc_etcd._client = etcd_client

        # first read should be from etcd_client.get and store to cache
        vnc_etcd.object_all('test_resource')
        # should read from cache
        vnc_etcd.object_all('test_resource')

        # client get should be called only once
        etcd_client.get_prefix.assert_called_once()


class EtcdCacheTest(unittest.TestCase):
    def test_create_default_cache_instance(self):
        cache = EtcdCache()

        self.assertEqual(cache._ttl, EtcdCache.DEFAULT_TTL)
        self.assertListEqual(cache._skip_keys, [])

    def test_create_custom_cache_instance(self):
        ttl = 900
        skip_keys = ['aa', 'bb', 'cc']
        cache = EtcdCache(ttl, skip_keys)

        self.assertEqual(cache._ttl, ttl)
        self.assertListEqual(cache._skip_keys, skip_keys)

    def test_cache_add_key(self):
        key = '/contrail/virtual_network/abcd'
        cache = EtcdCache(ttl=9000)
        cache[key] = EtcdCache.Record(resource={'uuid': 'abcd'})

        self.assertIn(key, cache)

    def test_revoke_expired_key(self):
        cache = EtcdCache(ttl=-100)  # simulate expire keys
        cache['/test/key'] = EtcdCache.Record(resource={'uuid': 'test item'})

        self.assertNotIn('/test/key', cache)

    def test_get_cached_key(self):
        key = '/contrail/virtual_network/abcd'
        cache = EtcdCache(ttl=9000)
        cache[key] = EtcdCache.Record(resource={'uuid': 'abcd'})

        resource = cache[key].resource
        self.assertEqual(resource['uuid'], 'abcd')

    def test_remove_key_if_exist(self):
        key = '/contrail/virtual_network/abcd'
        cache = EtcdCache(ttl=9000)
        cache[key] = EtcdCache.Record(resource={'uuid': 'abcd'})

        del cache[key]
        self.assertNotIn(key, cache)

    def test_remove_key_if_not_exist(self):
        cache = EtcdCache(ttl=9000)
        del cache['not existing key']

    def test_revoke_ttl_expired(self):
        cache = EtcdCache(ttl=-100)  # simulate expire keys
        cache['/test/key'] = EtcdCache.Record(resource={'uuid': 'test item'})

        cache.revoke_ttl('/test/key')
        self.assertNotIn('/test/key', cache)

    def test_not_revoke_ttl_if_not_expired(self):
        cache = EtcdCache(ttl=9000)  # simulate expire keys
        cache['/test/key'] = EtcdCache.Record(resource={'uuid': 'test item'})

        cache.revoke_ttl('/test/key')
        self.assertIn('/test/key', cache)
