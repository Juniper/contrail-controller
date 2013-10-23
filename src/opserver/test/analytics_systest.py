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
vizdtestdir = sys.path[0]

from gevent import monkey
monkey.patch_all()
import os
import unittest
import testtools
import fixtures
import socket
from utils.analytics_fixture import AnalyticsFixture
from utils.generator_fixture import GeneratorFixture
from mockcassandra import mockcassandra
import logging
import time
import pycassa
from pycassa.pool import ConnectionPool
from pycassa.columnfamily import ColumnFamily
from opserver.sandesh.viz.constants import *

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')


class AnalyticsTest(testtools.TestCase, fixtures.TestWithFixtures):

    @classmethod
    def setUpClass(cls):
        if AnalyticsTest._check_skip_test() is True:
            return

        if (os.getenv('LD_LIBRARY_PATH', '').find('build/lib') < 0):
            if (os.getenv('DYLD_LIBRARY_PATH', '').find('build/lib') < 0):
                assert(False)

        cls.cassandra_port = AnalyticsTest.get_free_port()
        mockcassandra.start_cassandra(cls.cassandra_port)

    @classmethod
    def tearDownClass(cls):
        if AnalyticsTest._check_skip_test() is True:
            return

        mockcassandra.stop_cassandra(cls.cassandra_port)
        pass

    def _update_analytics_start_time(self, start_time):
        pool = ConnectionPool(COLLECTOR_KEYSPACE, ['127.0.0.1:%s'
                              % (self.__class__.cassandra_port)])
        col_family = ColumnFamily(pool, SYSTEM_OBJECT_TABLE)
        col_family.insert(SYSTEM_OBJECT_ANALYTICS,
                          {SYSTEM_OBJECT_START_TIME: start_time})
    # end _update_analytics_start_time

    #@unittest.skip('Skipping non-cassandra test with vizd')
    def test_00_startup_nocass(self):
        '''
        This test starts redis,vizd,opserver and qed
        Then it checks that the collector UVE (via redis)
        can be accessed from opserver.
        '''
        logging.info("*** test_00_startup_nocass ***")
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             vizdtestdir + '/../../../../build/debug', 0))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        return True

    #@unittest.skip('Skipping cassandra test with vizd')
    def test_01_startup(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it checks that the collector UVE (via redis)
        and syslog (via cassandra) can be accessed from
        opserver.
        '''
        logging.info("*** test_01_startup ***")
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             vizdtestdir + '/../../../../build/debug',
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        return True

    #@unittest.skip('Query query engine logs to test QE')
    def test_02_startup(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it checks that the collector UVE (via redis)
        and syslog (via cassandra) can be accessed from
        opserver.
        '''
        logging.info("*** test_02_startup ***")
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             vizdtestdir + '/../../../../build/debug',
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_message_table_moduleid()
        assert vizd_obj.verify_message_table_messagetype()
        assert vizd_obj.verify_message_table_where_or()
        assert vizd_obj.verify_message_table_where_and()
        assert vizd_obj.verify_message_table_filter()
        assert vizd_obj.verify_message_table_sort()
        return True

    #@unittest.skip('Skipping VM UVE test')
    def test_03_vm_uve(self):
        '''
        This test starts redis, vizd, opserver, qed, and a python generator
        that simulates vrouter and sends UveVirtualMachineAgentTrace messages.
        It uses the test class' cassandra instance. Then it checks that the
        VM UVE (via redis) can be accessed from opserver.
        '''
        logging.info("*** test_03_vm_uve ***")
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             vizdtestdir + '/../../../../build/debug', 0))
        assert vizd_obj.verify_on_setup()
        generator_obj = self.useFixture(
            GeneratorFixture("VRouterAgent",
                             ("127.0.0.1", vizd_obj.get_collector_port()),
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()
        generator_obj.send_vm_uve(vm_id='abcd',
                                  num_vm_ifs=5,
                                  msg_count=5)
        assert generator_obj.verify_vm_uve(vm_id='abcd',
                                           num_vm_ifs=5,
                                           msg_count=5)
        return True

    #@unittest.skip('Send/query flow stats to test QE')
    def test_04_startup(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it sends flow stats to the collector
        and checks if flow stats can be accessed from
        QE.
        '''
        logging.info("*** test_04_startup ***")
        if AnalyticsTest._check_skip_test() is True:
            return True

        # set the start time in analytics db 1 hour earlier than
        # the current time. For flow series test, we need to create
        # flow samples older than the current time.
        start_time = UTCTimestampUsec() - 3600 * 1000 * 1000
        self._update_analytics_start_time(start_time)
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             vizdtestdir + '/../../../../build/debug',
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        generator_obj = self.useFixture(
            GeneratorFixture("VRouterAgent",
                             ("127.0.0.1", vizd_obj.get_collector_port()),
                             logging, vizd_obj.get_opserver_port(),
                             start_time))
        assert generator_obj.verify_on_setup()
        generator_obj.generate_flow_samples()
        assert vizd_obj.verify_flow_samples(generator_obj)
        assert vizd_obj.verify_flow_table(generator_obj)
        assert vizd_obj.verify_flow_series_aggregation_binning(generator_obj)
        return True

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

if __name__ == '__main__':
    unittest.main()
