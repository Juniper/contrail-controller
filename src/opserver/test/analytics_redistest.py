#!/usr/bin/env python

#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_redistest.py
#
# System tests for analytics redis
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
from mockcassandra import mockcassandra
import logging
import time
from opserver.sandesh.viz.constants import *

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')

class AnalyticsTest(testtools.TestCase, fixtures.TestWithFixtures):

    @classmethod
    def setUpClass(cls):
        if (os.getenv('LD_LIBRARY_PATH', '').find('build/lib') < 0):
            if (os.getenv('DYLD_LIBRARY_PATH', '').find('build/lib') < 0):
                assert(False)

        cls.cassandra_port = AnalyticsTest.get_free_port()
        mockcassandra.start_cassandra(cls.cassandra_port)

    @classmethod
    def tearDownClass(cls):
        mockcassandra.stop_cassandra(cls.cassandra_port)
        pass

    #@unittest.skip('verify source/module list')
    def test_00_table_source_module_list(self):
        '''
        This test verifies /analytics/table/<table>/column-values/Source
        and /analytics/table/<table>/column-values/ModuleId
        '''
        logging.info('*** test_00_source_module_list ***')

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.cassandra_port,
                             collector_ha_test=True, redis_password='contrail'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        exp_genlist1 = ['contrail-collector', 'contrail-analytics-api', 'contrail-query-engine']
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
    #end test_00_table_source_module_list

    #@unittest.skip('verify redis-uve restart')
    def test_01_redis_uve_restart(self):
        logging.info('*** test_01_redis_uve_restart ***')

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir,
                             self.__class__.cassandra_port,redis_password='contrail'))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_redis_uve_connection(
                            vizd_obj.collectors[0])
        assert vizd_obj.verify_opserver_redis_uve_connection(
                            vizd_obj.opserver)
        # verify redis-uve list
        host = socket.gethostname()
        gen_list = [host+':Analytics:contrail-collector:0',
                    host+':Analytics:contrail-query-engine:0',
                    host+':Analytics:contrail-analytics-api:0']
        assert vizd_obj.verify_generator_uve_list(gen_list)
        # stop redis-uve
        vizd_obj.redis_uves[0].stop()
        assert vizd_obj.verify_collector_redis_uve_connection(
                            vizd_obj.collectors[0], False)
        assert vizd_obj.verify_opserver_redis_uve_connection(
                            vizd_obj.opserver, False)
        # start redis-uve and verify that contrail-collector and Opserver are
        # connected to the redis-uve
        vizd_obj.redis_uves[0].start()
        assert vizd_obj.verify_collector_redis_uve_connection(
                            vizd_obj.collectors[0])
        assert vizd_obj.verify_opserver_redis_uve_connection(
                            vizd_obj.opserver)
        # verify that UVEs are resynced with redis-uve
        assert vizd_obj.verify_generator_uve_list(gen_list)
    # end test_01_redis_uve_restart

    #@unittest.skip('verify source/module list')
    def test_02_table_source_module_list(self):
        '''
        This test verifies /analytics/table/<table>/column-values/Source
        and /analytics/table/<table>/column-values/ModuleId
        '''
        logging.info('*** test_02_source_module_list ***')

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.cassandra_port,
                             collector_ha_test=True))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()
        exp_genlist1 = ['contrail-collector', 'contrail-analytics-api', 'contrail-query-engine']
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
    #end test_02_table_source_module_list

    #@unittest.skip('verify redis-uve restart')
    def test_03_redis_uve_restart(self):
        logging.info('*** test_03_redis_uve_restart ***')

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
        gen_list = [host+':Analytics:contrail-collector:0',
                    host+':Analytics:contrail-query-engine:0',
                    host+':Analytics:contrail-analytics-api:0']
        assert vizd_obj.verify_generator_uve_list(gen_list)
        # stop redis-uve
        vizd_obj.redis_uves[0].stop()
        assert vizd_obj.verify_collector_redis_uve_connection(
                            vizd_obj.collectors[0], False)
        assert vizd_obj.verify_opserver_redis_uve_connection(
                            vizd_obj.opserver, False)
        # start redis-uve and verify that contrail-collector and Opserver are
        # connected to the redis-uve
        vizd_obj.redis_uves[0].start()
        assert vizd_obj.verify_collector_redis_uve_connection(
                            vizd_obj.collectors[0])
        assert vizd_obj.verify_opserver_redis_uve_connection(
                            vizd_obj.opserver)
        # verify that UVEs are resynced with redis-uve
        assert vizd_obj.verify_generator_uve_list(gen_list)
    # end test_03_redis_uve_restart

    @staticmethod
    def get_free_port():
        cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        cs.bind(("", 0))
        cport = cs.getsockname()[1]
        cs.close()
        return cport

def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT,_term_handler)
    unittest.main(catchbreak=True)

