#!/usr/bin/env python

#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_db_test.py
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
from mockcassandra import mockcassandra
from mockredis import mockredis
import logging
from pysandesh.util import UTCTimestampUsec

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')


class AnalyticsDbTest(testtools.TestCase, fixtures.TestWithFixtures):

    @classmethod
    def setUpClass(cls):
        if AnalyticsDbTest._check_skip_test() is True:
            return

        if (os.getenv('LD_LIBRARY_PATH', '').find('build/lib') < 0):
            if (os.getenv('DYLD_LIBRARY_PATH', '').find('build/lib') < 0):
                assert(False)

        cls.cassandra_port = AnalyticsDbTest.get_free_port()
        mockcassandra.start_cassandra(cls.cassandra_port)
        cls.redis_port = AnalyticsDbTest.get_free_port()
        mockredis.start_redis(
            cls.redis_port, builddir+'/testroot/bin/redis-server')

    @classmethod
    def tearDownClass(cls):
        if AnalyticsDbTest._check_skip_test() is True:
            return

        mockcassandra.stop_cassandra(cls.cassandra_port)
        mockredis.stop_redis(cls.redis_port)
        pass

    def test_00_database_purge_query(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        and checks if database purge functonality is
        is working properly
        '''
        logging.info("%%% test_00_database_purge_query %%%")
        if AnalyticsDbTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port))
        self.verify_database_purge(vizd_obj)
        return True
    # end test_00_database_purge_query

    def test_01_database_purge_query_with_redis_password(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        and checks if database purge functonality is
        is working properly with redis password
        '''
        logging.info("%%% test_01_database_purge_query_with_redis_password %%%")
        if AnalyticsDbTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, -1,
                             self.__class__.cassandra_port,
                             redis_password='contrail'))
        self.verify_database_purge(vizd_obj)
        return True
    # end test_01_database_purge_query_with_redis_password

    def verify_database_purge(self, vizd_obj):
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_database_purge_with_percentage_input()
        assert vizd_obj.verify_database_purge_support_utc_time_format()
        assert vizd_obj.verify_database_purge_support_datetime_format()
        assert vizd_obj.verify_database_purge_support_deltatime_format()
        assert vizd_obj.verify_database_purge_request_limit()
    # end verify_database_purge

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

# end class AnalyticsDbTest

def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT,_term_handler)
    unittest.main(catchbreak=True)
