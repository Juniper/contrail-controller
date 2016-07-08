#!/usr/bin/env python

#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_uflow_test.py
#
# Underlay flow collector functionality - sflow, ipfix
#

import gevent
from gevent import monkey;monkey.patch_all()
import sys
import os
import socket
import signal
import logging
import unittest
import testtools
import fixtures
from collections import OrderedDict

from mockcassandra import mockcassandra
from mockredis import mockredis
from utils.analytics_fixture import AnalyticsFixture
from utils.opserver_introspect_utils import VerificationOpsSrv
from utils.util import retry, find_buildroot

builddir = find_buildroot(os.getcwd())

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')

class AnalyticsUFlowTest(testtools.TestCase, fixtures.TestWithFixtures):

    @classmethod
    def setUpClass(cls):
        if (os.getenv('LD_LIBRARY_PATH', '').find('build/lib') < 0):
            if (os.getenv('DYLD_LIBRARY_PATH', '').find('build/lib') < 0):
                assert(False)

        cls.cassandra_port = AnalyticsFixture.get_free_port()
        mockcassandra.start_cassandra(cls.cassandra_port)
        cls.redis_port = AnalyticsFixture.get_free_port()
        mockredis.start_redis(cls.redis_port)
    # end setUpClass

    @classmethod
    def tearDownClass(cls):
        mockcassandra.stop_cassandra(cls.cassandra_port)
        mockredis.stop_redis(cls.redis_port)
    # end tearDownClass

    @retry(delay=1, tries=5)
    def verify_uflow(self, vizd_obj, flow_type, exp_res):
        logging.info('verify <%s> data' % (flow_type))
        vns = VerificationOpsSrv('127.0.0.1', vizd_obj.get_opserver_port())
        res = vns.post_query('StatTable.UFlowData.flow',
            start_time='-1m', end_time='now',
            select_fields=['name', 'flow.flowtype', 'flow.sip', 'flow.sport'],
            where_clause = 'name=*')
        res = sorted([OrderedDict(sorted(r.items())) \
                     for r in res if r['flow.flowtype'] == flow_type])
        exp_res = sorted([OrderedDict(sorted(r.items())) for r in exp_res])
        logging.info('Expected Result: ' + str(exp_res))
        logging.info('Result: ' + str(res))
        if res != exp_res:
            return False
        return True
    # end verify_uflow

    #@unittest.skip('test_ipfix')
    def test_ipfix(self):
        '''
        This test starts redis, vizd, opserver and qed
        It uses the test class' cassandra instance
        Then it feeds IPFIX packets to the collector
        and queries for them
        '''

        logging.info('%%% test_ipfix %%%')
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port,
                             ipfix_port = True))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()

        ipfix_ip = '127.0.0.1'
        ipfix_port = vizd_obj.collectors[0].get_ipfix_port()
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        f1 = open(builddir + '/opserver/test/data/ipfix_t.data')
        sock.sendto(f1.read(), (ipfix_ip, ipfix_port))
        f2 = open(builddir + '/opserver/test/data/ipfix_d.data')
        sock.sendto(f2.read(), (ipfix_ip, ipfix_port))
        sock.close()

        uexp = [{'name': '127.0.0.1',
                 'flow.sport': 49152,
                 'flow.sip': '10.84.45.254',
                 'flow.flowtype': 'IPFIX'}
               ]
        assert(self.verify_uflow(vizd_obj, 'IPFIX', uexp))
    # end test_ipfix

    #@unittest.skip('test_sflow')
    def test_sflow(self):
        '''
        This test injects sFlow packets to the collector
        and verifies that the sFlow data is properly added
        in the analytics db
        '''

        logging.info('%%% test_sflow %%%')
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir,
                             self.__class__.redis_port,
                             self.__class__.cassandra_port,
                             sflow_port=True))
        assert vizd_obj.verify_on_setup()
        assert vizd_obj.verify_collector_obj_count()

        sflow_ip = '127.0.0.1'
        sflow_port = vizd_obj.collectors[0].get_sflow_port()
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sflow_files = ['sflow1.data', 'sflow2.data']
        for f in sflow_files:
            fd = open(builddir + '/opserver/test/data/' + f)
            sock.sendto(fd.read(), (sflow_ip, sflow_port))
            fd.close()
        sock.close()

        exp_res = [
                    {'name': '127.0.0.1',
                     'flow.flowtype': 'SFLOW',
                     'flow.sip': '10.204.217.24',
                     'flow.sport': 38287},
                    {'name': '127.0.0.1',
                     'flow.flowtype': 'SFLOW',
                     'flow.sip': '127.0.0.1',
                     'flow.sport': 3306},
                    {'name': '127.0.0.1',
                     'flow.flowtype': 'SFLOW',
                     'flow.sip': '127.0.0.1',
                     'flow.sport': 8086}
                  ]
        assert(self.verify_uflow(vizd_obj, 'SFLOW', exp_res))
    # end test_sflow

# end class AnalyticsUFlowTest

def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT,_term_handler)
    unittest.main(catchbreak=True)
