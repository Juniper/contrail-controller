#!/usr/bin/env python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_systest.py
#
# System tests for analytics
#

import sys
builddir = sys.path[0] + '/../..'

from gevent import monkey
monkey.patch_all()
import os
import unittest
import testtools
import fixtures
import socket
from utils.config_fixture import ConfigFixture
from mockcassandra import mockcassandra
import logging
import time
import pycassa
from pycassa.pool import ConnectionPool
from pycassa.columnfamily import ColumnFamily

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')

class ConfigTest(testtools.TestCase, fixtures.TestWithFixtures):

    @classmethod
    def setUpClass(cls):
        cls.cassandra_port = ConfigTest.get_free_port()
        mockcassandra.start_cassandra(cls.cassandra_port)

    @classmethod
    def tearDownClass(cls):
        mockcassandra.stop_cassandra(cls.cassandra_port)

    #@unittest.skip('Skipping non-cassandra test with vizd')
    def test_00_startapi(self):
        '''
        This test starts redis,vizd,opserver and qed
        Then it checks that the collector UVE (via redis)
        can be accessed from opserver.
        '''
        logging.info("*** test_00_nocassandra ***")

        config_obj = self.useFixture(
            ConfigFixture(logging,
                             builddir, self.cassandra_port))
        assert(config_obj.verify_default_project())
        return True
    # end test_00_nocassandra

    @staticmethod
    def get_free_port():
        cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        cs.bind(("", 0))
        cport = cs.getsockname()[1]
        cs.close()
        return cport

if __name__ == '__main__':
    unittest.main()
