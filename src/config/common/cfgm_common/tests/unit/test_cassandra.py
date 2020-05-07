# -*- coding: utf-8 -*-

#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import unittest

from cfgm_common.cassandra import api as cassa_api


class FakeDriver(cassa_api.CassandraDriver):
    def get_cf_batch(self, keyspace_name, cf_name):
        pass

    def get_range(self, keyspace_name, cf_name):
        pass

    def multiget(self, keyspace_name, cf_name, keys, columns=None, start='',
                 finish='', timestamp=False, num_columns=None):
        pass

    def get(self, keyspace_name, cf_name, key, columns=None, start='',
            finish=''):
        pass

    def get_one_col(self, keyspace_name, cf_name, key, column):
        pass

    def insert(self, key, columns, keyspace_name=None, cf_name=None,
               batch=None):
        pass

    def remove(self, key, columns=None, keyspace_name=None, cf_name=None,
               batch=None):
        pass


class TestOptions(unittest.TestCase):

    def test_keyspace_wipe(self):
        drv = FakeDriver([])
        self.assertEqual(
            "my_keyspace", drv.keyspace("my_keyspace"))

        drv = FakeDriver([], db_prefix='a_prefix')
        self.assertEqual(
            "a_prefix_my_keyspace", drv.keyspace("my_keyspace"))

    def test_reset_config_wipe(self):
        drv = FakeDriver([])
        self.assertFalse(drv.options.reset_config)

        drv = FakeDriver([], reset_config=True)
        self.assertTrue(drv.options.reset_config)
