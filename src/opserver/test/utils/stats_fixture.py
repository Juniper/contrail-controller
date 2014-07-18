#!/usr/bin/env python

#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

#
# stats_fixture.py
#
# Python generator test fixtures
#

from gevent import monkey
monkey.patch_all()
import fixtures
import socket
from util import retry
from pysandesh.sandesh_base import *
from sandesh.stats_test.ttypes import *
from sandesh.qe.ttypes import *
from analytics_fixture import AnalyticsFixture
from generator_introspect_utils import VerificationGenerator
from opserver_introspect_utils import VerificationOpsSrv

class StatsFixture(fixtures.Fixture):

    def __init__(self, name, collectors, logger,
                 opserver_port, start_time=None, node_type="Test", hostname=socket.gethostname()):
        #import pdb; pdb.set_trace()
        self._hostname = hostname
        self._name = name
        self._logger = logger
        self._collectors = collectors
        self._opserver_port = opserver_port
        self._start_time = start_time
        self._node_type = node_type
    # end __init__

    def setUp(self):
        super(StatsFixture, self).setUp()
        #import pdb; pdb.set_trace()
        self._sandesh_instance = Sandesh()
        self._http_port = AnalyticsFixture.get_free_port()
        self._sandesh_instance.init_generator(
            self._name, self._hostname, self._node_type, "0", self._collectors,
            '', self._http_port, sandesh_req_uve_pkg_list=['sandesh'])
        self._sandesh_instance.set_logging_params(enable_local_log=True,
                                                  level=SandeshLevel.UT_DEBUG)
    # end setUp

    def cleanUp(self):
        self._sandesh_instance._client._connection.set_admin_state(down=True)
        super(StatsFixture, self).cleanUp()
    # end tearDown

    @retry(delay=2, tries=5)
    def verify_on_setup(self):
        try:
            vg = VerificationGenerator('127.0.0.1', self._http_port)
            conn_status = vg.get_collector_connection_status()
        except:
            return False
        else:
            return conn_status['status'] == "Established"
    # end verify_on_setup

    def send_test_stat(self,nm,l1,s1,i1,d1,s2="",i2=0,d2=0):
        self._logger.info('Sending Test Stats tags %s, %i, %d' % (s1,i1, d1))
        tstat = StatTest()
        tstat.s1 = s1
        tstat.s2 = s2
        tstat.i1 = i1
        tstat.i2 = i2
        tstat.d1 = d1
        tstat.d2 = d2
        tstate = StatTestState(st = [tstat])
        tstate.name = nm
        tstate.l1 = l1
        tdata = StatTestTrace(
            data=tstate,
            sandesh=self._sandesh_instance)
        tdata.send(sandesh=self._sandesh_instance)

    def send_test_stat_dynamic(self,nm,s1,i1,d1,s2="",i2=0,d2=0):
        self._logger.info('Sending Test Stats tags %s, %i, %f' % (s1,i1, d1))
        tstat = TestStat()
        tstat.s1 = s1
        tstat.s2 = s2
        tstat.i1 = i1
        tstat.i2 = i2
        tstat.d1 = d1
        tstat.d2 = d2
        tstate = TestState(ts = [tstat])
        tstate.name = nm
        tdata = TestStateTrace(
            data=tstate,
            sandesh=self._sandesh_instance)
        tdata.send(sandesh=self._sandesh_instance)       
    
    @retry(delay=1, tries=5)
    def verify_test_stat(self, table, stime, select_fields, where_clause, num, 
                         check_rows = None, sort_fields=None, filt=None):
        self._logger.info("verify_test_stat")
        vns = VerificationOpsSrv('127.0.0.1', self._opserver_port)
        res = vns.post_query(table, start_time=stime, end_time='now',
                             select_fields=select_fields,
                             where_clause=where_clause,
                             sort_fields=sort_fields, 
                             filter=filt)
        self._logger.info('Recieved %s' % str(res))
        if (len(res) != num):
            self._logger.info("Found %d rows, expected %d" % (len(res),num))
            return False
        if check_rows is None:
            return True
        self._logger.info('Checking against %s' % str(check_rows))
        for crow in check_rows:
            match = False
            for row in res:
                for k in crow.keys():
                    if k in row:
                        if (crow[k] != row[k]):
                            self._logger.error('Expected %s : %s got %s : %s' % 
                                 (str(k), str(crow[k]), str(k), str(row[k])))
                            break
                    else:
                        break
                else:
                    match = True
                    break
            if match is False:
                self._logger.info("Could not match %s" % str(crow))
                return False
        return True

# end class StatsFixture
