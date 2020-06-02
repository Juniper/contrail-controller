from __future__ import absolute_import
import json

from vnc_api.vnc_api import Project
from vnc_api.vnc_api import VirtualNetwork
from . import test_case

from vnc_openstack.vnc_cache import VncCache


class TestBasicVncCache(test_case.NeutronBackendTestCase):
    @property
    def api(self):
        return self._vnc_lib

    def test_cache_stores_features(self):
        key, data = 'test-key', [1, 2, 3]

        cache = VncCache()
        cache.store(key, data)

        self.assertTrue(key in cache._ttl)
        self.assertTrue(key in cache._storage)
        self.assertEqual(cache._storage[key], data)

        self.assertTrue(cache.has(key))
        self.assertFalse(cache.has('foo'))

        retrieved_data = cache.retrieve(key)
        self.assertEqual(retrieved_data, data)

        # restore a cache with 0 TTL
        cache.TTL = 0
        cache.store(key, data)

        self.assertTrue(key in cache._ttl)
        self.assertTrue(key in cache._storage)

        # invalidate and check for key
        self.assertFalse(cache.has(key))

    def test_cache_clears_data_on_resource_change(self):
        project_uuid = self.api.project_create(
            Project('pr-cache-test-{}'.format(self.id())))

        vn = VirtualNetwork('vn-cache-test-{}'.format(self.id()),
                            proj_uuid=project_uuid)
        vn.uuid = self.api.virtual_network_create(vn)

        self.list_resource('network', project_uuid)
        self.list_resource('network', project_uuid)
        self.list_resource('network', project_uuid)

    def test_cache_reduces_execution_time(self):
        pass
