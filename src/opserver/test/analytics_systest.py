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
import threading
threading._DummyThread._Thread__stop = lambda x: 42
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
from utils.generator_fixture import GeneratorFixture
from mockcassandra import mockcassandra
import logging
import time
import pycassa
from pycassa.pool import ConnectionPool
from pycassa.columnfamily import ColumnFamily
from opserver.sandesh.viz.constants import *
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames

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
        assert vizd_obj.verify_message_table_select_uint_type()
        assert vizd_obj.verify_message_table_messagetype()
        assert vizd_obj.verify_message_table_where_or()
        assert vizd_obj.verify_message_table_where_and()
        assert vizd_obj.verify_message_table_filter()
        assert vizd_obj.verify_message_table_filter2()
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
        generator_obj1 = self.useFixture(
            GeneratorFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port(),
                             start_time, hostname=socket.gethostname() + "dup"))
        assert generator_obj1.verify_on_setup()
        generator_obj1.generate_flow_samples()
        generator_object = [generator_obj, generator_obj1]
        for obj in generator_object:
            assert vizd_obj.verify_flow_samples(obj)
        assert vizd_obj.verify_flow_table(generator_obj)
        assert vizd_obj.verify_flow_series_aggregation_binning(generator_object)
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
        # verify that the old UVEs are flushed from redis when collector restarts
        exp_genlist = [vizd_obj.collectors[0].get_generator_id()]
        assert vizd_obj.verify_generator_list_in_redis(\
                                vizd_obj.collectors[0].get_redis_uve(),
                                exp_genlist)

        # stop collectors[1] and verify that OpServer and QE switch 
        # from secondary to primary and VRouterAgent from primary to
        # secondary
        vizd_obj.collectors[1].stop()
        exp_genlist = ['Collector', 'VRouterAgent', 'OpServer', 'QueryEngine']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0],
                                              exp_genlist)
        # verify the generator list in redis
        exp_genlist = [vizd_obj.collectors[0].get_generator_id(),
                       vr_agent.get_generator_id(),
                       vizd_obj.opserver.get_generator_id(),
                       vizd_obj.query_engine.get_generator_id()]
        assert vizd_obj.verify_generator_list_in_redis(\
                                vizd_obj.collectors[0].get_redis_uve(),
                                exp_genlist)

        # stop Opserver and QE 
        vizd_obj.opserver.stop()
        vizd_obj.query_engine.stop()
        exp_genlist = ['Collector', 'VRouterAgent']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0],
                                              exp_genlist)

        # verify the generator list in redis
        exp_genlist = [vizd_obj.collectors[0].get_generator_id(),
                       vr_agent.get_generator_id()]
        assert vizd_obj.verify_generator_list_in_redis(\
                                vizd_obj.collectors[0].get_redis_uve(),
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

    #@unittest.skip('verify send-tracebuffer')
    def test_08_send_tracebuffer(self):
        '''
        This test verifies /analytics/send-tracebuffer/ REST API.
        Opserver publishes the request to send trace buffer to all
        the redis-uve instances. Collector forwards the request to
        the appropriate generator(s). Generator sends the tracebuffer
        to the Collector which then dumps the trace messages in the
        analytics db. Verify that the trace messages are written in
        the analytics db.
        '''
        logging.info('*** test_08_send_tracebuffer ***')
        if AnalyticsTest._check_skip_test() is True:
            return True
        
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.cassandra_port, 
                             collector_ha_test=True))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        # Make sure the Collector is connected to the redis-uve before 
        # sending the trace buffer request
        assert vizd_obj.verify_collector_redis_uve_connection(
                                    vizd_obj.collectors[0])
        # Send trace buffer request for only the first collector
        vizd_obj.opserver.send_tracebuffer_request(
                    vizd_obj.collectors[0].hostname,
                    ModuleNames[Module.COLLECTOR], '0',
                    'UveTrace')
        assert vizd_obj.verify_tracebuffer_in_analytics_db(
                    vizd_obj.collectors[0].hostname,
                    ModuleNames[Module.COLLECTOR], 'UveTrace')
        # There should be no trace buffer from the second collector
        assert not vizd_obj.verify_tracebuffer_in_analytics_db(
                        vizd_obj.collectors[1].hostname,
                        ModuleNames[Module.COLLECTOR], 'UveTrace')
        # Make sure the Collector is connected to the redis-uve before 
        # sending the trace buffer request
        assert vizd_obj.verify_collector_redis_uve_connection(
                                    vizd_obj.collectors[1])
        # Send trace buffer request for all collectors
        vizd_obj.opserver.send_tracebuffer_request(
                    '*', ModuleNames[Module.COLLECTOR], '0',
                    'UveTrace')
        assert vizd_obj.verify_tracebuffer_in_analytics_db(
                    vizd_obj.collectors[1].hostname,
                    ModuleNames[Module.COLLECTOR], 'UveTrace')
    #end test_08_send_tracebuffer 

    #@unittest.skip('verify source/module list')
    def test_09_table_source_module_list(self):
        '''
        This test verifies /analytics/table/<table>/column-values/Source
        and /analytics/table/<table>/column-values/ModuleId
        '''
        logging.info('*** test_09_source_module_list ***')
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.cassandra_port, 
                             collector_ha_test=True))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        exp_genlist1 = ['Collector', 'OpServer', 'QueryEngine']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0], 
                                              exp_genlist1)
        exp_genlist2 = ['Collector'] 
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[1], 
                                              exp_genlist2)
        exp_src_list = [col.hostname for col in vizd_obj.collectors]
        exp_mod_list = exp_genlist1 
        assert vizd_obj.verify_table_source_module_list(exp_src_list, 
                                                        exp_mod_list)
        # stop the second redis_uve instance and verify the src/module list
        vizd_obj.redis_uves[1].stop()
        exp_src_list = [vizd_obj.collectors[0].hostname]
        exp_mod_list = exp_genlist1
        assert vizd_obj.verify_table_source_module_list(exp_src_list, 
                                                        exp_mod_list)
    #end test_09_table_source_module_list 

    #@unittest.skip('verify redis-uve restart')
    def test_10_redis_uve_restart(self):
        logging.info('*** test_10_redis_uve_restart ***')
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_redis_uve_connection(
                            vizd_obj.collectors[0])
        assert vizd_obj.verify_opserver_redis_uve_connection(
                            vizd_obj.opserver)
        # verify redis-uve list
        host = socket.gethostname()
        gen_list = [host+':Analytics:Collector:0',
                    host+':Analytics:QueryEngine:0',
                    host+':Analytics:OpServer:0']
        assert vizd_obj.verify_generator_uve_list(gen_list)
        # stop redis-uve
        vizd_obj.redis_uves[0].stop()
        assert vizd_obj.verify_collector_redis_uve_connection(
                            vizd_obj.collectors[0], False)
        assert vizd_obj.verify_opserver_redis_uve_connection(
                            vizd_obj.opserver, False)
        # start redis-uve and verify that Collector and Opserver are 
        # connected to the redis-uve
        vizd_obj.redis_uves[0].start()
        assert vizd_obj.verify_collector_redis_uve_connection(
                            vizd_obj.collectors[0])
        assert vizd_obj.verify_opserver_redis_uve_connection(
                            vizd_obj.opserver)
        # verify that UVEs are resynced with redis-uve
        assert vizd_obj.verify_generator_uve_list(gen_list)
    # end test_10_redis_uve_restart
   
    #@unittest.skip(' where queries with different conditions')
    def test_11_where_clause_query(self):
        '''
        This test is used to check the working of integer 
        fields in the where query 
        '''
        logging.info("*** test_11_where_clause_query ***")

        if AnalyticsTest._check_skip_test() is True:
            return True

        start_time = UTCTimestampUsec() - 3600 * 1000 * 1000
        self._update_analytics_start_time(start_time)
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_where_query()
        #Query the flowseriestable with different where options
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port(),
                             start_time))
        assert generator_obj.verify_on_setup()
        generator_obj.generate_flow_samples()
	assert vizd_obj.verify_where_query_prefix(generator_obj)
        return True;
    #end test_11_where_clause_query

    #@unittest.skip('verify ObjectTable query')
    def test_12_verify_object_table_query(self):
        '''
        This test verifies the ObjectTable query.
        '''
        logging.info('*** test_12_verify_object_table_query ***')
        
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_object_table_query()
    # end test_12_verify_object_table_query

    #@unittest.skip('verify ObjectTable query')
    def test_13_verify_syslog_table_query(self):
        '''
        This test verifies the Syslog query.
        '''
        import logging.handlers
        logging.info('*** test_13_verify_syslog_table_query ***')

        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        syslogger = logging.getLogger("SYSLOGER")
        lh = logging.handlers.SysLogHandler(address=('127.0.0.1',
                    vizd_obj.collectors[0].get_syslog_port()))
        lh.setFormatter(logging.Formatter('%(asctime)s %(name)s:%(message)s',
                    datefmt='%b %d %H:%M:%S'))
        lh.setLevel(logging.INFO)
        syslogger.addHandler(lh)
        line = 'pizza pasta babaghanoush'
        syslogger.critical(line)
        assert vizd_obj.verify_keyword_query(line, ['pasta', 'pizza'])
        assert vizd_obj.verify_keyword_query(line, ['babaghanoush'])
        # SYSTEMLOG
        assert vizd_obj.verify_keyword_query(line, ['PROGRESS', 'QueryExec'])

    # end test_13_verify_syslog_table_query


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
