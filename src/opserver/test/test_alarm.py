#!/usr/bin/env python

#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import gevent
import time
from gevent import monkey; monkey.patch_all()
import json
import signal
import copy
import logging
import mock
import unittest
import collections
from utils.util import retry
from collections import namedtuple
from kafka.common import OffsetAndMessage,Message

from pysandesh.util import UTCTimestampUsec
from pysandesh.gen_py.sandesh_alarm.ttypes import SandeshAlarmAckRequest, \
    SandeshAlarmAckResponseCode
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import \
    AlarmTemplate, AlarmElement, Operand1, Operand2, \
    UVEAlarmInfo, UVEAlarmConfig, UVEAlarms, AllOf
from opserver.sandesh.alarmgen_ctrl.ttypes import UVEAlarmOperState, \
    UVEAlarmStateMachineInfo, UVEAlarmState
from opserver.uveserver import UVEServer
from opserver.partition_handler import PartitionHandler, UveStreamProc, \
    UveStreamer, UveStreamPart, PartInfo
from opserver.alarmgen import Controller, AlarmStateMachine
from opserver.alarmgen_cfg import CfgParser

logging.basicConfig(level=logging.DEBUG,
    format='%(asctime)s %(levelname)s %(message)s')
logging.getLogger("stevedore.extension").setLevel(logging.WARNING)

TestStage = namedtuple("TestStage",["i","o"])
PartHandlerInput = namedtuple("PartHandlerInput",
    ["redis_instances", "get_part", "get_messages"])
PartHandlerOutput = namedtuple("PartHandlerOutput",
    ["callbacks", "uvedb"])

class TestChecker(object):
    @retry(delay=1, tries=3)
    def checker_dict(self,expected,actual,match=True):
        residual = actual
        matched = True
        result = False
        for elem in expected:
            if residual and elem in residual:
                if isinstance(residual,dict):
                    residual = residual[elem]
                else:
                    residual = None
            else:
                matched = False
        if match:
            result = matched
        else:
            result = not matched
        logging.info("dict exp %s actual %s match %s" % \
            (str(expected), str(actual), str(match)))
        return result
    
    @retry(delay=1, tries=3)
    def checker_exact(self,expected,actual,match=True):
        result = False
        logging.info("exact exp %s actual %s match %s" % \
            (str(expected), str(actual), str(match)))
        if expected == actual:
            return match
        else:
            result = not match
        return result

class Mock_base(collections.Callable,collections.MutableMapping):
    def __init__(self, *args, **kwargs):
        self.store = dict()
        self.update(dict(*args, **kwargs))

    def __getitem__(self, key):
        return self.store[key]

    def __setitem__(self, key, value):
        self.store[key] = value

    def __delitem__(self, key):
        del self.store[key]

    def __iter__(self):
        return iter(self.store)

    def __len__(self):
        return len(self.store)

class Mock_get_part(Mock_base):
    def __init__(self, *args, **kwargs):
        Mock_base.__init__(self, *args, **kwargs)

    def __call__(self, part, r_inst):
        key = (part, r_inst)
        if key not in self.store:
            return {}
        return self.store[key]
        
class Mock_get_uve(Mock_base):
    def __init__(self, *args, **kwargs):
        Mock_base.__init__(self, *args, **kwargs)

    def __call__(self, key, flat, filters):
        if key not in self.store:
            return False, {}
        return False, self.store[key]

class Mock_get_messages(Mock_base):
    def __init__(self, *args, **kwargs):
        Mock_base.__init__(self)

    def __call__(self, num, timeout):
        vals = []
        for key in self.store.keys():
            vals.append(self.store[key])
            del self.store[key]
        gevent.sleep(timeout)
        if len(vals):
            return vals
        else:
            return [None]

class Mock_agp(Mock_base):
    def __init__(self, *args, **kwargs):
        Mock_base.__init__(self, *args, **kwargs)

    def __call__(self):
        logging.info("Reading AGP %s" % str(self.store))
        val = self.store
        return val

class Mock_usp(object):
    def __init__(self, partno, logger, cb, pi, rpass, content,\
            tablefilt, cfilter, patterns):
        self._cb = cb
        self._partno = partno
        self._pi = pi
        self._started = False
        self._content = content

    def start(self):
        self._started = True

    def kill(self):
        self._started = False

    def __call__(self, key, type, value):
        if self._started:
            if not self._content:
                if not value is None:
                    value = {}
            self._cb(self._partno, self._pi, key, type, value) 

# Tests for UveStreamer and UveCache
class TestUveStreamer(unittest.TestCase, TestChecker):
    @classmethod
    def setUpClass(cls):
        pass

    @classmethod
    def tearDownClass(cls):
        pass
    
    def setUp(self):
        self.mock_agp = Mock_agp()
        self.ustr = UveStreamer(logging, None, None, self.mock_agp, None,\
                None, None, None, Mock_usp)
        self.ustr.start()
        self.mock_agp[0] = PartInfo(ip_address = "127.0.0.1", 
                                    acq_time = 666,
                                    instance_id = "0",
                                    port = 6379)
        self.mock_agp[1] = PartInfo(ip_address = "127.0.0.1", 
                                    acq_time = 777,
                                    instance_id = "0",
                                    port = 6379)

    def tearDown(self):
        self.ustr.kill()

    #@unittest.skip('Skipping UveStreamer')
    def test_00_init(self):
        self.assertTrue(self.checker_dict([0], self.ustr._parts))
        self.ustr._parts[0]("ObjectXX:uve1","type1",{"xx": 0})
        self.assertTrue(self.checker_dict(\
                ["ObjectXX","uve1","type1"],\
                self.ustr._uvedbcache._uvedb))
        self.assertTrue(self.checker_dict(\
                ["type1","ObjectXX","uve1"],\
                self.ustr._uvedbcache._typekeys))
        self.assertTrue(self.checker_dict(\
                [0,"ObjectXX:uve1"],\
                self.ustr._uvedbcache._partkeys))

        # remove partition. UVE should go too        
        del self.mock_agp[0]
        self.assertTrue(self.checker_dict(\
                ["ObjectXX","uve1"],\
                self.ustr._uvedbcache._uvedb, False))
        self.assertTrue(self.checker_dict(\
                ["type1"],\
                self.ustr._uvedbcache._typekeys, False))
        self.assertTrue(self.checker_exact(\
                set(),
                self.ustr._uvedbcache._partkeys[0]))

    #@unittest.skip('Skipping UveStreamer')
    def test_00_deluve(self):
        self.assertTrue(self.checker_dict([0], self.ustr._parts))
        self.ustr._parts[0]("ObjectXX:uve1","type1",{"xx": 0})
        self.assertTrue(self.checker_dict(\
                ["ObjectXX","uve1","type1"],\
                self.ustr._uvedbcache._uvedb))
        self.assertTrue(self.checker_dict(\
                ["type1","ObjectXX","uve1"],\
                self.ustr._uvedbcache._typekeys))
        self.assertTrue(self.checker_dict(\
                [0,"ObjectXX:uve1"],\
                self.ustr._uvedbcache._partkeys))

        # remove UVE
        self.ustr._parts[0]("ObjectXX:uve1",None,None)
        self.assertTrue(self.checker_dict(\
                ["ObjectXX","uve1"],\
                self.ustr._uvedbcache._uvedb, False))
        self.assertTrue(self.checker_dict(\
                ["type1","ObjectXX"],\
                self.ustr._uvedbcache._typekeys, False))
        self.assertTrue(self.checker_exact(\
                set(),
                self.ustr._uvedbcache._partkeys[0]))


# Tests for all AlarmGenerator code, using mocks for 
# external interfaces for UVEServer, Kafka, libpartition
# and Discovery
class TestAlarmGen(unittest.TestCase, TestChecker):
    @classmethod
    def setUpClass(cls):
        cls._pc = mock.patch('opserver.alarmgen.PartitionClient', autospec=True)
        cls._pc.start()
        cls._dc = mock.patch('opserver.alarmgen.client.DiscoveryClient', autospec=True)
        cls._dc.start()
        cls._kc = mock.patch('opserver.partition_handler.KafkaClient', autospec=True)
        cls._kc.start()
        cls._ac = mock.patch('opserver.alarmgen.KafkaClient', autospec=True)
        cls._ac.start()
        cls._sc = mock.patch('opserver.alarmgen.SimpleProducer', autospec=True)
        cls._sc.start()

    @classmethod
    def tearDownClass(cls):
        cls._dc.stop()
        cls._pc.stop()
        cls._kc.stop()
        cls._ac.stop()
        cls._sc.stop()
    
    def setUp(self):
        config = CfgParser('--http_server_port 0 '
                           '--zk_list 127.0.0.1:0 '
                           '--disc_server_ip 127.0.0.1 '
                           '--redis_server_port 0')
        config.parse()
        self._ag = Controller(config, logging)
        self._agtask = gevent.spawn(self._ag.run_uve_processing)

    def tearDown(self):
        self._agtask.kill()

    def create_test_alarm_info(self, table, name, alarm_type):
        or_list = []
        or_list.append([AllOf(all_of=[AlarmElement(\
            rule=AlarmTemplate(oper="!=",
                operand1=Operand1(keys=["dummytoken"]),
                operand2=Operand2(json_value=json.dumps('UP'))),
            json_operand1_value=json.dumps('DOWN'))])])
        uai = UVEAlarmInfo(type=alarm_type, severity=1,
                                  timestamp=UTCTimestampUsec(),
                                  token="dummytoken",
                                  any_of=or_list, ack=False)
        conf = UVEAlarmConfig()
        state = UVEAlarmOperState(state = UVEAlarmState.Active,
                                head_timestamp = 0, alarm_timestamp = [])
	uv = table + ':' + name
        alarm_info = AlarmStateMachine(tab = table, uv = uv, nm = \
		alarm_type, activeTimer = 0, idleTimer = 0, freqCheck_Times\
		= 0, freqCheck_Seconds = 0, freqExceededCheck = False, sandesh=self._ag._sandesh)
	alarm_info.set_uai(uai)
        return alarm_info
    # end create_test_alarm_info

    def add_test_alarm(self, table, name, atype):
        if not self._ag.tab_alarms.has_key(table):
            self._ag.tab_alarms[table] = {}
        key = table+':'+name
        if not self._ag.tab_alarms[table].has_key(key):
            self._ag.tab_alarms[table][key] = {}
        self._ag.tab_alarms[table][key][atype] = \
            self.create_test_alarm_info(table, name, atype)
    # end add_test_alarm

    def get_test_alarm(self, table, name, atype):
        key = table+':'+name
        return self._ag.tab_alarms[table][key][atype].get_uai(forced=True)
    # end get_test_alarm

    @mock.patch('opserver.alarmgen.Controller.reconnect_agg_uve')
    @mock.patch('opserver.alarmgen.Controller.clear_agg_uve')
    @mock.patch('opserver.alarmgen.Controller.send_agg_uve')
    @mock.patch.object(UVEServer, 'get_part')
    @mock.patch.object(UVEServer, 'get_uve')
    @mock.patch('opserver.partition_handler.SimpleConsumer', autospec=True)
    # Test partition Initialization, including boot-straping using UVEServer
    # Test partition shutdown as well
    def test_00_init(self,
            mock_SimpleConsumer,
            mock_get_uve, mock_get_part,
            mock_send_agg_uve, mock_clear_agg_uve, mock_reconnect_agg_uve):

        m_get_part = Mock_get_part() 
        m_get_part[(1,("127.0.0.1",0,0))] = "127.0.0.1:0", \
            { "gen1" :
                { "ObjectXX:uve1" : {"type1":{}}  }}
        mock_get_part.side_effect = m_get_part

        m_get_uve = Mock_get_uve()
        m_get_uve["ObjectXX:uve1"] = {"type1": {"xx": 0}}
        mock_get_uve.side_effect = m_get_uve

        m_get_messages = Mock_get_messages()
        mock_SimpleConsumer.return_value.get_messages.side_effect = \
            m_get_messages

        self._ag.disc_cb_coll([{"ip-address":"127.0.0.1","pid":0}])
        self._ag.libpart_cb([1])
        self.assertTrue(self.checker_dict([1, "ObjectXX", "uve1"], self._ag.ptab_info))
        self.assertTrue(self.checker_exact(\
            self._ag.ptab_info[1]["ObjectXX"]["uve1"].values(), {"type1" : {"xx": 0}}))

        # Shutdown partition
        self._ag.libpart_cb([])
        self.assertTrue(self.checker_dict([1, "ObjectXX", "uve1"],\
            self._ag.ptab_info, False))
        

    @mock.patch('opserver.alarmgen.Controller.reconnect_agg_uve')
    @mock.patch('opserver.alarmgen.Controller.clear_agg_uve')
    @mock.patch('opserver.alarmgen.Controller.send_agg_uve')
    @mock.patch.object(UVEServer, 'get_part')
    @mock.patch.object(UVEServer, 'get_uve')
    @mock.patch('opserver.partition_handler.SimpleConsumer', autospec=True)
    # Test initialization followed by read from Kafka
    # Also test for deletetion of a boot-straped UVE
    def test_01_rxmsg(self,
            mock_SimpleConsumer,
            mock_get_uve, mock_get_part,
            mock_send_agg_uve, mock_clear_agg_uve, mock_reconnect_agg_uve):

        m_get_part = Mock_get_part() 
        m_get_part[(1,("127.0.0.1",0,0))] = "127.0.0.1:0", \
            { "gen1" :
                { "ObjectXX:uve1" : {"type1":{}}  }}
        mock_get_part.side_effect = m_get_part

        # Boostraped UVE ObjectXX:uve1 is not present!
        m_get_uve = Mock_get_uve()
        m_get_uve["ObjectYY:uve2"] = {"type2": {"yy": 1}}
        mock_get_uve.side_effect = m_get_uve

        m_get_messages = Mock_get_messages()
        m_get_messages["ObjectYY:uve2"] = OffsetAndMessage(offset=0,
                    message=Message(magic=0, attributes=0,
                    key='ObjectYY:uve2|type2|gen1|127.0.0.1:0',
                    value='{}'))
        mock_SimpleConsumer.return_value.get_messages.side_effect = \
            m_get_messages

        self._ag.disc_cb_coll([{"ip-address":"127.0.0.1","pid":0}])
        self._ag.libpart_cb([1])
        self.assertTrue(self.checker_dict([1, "ObjectXX", "uve1"], self._ag.ptab_info, False))
        self.assertTrue(self.checker_dict([1, "ObjectYY", "uve2"], self._ag.ptab_info))
        self.assertTrue(self.checker_exact(\
            self._ag.ptab_info[1]["ObjectYY"]["uve2"].values(), {"type2" : {"yy": 1}}))

    @mock.patch('opserver.alarmgen.Controller.reconnect_agg_uve')
    @mock.patch('opserver.alarmgen.Controller.clear_agg_uve')
    @mock.patch('opserver.alarmgen.Controller.send_agg_uve')
    @mock.patch.object(UVEServer, 'get_part')
    @mock.patch.object(UVEServer, 'get_uve')
    @mock.patch('opserver.partition_handler.SimpleConsumer', autospec=True)
    # Test late bringup of collector
    # Also test collector shutdown
    def test_02_collectorha(self,
            mock_SimpleConsumer,
            mock_get_uve, mock_get_part,
            mock_send_agg_uve, mock_clear_agg_uve, mock_reconnect_agg_uve):

        m_get_part = Mock_get_part() 
        m_get_part[(1,("127.0.0.1",0,0))] = "127.0.0.1:0", \
            { "gen1" :
                { "ObjectXX:uve1" : { "type1":{} } }}
        m_get_part[(1,("127.0.0.5",0,0))] = "127.0.0.5:0", \
            { "gen1" :
                { "ObjectZZ:uve3" : { "type3":{}}  }}
        mock_get_part.side_effect = m_get_part

        m_get_uve = Mock_get_uve()
        m_get_uve["ObjectXX:uve1"] = {"type1": {"xx": 0}}
        m_get_uve["ObjectYY:uve2"] = {"type2": {"yy": 1}}
        m_get_uve["ObjectZZ:uve3"] = {"type3": {"zz": 2}}
        mock_get_uve.side_effect = m_get_uve

        # When this message is read, 127.0.0.5 will not be present
        m_get_messages = Mock_get_messages()
        m_get_messages["ObjectYY:uve2"] = OffsetAndMessage(offset=0,
                    message=Message(magic=0, attributes=0,
                    key='ObjectYY:uve2|type2|gen1|127.0.0.5:0',
                    value='{}'))
        mock_SimpleConsumer.return_value.get_messages.side_effect = \
            m_get_messages

        self._ag.disc_cb_coll([{"ip-address":"127.0.0.1","pid":0}])
        self._ag.libpart_cb([1])

        # Now bringup collector 127.0.0.5
        self.assertTrue(self.checker_dict([1, "ObjectZZ", "uve3"], self._ag.ptab_info, False))
        self._ag.disc_cb_coll([{"ip-address":"127.0.0.1","pid":0}, {"ip-address":"127.0.0.5","pid":0}])
        self.assertTrue(self.checker_dict([1, "ObjectZZ", "uve3"], self._ag.ptab_info))

        self.assertTrue(self.checker_dict([1, "ObjectYY", "uve2"], self._ag.ptab_info, False))
        # Feed the message in again
        m_get_messages["ObjectYY:uve2"] = OffsetAndMessage(offset=0,
                    message=Message(magic=0, attributes=0,
                    key='ObjectYY:uve2|type2|gen1|127.0.0.5:0',
                    value='{}'))
        self.assertTrue(self.checker_dict([1, "ObjectYY", "uve2"], self._ag.ptab_info))

        
        # Withdraw collector 127.0.0.1
        self.assertTrue(self.checker_dict([1, "ObjectXX", "uve1"], self._ag.ptab_info))
        del m_get_uve["ObjectXX:uve1"]
        self._ag.disc_cb_coll([{"ip-address":"127.0.0.5","pid":0}])
        self.assertTrue(self.checker_dict([1, "ObjectXX", "uve1"], self._ag.ptab_info, False))

    @mock.patch('opserver.alarmgen.AlarmTrace', autospec=True)
    def test_03_alarm_ack_callback(self, MockAlarmTrace):
        self._ag.tab_alarms = {}
        self.add_test_alarm('table1', 'name1', 'type1')
        self.add_test_alarm('table1', 'name1', 'type2')
	tab_alarms_copy = {}
	for tab in self._ag.tab_alarms.keys():
	    for uk,uv in self._ag.tab_alarms[tab].iteritems():
		for ak,av in uv.iteritems():
		    uai = av.get_uai(forced=True)
		    if uai:
			if not tab in tab_alarms_copy.keys():
			    tab_alarms_copy[tab] = {}
			if not uk in tab_alarms_copy[tab].keys():
			    tab_alarms_copy[tab][uk] = {}
        		tab_alarms_copy[tab][uk][ak] = copy.deepcopy(uai)

        TestCase = namedtuple('TestCase', ['name', 'input', 'output'])
        TestInput = namedtuple('TestInput', ['alarm_ack_req'])
        TestOutput = namedtuple('TestOutput', ['return_code', 'alarm_send',
                                               'ack_values'])

        tests = [
            TestCase(
                name='case 1: Invalid "table"',
                input=TestInput(
                    alarm_ack_req=SandeshAlarmAckRequest(table='invalid_table',
                        name='name1', type='type1',
                        timestamp=UTCTimestampUsec())),
                output=TestOutput(
                    return_code=SandeshAlarmAckResponseCode.ALARM_NOT_PRESENT,
                    alarm_send=False, ack_values=None)
            ),
            TestCase(
                name='case 2: Invalid "name"',
                input=TestInput(
                    alarm_ack_req=SandeshAlarmAckRequest(table='table1',
                        name='invalid_name', type='type1',
                        timestamp=UTCTimestampUsec())),
                output=TestOutput(
                    return_code=SandeshAlarmAckResponseCode.ALARM_NOT_PRESENT,
                    alarm_send=False, ack_values=None)
            ),
            TestCase(
                name='case 3: Invalid "type"',
                input=TestInput(
                    alarm_ack_req=SandeshAlarmAckRequest(table='table1',
                        name='name1', type='invalid_type',
                        timestamp=UTCTimestampUsec())),
                output=TestOutput(
                    return_code=SandeshAlarmAckResponseCode.ALARM_NOT_PRESENT,
                    alarm_send=False, ack_values=None)
            ),
            TestCase(
                name='case 4: Invalid "timestamp"',
                input=TestInput(
                    alarm_ack_req=SandeshAlarmAckRequest(table='table1',
                        name='name1', type='type1',
                        timestamp=UTCTimestampUsec())),
                output=TestOutput(
                    return_code=\
                        SandeshAlarmAckResponseCode.INVALID_ALARM_REQUEST,
                    alarm_send=False, ack_values=None)
            ),
            TestCase(
                name='case 5: Valid ack request',
                input=TestInput(
                    alarm_ack_req=SandeshAlarmAckRequest(table='table1',
                        name='name1', type='type2',
                        timestamp=self.get_test_alarm(
                            'table1', 'name1', 'type2').timestamp)),
                output=TestOutput(
                    return_code=SandeshAlarmAckResponseCode.SUCCESS,
                    alarm_send=True, ack_values={'type1':False, 'type2':True})
            ),
            TestCase(
                name='case 6: Duplicate ack request',
                input=TestInput(
                    alarm_ack_req=SandeshAlarmAckRequest(table='table1',
                        name='name1', type='type2',
                        timestamp=self.get_test_alarm(
                            'table1', 'name1', 'type2').timestamp)),
                output=TestOutput(
                    return_code=SandeshAlarmAckResponseCode.SUCCESS,
                    alarm_send=False, ack_values=None)
            ),
            TestCase(
                name='case 7: Valid ack request - different alarm type',
                input=TestInput(
                    alarm_ack_req=SandeshAlarmAckRequest(table='table1',
                        name='name1', type='type1',
                        timestamp=self.get_test_alarm(
                            'table1', 'name1', 'type1').timestamp)),
                output=TestOutput(
                    return_code=SandeshAlarmAckResponseCode.SUCCESS,
                    alarm_send=True, ack_values={'type1':True, 'type2':True})
            )
        ]

        self._ag.tab_alarms['table1']['table1:name1']['type1'].\
                    get_uas().state = UVEAlarmState.Active
        self._ag.tab_alarms['table1']['table1:name1']['type2'].\
                    get_uas().state = UVEAlarmState.Active
        for case in tests:
            logging.info('=== Test %s ===' % (case.name))
            return_code = self._ag.alarm_ack_callback(case.input.alarm_ack_req)
            # verify return code
            self.assertEqual(case.output.return_code, return_code)
            table = case.input.alarm_ack_req.table
            name = case.input.alarm_ack_req.name
            if case.output.alarm_send is True:
                # verify alarm ack message is sent
                uvekey = table+':'+name
                for atype, alarm in tab_alarms_copy[table][uvekey].iteritems():
                    if atype in case.output.ack_values:
			alarm.ack = case.output.ack_values[atype]
		alarms = copy.deepcopy(tab_alarms_copy[table][uvekey])
		alarm_data = UVEAlarms(name=name, alarms=alarms.values())
                MockAlarmTrace.assert_called_once_with(data=alarm_data,
                    table=table, sandesh=self._ag._sandesh)
                MockAlarmTrace().send.assert_called_once_with(
                    sandesh=self._ag._sandesh)
                MockAlarmTrace.reset_mock()
            else:
                self.assertFalse(MockAlarmTrace.called)
            # verify the alarm table after every call to alarm_ack_callback.
            # verify that ack field is set in the alarm table upon
            # successful acknowledgement and the table is untouched in case
            # of failure.
            #self.assertEqual(tab_alarms_copy, self._ag.tab_alarms)
	    for tab in self._ag.tab_alarms.keys():
		for uk,uv in self._ag.tab_alarms[tab].iteritems():
		    for ak,av in uv.iteritems():
			uai = av.get_uai(forced=True)
			if uai:
        		    self.assertEqual(uai, tab_alarms_copy[tab][uk][ak])
    # end test_03_alarm_ack_callback

    def test_04_alarm_state_machine(self):
        self._ag.tab_alarms = {}
        self.add_test_alarm('table1', 'name1', 'type1')

        TestCase = namedtuple('TestCase', ['name', 'initial_state',
	    'timer', 'expected_output_state'])
        set_alarm_test1 = [
            TestCase (
		name = "set alarm in Idle",
		initial_state = UVEAlarmState.Idle,
		timer = 1,
		expected_output_state = UVEAlarmState.Soak_Active
            ),
	]
	test_count = 1

        for case in set_alarm_test1:
            logging.info('=== Test case%s %s ===' % (test_count, case.name))
	    test_count += 1
            self._ag.tab_alarms['table1']['table1:name1']['type1'].\
                    get_uas().state = case.initial_state
            self._ag.tab_alarms['table1']['table1:name1']['type1'].\
                    get_uac().ActiveTimer = case.timer
            update_alarm = self._ag.tab_alarms['table1']['table1:name1']\
		    ['type1'].set_alarms()
            # verify output state
            output_state = self._ag.tab_alarms['table1']['table1:name1']\
                    ['type1'].get_uas().state
            self.assertEqual(case.expected_output_state, output_state)
            self.assertEqual(False, update_alarm)

	curr_time = int(time.time())
        logging.info('=== Test case%s checking activeTimerExpiry ===' % (test_count))
	test_count += 1
	delete_alarms, update_alarms = AlarmStateMachine.run_timers\
                (curr_time, self._ag.tab_alarms)
        self.assertEqual(update_alarms, [])

	time.sleep(1)
	curr_time += 1
	delete_alarms, update_alarms = AlarmStateMachine.run_timers\
                (curr_time, self._ag.tab_alarms)
        self.assertEqual(len(update_alarms), 1)

	clear_alarm_test1 = [
            TestCase (
		name = "clear alarm in Active",
		initial_state = UVEAlarmState.Active,
		timer = 0,
		expected_output_state = UVEAlarmState.Idle
            ),
            TestCase (
		name = "case3 clear alarm in Active with Timer",
		initial_state = UVEAlarmState.Active,
		timer = 1,
		expected_output_state = UVEAlarmState.Soak_Idle
            ),
        ]

        for case in clear_alarm_test1:
            logging.info('=== Test case%s %s ===' % (test_count, case.name))
	    test_count += 1
            self._ag.tab_alarms['table1']['table1:name1']['type1'].\
                    uas.state = case.initial_state
            self._ag.tab_alarms['table1']['table1:name1']['type1'].\
                    uac.IdleTimer = case.timer
            self._ag.tab_alarms['table1']['table1:name1']['type1'].\
                    uac.FreqCheck_Seconds = case.timer
            delete_alarm, update_alarm = self._ag.tab_alarms['table1']\
		    ['table1:name1']['type1'].clear_alarms()
            # verify output state
            output_state = self._ag.tab_alarms['table1']['table1:name1']\
                    ['type1'].uas.state
            self.assertEqual(case.expected_output_state, output_state)
	    if(case.expected_output_state == UVEAlarmState.Idle):
	    	self.assertEqual(delete_alarm, True)
	    	self.assertEqual(update_alarm, True)
	    elif case.expected_output_state == UVEAlarmState.Soak_Idle:
	    	self.assertEqual(delete_alarm, False)
	    	self.assertEqual(update_alarm, False)

        logging.info('=== Test case%s checking idleTimerExpiry ===' % (test_count))
	test_count += 1
	delete_alarms, update_alarms = AlarmStateMachine.run_timers\
                (curr_time, self._ag.tab_alarms)
        self.assertEqual(delete_alarms, [])
        self.assertEqual(update_alarms, [])

	time.sleep(1)
	curr_time += 1
	delete_alarms, update_alarms = AlarmStateMachine.run_timers\
                (curr_time, self._ag.tab_alarms)
        self.assertEqual(len(delete_alarms), 0)
        self.assertEqual(len(update_alarms), 1)

        logging.info('=== Test case%s checking deleteTimerExpiry ===' % (test_count))
	test_count += 1
	delete_alarms, update_alarms = AlarmStateMachine.run_timers\
                (curr_time, self._ag.tab_alarms)
        self.assertEqual(delete_alarms, [])

	time.sleep(1)
	curr_time += 1
	delete_alarms, update_alarms = AlarmStateMachine.run_timers\
                (curr_time, self._ag.tab_alarms)
        self.assertEqual(len(delete_alarms), 1)

	clear_alarm_test2 = [
            TestCase (
		name = "clear alarm in Active with Timer",
		initial_state = UVEAlarmState.Active,
		timer = 1,
		expected_output_state = UVEAlarmState.Soak_Idle
            ),
        ]

        for case in clear_alarm_test2:
            logging.info('=== Test case%s %s ===' % (test_count, case.name))
	    test_count += 1
            self._ag.tab_alarms['table1']['table1:name1']['type1'].\
                    uas.state = case.initial_state
            self._ag.tab_alarms['table1']['table1:name1']['type1'].\
                    uac.IdleTimer = case.timer
            self._ag.tab_alarms['table1']['table1:name1']['type1'].\
                    uac.FreqCheck_Seconds = case.timer
            delete_alarm, update_alarm = self._ag.tab_alarms['table1']\
		    ['table1:name1']['type1'].clear_alarms()
            # verify output state
            output_state = self._ag.tab_alarms['table1']['table1:name1']\
                    ['type1'].uas.state
            self.assertEqual(case.expected_output_state, output_state)
	    self.assertEqual(delete_alarm, False)
	    self.assertEqual(update_alarm, False)

        set_alarm_test2 = [
            TestCase (
		name = "set alarm in Soak_Idle",
		initial_state = UVEAlarmState.Soak_Idle,
		timer = 1,
		expected_output_state = UVEAlarmState.Active
            ),
	]

        for case in set_alarm_test2:
            logging.info('=== Test case%s %s ===' % (test_count, case.name))
	    test_count += 1
            self._ag.tab_alarms['table1']['table1:name1']['type1'].\
                    get_uas().state = case.initial_state
            self._ag.tab_alarms['table1']['table1:name1']['type1'].\
                    get_uac().ActiveTimer = case.timer
            update_alarm = self._ag.tab_alarms['table1']['table1:name1']\
		    ['type1'].set_alarms()
            # verify output state
            output_state = self._ag.tab_alarms['table1']['table1:name1']\
                    ['type1'].get_uas().state
            self.assertEqual(case.expected_output_state, output_state)
            self.assertEqual(False, update_alarm)

    # end test_04_alarm_state_machine

# end class TestAlarmGen


def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT, _term_handler)
    unittest.main(verbosity=2, catchbreak=True)
