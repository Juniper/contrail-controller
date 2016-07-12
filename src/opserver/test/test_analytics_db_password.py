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
from utils.util import find_buildroot

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')
builddir = find_buildroot(os.getcwd())


class AnalyticsDbTest(testtools.TestCase, fixtures.TestWithFixtures):

    @classmethod
    def setUpClass(cls):
        if AnalyticsDbTest._check_skip_test() is True:
            return

        if (os.getenv('LD_LIBRARY_PATH', '').find('build/lib') < 0):
            if (os.getenv('DYLD_LIBRARY_PATH', '').find('build/lib') < 0):
                assert(False)

        cls.cassandra_port = AnalyticsDbTest.get_free_port()
        mockcassandra.start_cassandra(cls.cassandra_port,cassandra_user='cassandra', cassandra_password='cassandra')
        cls.redis_port = AnalyticsDbTest.get_free_port()
        mockredis.start_redis(cls.redis_port)
        cls.redis_password_port = AnalyticsDbTest.get_free_port()
        mockredis.start_redis(cls.redis_password_port, password='contrail')

    @classmethod
    def tearDownClass(cls):
        if AnalyticsDbTest._check_skip_test() is True:
            return

        mockcassandra.stop_cassandra(cls.cassandra_port)
        mockredis.stop_redis(cls.redis_port)
        mockredis.stop_redis(cls.redis_password_port, password='contrail')
        pass

    #@unittest.skip('Skipping test_00_verify_database_purge_with_percentage_input')
    def test_00_verify_database_purge_with_percentage_input(self):
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
                             self.__class__.cassandra_port,
                             cassandra_user='cassandra',
                             cassandra_password='cassandra'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_generator_collector_connection(
                    vizd_obj.opserver.http_port)
        assert vizd_obj.verify_database_purge_with_percentage_input()
        return True
    # end test_00_database_purge_query

    #@unittest.skip('Skipping test_01_verify_database_purge_support_utc_time_format')
    def test_01_verify_database_purge_support_utc_time_format(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        and checks if database purge functonality is
        is working properly
        '''
        logging.info("%%% test_01_database_purge_query %%%")
        if AnalyticsDbTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port,
                             cassandra_user='cassandra',
                             cassandra_password='cassandra'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_generator_collector_connection(
                    vizd_obj.opserver.http_port)
        assert vizd_obj.verify_database_purge_support_utc_time_format()
        return True
    # end test_01_database_purge_query

    #@unittest.skip('Skipping test_02_verify_database_purge_support_datetime_format')
    def test_02_verify_database_purge_support_datetime_format(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        and checks if database purge functonality is
        is working properly
        '''
        logging.info("%%% test_02_database_purge_query %%%")
        if AnalyticsDbTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port,
                             cassandra_user='cassandra',
                             cassandra_password='cassandra'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_generator_collector_connection(
                    vizd_obj.opserver.http_port)
        assert vizd_obj.verify_database_purge_support_datetime_format()
        return True
    # end test_02_database_purge_query

    #@unittest.skip('Skipping test_03_verify_database_purge_support_deltatime_format')
    def test_03_verify_database_purge_support_deltatime_format(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        and checks if database purge functonality is
        is working properly
        '''
        logging.info("%%% test_03_database_purge_query %%%")
        if AnalyticsDbTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port,
                             cassandra_user='cassandra',
                             cassandra_password='cassandra'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_generator_collector_connection(
                    vizd_obj.opserver.http_port)
        assert vizd_obj.verify_database_purge_support_deltatime_format()
        return True
    # end test_03_database_purge_query

    #@unittest.skip('Skipping test_04_verify_database_purge_request_limit')
    def test_04_verify_database_purge_request_limit(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        and checks if database purge functonality is
        is working properly
        '''
        logging.info("%%% test_04_database_purge_query %%%")
        if AnalyticsDbTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port,
                             cassandra_user='cassandra',
                             cassandra_password='cassandra'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_generator_collector_connection(
                    vizd_obj.opserver.http_port)
        assert vizd_obj.verify_database_purge_request_limit()
        return True
    # end test_04_database_purge_query

    #@unittest.skip('Skipping test_05_verify_database_purge_with_percentage_input_with_redis_password')
    def test_05_verify_database_purge_with_percentage_input_with_redis_password(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        and checks if database purge functonality is
        is working properly
        '''
        logging.info("%%% test_05_database_purge_query %%%")
        if AnalyticsDbTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_password_port,
                             self.__class__.cassandra_port,
                             cassandra_user='cassandra',
                             cassandra_password='cassandra',
                             redis_password='contrail'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_generator_collector_connection(
                    vizd_obj.opserver.http_port)
        assert vizd_obj.verify_database_purge_with_percentage_input()
        return True
    # end test_05_database_purge_query

    #@unittest.skip('Skipping test_06_verify_database_purge_support_utc_time_format_with_redis_password')
    def test_06_verify_database_purge_support_utc_time_format_with_redis_password(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        and checks if database purge functonality is
        is working properly
        '''
        logging.info("%%% test_06_database_purge_query %%%")
        if AnalyticsDbTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_password_port,
                             self.__class__.cassandra_port,
                             cassandra_user='cassandra',
                             cassandra_password='cassandra',
                             redis_password='contrail'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_generator_collector_connection(
                    vizd_obj.opserver.http_port)
        assert vizd_obj.verify_database_purge_support_utc_time_format()
        return True
    # end test_06_database_purge_query

    #@unittest.skip('Skipping test_07_verify_database_purge_support_datetime_format_with_redis_password')
    def test_07_verify_database_purge_support_datetime_format_with_redis_password(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        and checks if database purge functonality is
        is working properly
        '''
        logging.info("%%% test_07_database_purge_query %%%")
        if AnalyticsDbTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_password_port,
                             self.__class__.cassandra_port,
                             cassandra_user='cassandra',
                             cassandra_password='cassandra',
                             redis_password='contrail'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_generator_collector_connection(
                    vizd_obj.opserver.http_port)
        assert vizd_obj.verify_database_purge_support_datetime_format()
        return True
    # end test_07_database_purge_query

    #@unittest.skip('Skipping test_08_verify_database_purge_support_deltatime_format_with_redis_password')
    def test_08_verify_database_purge_support_deltatime_format_with_redis_password(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        and checks if database purge functonality is
        is working properly
        '''
        logging.info("%%% test_08_database_purge_query %%%")
        if AnalyticsDbTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_password_port,
                             self.__class__.cassandra_port,
                             cassandra_user='cassandra',
                             cassandra_password='cassandra',
                             redis_password='contrail'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_generator_collector_connection(
                    vizd_obj.opserver.http_port)
        assert vizd_obj.verify_database_purge_support_deltatime_format()
        return True
    # end test_08_database_purge_query

    #@unittest.skip('Skipping test_09_verify_database_purge_request_limit_with_redis_password')
    def test_09_verify_database_purge_request_limit_with_redis_password(self):
        '''
        This test starts redis,vizd,opserver and qed
        It uses the test class' cassandra instance
        and checks if database purge functonality is
        is working properly
        '''
        logging.info("%%% test_09_database_purge_query %%%")
        if AnalyticsDbTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_password_port,
                             self.__class__.cassandra_port,
                             cassandra_user='cassandra',
                             cassandra_password='cassandra',
                             redis_password='contrail'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        assert vizd_obj.verify_generator_collector_connection(
                    vizd_obj.opserver.http_port)
        assert vizd_obj.verify_database_purge_request_limit()
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

# end class AnalyticsDbTest

def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT,_term_handler)
    unittest.main(catchbreak=True)
