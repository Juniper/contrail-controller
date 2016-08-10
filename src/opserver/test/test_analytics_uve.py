#!/usr/bin/env python

#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

#
# analytics_uvetest.py
#
# UVE and Alarm tests
#

import os
import sys
import threading
threading._DummyThread._Thread__stop = lambda x: 42
import signal
import gevent
from gevent import monkey
monkey.patch_all()
import unittest
import testtools
import fixtures
import socket
from utils.util import obj_to_dict, find_buildroot
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
import platform

logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s %(levelname)s %(message)s')
builddir = find_buildroot(os.getcwd())


class AnalyticsUveTest(testtools.TestCase, fixtures.TestWithFixtures):

    @classmethod
    def setUpClass(cls):

        if (os.getenv('LD_LIBRARY_PATH', '').find('build/lib') < 0):
            if (os.getenv('DYLD_LIBRARY_PATH', '').find('build/lib') < 0):
                assert(False)

        cls.redis_port = AnalyticsUveTest.get_free_port()
        mockredis.start_redis(cls.redis_port)

    @classmethod
    def tearDownClass(cls):

        mockredis.stop_redis(cls.redis_port)

    #@unittest.skip('Skipping non-cassandra test with vizd')
    def test_00_nocassandra(self):
        '''
        This test starts redis,vizd,opserver and qed
        Then it checks that the collector UVE (via redis)
        can be accessed from opserver.
        '''
        logging.info("%%% test_00_nocassandra %%%")

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
        logging.info("%%% test_01_vm_uve %%%")

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
        logging.info("%%% test_02_vm_uve_with_password %%%")

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
        logging.info('%%% test_03_redis_uve_restart %%%')

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, -1, 0,
            start_kafka = True))
        assert vizd_obj.verify_on_setup()

        collectors = [vizd_obj.get_collector()]
        alarm_gen1 = self.useFixture(
            GeneratorFixture('vrouter-agent', collectors, logging,
                             None, hostname=socket.gethostname()))
        alarm_gen1.verify_on_setup()

        # send vrouter UVE without build_info !!!
        # check for PartialSysinfo alarm
        alarm_gen1.send_vrouterinfo("myvrouter1")
        assert(vizd_obj.verify_uvetable_alarm("ObjectVRouter",
            "ObjectVRouter:myvrouter1", "partial-sysinfo-compute"))

        self.verify_uve_resync(vizd_obj)
 
        # Alarm should return after redis restart
        assert(vizd_obj.verify_uvetable_alarm("ObjectVRouter",
            "ObjectVRouter:myvrouter1", "partial-sysinfo-compute"))

        # should there be a return True here?
    # end test_03_redis_uve_restart

    #@unittest.skip('verify redis-uve restart')
    def test_04_redis_uve_restart_with_password(self):
        logging.info('%%% test_03_redis_uve_restart_with_password %%%')

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
        logging.info('%%% test_05_collector_ha %%%')
        
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

        # stop QE 
        vizd_obj.query_engine.stop()
        exp_genlist = ['contrail-collector', 'contrail-vrouter-agent',
                       'contrail-analytics-api']
        assert vizd_obj.verify_generator_list(vizd_obj.collectors[0],
                                              exp_genlist)

        # verify the generator list in redis
        exp_genlist = [vizd_obj.collectors[0].get_generator_id(),
                       vizd_obj.opserver.get_generator_id(),
                       vr_agent.get_generator_id()]
        assert vizd_obj.verify_generator_list_in_redis(\
                                vizd_obj.collectors[0].get_redis_uve(),
                                exp_genlist)

        # start a python generator and QE with collectors[1] as the primary and
        # collectors[0] as the secondary. On generator startup, verify 
        # that they connect to the secondary collector, if the 
        # connection to the primary fails
        vr2_collectors = [vizd_obj.collectors[1].get_addr(), 
                          vizd_obj.collectors[0].get_addr()]
        vr2_agent = self.useFixture(
            GeneratorFixture("contrail-snmp-collector", collectors,
                             logging, vizd_obj.get_opserver_port()))
        assert vr2_agent.verify_on_setup()
        vizd_obj.query_engine.set_primary_collector(
                            vizd_obj.collectors[1].get_addr())
        vizd_obj.query_engine.set_secondary_collector(
                            vizd_obj.collectors[0].get_addr())
        vizd_obj.query_engine.start()
        exp_genlist = ['contrail-collector', 'contrail-vrouter-agent',
                       'contrail-analytics-api', 'contrail-snmp-collector',
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
                       'contrail-analytics-api', 'contrail-snmp-collector',
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
        logging.info("%%% test_06_alarmgen_basic %%%")

        if AnalyticsUveTest._check_skip_kafka() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, self.__class__.redis_port, 0,
            start_kafka = True))
        assert vizd_obj.verify_on_setup()

        assert(vizd_obj.verify_uvetable_alarm("ObjectCollectorInfo",
            "ObjectCollectorInfo:" + socket.gethostname(), "process-status"))
        # setup generator for sending Vrouter build_info
        collector = vizd_obj.collectors[0].get_addr()
        alarm_gen1 = self.useFixture(
            GeneratorFixture('vrouter-agent', [collector], logging,
                             None, hostname=socket.gethostname()))
        alarm_gen1.verify_on_setup()

        # send vrouter UVE without build_info !!!
        # check for PartialSysinfo alarm
        alarm_gen1.send_vrouterinfo("myvrouter1")
        assert(vizd_obj.verify_uvetable_alarm("ObjectVRouter",
            "ObjectVRouter:myvrouter1", "partial-sysinfo-compute",
            rules=[{"and_list": [{
                "condition": {
                    "operation": "==",
                    "operand1": "ObjectVRouter.build_info",
                    "operand2": {
                        "json_value": "null"
                    }
                },
                "match": [{"json_operand1_value": "null"}]
            }]}]
        ))

        # Now try to clear the alarm by sending build_info
        alarm_gen1.send_vrouterinfo("myvrouter1", b_info = True)
        assert(vizd_obj.verify_uvetable_alarm("ObjectVRouter",
            "ObjectVRouter:myvrouter1", "partial-sysinfo-compute", is_set = False))

        # send vrouter UVE without build_info !!!
        # check for PartialSysinfo alarm
        alarm_gen1.send_vrouterinfo("myvrouter1", deleted = True)
        alarm_gen1.send_vrouterinfo("myvrouter1")
        assert(vizd_obj.verify_uvetable_alarm("ObjectVRouter",
            "ObjectVRouter:myvrouter1", "partial-sysinfo-compute"))

        # Now try to clear the alarm by deleting the UVE
        alarm_gen1.send_vrouterinfo("myvrouter1", deleted = True)
        assert(vizd_obj.verify_uvetable_alarm("ObjectVRouter",
            "ObjectVRouter:myvrouter1", "partial-sysinfo-compute", is_set = False))

        alarm_gen2 = self.useFixture(
            GeneratorFixture('vrouter-agent', [collector], logging,
                             None, hostname=socket.gethostname(), inst = "1"))
        alarm_gen2.verify_on_setup()

        # send vrouter UVE without build_info !!!
        # check for PartialSysinfo alarm
        alarm_gen2.send_vrouterinfo("myvrouter2")
        assert(vizd_obj.verify_uvetable_alarm("ObjectVRouter",
            "ObjectVRouter:myvrouter2", "partial-sysinfo-compute"))

        # Now try to clear the alarm by disconnecting the generator
        alarm_gen2._sandesh_instance._client._connection.set_admin_state(\
            down=True)
        assert(vizd_obj.verify_uvetable_alarm("ObjectVRouter",
            "ObjectVRouter:myvrouter2", "partial-sysinfo-compute", is_set = False))
         
        # send vrouter UVE of myvrouter without build_info again !!!
        # check for PartialSysinfo alarm
        alarm_gen1.send_vrouterinfo("myvrouter1")
        assert(vizd_obj.verify_uvetable_alarm("ObjectVRouter",
            "ObjectVRouter:myvrouter1", "partial-sysinfo-compute"))

        # Verify that we can give up partition ownership 
        assert(vizd_obj.set_alarmgen_partition(0,0) == 'true')
        assert(vizd_obj.verify_alarmgen_partition(0,'false'))

        # Give up the other partitions
        assert(vizd_obj.set_alarmgen_partition(1,0) == 'true')
        assert(vizd_obj.set_alarmgen_partition(2,0) == 'true')
        assert(vizd_obj.set_alarmgen_partition(3,0) == 'true')

        # Confirm that alarms are all gone
        assert(vizd_obj.verify_uvetable_alarm("ObjectVRouter",
            None, None))

        # Get the partitions again
        assert(vizd_obj.set_alarmgen_partition(0,1) == 'true')
        assert(vizd_obj.set_alarmgen_partition(1,1) == 'true')
        assert(vizd_obj.set_alarmgen_partition(2,1) == 'true')
        assert(vizd_obj.set_alarmgen_partition(3,1) == 'true')
        assert(vizd_obj.verify_alarmgen_partition(0,'true'))

        # The PartialSysinfo alarm om myvrouter should return
        assert(vizd_obj.verify_uvetable_alarm("ObjectVRouter",
            "ObjectVRouter:myvrouter1", "partial-sysinfo-compute"))

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
        logging.info('%%% test_07_alarm %%%')

        if AnalyticsUveTest._check_skip_kafka() is True:
            return True

        # collector_ha_test flag is set to True, because we wanna test
        # retrieval of alarms across multiple redis servers.
        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, -1, 0,
                             collector_ha_test=True,
                             start_kafka = True))
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
        assert(vizd_obj.verify_alarm_list_include(analytics_tbl,
                                          expected_alarms=keys))
        assert(vizd_obj.verify_alarm(analytics_tbl, keys[0], obj_to_dict(
            alarm_gen1.alarms[COLLECTOR_INFO_TABLE][keys[0]].data)))
        assert(vizd_obj.verify_alarm(analytics_tbl, keys[1], obj_to_dict(
            alarm_gen2.alarms[COLLECTOR_INFO_TABLE][keys[1]].data)))

        keys = ['<&'+socket.gethostname()+'_1>']
        assert(vizd_obj.verify_alarm_list_include(control_tbl, expected_alarms=keys))
        assert(vizd_obj.verify_alarm(control_tbl, keys[0], obj_to_dict(
            alarm_gen1.alarms[BGP_ROUTER_TABLE][keys[0]].data)))

        # delete analytics-node alarm generated by alarm_gen2
        alarm_gen2.delete_alarm(socket.gethostname()+'_2',
                                COLLECTOR_INFO_TABLE)

        # verify analytics-node alarms
        keys = [socket.gethostname()+'_1']
        assert(vizd_obj.verify_alarm_list_include(analytics_tbl,
            expected_alarms=keys))
        ukeys = [socket.gethostname()+'_2']
        assert(vizd_obj.verify_alarm_list_exclude(analytics_tbl,
            unexpected_alms=ukeys))
        assert(vizd_obj.verify_alarm(analytics_tbl, keys[0], obj_to_dict(
            alarm_gen1.alarms[COLLECTOR_INFO_TABLE][keys[0]].data)))
        assert(vizd_obj.verify_alarm(analytics_tbl, ukeys[0], {}))
       
        # Disconnect alarm_gen1 from Collector and verify that all
        # alarms generated by alarm_gen1 is removed by the Collector. 
        alarm_gen1.disconnect_from_collector()
        ukeys = [socket.gethostname()+'_1']
        assert(vizd_obj.verify_alarm_list_exclude(analytics_tbl,
            unexpected_alms=ukeys))
        assert(vizd_obj.verify_alarm(analytics_tbl, ukeys[0], {}))

        ukeys = ['<&'+socket.gethostname()+'_1']
        assert(vizd_obj.verify_alarm_list_exclude(control_tbl,
            unexpected_alms=ukeys))
        assert(vizd_obj.verify_alarm(control_tbl, ukeys[0], {}))

        # update analytics-node alarm in disconnect state
        alarms = alarm_gen1.create_process_state_alarm(
                    'contrail-snmp-collector')
        alarm_gen1.send_alarm(socket.gethostname()+'_1', alarms,
                              COLLECTOR_INFO_TABLE)
        
        # Connect alarm_gen1 to Collector and verify that all
        # alarms generated by alarm_gen1 is synced with Collector.
        alarm_gen1.connect_to_collector()
        keys = [socket.gethostname()+'_1']
        assert(vizd_obj.verify_alarm_list_include(analytics_tbl, 
            expected_alarms=keys))
        assert(vizd_obj.verify_alarm(analytics_tbl, keys[0], obj_to_dict(
            alarm_gen1.alarms[COLLECTOR_INFO_TABLE][keys[0]].data)))
        
        keys = ['<&'+socket.gethostname()+'_1>']
        assert(vizd_obj.verify_alarm_list_include(control_tbl,
            expected_alarms=keys))
        assert(vizd_obj.verify_alarm(control_tbl, keys[0], obj_to_dict(
            alarm_gen1.alarms[BGP_ROUTER_TABLE][keys[0]].data)))
    # end test_07_alarm

    #@unittest.skip('Skipping UVE/Alarm Filter test')
    def test_08_uve_alarm_filter(self):
        '''
        This test verifies the filter options kfilt, sfilt, mfilt and cfilt
        in the UVE/Alarm GET and POST methods.
        '''
        logging.info('%%% test_08_uve_alarm_filter %%%')

        if AnalyticsUveTest._check_skip_kafka() is True:
            return True

        vizd_obj = self.useFixture(
            AnalyticsFixture(logging, builddir, -1, 0,
                collector_ha_test=True, start_kafka = True))
        assert vizd_obj.verify_on_setup()

        collectors = [vizd_obj.collectors[0].get_addr(),
                      vizd_obj.collectors[1].get_addr()]
        api_server_name = socket.gethostname()+'_1'
        api_server = self.useFixture(
            GeneratorFixture('contrail-api', [collectors[0]], logging,
                             None, node_type='Config',
                             hostname=api_server_name))
        vr_agent_name = socket.gethostname()+'_2'
        vr_agent = self.useFixture(
            GeneratorFixture('contrail-vrouter-agent', [collectors[1]],
                             logging, None, node_type='Compute',
                             hostname=vr_agent_name))
        alarm_gen1_name = socket.gethostname()+'_1'
        alarm_gen1 = self.useFixture(
            GeneratorFixture('contrail-alarm-gen', [collectors[0]], logging,
                             None, node_type='Analytics',
                             hostname=alarm_gen1_name))
        alarm_gen2_name = socket.gethostname()+'_3'
        alarm_gen2 = self.useFixture(
            GeneratorFixture('contrail-alarm-gen', [collectors[1]], logging,
                             None, node_type='Analytics',
                             hostname=alarm_gen2_name))
        api_server.verify_on_setup()
        vr_agent.verify_on_setup()
        alarm_gen1.verify_on_setup()
        alarm_gen2.verify_on_setup()

        vn_list = ['default-domain:project1:vn1',
                   'default-domain:project1:vn2',
                   'default-domain:project2:vn1',
                   'default-domain:project2:vn1&']
        # generate UVEs for the filter test
        api_server.send_vn_config_uve(name=vn_list[0],
                                      partial_conn_nw=[vn_list[1]],
                                      num_acl_rules=2)
        api_server.send_vn_config_uve(name=vn_list[1],
                                      num_acl_rules=3)
        vr_agent.send_vn_agent_uve(name=vn_list[1], num_acl_rules=3,
                                   ipkts=2, ibytes=1024)
        vr_agent.send_vn_agent_uve(name=vn_list[2], ipkts=4, ibytes=128)
        vr_agent.send_vn_agent_uve(name=vn_list[3], ipkts=8, ibytes=256)
        # generate Alarms for the filter test
        alarms = alarm_gen1.create_alarm('InPktsThreshold')
        alarms += alarm_gen1.create_alarm('InBytesThreshold', ack=True)
        alarm_gen1.send_alarm(vn_list[1], alarms, VN_TABLE)
        alarms = alarm_gen2.create_alarm('ConfigNotPresent', ack=False)
        alarm_gen2.send_alarm(vn_list[2], alarms, VN_TABLE)
        alarms = alarm_gen2.create_alarm('ConfigNotPresent', ack=False)
        alarm_gen2.send_alarm(vn_list[3], alarms, VN_TABLE)

        filt_test = [
            # no filter
            {
                'uve_list_get': [
                    'default-domain:project1:vn1',
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'get_alarms': {
                    'virtual-network': [
                         {  'name' : 'default-domain:project1:vn2',
                            'value' : { 'UVEAlarms': { 
                                'alarms': [
                                    {
                                        'type': 'InPktsThreshold',
                                    },
                                    {
                                        'type': 'InBytesThreshold',
                                        'ack': True
                                    }
                                ]
                            } }
                         },
                         {  'name' : 'default-domain:project2:vn1',
                            'value' : { 'UVEAlarms': {
                                'alarms': [
                                    {
                                        'type': 'ConfigNotPresent',
                                        'ack': False
                                    }
                                ]
                            } }
                         },
                         {  'name' : 'default-domain:project2:vn1&',
                            'value' : { 'UVEAlarms': { 
                                'alarms': [
                                    {
                                        'type': 'ConfigNotPresent',
                                        'ack': False
                                    }
                                ]
                            } }
                         },
                     ]
                },
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn1',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'partially_connected_networks': [
                                        'default-domain:project1:vn2'
                                    ],
                                    'total_acl_rules': 2
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 2,
                                    'in_bytes': 1024,
                                    'total_acl_rules': 3
                                },
                                'UveVirtualNetworkConfig': {
                                    'total_acl_rules': 3
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'InPktsThreshold',
                                        },
                                        {
                                            'type': 'InBytesThreshold',
                                            'ack': True
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 4,
                                    'in_bytes': 128
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 8,
                                    'in_bytes': 256
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },

            # kfilt
            {
                'kfilt': ['*'],
                'uve_list_get': [
                    'default-domain:project1:vn1',
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn1',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'partially_connected_networks': [
                                        'default-domain:project1:vn2'
                                    ],
                                    'total_acl_rules': 2
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 2,
                                    'in_bytes': 1024,
                                    'total_acl_rules': 3
                                },
                                'UveVirtualNetworkConfig': {
                                    'total_acl_rules': 3
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'InPktsThreshold',
                                        },
                                        {
                                            'type': 'InBytesThreshold',
                                            'ack': True
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 4,
                                    'in_bytes': 128
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 8,
                                    'in_bytes': 256
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },
            {
                'kfilt': ['default-domain:project1:*',
                          'default-domain:project2:*'],
                'uve_list_get': [
                    'default-domain:project1:vn1',
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn1',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'partially_connected_networks': [
                                        'default-domain:project1:vn2'
                                    ],
                                    'total_acl_rules': 2
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 2,
                                    'in_bytes': 1024,
                                    'total_acl_rules': 3
                                },
                                'UveVirtualNetworkConfig': {
                                    'total_acl_rules': 3
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'InPktsThreshold',
                                        },
                                        {
                                            'type': 'InBytesThreshold',
                                            'ack': True
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 4,
                                    'in_bytes': 128
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 8,
                                    'in_bytes': 256
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },
            {
                'kfilt': ['default-domain:project1:vn1',
                          'default-domain:project2:*'],
                'uve_list_get': [
                    'default-domain:project1:vn1',
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn1',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'partially_connected_networks': [
                                        'default-domain:project1:vn2'
                                    ],
                                    'total_acl_rules': 2
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 4,
                                    'in_bytes': 128
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 8,
                                    'in_bytes': 256
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },
            {
                'kfilt': [
                    'default-domain:project2:*',
                    'invalid-vn:*'
                ],
                'get_alarms': {
                    'virtual-network': [
                         {  'name' : 'default-domain:project2:vn1',
                            'value' : { 'UVEAlarms': { 
                                'alarms': [
                                    {
                                        'type': 'ConfigNotPresent',
                                        'ack': False
                                    }
                                ]
                            } }
                         },
                         {  'name' : 'default-domain:project2:vn1&',
                            'value' : { 'UVEAlarms': {
                                'alarms': [
                                    {
                                        'type': 'ConfigNotPresent',
                                        'ack': False
                                    }
                                ]
                            } }
                         },
                     ]
                },
                'uve_list_get': [
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 4,
                                    'in_bytes': 128
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 8,
                                    'in_bytes': 256
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },
            {
                'kfilt': [
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1&',
                    'invalid-vn'
                ],
                'uve_list_get': [
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 2,
                                    'in_bytes': 1024,
                                    'total_acl_rules': 3
                                },
                                'UveVirtualNetworkConfig': {
                                    'total_acl_rules': 3
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'InPktsThreshold',
                                        },
                                        {
                                            'type': 'InBytesThreshold',
                                            'ack': True
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 8,
                                    'in_bytes': 256
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },
            {
                'kfilt': ['invalid-vn'],
                'uve_list_get': [],
                'uve_get_post': {'value': []},
            },

            # sfilt
            {
                'sfilt': socket.gethostname()+'_1',
                'uve_list_get': [
                    'default-domain:project1:vn1',
                    'default-domain:project1:vn2'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn1',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'partially_connected_networks': [
                                        'default-domain:project1:vn2'
                                    ],
                                    'total_acl_rules': 2
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'total_acl_rules': 3
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'InPktsThreshold',
                                        },
                                        {
                                            'type': 'InBytesThreshold',
                                            'ack': True
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },
            {
                'sfilt': socket.gethostname()+'_3',
                'uve_list_get': [
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },
            {
                'sfilt': 'invalid_source',
                'uve_list_get': [],
                'uve_get_post': {'value': []},
            },

            # mfilt
            {
                'mfilt': 'Config:contrail-api:0',
                'uve_list_get': [
                    'default-domain:project1:vn1',
                    'default-domain:project1:vn2'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn1',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'partially_connected_networks': [
                                        'default-domain:project1:vn2'
                                    ],
                                    'total_acl_rules': 2
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'total_acl_rules': 3
                                }
                            }
                        }
                    ]
                },
            },
            {
                'mfilt': 'Analytics:contrail-alarm-gen:0',
                'uve_list_get': [
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'InPktsThreshold',
                                        },
                                        {
                                            'type': 'InBytesThreshold',
                                            'ack': True
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },
            {
                'mfilt': 'Analytics:contrail-invalid:0',
                'uve_list_get': [],
                'uve_get_post': {'value': []},
            },

            # cfilt
            {
                'cfilt': ['UveVirtualNetworkAgent'],
                'uve_list_get': [
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 2,
                                    'in_bytes': 1024,
                                    'total_acl_rules': 3
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 4,
                                    'in_bytes': 128
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 8,
                                    'in_bytes': 256
                                }
                            }
                        }
                    ]
                },
            },
            {
                'cfilt': [
                    'UveVirtualNetworkAgent:total_acl_rules',
                    'UveVirtualNetworkConfig:partially_connected_networks'
                ],
                'uve_list_get': [
                    'default-domain:project1:vn1',
                    'default-domain:project1:vn2'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn1',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'partially_connected_networks': [
                                        'default-domain:project1:vn2'
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'total_acl_rules': 3
                                }
                            }
                        }
                    ]
                },
            },
            {
                'cfilt': [
                    'UveVirtualNetworkConfig:invalid',
                    'UveVirtualNetworkAgent:in_tpkts',
                    'UVEAlarms:alarms'
                ],
                'uve_list_get': [
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 2,
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'InPktsThreshold',
                                        },
                                        {
                                            'type': 'InBytesThreshold',
                                            'ack': True
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 4,
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 8,
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },
            {
                'cfilt': [
                    'UveVirtualNetworkAgent:invalid',
                    'UVEAlarms:invalid_alarms',
                    'invalid'
                ],
                'uve_list_get': [],
                'uve_get_post': {'value': []},
            },

            # ackfilt
            {
                'ackfilt': True,
                'uve_list_get': [
                    'default-domain:project1:vn1',
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn1',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'partially_connected_networks': [
                                        'default-domain:project1:vn2'
                                    ],
                                    'total_acl_rules': 2
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 2,
                                    'in_bytes': 1024,
                                    'total_acl_rules': 3
                                },
                                'UveVirtualNetworkConfig': {
                                    'total_acl_rules': 3
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'InBytesThreshold',
                                            'ack': True
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 4,
                                    'in_bytes': 128
                                },
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 8,
                                    'in_bytes': 256
                                },
                            }
                        }
                    ]
                },
            },
            {
                'ackfilt': False,
                'uve_list_get': [
                    'default-domain:project1:vn1',
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'get_alarms': {
                    'virtual-network': [
                         {  'name' : 'default-domain:project1:vn2',
                            'value' : { 'UVEAlarms': { 
                                'alarms': [
                                    {
                                        'type': 'InPktsThreshold',
                                    },
                                ]
                            } }
                         },
                         {  'name' : 'default-domain:project2:vn1',
                            'value' : { 'UVEAlarms': { 
                                'alarms': [
                                    {
                                        'type': 'ConfigNotPresent',
                                        'ack': False
                                    }
                                ]
                            } }
                         },
                         {  'name' : 'default-domain:project2:vn1&',
                            'value' : { 'UVEAlarms': {
                                'alarms': [
                                    {
                                        'type': 'ConfigNotPresent',
                                        'ack': False
                                    }
                                ]
                            } }
                         },
                     ]
                },
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn1',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'partially_connected_networks': [
                                        'default-domain:project1:vn2'
                                    ],
                                    'total_acl_rules': 2
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 2,
                                    'in_bytes': 1024,
                                    'total_acl_rules': 3
                                },
                                'UveVirtualNetworkConfig': {
                                    'total_acl_rules': 3
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'InPktsThreshold',
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 4,
                                    'in_bytes': 128
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 8,
                                    'in_bytes': 256
                                },
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },

            # kfilt + sfilt
            {
                'kfilt': [
                    'default-domain:project1:*',
                    'default-domain:project2:vn1',
                    'default-domain:invalid'
                ],
                'sfilt': socket.gethostname()+'_2',
                'uve_list_get': [
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 2,
                                    'in_bytes': 1024,
                                    'total_acl_rules': 3
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 4,
                                    'in_bytes': 128
                                }
                            }
                        }
                    ]
                },
            },

            # kfilt + sfilt + ackfilt
            {
                'kfilt': [
                    'default-domain:project1:vn1',
                    'default-domain:project2:*',
                    'default-domain:invalid'
                ],
                'sfilt': socket.gethostname()+'_2',
                'ackfilt': True,
                'uve_list_get': [
                    'default-domain:project2:vn1',
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 4,
                                    'in_bytes': 128
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project2:vn1&',
                            'value': {
                                'UveVirtualNetworkAgent': {
                                    'in_tpkts': 8,
                                    'in_bytes': 256
                                }
                            }
                        }
                    ]
                },
            },

            # kfilt + sfilt + cfilt
            {
                'kfilt': [
                    'default-domain:project1:vn1',
                    'default-domain:project1:vn2',
                    'default-domain:project2:vn1'
                ],
                'sfilt': socket.gethostname()+'_1',
                'cfilt': [
                    'UveVirtualNetworkAgent',
                    'UVEAlarms',
                    'UveVirtualNetworkConfig:Invalid'
                ],
                'uve_list_get': [
                    'default-domain:project1:vn2'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'InPktsThreshold',
                                        },
                                        {
                                            'type': 'InBytesThreshold',
                                            'ack': True
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },

            # kfilt + mfilt + cfilt
            {
                'kfilt': ['*'],
                'mfilt': 'Config:contrail-api:0',
                'cfilt': [
                    'UveVirtualNetworkAgent',
                    'UVEAlarms:alarms'
                ],
                'uve_list_get': [],
                'uve_get_post': {'value': []},
            },

            # kfilt + sfilt + mfilt + cfilt
            {
                'kfilt': [
                    'default-domain:project1:vn1',
                    'default-domain:project1:vn2',
                    'default-domain:project2:*'
                ],
                'sfilt': socket.gethostname()+'_1',
                'mfilt': 'Config:contrail-api:0',
                'cfilt': [
                    'UveVirtualNetworkConfig:partially_connected_networks',
                    'UveVirtualNetworkConfig:total_acl_rules',
                    'UVEAlarms'
                ],
                'uve_list_get': [
                    'default-domain:project1:vn1',
                    'default-domain:project1:vn2'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project1:vn1',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'partially_connected_networks': [
                                        'default-domain:project1:vn2'
                                    ],
                                    'total_acl_rules': 2
                                }
                            }
                        },
                        {
                            'name': 'default-domain:project1:vn2',
                            'value': {
                                'UveVirtualNetworkConfig': {
                                    'total_acl_rules': 3
                                },
                            }
                        }
                    ]
                },
            },
            {
                'kfilt': [
                    'default-domain:project1:*',
                    'default-domain:project2:vn1',
                    'default-domain:project2:invalid'
                ],
                'sfilt': socket.gethostname()+'_3',
                'mfilt': 'Analytics:contrail-alarm-gen:0',
                'cfilt': [
                    'UveVirtualNetworkConfig',
                    'UVEAlarms:alarms',
                    'UveVirtualNetworkAgent'
                ],
                'uve_list_get': [
                    'default-domain:project2:vn1'
                ],
                'uve_get_post': {
                    'value': [
                        {
                            'name': 'default-domain:project2:vn1',
                            'value': {
                                'UVEAlarms': {
                                    'alarms': [
                                        {
                                            'type': 'ConfigNotPresent',
                                            'ack': False
                                        }
                                    ]
                                }
                            }
                        }
                    ]
                },
            },

            # kfilt + sfilt + mfilt + cfilt + ackfilt
            {
                'kfilt': [
                    'default-domain:project1:*',
                    'default-domain:project2:vn1&',
                    'default-domain:project2:invalid'
                ],
                'sfilt': socket.gethostname()+'_3',
                'mfilt': 'Analytics:contrail-alarm-gen:0',
                'cfilt': [
                    'UveVirtualNetworkConfig',
                    'UVEAlarms:alarms',
                    'UveVirtualNetworkAgent'
                ],
                'ackfilt': True,
                'uve_list_get': [
                    'default-domain:project2:vn1&'
                ],
                'uve_get_post': {'value': []},
            }
        ]

        vn_table = _OBJECT_TABLES[VN_TABLE].log_query_name

        for i in range(len(filt_test)):
            filters = dict(kfilt=filt_test[i].get('kfilt'),
                           sfilt=filt_test[i].get('sfilt'),
                           mfilt=filt_test[i].get('mfilt'),
                           cfilt=filt_test[i].get('cfilt'),
                           ackfilt=filt_test[i].get('ackfilt'))
            assert(vizd_obj.verify_uve_list(vn_table,
                filts=filters, exp_uve_list=filt_test[i]['uve_list_get']))
            assert(vizd_obj.verify_multi_uve_get(vn_table,
                filts=filters, exp_uves=filt_test[i]['uve_get_post']))
            assert(vizd_obj.verify_uve_post(vn_table,
                filts=filters, exp_uves=filt_test[i]['uve_get_post']))
            if 'get_alarms' in filt_test[i]:
                filters['tablefilt'] = 'virtual-network'
                assert(vizd_obj.verify_get_alarms(vn_table,
                    filts=filters, exp_uves=filt_test[i]['get_alarms']))
    # end test_08_uve_alarm_filter

    @staticmethod
    def get_free_port():
        cs = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        cs.bind(("", 0))
        cport = cs.getsockname()[1]
        cs.close()
        return cport

    @staticmethod
    def _check_skip_kafka():
      
        (PLATFORM, VERSION, EXTRA) = platform.linux_distribution()
        if PLATFORM.lower() == 'ubuntu':
            if VERSION.find('12.') == 0:
                return True
        if PLATFORM.lower() == 'centos':
            if VERSION.find('6.') == 0:
                return True
        return False

def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT,_term_handler)
    unittest.main(catchbreak=True)
