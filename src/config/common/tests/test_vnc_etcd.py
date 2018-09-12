# -*- coding: utf-8 -*-
import json
import unittest

import etcd3
import mock
from vnc_etcd import EtcdCache, VncEtcdClient, NoIdError


class VncEtcdClientTest(unittest.TestCase):
    def test_create_etcd_client_instance(self):
        vnc_etcd = VncEtcdClient(host='etcd-host-01', port='2379',
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

    @mock.patch('etcd3.client')
    def test_object_read(self, etcd_client):
        example_resource = str('{"uuid":"ba3442c8a3ec",'
                               '"parent_uuid":"beefbeef0003",'
                               '"parent_type":"project"}')

        vnc_etcd = VncEtcdClient(host='etcd-host-01', port='2379',
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

        vnc_etcd = VncEtcdClient(host='etcd-host-01', port='2379',
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
        vnc_etcd = VncEtcdClient(host='etcd-host-01', port='2379',
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
        vnc_etcd = VncEtcdClient(host='etcd-host-01', port='2379',
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
