#!/usr/bin/env python

#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_stattest.py
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
from utils.stats_fixture import StatsFixture
from mockcassandra import mockcassandra
import logging
import time
from opserver.sandesh.viz.constants import *
from utils.opserver_introspect_utils import VerificationOpsSrv
from utils.util import retry, find_buildroot

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')
builddir = find_buildroot(os.getcwd())


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
        logging.info("%%% test_00_basicsamples %%%")
        if StatsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]

        generator_obj = self.useFixture(
            StatsFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()

        logging.info("Starting stat gen " + str(UTCTimestampUsec()))

        generator_obj.send_test_stat_dynamic("t00","samp1",1,1);
        generator_obj.send_test_stat_dynamic("t00","samp2",2,1.1);
        generator_obj.send_test_stat_dynamic("t00","samp3",1,-5062);
        generator_obj.send_test_stat_dynamic("t00&t01","samp1&samp2",2,1.1);
        generator_obj.send_test_stat_dynamic("t00>t01>","samp1&samp2",2,1.1,
                                             "&test_s2>");

        logging.info("Checking Stats " + str(UTCTimestampUsec()))

        assert generator_obj.verify_test_stat("StatTable.TestStateDynamic.ts",
            "-2m", select_fields = [ "UUID", "ts.s1", "ts.i1", "ts.d1" ],
            where_clause = 'name="t00"', num = 3, check_rows =
            [{ "ts.s1":"samp2", "ts.i1":2, "ts.d1":1.1},
             { "ts.s1":"samp1", "ts.i1":1, "ts.d1":1},
             { "ts.s1":"samp3", "ts.i1":1, "ts.d1":-5062}]);
        assert generator_obj.verify_test_stat("StatTable.TestStateDynamic.ts",
            "-2m", select_fields = [ "UUID", "ts.s1", "ts.s2" ],
            where_clause = 'name="t00&t01"', num = 1, check_rows =
            [{ "ts.s1":"samp1&samp2", "ts.s2": "" }])
        assert generator_obj.verify_test_stat("StatTable.TestStateDynamic.ts",
            "-2m", select_fields = [ "UUID", "name", "ts.s2" ],
            where_clause = 'ts.s1="samp1&samp2"', num = 2, check_rows =
            [{ "name":"t00&t01", "ts.s2": "" },
             { "name":"t00>t01>", "ts.s2":"&test_s2>" }])
            
        return True
    # end test_00_basicsamples

    #@unittest.skip('Get samples using StatsOracle')
    def test_01_statprefix(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it sends test stats to the collector
        and checks if they can be accessed from QE, using prefix-suffix indexes
        '''
        logging.info("%%% test_01_statprefix %%%")
        if StatsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]

        generator_obj = self.useFixture(
            StatsFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()

        logging.info("Starting stat gen " + str(UTCTimestampUsec()))

        generator_obj.send_test_stat("t010","lxxx","samp1",1,1);
        generator_obj.send_test_stat("t010","lyyy","samp1",2,2);
        generator_obj.send_test_stat("t010","lyyy","samp3",2,2,"",5);
        generator_obj.send_test_stat("t010","lyyy&","samp3>",2,2,"");
        generator_obj.send_test_stat("t011","lyyy","samp2",1,1.1,"",7);
        generator_obj.send_test_stat("t011","lxxx","samp2",2,1.2);
        generator_obj.send_test_stat("t011","lxxx","samp2",2,1.2,"",9);
        generator_obj.send_test_stat("t010&t011","lxxx","samp2",1,1.4);
        generator_obj.send_test_stat("t010&t011","lx>ly","samp2",1,1.4);

        logging.info("Checking Stats str-str " + str(UTCTimestampUsec()))

        assert generator_obj.verify_test_stat("StatTable.StatTestState.st","-2m",
            select_fields = [ "UUID", "st.s1", "st.i1", "st.d1" ],
            where_clause = 'name|st.s1=t010|samp1', num = 2, check_rows =
            [{ "st.s1":"samp1", "st.i1":2, "st.d1":2},
             { "st.s1":"samp1", "st.i1":1, "st.d1":1}]);
        assert generator_obj.verify_test_stat("StatTable.StatTestState.st",
            "-2m", select_fields = [ "UUID", "l1" ], where_clause =
            'name|st.s1=t010&t011|samp2 OR name|st.s1=t010|samp3>',
            num = 3, check_rows = [{ "l1":"lxxx" }, { "l1":"lx>ly" },
            { "l1":"lyyy&" }])

        assert generator_obj.verify_test_stat("StatTable.StatTestState.st","-2m",
            select_fields = [ "UUID", "st.s1", "st.i1", "st.d1" ],
            where_clause = 'st.i1|st.i2=2|1<6', num = 1, check_rows =
            [{ "st.s1":"samp3", "st.i1":2, "st.d1":2}]);
        
        logging.info("Checking CLASS " + str(UTCTimestampUsec()))

        assert generator_obj.verify_test_stat("StatTable.StatTestState.st","-2m",
            select_fields = [ "T", "name", "l1", "CLASS(T)" ],
            where_clause = 'name=*', num = 9, check_uniq =
            { "CLASS(T)":7 })
         
        return True
    # end test_01_statprefix

    #@unittest.skip('Get samples using StatsOracle')
    def test_02_overflowsamples(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it sends test stats to the collector
        and checks if they can be accessed from QE.
        '''
        logging.info("%%% test_02_overflowsamples %%%")
        if StatsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]

        generator_obj = self.useFixture(
            StatsFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()

        logging.info("Starting stat gen " + str(UTCTimestampUsec()))

        generator_obj.send_test_stat_dynamic("t02","samp02-2",0x7fffffffffffffff,1.1);

        logging.info("Checking Stats " + str(UTCTimestampUsec()))

        assert generator_obj.verify_test_stat("StatTable.TestStateDynamic.ts",
            "-2m", select_fields = [ "UUID", "ts.s1", "ts.i1", "ts.d1" ],
            where_clause = 'name="t02"', num = 1, check_rows =
            [{"ts.s1":"samp02-2", "ts.i1":0x7fffffffffffffff, "ts.d1":1.1}])
            
        return True
    # end test_02_overflowsamples

    #@unittest.skip('Get minmax values from inserted stats')
    def test_03_min_max_query(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it inserts into the stat table rows 
        and queries MAX and MIN on them
        '''
        logging.info("%%% test_03_min_max_query %%%")
        if StatsTest._check_skip_test() is True:
            return True
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]

        generator_obj = self.useFixture(
            StatsFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()

        logging.info("Starting stat gen " + str(UTCTimestampUsec()))

        generator_obj.send_test_stat("t04","lxxx","samp1",1,5);
        generator_obj.send_test_stat("t04","lyyy","samp1",4,3.4);
        generator_obj.send_test_stat("t04","lyyy","samp1",2,4,"",5);

        logging.info("Checking Stats " + str(UTCTimestampUsec()))
        assert generator_obj.verify_test_stat("StatTable.StatTestState.st","-2m",
            select_fields = [ "MAX(st.i1)", "PERCENTILES(st.i1)", "AVG(st.i1)"],
            where_clause = 'name|st.s1=t04|samp1', num = 1, check_rows =
            [{u'MAX(st.i1)': 4, u'PERCENTILES(st.i1)': None, u'AVG(st.i1)': 2.33333}]);

        assert generator_obj.verify_test_stat("StatTable.StatTestState.st","-2m",
            select_fields = [ "MIN(st.d1)", "AVG(st.d1)"],
            where_clause = 'name|st.s1=t04|samp1', num = 1, check_rows =
            [{u'MIN(st.d1)': 3.4, u'AVG(st.d1)': 4.13333}]);

        return True
    # end test_03_min_max_query

    #@unittest.skip('Get samples from objectlog stats')
    def test_04_statprefix_obj(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it sends test object stats to the collector
        and checks if they can be accessed from QE, using prefix-suffix indexes
        '''
        logging.info("%%% test_04_statprefix_obj %%%")
        if StatsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]

        generator_obj = self.useFixture(
            StatsFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()

        logging.info("Starting stat gen " + str(UTCTimestampUsec()))

        generator_obj.send_test_obj_stat("t010","lxxx","samp1",1,1);
        generator_obj.send_test_obj_stat("t010","lyyy","samp1",2,2);
        generator_obj.send_test_obj_stat("t010","lyyy","samp3",2,2,"",5);
        generator_obj.send_test_obj_stat("t011","lyyy","samp2",1,1.1,"",7);
        generator_obj.send_test_obj_stat("t011","lxxx","samp2",2,1.2);
        generator_obj.send_test_obj_stat("t011","lxxx","samp2",2,1.2,"",9);

        logging.info("Checking Objectlog Stats str-str " + str(UTCTimestampUsec()))

        assert generator_obj.verify_test_stat("StatTable.StatTestObj.st","-2m",
            select_fields = [ "UUID", "st.s1", "st.i1", "st.d1" ],
            where_clause = 'name|st.s1=t010|samp1', num = 2, check_rows =
            [{ "st.s1":"samp1", "st.i1":2, "st.d1":2},
             { "st.s1":"samp1", "st.i1":1, "st.d1":1}]);

        return True
    # end test_04_statprefix_obj

    #@unittest.skip('Send stats with 2nd level of hierarchy')
    def test_05_statprefix_double(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it sends test 2nd-level stats to the collector
        and checks if they can be accessed from QE, using prefix-suffix indexes
        '''
        logging.info("%%% test_05_statprefix_double %%%")
        if StatsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]

        generator_obj = self.useFixture(
            StatsFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()

        logging.info("Starting stat gen " + str(UTCTimestampUsec()))

        generator_obj.send_test_stat_double("t010","lxxx","samp1",1,1);
        generator_obj.send_test_stat_double("t010","lyyy","samp1",2,3);
        generator_obj.send_test_stat_double("t010","lyyy","samp3",2,3,"misc2",5);
        generator_obj.send_test_stat_double("t011","lyyy","samp2",1,1.1,"misc1",7);
        generator_obj.send_test_stat_double("t011","lxxx","samp2",2,1.2);
        generator_obj.send_test_stat_double("t011","lxxx","samp2",2,1.2,"",9);

        logging.info("Checking 2nd-level Stats str-double" + str(UTCTimestampUsec()))

        assert generator_obj.verify_test_stat("StatTable.StatTestStateDouble.dst.st","-2m",
            select_fields = [ "UUID", "dst.st.s1", "dst.st.i1", "dst.l1" ],
            where_clause = 'dst.l1|dst.st.s2=lyyy|misc1', num = 1, check_rows =
            [{ "dst.st.s1":"samp2", "dst.st.i1":1, "dst.l1":"lyyy"}]);

        return True
    # end test_05_statprefix_double

    #@unittest.skip('Stats query filter test')
    def test_06_stats_filter(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        Then it sends test stats to the collector
        and checks if all filter operations work properly.
        '''
        logging.info("%%% test_06_stats_filter %%%")
        if StatsTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.cassandra_port))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        collectors = [vizd_obj.get_collector()]

        generator_obj = self.useFixture(
            StatsFixture("VRouterAgent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()

        logging.info("Starting stat gen " + str(UTCTimestampUsec()))

        generator_obj.send_test_stat("name0", "lxxx", "samp1", 10, 12)
        generator_obj.send_test_stat("name0", "lxxx", "samp2", 20, 12.6)
        generator_obj.send_test_stat("name0", "lyyy", "samp1", 500, 2.345)
        generator_obj.send_test_stat("name0", "lyyy", "samp2", 1000, 15.789)

        # verify that all the stats messages are added in the analytics db
        # before starting the filter tests
        assert generator_obj.verify_test_stat("StatTable.StatTestState.st",
            "-2m", select_fields=["UUID", "l1", "st.s1", "st.i1", "st.d1"],
            where_clause = 'name=name0', num=4, check_rows=
            [{"l1":"lxxx", "st.s1":"samp1", "st.i1":10, "st.d1":12},
             {"l1":"lxxx", "st.s1":"samp2", "st.i1":20, "st.d1":12.6},
             {"l1":"lyyy", "st.s1":"samp1", "st.i1":500, "st.d1":2.345},
             {"l1":"lyyy", "st.s1":"samp2", "st.i1":1000, "st.d1":15.789}])

        assert generator_obj.verify_test_stat("StatTable.StatTestState.st",
            "-2m", select_fields=["UUID", "l1", "st.s1", "st.i1", "st.d1"],
            where_clause = 'name=name0', num=2, check_rows=
            [{"l1":"lxxx", "st.s1":"samp1", "st.i1":10, "st.d1":12},
             {"l1":"lxxx", "st.s1":"samp2", "st.i1":20, "st.d1":12.6}],
            filt="l1 = lxxx")

        assert generator_obj.verify_test_stat("StatTable.StatTestState.st",
            "-2m", select_fields=["UUID", "l1", "st.s1", "st.i1", "st.d1"],
            where_clause = 'name=name0', num=1, check_rows=
            [{"l1":"lyyy", "st.s1":"samp1", "st.i1":500, "st.d1":2.345}],
            filt="st.i1 = 500")

        assert generator_obj.verify_test_stat("StatTable.StatTestState.st",
            "-2m", select_fields=["UUID", "l1", "st.s1", "st.i1", "st.d1"],
            where_clause = 'name=name0', num=2, check_rows=
            [{"l1":"lxxx", "st.s1":"samp1", "st.i1":10, "st.d1":12},
             {"l1":"lxxx", "st.s1":"samp2", "st.i1":20, "st.d1":12.6}],
            filt="st.i1 <= 400")

        assert generator_obj.verify_test_stat("StatTable.StatTestState.st",
            "-2m", select_fields=["UUID", "l1", "st.s1", "st.i1", "st.d1"],
            where_clause = 'name=name0', num=2, check_rows=
            [{"l1":"lyyy", "st.s1":"samp1", "st.i1":500, "st.d1":2.345},
             {"l1":"lyyy", "st.s1":"samp2", "st.i1":1000, "st.d1":15.789}],
            filt="st.i1 >= 500")

        assert generator_obj.verify_test_stat("StatTable.StatTestState.st",
            "-2m", select_fields=["UUID", "l1", "st.s1", "st.i1", "st.d1"],
            where_clause = 'name=name0', num=1, check_rows=
            [{"l1":"lyyy", "st.s1":"samp1", "st.i1":500, "st.d1":2.345}],
            filt="st.d1 <= 3")

        assert generator_obj.verify_test_stat("StatTable.StatTestState.st",
            "-2m", select_fields=["UUID", "l1", "st.s1", "st.i1", "st.d1"],
            where_clause = 'name=name0', num=2, check_rows=
            [{"l1":"lxxx", "st.s1":"samp2", "st.i1":20, "st.d1":12.6},
             {"l1":"lyyy", "st.s1":"samp2", "st.i1":1000, "st.d1":15.789}],
            filt="st.d1 >= 12.5")
    # end test_06_stats_filter

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
