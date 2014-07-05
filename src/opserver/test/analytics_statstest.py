#!/usr/bin/env python

#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_stattest.py
#
# System tests for analytics
#

import sys
builddir = sys.path[0] + '/../..'

import signal
import gevent
from gevent import monkey
monkey.patch_all()
import os
import unittest
import testtools
import fixtures
import socket
from utils.analytics_fixture import AnalyticsFixture
from utils.stats_fixture import StatsFixture
from mockcassandra import mockcassandra
import logging
import time
import pycassa
from pycassa.pool import ConnectionPool
from pycassa.columnfamily import ColumnFamily
from opserver.sandesh.viz.constants import *

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')


class StatsTest(testtools.TestCase, fixtures.TestWithFixtures):

    @classmethod
    def setUpClass(cls):
        if StatsTest._check_skip_test() is True:
            return

        if (os.getenv('LD_LIBRARY_PATH', '').find('build/lib') < 0):
            if (os.getenv('DYLD_LIBRARY_PATH', '').find('build/lib') < 0):
                assert(False)

        cls.cassandra_port = StatsTest.get_free_port()
        mockcassandra.start_cassandra(cls.cassandra_port)

    @classmethod
    def tearDownClass(cls):
        if StatsTest._check_skip_test() is True:
            return

        mockcassandra.stop_cassandra(cls.cassandra_port)
        pass

    #@unittest.skip('Get samples using StatsOracle')
    def test_00_basicsamples(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it sends test stats to the collector
        and checks if they can be accessed from QE.
        '''
        logging.info("*** test_00_basicsamples ***")
        if StatsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]

        generator_obj = self.useFixture(
            StatsFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()

        logging.info("Starting stat gen " + str(UTCTimestampUsec()))

        generator_obj.send_test_stat("t00","samp1",1,1);
        generator_obj.send_test_stat("t00","samp2",2,1.1);


        logging.info("Checking Stats " + str(UTCTimestampUsec()))

        assert generator_obj.verify_test_stat("StatTable.TestState.ts","-2m",
            select_fields = [ "UUID", "ts.s1", "ts.i1", "ts.d1" ],
            where_clause = 'name="t00"', num = 2, check_rows =
            [{ "ts.s1":"samp2", "ts.i1":2, "ts.d1":1.1},
             { "ts.s1":"samp1", "ts.i1":1, "ts.d1":1}]);
            
        return True
    # end test_00_basicsamples


    @staticmethod
    def get_free_port():
        cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        cs.bind(("", 0))
        cport = cs.getsockname()[1]
        cs.close()
        return cport

    @staticmethod
    def _check_skip_test():
        if (socket.gethostname() == 'build01'):
            logging.info("Skipping test")
            return True
        return False

def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT,_term_handler)
    unittest.main(catchbreak=True)
