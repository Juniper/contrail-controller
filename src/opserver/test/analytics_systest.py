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
    def test_00_nocassandra(self):
        '''
        This test starts redis,vizd,opserver and qed
        Then it checks that the collector UVE (via redis)
        can be accessed from opserver.
        '''
        logging.info("*** test_00_nocassandra ***")
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir, 0))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()

        return True
    # end test_00_nocassandra

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
                             builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        return True

    #@unittest.skip('Query query engine logs to test QE')
    def test_02_message_table_query(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it checks that the collector UVE (via redis)
        and syslog (via cassandra) can be accessed from
        opserver.
        '''
        logging.info("*** test_02_message_table_query ***")
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir,
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
    # end test_02_message_table_query

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
                             builddir, 0))
        assert vizd_obj.verify_on_setup()
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()
        generator_obj.send_vm_uve(vm_id='abcd',
                                  num_vm_ifs=5,
                                  msg_count=5)
        assert generator_obj.verify_vm_uve(vm_id='abcd',
                                           num_vm_ifs=5,
                                           msg_count=5)
        # Delete the VM UVE and verify that the deleted flag is set
        # in the UVE cache
        generator_obj.delete_vm_uve('abcd')
        assert generator_obj.verify_vm_uve_cache(vm_id='abcd', delete=True)
        # Add the VM UVE with the same vm_id and verify that the deleted flag
        # is cleared in the UVE cache
        generator_obj.send_vm_uve(vm_id='abcd',
                                  num_vm_ifs=5,
                                  msg_count=5)
        assert generator_obj.verify_vm_uve_cache(vm_id='abcd')
        assert generator_obj.verify_vm_uve(vm_id='abcd',
                                           num_vm_ifs=5,
                                           msg_count=5)
        return True
    # end test_03_vm_uve

    #@unittest.skip('Send/query flow stats to test QE')
    def test_04_flow_query(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it sends flow stats to the collector
        and checks if flow stats can be accessed from
        QE.
        '''
        logging.info("*** test_04_flow_query ***")
        if AnalyticsTest._check_skip_test() is True:
            return True

        # set the start time in analytics db 1 hour earlier than
        # the current time. For flow series test, we need to create
        # flow samples older than the current time.
        start_time = UTCTimestampUsec() - 3600 * 1000 * 1000
        self._update_analytics_start_time(start_time)
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port(),
                             start_time))
        assert generator_obj.verify_on_setup()
        generator_obj.generate_flow_samples()
        assert vizd_obj.verify_flow_samples(generator_obj)
        assert vizd_obj.verify_flow_table(generator_obj)
        assert vizd_obj.verify_flow_series_aggregation_binning(generator_obj)
        return True
    # end test_04_flow_query 

    #@unittest.skip('Skipping Collector HA test')
    def test_05_collector_ha(self):
        logging.info('*** test_05_collector_ha ***')
        if AnalyticsTest._check_skip_test() is True:
            return True
        
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir,
                             self.__class__.cassandra_port, 
                             collector_ha_test=True))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        # OpServer and QueryEngine are started with collectors[0] as 
        # primary and collectors[1] as secondary
        host = socket.gethostname()
        exp_genlist = ['Collector', 'OpServer', 'QueryEngine']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0], 
                                              exp_genlist)
        # start the VRouterAgent with collectors[1] as primary and 
        # collectors[0] as secondary 
        collectors = [vizd_obj.collectors[1].get_addr(), 
                      vizd_obj.collectors[0].get_addr()]
        vr_agent = self.useFixture(
            GeneratorFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert vr_agent.verify_on_setup()
        exp_genlist = ['Collector', 'VRouterAgent']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[1], 
                                              exp_genlist)
        # stop collectors[0] and verify that OpServer and QE switch 
        # from primary to secondary collector
        vizd_obj.collectors[0].stop()
        exp_genlist = ['Collector', 'VRouterAgent', 'OpServer', 'QueryEngine']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[1], 
                                              exp_genlist)
        # start collectors[0]
        vizd_obj.collectors[0].start()
        exp_genlist = ['Collector']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0],
                                              exp_genlist)
        # stop collectors[1] and verify that OpServer and QE switch 
        # from secondary to primary and VRouterAgent from primary to
        # secondary
        vizd_obj.collectors[1].stop()
        exp_genlist = ['Collector', 'VRouterAgent', 'OpServer', 'QueryEngine']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0],
                                              exp_genlist)
        # stop Opserver and QE 
        vizd_obj.opserver.stop()
        vizd_obj.query_engine.stop()
        exp_genlist = ['Collector', 'VRouterAgent']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0],
                                              exp_genlist)
        # start Opserver and QE with collectors[1] as the primary and
        # collectors[0] as the secondary. On generator startup, verify 
        # that it connects to the secondary collector, if the 
        # connection to the primary fails
        vizd_obj.opserver.set_primary_collector(
                            vizd_obj.collectors[1].get_addr())
        vizd_obj.opserver.set_secondary_collector(
                            vizd_obj.collectors[0].get_addr())
        vizd_obj.opserver.start()
        vizd_obj.query_engine.set_primary_collector(
                            vizd_obj.collectors[1].get_addr())
        vizd_obj.query_engine.set_secondary_collector(
                            vizd_obj.collectors[0].get_addr())
        vizd_obj.query_engine.start()
        exp_genlist = ['Collector', 'VRouterAgent', 'OpServer', 'QueryEngine']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0],
                                              exp_genlist)
        # stop the collectors[0] - both collectors[0] and collectors[1] are down
        # send the VM UVE and verify that the VM UVE is synced after connection
        # to the collector
        vizd_obj.collectors[0].stop()
        # Make sure the connection to the collector is teared down before 
        # sending the VM UVE
        while True:
            if vr_agent.verify_on_setup() is False:
                break
        vr_agent.send_vm_uve(vm_id='abcd-1234-efgh-5678',
                             num_vm_ifs=5, msg_count=5) 
        vizd_obj.collectors[1].start()
        exp_genlist = ['Collector', 'VRouterAgent', 'OpServer', 'QueryEngine']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[1],
                                              exp_genlist)
        assert vr_agent.verify_vm_uve(vm_id='abcd-1234-efgh-5678',
                                      num_vm_ifs=5, msg_count=5)
    # end test_05_collector_ha

    #@unittest.skip('InterVN stats using StatsOracle')
    def test_06_intervn_query(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it sends intervn stats to the collector
        and checks if intervn stats can be accessed from
        QE.
        '''
        logging.info("*** test_06_intervn_query ***")
        if AnalyticsTest._check_skip_test() is True:
            return True

        # set the start time in analytics db 1 hour earlier than
        # the current time. For flow series test, we need to create
        # flow samples older than the current time.
        start_time = UTCTimestampUsec() - 3600 * 1000 * 1000
        self._update_analytics_start_time(start_time)
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port(),
                             start_time))
        assert generator_obj.verify_on_setup()
        logging.info("Starting intervn gen " + str(UTCTimestampUsec()))
        generator_obj.generate_intervn()
        logging.info("Ending intervn gen " + str(UTCTimestampUsec()))
        assert vizd_obj.verify_intervn_all(generator_obj)
        assert vizd_obj.verify_intervn_sum(generator_obj)
        return True
    # end test_06_intervn_query 

    #@unittest.skip(' Messagetype and Objecttype queries')
    def test_07_fieldname_query(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        It then queries the stats table for messagetypes
        and objecttypes
        '''
        logging.info("*** test_07_fieldname_query ***")
        start_time = UTCTimestampUsec() - 3600 * 1000 * 1000
        self._update_analytics_start_time(start_time)
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_fieldname_messagetype();
        assert vizd_obj.verify_fieldname_objecttype();
        return True;
    #end test_07_fieldname_query

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
