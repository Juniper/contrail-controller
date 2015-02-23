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
from utils.util import obj_to_dict
from utils.analytics_fixture import AnalyticsFixture
from utils.generator_fixture import GeneratorFixture
from mockredis import mockredis
from mockzoo import mockzoo
import logging
import time
from opserver.sandesh.viz.constants import *
from opserver.sandesh.viz.constants import _OBJECT_TABLES
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')


class AnalyticsUveTest(testtools.TestCase, fixtures.TestWithFixtures):

    @classmethod
    def setUpClass(cls):
        if AnalyticsUveTest._check_skip_test() is True:
            return

        if (os.getenv('LD_LIBRARY_PATH', '').find('build/lib') < 0):
            if (os.getenv('DYLD_LIBRARY_PATH', '').find('build/lib') < 0):
                assert(False)

        cls.redis_port = AnalyticsUveTest.get_free_port()
        mockredis.start_redis(
            cls.redis_port, builddir+'/testroot/bin/redis-server')
        cls.zk_port = AnalyticsUveTest.get_free_port()
        mockzoo.start_zoo(cls.zk_port)

    @classmethod
    def tearDownClass(cls):
        if AnalyticsUveTest._check_skip_test() is True:
            return

        mockredis.stop_redis(cls.redis_port)
        mockzoo.stop_zoo(cls.zk_port)

    #@unittest.skip('Skipping non-cassandra test with vizd')
    def test_00_nocassandra(self):
        '''
        This test starts redis,vizd,opserver and qed
        Then it checks that the collector UVE (via redis)
        can be accessed from opserver.
        '''
        logging.info("*** test_00_nocassandra ***")
        if AnalyticsUveTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, self.__class__.redis_port, 0)) 
        assert vizd_obj.verify_on_setup()

        return True
    # end test_00_nocassandra

    #@unittest.skip('Skipping VM UVE test')
    def test_01_vm_uve(self):
        '''
        This test starts redis, vizd, opserver, qed, and a python generator
        that simulates vrouter and sends UveVirtualMachineAgentTrace messages.
        Then it checks that the VM UVE (via redis) can be accessed from
        opserver.
        '''
        logging.info("*** test_01_vm_uve ***")
        if AnalyticsUveTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, self.__class__.redis_port, 0))
        assert vizd_obj.verify_on_setup()
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture("contrail-vrouter-agent", collectors,
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
        # Generate VM with vm_id containing XML control character
        generator_obj.send_vm_uve(vm_id='<abcd&>', num_vm_ifs=2, msg_count=2)
        assert generator_obj.verify_vm_uve(vm_id='<abcd&>', num_vm_ifs=2,
                                           msg_count=2)
        return True
    # end test_01_vm_uve

    #@unittest.skip('Skipping VM UVE test')
    def test_02_vm_uve_with_password(self):
        '''
        This test starts redis, vizd, opserver, qed, and a python generator
        that simulates vrouter and sends UveVirtualMachineAgentTrace messages.
        Then it checks that the VM UVE (via redis) can be accessed from
        opserver.
        '''
        logging.info("*** test_02_vm_uve_with_password ***")
        if AnalyticsUveTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, -1, 0,
                             redis_password='contrail'))
        assert vizd_obj.verify_on_setup()
        collectors = [vizd_obj.get_collector()]
        generator_obj = self.useFixture(
            GeneratorFixture("contrail-vrouter-agent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert generator_obj.verify_on_setup()
        generator_obj.send_vm_uve(vm_id='abcd',
                                  num_vm_ifs=5,
                                  msg_count=5)
        assert generator_obj.verify_vm_uve(vm_id='abcd',
                                           num_vm_ifs=5,
                                           msg_count=5)
        return True
    # end test_02_vm_uve_with_password

    #@unittest.skip('verify redis-uve restart')
    def test_03_redis_uve_restart(self):
        logging.info('*** test_03_redis_uve_restart ***')

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir, -1, 0))
        self.verify_uve_resync(vizd_obj)
    # end test_03_redis_uve_restart

    #@unittest.skip('verify redis-uve restart')
    def test_04_redis_uve_restart_with_password(self):
        logging.info('*** test_03_redis_uve_restart_with_password ***')

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging,
                             builddir, -1, 0,
                             redis_password='contrail'))
        self.verify_uve_resync(vizd_obj)
        return True
    # end test_04_redis_uve_restart

    def verify_uve_resync(self, vizd_obj):
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

    #@unittest.skip('Skipping contrail-collector HA test')
    def test_05_collector_ha(self):
        logging.info('*** test_05_collector_ha ***')
        if AnalyticsUveTest._check_skip_test() is True:
            return True
        
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, -1, 0,
                             collector_ha_test=True))
        assert vizd_obj.verify_on_setup()
        # OpServer, AlarmGen and QE are started with collectors[0] as 
        # primary and collectors[1] as secondary
        exp_genlist = ['contrail-collector', 'contrail-analytics-api',
                       'contrail-query-engine']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0], 
                                              exp_genlist)
        # start the contrail-vrouter-agent with collectors[1] as primary and 
        # collectors[0] as secondary 
        collectors = [vizd_obj.collectors[1].get_addr(), 
                      vizd_obj.collectors[0].get_addr()]
        vr_agent = self.useFixture(
            GeneratorFixture("contrail-vrouter-agent", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert vr_agent.verify_on_setup()
        exp_genlist = ['contrail-collector', 'contrail-vrouter-agent']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[1], 
                                              exp_genlist)
        # stop collectors[0] and verify that OpServer, AlarmGen and QE switch 
        # from primary to secondary collector
        vizd_obj.collectors[0].stop()
        exp_genlist = ['contrail-collector', 'contrail-vrouter-agent',
                       'contrail-analytics-api',
                       'contrail-query-engine']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[1], 
                                              exp_genlist)
        # start collectors[0]
        vizd_obj.collectors[0].start()
        exp_genlist = ['contrail-collector']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0],
                                              exp_genlist)
        # verify that the old UVEs are flushed from redis when collector restarts
        exp_genlist = [vizd_obj.collectors[0].get_generator_id()]
        assert vizd_obj.verify_generator_list_in_redis(\
                                vizd_obj.collectors[0].get_redis_uve(),
                                exp_genlist)

        # stop collectors[1] and verify that OpServer, AlarmGen and QE switch 
        # from secondary to primary and contrail-vrouter-agent from primary to
        # secondary
        vizd_obj.collectors[1].stop()
        exp_genlist = ['contrail-collector', 'contrail-vrouter-agent',
                       'contrail-analytics-api',
                       'contrail-query-engine']
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

        # stop Opserver , AlarmGen and QE 
        vizd_obj.opserver.stop()
        vizd_obj.query_engine.stop()
        exp_genlist = ['contrail-collector', 'contrail-vrouter-agent']
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
        exp_genlist = ['contrail-collector', 'contrail-vrouter-agent',
                       'contrail-analytics-api',
                       'contrail-query-engine']
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
        exp_genlist = ['contrail-collector', 'contrail-vrouter-agent',
                       'contrail-analytics-api',
                       'contrail-query-engine']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[1],
                                              exp_genlist)
        assert vr_agent.verify_vm_uve(vm_id='abcd-1234-efgh-5678',
                                      num_vm_ifs=5, msg_count=5)
    # end test_05_collector_ha

    #@unittest.skip('Skipping AlarmGen basic test')
    def test_06_alarmgen_basic(self):
        '''
        This test starts the analytics processes.
        It enables partition 0 on alarmgen, and confirms
        that it got enabled
        '''
        logging.info("*** test_06_alarmgen_basic ***")
        if AnalyticsUveTest._check_skip_test() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, self.__class__.redis_port, 0,
                             kafka_zk = self.__class__.zk_port))
        assert vizd_obj.verify_on_setup()

        assert(vizd_obj.set_alarmgen_partition(0,1) == 'true')
        assert(vizd_obj.verify_alarmgen_partition(0,'true'))
        assert(vizd_obj.set_alarmgen_partition(1,1) == 'true')
        assert(vizd_obj.set_alarmgen_partition(2,1) == 'true')
        assert(vizd_obj.set_alarmgen_partition(3,1) == 'true')
        assert(vizd_obj.verify_uvetable_alarm("ObjectCollectorInfo",
            "ObjectCollectorInfo:" + socket.gethostname(), "ProcessStatus"))
        return True
    # end test_06_alarmgen_basic

    #@unittest.skip('Skipping Alarm test')
    def test_07_alarm(self):
        '''
        This test starts redis, collectors, analytics-api and
        python generators that simulates alarm generator. This
        test sends alarms from alarm generators and verifies the
        retrieval of alarms from analytics-api.
        '''
        logging.info('*** test_07_alarm ***')
        # collector_ha_test flag is set to True, because we wanna test
        # retrieval of alarms across multiple redis servers.
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, -1, 0,
                             collector_ha_test=True))
        assert vizd_obj.verify_on_setup()

        # create alarm-generator and attach it to the first collector.
        collectors = [vizd_obj.collectors[0].get_addr(), 
                      vizd_obj.collectors[1].get_addr()]
        alarm_gen1 = self.useFixture(
            GeneratorFixture('contrail-alarm-gen', [collectors[0]], logging,
                             None, hostname=socket.gethostname()+'_1'))
        alarm_gen1.verify_on_setup()

        # send process state alarm for analytics-node
        alarms = alarm_gen1.create_process_state_alarm(
                    'contrail-query-engine')
        alarms += alarm_gen1.create_process_state_alarm(
                    'contrail-snmp-collector')
        alarm_gen1.send_alarm(socket.gethostname()+'_1', alarms,
                              COLLECTOR_INFO_TABLE)
        analytics_tbl = _OBJECT_TABLES[COLLECTOR_INFO_TABLE].log_query_name

        # send proces state alarm for control-node
        alarms = alarm_gen1.create_process_state_alarm('contrail-dns')
        alarm_gen1.send_alarm('<&'+socket.gethostname()+'_1>', alarms,
                              BGP_ROUTER_TABLE)
        control_tbl = _OBJECT_TABLES[BGP_ROUTER_TABLE].log_query_name

        # create another alarm-generator and attach it to the second collector.
        alarm_gen2 = self.useFixture(
            GeneratorFixture('contrail-alarm-gen', [collectors[1]], logging,
                             None, hostname=socket.gethostname()+'_2'))
        alarm_gen2.verify_on_setup()
        
        # send process state alarm for analytics-node
        alarms = alarm_gen2.create_process_state_alarm(
                    'contrail-topology')
        alarm_gen2.send_alarm(socket.gethostname()+'_2', alarms,
                              COLLECTOR_INFO_TABLE)

        keys = [socket.gethostname()+'_1', socket.gethostname()+'_2']
        assert(vizd_obj.verify_alarms_table(analytics_tbl, keys))
        assert(vizd_obj.verify_alarm(analytics_tbl, keys[0], obj_to_dict(
            alarm_gen1.alarms[COLLECTOR_INFO_TABLE][keys[0]].data)))
        assert(vizd_obj.verify_alarm(analytics_tbl, keys[1], obj_to_dict(
            alarm_gen2.alarms[COLLECTOR_INFO_TABLE][keys[1]].data)))

        keys = ['<&'+socket.gethostname()+'_1>']
        assert(vizd_obj.verify_alarms_table(control_tbl, keys))
        assert(vizd_obj.verify_alarm(control_tbl, keys[0], obj_to_dict(
            alarm_gen1.alarms[BGP_ROUTER_TABLE][keys[0]].data)))

        # delete analytics-node alarm generated by alarm_gen2
        alarm_gen2.delete_alarm(socket.gethostname()+'_2',
                                COLLECTOR_INFO_TABLE)

        # verify analytics-node alarms
        keys = [socket.gethostname()+'_1']
        assert(vizd_obj.verify_alarms_table(analytics_tbl, keys))
        assert(vizd_obj.verify_alarm(analytics_tbl, keys[0], obj_to_dict(
            alarm_gen1.alarms[COLLECTOR_INFO_TABLE][keys[0]].data)))
        assert(vizd_obj.verify_alarm(analytics_tbl,
            socket.gethostname()+'_2', {}))
       
        # Disconnect alarm_gen1 from Collector and verify that all
        # alarms generated by alarm_gen1 is removed by the Collector. 
        alarm_gen1.disconnect_from_collector()
        assert(vizd_obj.verify_alarms_table(analytics_tbl, []))
        assert(vizd_obj.verify_alarm(analytics_tbl,
            socket.gethostname()+'_1', {}))
        assert(vizd_obj.verify_alarms_table(control_tbl, []))
        assert(vizd_obj.verify_alarm(control_tbl,
            '<&'+socket.gethostname()+'_1', {}))

        # update analytics-node alarm in disconnect state
        alarms = alarm_gen1.create_process_state_alarm(
                    'contrail-snmp-collector')
        alarm_gen1.send_alarm(socket.gethostname()+'_1', alarms,
                              COLLECTOR_INFO_TABLE)
        
        # Connect alarm_gen1 to Collector and verify that all
        # alarms generated by alarm_gen1 is synced with Collector.
        alarm_gen1.connect_to_collector()
        keys = [socket.gethostname()+'_1']
        assert(vizd_obj.verify_alarms_table(analytics_tbl, keys))
        assert(vizd_obj.verify_alarm(analytics_tbl, keys[0], obj_to_dict(
            alarm_gen1.alarms[COLLECTOR_INFO_TABLE][keys[0]].data)))
        
        keys = ['<&'+socket.gethostname()+'_1>']
        assert(vizd_obj.verify_alarms_table(control_tbl, keys))
        assert(vizd_obj.verify_alarm(control_tbl, keys[0], obj_to_dict(
            alarm_gen1.alarms[BGP_ROUTER_TABLE][keys[0]].data)))
    # end test_07_alarm

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
