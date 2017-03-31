#!/usr/bin/env python

#
# analytics_perftest.py
#
# System tests for analytics
#
# Created by Anish Mehta on 05/16/2013
#
# Copyright (c) 2013, Contrail Systems, Inc. All rights reserved.
#

import os
import sys
from gevent import monkey; monkey.patch_all()
import subprocess
import shutil
import glob
import unittest
import testtools
import fixtures
import socket
from utils.analytics_fixture import AnalyticsFixture
from utils.generator_fixture import GeneratorFixture
from utils.opserver_introspect_utils import VerificationOpsSrv
from mockcassandra import mockcassandra
import logging
import time
import json
from opserver.sandesh.viz.constants import *
from utils.util import find_buildroot

logging.basicConfig(level=logging.INFO,
                            format='%(asctime)s %(levelname)s %(message)s')
builddir = find_buildroot(os.getcwd())

class cd:
    """Context manager for changing the current working directory"""
    def __init__(self, newPath):
        self.newPath = newPath
    def __enter__(self):
        self.savedPath = os.getcwd()
        os.chdir(self.newPath)
    def __exit__(self, etype, value, traceback):
        os.chdir(self.savedPath)

class AnalyticsTest(testtools.TestCase, fixtures.TestWithFixtures):
    @classmethod
    def setUpClass(cls):
        if AnalyticsTest._check_skip_test() == True:
            return

        if (os.getenv('LD_LIBRARY_PATH','').find('build/lib') < 0):
            if (os.getenv('DYLD_LIBRARY_PATH','').find('build/lib') < 0):
                assert(False)
        cls.cassandra_port = AnalyticsTest.get_free_port()
        # the sstableloader seems to work only if we have storage port is
        # default 7000 - needs to be investigated
        cls.storage_port = 7000
        cls._cassbase, cls._basefile = mockcassandra.start_cassandra(cls.cassandra_port, cls.storage_port)

    @classmethod
    def tearDownClass(cls):
        if AnalyticsTest._check_skip_test() == True:
            return
        mockcassandra.stop_cassandra(cls.cassandra_port)
        pass

    @unittest.skip('Skipping test_00_startup performance test')
    def test_00_startup(self):
        '''
        This test loads the pre existing data into cassandra and does
        queries to opserver
        The idea is to use this to monitor/improve qed performance
        '''
        logging.info("%%% test_00_startup %%%")
        if AnalyticsTest._check_skip_test() == True:
            return True

        vizd_obj = self.useFixture(AnalyticsFixture(logging, \
            builddir, self.__class__.cassandra_port, True))
        assert vizd_obj.verify_on_setup()

        assert AnalyticsTest._load_data_into_cassandra(self.__class__.cassandra_port)

        #'SystemObjectTable' is not getting the updated timestamp, hence we are hardcoding
        # analytics_start_time
        #
        #pool = ConnectionPool(COLLECTOR_KEYSPACE, ['127.0.0.1:%s'
        #                      % (self.__class__.cassandra_port)])
        #col_family = ColumnFamily(pool, SYSTEM_OBJECT_TABLE)
        #analytics_info_row = col_family.get(SYSTEM_OBJECT_ANALYTICS)
        #if analytics_info_row[SYSTEM_OBJECT_START_TIME]:
        #    analytics_start_time = analytics_info_row[SYSTEM_OBJECT_START_TIME]
        #else:
        #    assert False

        analytics_start_time = 1387254916542720

        vizd_obj.query_engine.start(analytics_start_time)

        opserver_port = vizd_obj.get_opserver_port()

        vns = VerificationOpsSrv('127.0.0.1', opserver_port)
        
        # message table
        a_query = Query(table="MessageTable",
                start_time=analytics_start_time+5*60*1000000,
                end_time=analytics_start_time+10*60*1000000,
                select_fields=["MessageTS", "Source", "ModuleId", "Messagetype", "Xmlmessage"],
                sort_fields = ["MessageTS"],
                sort = 1)
        json_qstr = json.dumps(a_query.__dict__)
        t1=time.time();
        res = vns.post_query_json(json_qstr)
        t2=time.time();
        logging.info("Result Length: "+str(len(res)))
        logging.info("Query: "+json_qstr)
        logging.info("Time(s): "+str(t2-t1))

        # flow series table aggregation on a tuple
        a_query = Query(table="FlowSeriesTable",
                start_time=analytics_start_time+40*60*1000000,
                end_time=analytics_start_time+100*60*1000000,
                select_fields=["sourcevn", "sourceip", "destvn", "destip", "sum(bytes)"])
        json_qstr = json.dumps(a_query.__dict__)
        t1=time.time();
        res = vns.post_query_json(json_qstr)
        t2=time.time();
        logging.info("Result Length: "+str(len(res)))
        logging.info("Query: "+json_qstr)
        logging.info("Time(s): "+str(t2-t1))

        # flow series table port distribution table
        a_query = Query(table="FlowSeriesTable",
                start_time=analytics_start_time+40*60*1000000,
                end_time=analytics_start_time+100*60*1000000,
                select_fields=["dport", "protocol", "flow_count", "sum(bytes)"],
                sort=2,
                sort_fields=["sum(bytes)"],
                where=[[{"name": "protocol", "value": 1, "op": 1},
                {"name": "sourcevn", "value": "default-domain:demo:vn0", "op": 1}],
                [{"name": "protocol", "value": 6, "op": 1},
                {"name": "sourcevn", "value": "default-domain:demo:vn0", "op": 1}],
                [{"name": "protocol", "value": 17, "op": 1},
                {"name": "sourcevn", "value": "default-domain:demo:vn0", "op": 1}]])
        json_qstr = json.dumps(a_query.__dict__)
        t1=time.time();
        res = vns.post_query_json(json_qstr)
        t2=time.time();
        logging.info("Result Length: "+str(len(res)))
        logging.info("Query: "+json_qstr)
        logging.info("Time(s): "+str(t2-t1))

        # flow series map
        a_query = Query(table="FlowSeriesTable",
                start_time=analytics_start_time+40*60*1000000,
                end_time=analytics_start_time+100*60*1000000,
                select_fields=["sum(bytes)", "sum(packets)", "T=7", "sourcevn", "flow_count"],
                where=[[{"name": "sourcevn", "value": "default-domain:demo:vn0", "op": 1}]])
        json_qstr = json.dumps(a_query.__dict__)
        t1=time.time();
        res = vns.post_query_json(json_qstr)
        t2=time.time();
        logging.info("Result Length: "+str(len(res)))
        logging.info("Query: "+json_qstr)
        logging.info("Time(s): "+str(t2-t1))

        return True

    @staticmethod 
    def get_free_port():
        cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        cs.bind(("",0))
        cport = cs.getsockname()[1]
        cs.close() 
        return cport

    @staticmethod 
    def _check_skip_test():
        if (socket.gethostname() == 'build01'):
            logging.info("Skipping test")
            return True
        return False

    @staticmethod
    def _load_data_into_cassandra(cassandra_port):
        files_to_load = ['a3s28-120', 'a3s30-120']

        with cd(AnalyticsTest._cassbase):
            for file in files_to_load:
                ret_code = subprocess.call(['tar', 'xzf', builddir+'/opserver/test/data/'+file+'.tgz'])
                if ret_code != 0:
                    return False
                sstableloader = AnalyticsTest._cassbase + AnalyticsTest._basefile + \
                    '/bin/sstableloader ' + \
                    '--no-progress --nodes localhost --port ' + str(cassandra_port)
                for dir in glob.glob(file+'/ContrailAnalytics/*'):
                    ret_code = subprocess.call(sstableloader.split(' ') + [dir])
                    if ret_code != 0:
                        return False
        return True

if __name__ == '__main__':
    unittest.main()
