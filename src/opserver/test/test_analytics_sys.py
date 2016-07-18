#!/usr/bin/env python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_systest.py
#
# System tests for analytics
#

import os
import sys
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
from mockredis import mockredis
import logging
import time
import pycassa
from pycassa.pool import ConnectionPool
from pycassa.columnfamily import ColumnFamily
from opserver.sandesh.viz.constants import *
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames
from utils.util import find_buildroot
from cassandra.cluster import Cluster

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')
builddir = find_buildroot(os.getcwd())


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
        cls.redis_port = AnalyticsTest.get_free_port()
        mockredis.start_redis(cls.redis_port)


    @classmethod
    def tearDownClass(cls):
        if AnalyticsTest._check_skip_test() is True:
            return

        mockcassandra.stop_cassandra(cls.cassandra_port)
        mockredis.stop_redis(cls.redis_port)
        pass

    def _update_analytics_start_time(self, start_time):
        cluster = Cluster(['127.0.0.1'],
            port=int(self.__class__.cassandra_port))
        session = cluster.connect(COLLECTOR_KEYSPACE_CQL)
        query = "INSERT INTO {0} (key, \"{1}\") VALUES ('{2}', {3})".format(
            SYSTEM_OBJECT_TABLE, SYSTEM_OBJECT_START_TIME,
            SYSTEM_OBJECT_ANALYTICS, start_time)
        try:
            session.execute(query)
        except Exception as e:
            logging.error("INSERT INTO %s: Key %s Column %s Value %d "
                "FAILED: %s" % (SYSTEM_OBJECT_TABLE,
                SYSTEM_OBJECT_ANALYTICS, SYSTEM_OBJECT_START_TIME,
                start_time, str(e)))
            assert False
        else:
            cluster.shutdown()
    # end _update_analytics_start_time

    #@unittest.skip('Skipping cassandra test with vizd')
    def test_01_startup(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it checks that the collector UVE (via redis)
        and syslog (via cassandra) can be accessed from
        opserver.
        '''
        logging.info("%%% test_01_startup %%%")
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
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
        logging.info("%%% test_02_message_table_query %%%")
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_message_table_moduleid()
        assert vizd_obj.verify_message_table_select_uint_type()
        assert vizd_obj.verify_message_table_messagetype()
        assert vizd_obj.verify_message_table_where_or()
        assert vizd_obj.verify_message_table_where_and()
        assert vizd_obj.verify_message_table_where_prefix()
        assert vizd_obj.verify_message_table_filter()
        assert vizd_obj.verify_message_table_filter2()
        assert vizd_obj.verify_message_table_sort()
        assert vizd_obj.verify_message_table_limit()
        return True
    # end test_02_message_table_query

    #@unittest.skip('Send/query flow stats to test QE')
    def test_03_flow_query(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it sends flow stats to the collector
        and checks if flow stats can be accessed from
        QE.
        '''
        logging.info("%%% test_03_flow_query %%%")
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        # set the start time in analytics db 1 hour earlier than
        # the current time. For flow series test, we need to create
        # flow samples older than the current time.
        start_time = UTCTimestampUsec() - 3600 * 1000 * 1000
        self._update_analytics_start_time(start_time)
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture("contrail-vrouter-agent", collectors,
                             logging, vizd_obj.get_opserver_port(),
                             start_time))
        assert generator_obj.verify_on_setup()
        generator_obj.generate_flow_samples()
        generator_obj1 = self.useFixture(
            GeneratorFixture("contrail-vrouter-agent", collectors,
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
    # end test_03_flow_query

    #@unittest.skip('InterVN stats using StatsOracle')
    def test_04_intervn_query(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it sends intervn stats to the collector
        and checks if intervn stats can be accessed from
        QE.
        '''
        logging.info("%%% test_04_intervn_query %%%")
        if AnalyticsTest._check_skip_test() is True:
            return True

        # set the start time in analytics db 1 hour earlier than
        # the current time. For flow series test, we need to create
        # flow samples older than the current time.
        start_time = UTCTimestampUsec() - 3600 * 1000 * 1000
        self._update_analytics_start_time(start_time)
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture("contrail-vrouter-agent", collectors,
                             logging, vizd_obj.get_opserver_port(),
                             start_time))
        assert generator_obj.verify_on_setup()
        logging.info("Starting intervn gen " + str(UTCTimestampUsec()))
        generator_obj.generate_intervn()
        logging.info("Ending intervn gen " + str(UTCTimestampUsec()))
        assert vizd_obj.verify_intervn_all(generator_obj)
        assert vizd_obj.verify_intervn_sum(generator_obj)
        return True
    # end test_04_intervn_query

    #@unittest.skip('Fieldname queries')
    def test_05_fieldname_query(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        It then queries the stats table for messagetypes
        and objecttypes
        '''
        logging.info("%%% test_05_fieldname_query %%%")
        start_time = UTCTimestampUsec() - 3600 * 1000 * 1000
        self._update_analytics_start_time(start_time)
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()
        # Sends 2 different vn uves in 1 sec spacing
        generator_obj.generate_intervn()
        assert vizd_obj.verify_fieldname_messagetype()
        assert vizd_obj.verify_fieldname_table()
        return True
    # end test_05_fieldname_query

    #@unittest.skip('verify send-tracebuffer')
    def test_06_send_tracebuffer(self):
        '''
        This test verifies /analytics/send-tracebuffer/ REST API.
        Opserver publishes the request to send trace buffer to all
        the redis-uve instances. Collector forwards the request to
        the appropriate generator(s). Generator sends the tracebuffer
        to the Collector which then dumps the trace messages in the
        analytics db. Verify that the trace messages are written in
        the analytics db.
        '''
        logging.info('%%% test_06_send_tracebuffer %%%')
        if AnalyticsTest._check_skip_test() is True:
            return True
        
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, -1,
                             self.__class__.cassandra_port, 
                             collector_ha_test=True))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        # Make sure the contrail-collector is connected to the redis-uve before 
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
        # Make sure the contrail-collector is connected to the redis-uve before 
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
    #end test_06_send_tracebuffer

    #@unittest.skip('verify source/module list')
    def test_07_table_source_module_list(self):
        '''
        This test verifies /analytics/table/<table>/column-values/Source
        and /analytics/table/<table>/column-values/ModuleId
        '''
        logging.info('%%% test_07_table_source_module_list %%%')
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, -1,
                             self.__class__.cassandra_port, 
                             collector_ha_test=True))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        exp_genlist1 = ['contrail-collector', 'contrail-analytics-api',
                        'contrail-query-engine']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0], 
                                              exp_genlist1)
        exp_genlist2 = ['contrail-collector'] 
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
    #end test_07_table_source_module_list

    #@unittest.skip(' where queries with different conditions')
    def test_08_where_clause_query(self):
        '''
        This test is used to check the working of integer 
        fields in the where query 
        '''
        logging.info("%%% test_08_where_clause_query %%%")

        if AnalyticsTest._check_skip_test() is True:
            return True

        start_time = UTCTimestampUsec() - 3600 * 1000 * 1000
        self._update_analytics_start_time(start_time)
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_where_query()
        #Query the flowseriestable with different where options
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture("contrail-vrouter-agent", collectors,
                             logging, vizd_obj.get_opserver_port(),
                             start_time))
        assert generator_obj.verify_on_setup()
        generator_obj.generate_flow_samples()
        assert vizd_obj.verify_where_query_prefix(generator_obj)
        return True
    #end test_08_where_clause_query

    #@unittest.skip('verify ObjectTable query')
    def test_09_verify_object_table_query(self):
        '''
        This test verifies the Object Table query.
        '''
        logging.info('%%% test_09_verify_object_table_query %%%')
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture('contrail-control', collectors,
                             logging, None, node_type='Control'))
        assert generator_obj.verify_on_setup()
        msg_types = generator_obj.send_all_sandesh_types_object_logs(
                        socket.gethostname())
        assert vizd_obj.verify_object_table_sandesh_types('ObjectBgpRouter',
                socket.gethostname(), msg_types)
    # end test_09_verify_object_table_query

    #@unittest.skip('verify ObjectValueTable query')
    def test_10_verify_object_value_table_query(self):
        '''
        This test verifies the ObjectValueTable query.
        '''
        logging.info('%%% test_10_verify_object_value_table_query %%%')
        
        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_object_value_table_query(
            table='ObjectCollectorInfo',
            exp_object_values=[vizd_obj.collectors[0].hostname])
        # verify that the object table query works for object id containing
        # XML control characters.
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture('contrail-vrouter-agent', collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()
        generator_obj.send_vm_uve(vm_id='vm11&>', num_vm_ifs=2,
                                  msg_count=1)
        assert vizd_obj.verify_object_value_table_query(table='ObjectVMTable',
            exp_object_values=['vm11&>'])
    # end test_10_verify_object_table_query

    #@unittest.skip('verify syslog query')
    def test_11_verify_syslog_table_query(self):
        '''
        This test verifies the Syslog query.
        '''
        import logging.handlers
        logging.info('%%% test_11_verify_syslog_table_query %%%')

        if AnalyticsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port,
                             syslog_port = True))
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
        # bad charecter (loose?)
        line = 'football ' + chr(201) + chr(203) + chr(70) + ' and baseball'
        syslogger.critical(line)
        assert vizd_obj.verify_keyword_query(line, ['football', 'baseball'])

    # end test_11_verify_syslog_table_query

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
