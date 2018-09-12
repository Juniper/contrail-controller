# -*- coding: utf-8 -*-
import json
import mock
import unittest

import etcd3

from cfgm_common.exceptions import NoIdError
from vnc_etcd import EtcdCache, VncEtcd


def _vnc_etcd_factory(host='etcd-host-01', port='2379', prefix='/contrail',
                      logger=None, obj_cache_exclude_types=None,
                      log_response_time=None, credential=None,
                      ssl_enabled=False, ca_certs=None):
    return VncEtcd(host=host, port=port, prefix=prefix, logger=logger,
                   obj_cache_exclude_types=obj_cache_exclude_types,
                   log_response_time=log_response_time, credential=credential,
                   ssl_enabled=ssl_enabled, ca_certs=ca_certs)


class VncEtcdTest(unittest.TestCase):
    def test_create_etcd_client_instance(self):
        vnc_etcd = _vnc_etcd_factory(obj_cache_exclude_types=['tag_type'])

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
        vnc_etcd = _vnc_etcd_factory(obj_cache_exclude_types=[])
        result = vnc_etcd._key_prefix(obj_type='test_object')
        expected = '/contrail/test_object'
        self.assertEqual(result, expected)

    def test_key_obj(self):
        vnc_etcd = _vnc_etcd_factory(obj_cache_exclude_types=[])
        result = vnc_etcd._key_obj(obj_type='test_object', obj_id='qwerty')
        expected = '/contrail/test_object/qwerty'
        self.assertEqual(result, expected)

    @mock.patch('etcd3.client')
    def test_object_read(self, etcd_client):
        example_resource = str('{"uuid":"ba3442c8a3ec",'
                               '"parent_uuid":"beefbeef0003",'
                               '"parent_type":"project"}')

        vnc_etcd = _vnc_etcd_factory()
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

        vnc_etcd = _vnc_etcd_factory()
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
        vnc_etcd = _vnc_etcd_factory()
        etcd_client.get = mock.MagicMock()
        etcd_client.get.return_value = (None, None)
        vnc_etcd._client = etcd_client

        self.assertRaises(NoIdError, vnc_etcd.object_read,
                          'virtual_network', ['ba3442c8a3ec'])

    @mock.patch('etcd3.client')
    def test_object_read_return_empty_result(self, etcd_client):
        vnc_etcd = _vnc_etcd_factory()
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

        vnc_etcd = _vnc_etcd_factory()
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

        vnc_etcd = _vnc_etcd_factory()
        etcd_client.get_prefix = mock.MagicMock()
        etcd_client.get_prefix.return_value = example_resource
        vnc_etcd._client = etcd_client

        result = vnc_etcd.object_all('test_resource')
        expected = [json.loads(res) for res, kv in example_resource]
        self.assertListEqual(result, expected)

    @mock.patch('etcd3.client')
    def test_object_list_for_parent_uuid(self, etcd_client):
        parent_uuids = ['parent-uuid']

        vnc_etcd = _vnc_etcd_factory()
        vnc_etcd.object_read = mock.MagicMock(
            return_value=(True, [{'uuid': 'parent-uuid'}]))
        vnc_etcd._get_backrefs = mock.MagicMock(
            return_value=[{'uuid': 'backref-uuid'}])

        ok, result, marker = vnc_etcd.object_list(obj_type='test_obj',
                                                  parent_uuids=parent_uuids,
                                                  obj_uuids=['backref-uuid'])
        self.assertTrue(ok)
        self.assertListEqual(result, ['backref-uuid'])
        self.assertIsNone(marker)

    @mock.patch('etcd3.client')
    def test_object_list_with_back_ref_uuid(self, etcd_client):
        parent_uuids = ['parent-uuid']

        vnc_etcd = _vnc_etcd_factory()
        vnc_etcd.object_read = mock.MagicMock(
            return_value=(True, [{'uuid': 'parent-uuid'}]))
        vnc_etcd._get_backrefs = mock.MagicMock(
            return_value=[{'uuid': 'backref-uuid'}])

        ok, result, marker = vnc_etcd.object_list(obj_type='test_obj',
                                                  back_ref_uuids=parent_uuids,
                                                  obj_uuids=['backref-uuid'])
        self.assertTrue(ok)
        self.assertListEqual(result, ['backref-uuid'])
        self.assertIsNone(marker)

    @mock.patch('etcd3.client')
    def test_object_list_return_empty_if_obj_uuids_not_match(self, etcd_client):
        parent_uuids = ['parent-uuid']

        vnc_etcd = _vnc_etcd_factory()
        vnc_etcd.object_read = mock.MagicMock(
            return_value=(True, [{'uuid': 'parent-uuid'}]))
        vnc_etcd._get_backrefs = mock.MagicMock(
            return_value=[{'uuid': 'backref-uuid'}])

        ok, result, marker = vnc_etcd.object_list(obj_type='test_obj',
                                                  back_ref_uuids=parent_uuids,
                                                  obj_uuids=['not_matched'])
        self.assertTrue(ok)
        self.assertListEqual(result, [])
        self.assertIsNone(marker)

    @mock.patch('etcd3.client')
    def test_object_list_return_count(self, etcd_client):
        backref_uuids = ['aaa', 'bbb', 'ccc']

        vnc_etcd = _vnc_etcd_factory()
        vnc_etcd.object_read = mock.MagicMock(
            return_value=(True, [{'uuid': 'ddd'}]))
        vnc_etcd._get_backrefs = mock.MagicMock(
            return_value=[{'uuid': 'ddd'}])

        ok, result, marker = vnc_etcd.object_list(obj_type='test_obj',
                                                  back_ref_uuids=backref_uuids,
                                                  obj_uuids=['ddd'],
                                                  count=True)
        self.assertTrue(ok)
        self.assertEqual(result, 1)
        self.assertIsNone(marker)

    @mock.patch('etcd3.client')
    def test_object_list_should_return_all_resources(self, etcd_client):
        obj_all = [{'uuid': 'aaa'}, {'uuid': 'bbb'}, {'uuid': 'ccc'}]

        vnc_etcd = _vnc_etcd_factory()
        vnc_etcd.object_read = mock.MagicMock(return_value=(True, obj_all))
        vnc_etcd._get_backrefs = mock.MagicMock(
            return_value=[{'uuid': 'ddd'}, {'uuid': 'eee'}])

        ok, result, marker = vnc_etcd.object_list(obj_type='test_obj',
                                                  obj_uuids=['aaa', 'ccc'])
        self.assertTrue(ok)
        # _get_backrefs are called twice which causes MagicMock to return
        # the same data twice. This is why expected list looks strange.
        self.assertListEqual(result, ['ddd', 'eee', 'ddd', 'eee'])
        self.assertIsNone(marker)

    @mock.patch('etcd3.client')
    def test_object_list_with_pagination(self, etcd_client):
        parent_uuids = ['aaa', 'bbb', 'ccc']

        vnc_etcd = _vnc_etcd_factory()
        vnc_etcd.object_read = mock.MagicMock(return_value=(True, []))
        vnc_etcd._get_backrefs = mock.MagicMock(return_value=[
            {'uuid': 'ddd', 'id_perms': {'created': 2}},
            {'uuid': 'eee', 'id_perms': {'created': 3}},
            {'uuid': 'fff', 'id_perms': {'created': 5}},
        ])

        ok, result, marker = vnc_etcd.object_list(
            obj_type='test_obj', parent_uuids=parent_uuids,
            obj_uuids=['ddd', 'eee', 'fff'], paginate_start='eee',
            paginate_count=2)

        self.assertTrue(ok)
        self.assertListEqual(result, ['eee', 'fff'])
        self.assertEqual(marker, 'fff')

    @mock.patch('etcd3.client')
    def test_get_backrefs(self, etcd_client):
        parents = [{'uuid': 'aaa',
                    'children': [{'uuid': 'aaa_child'}]},
                   {'uuid': 'bbb',
                    'children': [{'uuid': 'bbb_child'}]},
                   {'uuid': 'ccc',
                    'children': [{'uuid': 'ccc_child'}]}]

        vnc_etcd = _vnc_etcd_factory()
        vnc_etcd.object_read = lambda _, refs, **kw: (True, {'uuid': refs[0]})

        result = vnc_etcd._get_backrefs(parents, obj_type='test',
                                        field='children')
        self.assertListEqual(list(result), [{'uuid': 'aaa_child'},
                                            {'uuid': 'bbb_child'},
                                            {'uuid': 'ccc_child'}])

    @mock.patch('etcd3.client')
    def test_get_backrefs_should_return_generator(self, etcd_client):
        vnc_etcd = _vnc_etcd_factory()
        result = vnc_etcd._get_backrefs(parents=[], obj_type='test',
                                        field='test')
        self.assertListEqual(list(result), [])

    @mock.patch('etcd3.client')
    def test_paginate_return_sorted_objs_and_last_item(self, etcd_client):
        objs = [
            {'uuid': 'bbb', 'id_perms': {'created': '2018-09-20T20:02:01'}},
            {'uuid': 'ccc', 'id_perms': {'created': '2018-09-21T20:02:00'}},
            {'uuid': 'aaa', 'id_perms': {'created': '2018-09-20T20:02:00'}},
            {'uuid': 'ddd', 'id_perms': {'created': '2018-09-19T20:00:00'}},
        ]

        vnc_etcd = _vnc_etcd_factory()
        result, marker = vnc_etcd._paginate(objs, paginate_start='',
                                            paginate_count=4)
        expected = [
            {'uuid': 'ddd', 'id_perms': {'created': '2018-09-19T20:00:00'}},
            {'uuid': 'aaa', 'id_perms': {'created': '2018-09-20T20:02:00'}},
            {'uuid': 'bbb', 'id_perms': {'created': '2018-09-20T20:02:01'}},
            {'uuid': 'ccc', 'id_perms': {'created': '2018-09-21T20:02:00'}},
        ]
        self.assertListEqual(result, expected)
        self.assertEqual(marker, 'ccc')

    @mock.patch('etcd3.client')
    def test_paginate_return_last_2_objs_and_last_item(self, etcd_client):
        objs = [
            {'uuid': 'bbb', 'id_perms': {'created': '2018-09-20T20:02:01'}},
            {'uuid': 'ccc', 'id_perms': {'created': '2018-09-21T20:02:00'}},
            {'uuid': 'aaa', 'id_perms': {'created': '2018-09-20T20:02:00'}},
            {'uuid': 'ddd', 'id_perms': {'created': '2018-09-19T20:00:00'}},
        ]

        vnc_etcd = _vnc_etcd_factory()
        result, marker = vnc_etcd._paginate(objs, paginate_start='aaa',
                                            paginate_count=2)
        expected = [
            {'uuid': 'aaa', 'id_perms': {'created': '2018-09-20T20:02:00'}},
            {'uuid': 'bbb', 'id_perms': {'created': '2018-09-20T20:02:01'}},
        ]
        self.assertListEqual(result, expected)
        self.assertEqual(marker, 'bbb')

    @mock.patch('etcd3.client')
    def test_paginate_return_empty_and_null_as_last_item(self, etcd_client):
        objs = []

        vnc_etcd = _vnc_etcd_factory()
        result, marker = vnc_etcd._paginate(objs, paginate_start='ccc',
                                            paginate_count=2)
        expected = []
        self.assertListEqual(result, expected)
        self.assertIsNone(marker)

    @mock.patch('etcd3.client')
    def test_filter_return_empty(self, etcd_client):
        objs = []

        vnc_etcd = _vnc_etcd_factory()
        result = vnc_etcd._filter(objs)
        self.assertListEqual(list(result), [])

    @mock.patch('etcd3.client')
    def test_filter_objs_which_exists_in_obj_uuids(self, etcd_client):
        objs = [{'uuid': 'aaa'}, {'uuid': 'bbb'}, {'uuid': 'ccc'}]
        obj_uuids = ['aaa', 'ccc']

        vnc_etcd = _vnc_etcd_factory()
        result = vnc_etcd._filter(objs, obj_uuids)

        expected = [{'uuid': 'aaa'}, {'uuid': 'ccc'}]
        self.assertListEqual(list(result), expected)

    @mock.patch('etcd3.client')
    def test_filter_objs_with_apply_filters(self, etcd_client):
        objs = [
            {'uuid': 'aaa', 'foo': 12, 'bar': 'aaa'},
            {'uuid': 'bbb', 'foo': 100},
            {'uuid': 'ccc', 'bar': 'aaa'},
        ]
        obj_uuids = ['aaa', 'bbb', 'ccc']
        filters = {'foo': ["12"]}

        vnc_etcd = _vnc_etcd_factory()
        result = vnc_etcd._filter(objs, obj_uuids, filters)

        expected = [{'uuid': 'aaa', 'foo': 12, 'bar': 'aaa'},
                    {'uuid': 'bbb', 'foo': 100}]
        self.assertListEqual(list(result), expected)

    @mock.patch('etcd3.client')
    def test_filter_objs_with_apply_nested_filters(self, etcd_client):
        objs = [
            {'uuid': 'aaa', 'foo': {'bar': 'baz'}},
            {'uuid': 'bbb', 'foo': {'field': 123}},
            {'uuid': 'ccc', 'foo': 'aaa'},
        ]
        obj_uuids = ['aaa', 'bbb', 'ccc']
        filters = {'foo': ['{"bar": "baz"}']}

        vnc_etcd = _vnc_etcd_factory()
        result = vnc_etcd._filter(objs, obj_uuids, filters)

        expected = [{'uuid': 'aaa', 'foo': {'bar': 'baz'}},
                    {'uuid': 'ccc', 'foo': 'aaa'}]
        self.assertListEqual(list(result), expected)


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
