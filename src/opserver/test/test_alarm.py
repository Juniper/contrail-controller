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

from vnc_api.gen.resource_client import Alarm
from vnc_api.gen.resource_xsd import AlarmExpression, AlarmOperand2, \
    AlarmAndList, AlarmOrList, UveKeysType
from pysandesh.util import UTCTimestampUsec
from pysandesh.gen_py.sandesh_alarm.ttypes import SandeshAlarmAckRequest, \
    SandeshAlarmAckResponseCode
from opserver.sandesh.alarmgen_ctrl.sandesh_alarm_base.ttypes import \
    UVEAlarmInfo, UVEAlarmConfig, UVEAlarms, AlarmRules, \
    AlarmAndList as SandeshAlarmAndList, AlarmCondition, AlarmMatch, \
    AlarmConditionMatch, AlarmOperand2 as SandeshAlarmOperand2
from opserver.sandesh.alarmgen_ctrl.ttypes import UVEAlarmOperState, \
    UVEAlarmStateMachineInfo, UVEAlarmState
from opserver.uveserver import UVEServer, RedisInfo
from opserver.partition_handler import PartitionHandler, UveStreamProc, \
    UveStreamer, UveStreamPart, PartInfo
from opserver.alarmgen import Controller, AlarmStateMachine, AlarmProcessor
from opserver.alarmgen_cfg import CfgParser
from opserver.plugins.alarm_base import AlarmBase

logging.basicConfig(level=logging.DEBUG,
    format='%(asctime)s %(levelname)s %(message)s')
logging.getLogger("stevedore.extension").setLevel(logging.WARNING)


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

class Mock_redis_instances(Mock_base):
    def __init__(self, *args, **kwargs):
        Mock_base.__init__(self, *args, **kwargs)

    def __call__(self):
        ret = []
        for k,v in self.store.iteritems():
            ipaddr,rport = k
            ret.append(RedisInfo(ip=ipaddr, port=rport, pid=v))
        return set(ret)

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
        cls._kc = mock.patch('opserver.partition_handler.KafkaClient', autospec=True)
        cls._kc.start()
        cls._ac = mock.patch('opserver.alarmgen.KafkaClient', autospec=True)
        cls._ac.start()
        cls._sc = mock.patch('opserver.alarmgen.SimpleProducer', autospec=True)
        cls._sc.start()

    @classmethod
    def tearDownClass(cls):
        cls._pc.stop()
        cls._kc.stop()
        cls._ac.stop()
        cls._sc.stop()
    
    def setUp(self):
        config = CfgParser('--http_server_port 0 '
                           '--zk_list 127.0.0.1:0 '
                           '--redis_uve_list 127.0.0.1:0 '
                           '--redis_server_port 0')
        config.parse()
        self._ag = Controller(config, logging)
        self._agtask = gevent.spawn(self._ag.run_uve_processing)

    def tearDown(self):
        self._agtask.kill()

    def create_test_alarm_info(self, table, name, alarm_type):
        or_list = []
        condition_match = AlarmConditionMatch(
            condition=AlarmCondition(operation="!=", operand1="dummytoken",
                operand2=SandeshAlarmOperand2(json_value=json.dumps("UP"))),
            match=[AlarmMatch(json_operand1_value=json.dumps("DOWN"))])
        or_list.append(SandeshAlarmAndList([condition_match]))
        uai = UVEAlarmInfo(type=alarm_type, severity=AlarmBase.ALARM_MAJOR,
                           timestamp=UTCTimestampUsec(),
                           token="dummytoken",
                           alarm_rules=AlarmRules(or_list), ack=False)
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

    def get_alarm_config_object(self, config_dict):
        alarm_or_list = []
        for and_list in config_dict['alarm_rules']['or_list']:
            alarm_and_list = []
            for exp in and_list['and_list']:
                alarm_and_list.append(AlarmExpression(
                    operation=exp['operation'],
                    operand1=exp['operand1'],
                    operand2=AlarmOperand2(uve_attribute=exp['operand2'].get(
                        'uve_attribute'), json_value=exp['operand2'].get(
                        'json_value')),
                    variables=exp.get('variables')))
            alarm_or_list.append(AlarmAndList(alarm_and_list))
        return Alarm(name=config_dict['name'],
                     uve_keys=UveKeysType(config_dict['uve_keys']),
                     alarm_severity=config_dict['alarm_severity'],
                     alarm_rules=AlarmOrList(alarm_or_list),
                     **config_dict['kwargs'])
    # end get_alarm_config_object

    def get_alarm_object(self, alarm_dict):
        or_list = []
        for elt in alarm_dict['or_list']:
            and_list = []
            for and_elt in elt['and_list']:
                cond = and_elt['condition']
                match = and_elt['match']
                and_list.append(AlarmConditionMatch(
                    condition=AlarmCondition(
                        operation=cond['operation'],
                        operand1=cond['operand1'],
                        operand2=SandeshAlarmOperand2(
                            uve_attribute=cond['operand2'].get(
                                'uve_attribute'),
                            json_value=cond['operand2'].get(
                                'json_value')),
                        variables=cond.get('variables') or []),
                    match=[AlarmMatch(
                        json_operand1_value=m['json_operand1_val'],
                        json_operand2_value=m.get('json_operand2_val'),
                        json_variables=m.get('json_variables') or {}) \
                            for m in match]))
            or_list.append(SandeshAlarmAndList(and_list))
        return UVEAlarmInfo(alarm_rules=AlarmRules(or_list))
    # end get_alarm_object

    @mock.patch('opserver.alarmgen.Controller.reconnect_agg_uve')
    @mock.patch('opserver.alarmgen.Controller.clear_agg_uve')
    @mock.patch('opserver.alarmgen.Controller.send_agg_uve')
    @mock.patch.object(UVEServer, 'redis_instances')
    @mock.patch.object(UVEServer, 'get_part')
    @mock.patch.object(UVEServer, 'get_uve')
    @mock.patch('opserver.partition_handler.SimpleConsumer', autospec=True)
    # Test partition Initialization, including boot-straping using UVEServer
    # Test partition shutdown as well
    def test_00_init(self,
            mock_SimpleConsumer,
            mock_get_uve, mock_get_part, mock_redis_instances,
            mock_send_agg_uve, mock_clear_agg_uve, mock_reconnect_agg_uve):

        m_get_part = Mock_get_part() 
        m_get_part[(1,("127.0.0.1",0,0))] = "127.0.0.1:0", \
            { "gen1" :
                { "ObjectXX:uve1" : {"type1":{}}  }}
        mock_get_part.side_effect = m_get_part

        m_get_uve = Mock_get_uve()
        m_get_uve["ObjectXX:uve1"] = {"type1": {"xx": 0}}
        mock_get_uve.side_effect = m_get_uve

        m_redis_instances = Mock_redis_instances()
        m_redis_instances[("127.0.0.1",0)] = 0
        mock_redis_instances.side_effect = m_redis_instances

        m_get_messages = Mock_get_messages()
        mock_SimpleConsumer.return_value.get_messages.side_effect = \
            m_get_messages

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
    @mock.patch.object(UVEServer, 'redis_instances')
    @mock.patch.object(UVEServer, 'get_part')
    @mock.patch.object(UVEServer, 'get_uve')
    @mock.patch('opserver.partition_handler.SimpleConsumer', autospec=True)
    # Test initialization followed by read from Kafka
    # Also test for deletetion of a boot-straped UVE
    def test_01_rxmsg(self,
            mock_SimpleConsumer,
            mock_get_uve, mock_get_part, mock_redis_instances,
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

        m_redis_instances = Mock_redis_instances()
        m_redis_instances[("127.0.0.1",0)] = 0
        mock_redis_instances.side_effect = m_redis_instances

        m_get_messages = Mock_get_messages()
        m_get_messages["ObjectYY:uve2"] = OffsetAndMessage(offset=0,
                    message=Message(magic=0, attributes=0,
                    key='ObjectYY:uve2|type2|gen1|127.0.0.1:0',
                    value='{}'))
        mock_SimpleConsumer.return_value.get_messages.side_effect = \
            m_get_messages

        self._ag.libpart_cb([1])
        self.assertTrue(self.checker_dict([1, "ObjectXX", "uve1"], self._ag.ptab_info, False))
        self.assertTrue(self.checker_dict([1, "ObjectYY", "uve2"], self._ag.ptab_info))
        self.assertTrue(self.checker_exact(\
            self._ag.ptab_info[1]["ObjectYY"]["uve2"].values(), {"type2" : {"yy": 1}}))

    @mock.patch('opserver.alarmgen.Controller.reconnect_agg_uve')
    @mock.patch('opserver.alarmgen.Controller.clear_agg_uve')
    @mock.patch('opserver.alarmgen.Controller.send_agg_uve')
    @mock.patch.object(UVEServer, 'redis_instances')
    @mock.patch.object(UVEServer, 'get_part')
    @mock.patch.object(UVEServer, 'get_uve')
    @mock.patch('opserver.partition_handler.SimpleConsumer', autospec=True)
    # Test late bringup of collector
    # Also test collector shutdown
    def test_02_collectorha(self,
            mock_SimpleConsumer,
            mock_get_uve, mock_get_part, mock_redis_instances,
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

        m_redis_instances = Mock_redis_instances()
        m_redis_instances[("127.0.0.1",0)] = 0
        mock_redis_instances.side_effect = m_redis_instances

        # When this message is read, 127.0.0.5 will not be present
        m_get_messages = Mock_get_messages()
        m_get_messages["ObjectYY:uve2"] = OffsetAndMessage(offset=0,
                    message=Message(magic=0, attributes=0,
                    key='ObjectYY:uve2|type2|gen1|127.0.0.5:0',
                    value='{}'))
        mock_SimpleConsumer.return_value.get_messages.side_effect = \
            m_get_messages

        self._ag.libpart_cb([1])

        # Now bringup collector 127.0.0.5
        self.assertTrue(self.checker_dict([1, "ObjectZZ", "uve3"], self._ag.ptab_info, False))
        m_redis_instances[("127.0.0.5",0)] = 0
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
        del m_redis_instances[("127.0.0.1",0)]
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
            self._ag.tab_alarms['table1']['table1:name1']\
		    ['type1'].set_alarms()
            # verify output state
            output_state = self._ag.tab_alarms['table1']['table1:name1']\
                    ['type1'].get_uas().state
            self.assertEqual(case.expected_output_state, output_state)

	curr_time = int(time.time())
        logging.info('=== Test case%s checking activeTimerExpiry ===' % (test_count))
	test_count += 1
	delete_alarms, update_alarms = AlarmStateMachine.run_timers\
                (curr_time, self._ag.tab_alarms)
        self.assertEqual(update_alarms, [])

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
            delete_alarm = self._ag.tab_alarms['table1']\
		    ['table1:name1']['type1'].clear_alarms()
            # verify output state
            output_state = self._ag.tab_alarms['table1']['table1:name1']\
                    ['type1'].uas.state
            self.assertEqual(case.expected_output_state, output_state)
	    if(case.expected_output_state == UVEAlarmState.Idle):
	    	self.assertEqual(delete_alarm, True)
	    elif case.expected_output_state == UVEAlarmState.Soak_Idle:
	    	self.assertEqual(delete_alarm, False)

        logging.info('=== Test case%s checking idleTimerExpiry ===' % (test_count))
	test_count += 1
	curr_time = int(time.time())
	delete_alarms, update_alarms = AlarmStateMachine.run_timers\
                (curr_time, self._ag.tab_alarms)
        self.assertEqual(delete_alarms, [])
        self.assertEqual(update_alarms, [])

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
            delete_alarm = self._ag.tab_alarms['table1']\
		    ['table1:name1']['type1'].clear_alarms()
            # verify output state
            output_state = self._ag.tab_alarms['table1']['table1:name1']\
                    ['type1'].uas.state
            self.assertEqual(case.expected_output_state, output_state)
	    self.assertEqual(delete_alarm, False)

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
            self._ag.tab_alarms['table1']['table1:name1']\
		    ['type1'].set_alarms()
            # verify output state
            output_state = self._ag.tab_alarms['table1']['table1:name1']\
                    ['type1'].get_uas().state
            self.assertEqual(case.expected_output_state, output_state)

    # end test_04_alarm_state_machine

    def test_05_evaluate_uve_for_alarms(self):
        TestCase = namedtuple('TestCase', ['name', 'input', 'output'])
        TestInput = namedtuple('TestInput', ['alarm_cfg', 'uve_key', 'uve'])
        TestOutput = namedtuple('TestOutput', ['or_list'])

        alarm_config1 = self.get_alarm_config_object(
            {
                'name': 'alarm1',
                'uve_keys': ['key1', 'key2'],
                'alarm_severity': AlarmBase.ALARM_MINOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A',
                                    'operation': '==',
                                    'operand2': {
                                        'json_value': 'null'
                                    }
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm1']
                }
            }
        )

        alarm_config2 = self.get_alarm_config_object(
            {
                'name': 'alarm2',
                'uve_keys': ['key1'],
                'alarm_severity': AlarmBase.ALARM_MAJOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B.C',
                                    'operation': 'not in',
                                    'operand2': {
                                        'uve_attribute': 'X.Y'
                                    }
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm2']
                }
            }
        )

        alarm_config3 = self.get_alarm_config_object(
            {
                'name': 'alarm3',
                'uve_keys': ['key1'],
                'alarm_severity': AlarmBase.ALARM_CRITICAL,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B',
                                    'operation': '!=',
                                    'operand2': {
                                        'uve_attribute': 'A.C'
                                    }
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm3']
                }
            }
        )

        alarm_config4 = self.get_alarm_config_object(
            {
                'name': 'alarm4',
                'uve_keys': ['key1'],
                'alarm_severity': AlarmBase.ALARM_MAJOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B',
                                    'operation': '!=',
                                    'operand2': {
                                        'json_value': '2'
                                    }
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm4']
                }
            }
        )

        alarm_config5 = self.get_alarm_config_object(
            {
                'name': 'alarm5',
                'uve_keys': ['key5'],
                'alarm_severity': AlarmBase.ALARM_MINOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B',
                                    'operation': '<=',
                                    'operand2': {
                                        'uve_attribute': 'A.C.D'
                                    },
                                    'variables': ['A.C.N']
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm5']
                }
            }
        )

        alarm_config6 = self.get_alarm_config_object(
            {
                'name': 'alarm6',
                'uve_keys': ['key1'],
                'alarm_severity': AlarmBase.ALARM_MAJOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B',
                                    'operation': '<=',
                                    'operand2': {
                                        'uve_attribute': 'A.C.D'
                                    },
                                    'variables': ['A.C.N']
                                },
                                {
                                    'operand1': 'A.B',
                                    'operation': '>=',
                                    'operand2': {
                                        'uve_attribute': 'A.C.E'
                                    },
                                    'variables': ['A.C.N']
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm6']
                }
            }
        )

        alarm_config7 = self.get_alarm_config_object(
            {
                'name': 'alarm7',
                'uve_keys': ['key1'],
                'alarm_severity': AlarmBase.ALARM_CRITICAL,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A',
                                    'operation': '!=',
                                    'operand2': {
                                        'json_value': 'null'
                                    }
                                },
                                {
                                    'operand1': 'A.B',
                                    'operation': 'not in',
                                    'operand2': {
                                        'json_value': '["abc", "def"]'
                                    }
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'operand1': 'A',
                                    'operation': '!=',
                                    'operand2': {
                                        'json_value': 'null'
                                    }
                                },
                                {
                                    'operand1': 'A.B',
                                    'operation': '==',
                                    'operand2': {
                                        'uve_attribute': 'A.D'
                                    }
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm7']
                }
            }
        )

        alarm_config8 = self.get_alarm_config_object(
            {
                'name': 'alarm8',
                'uve_keys': ['key8'],
                'alarm_severity': AlarmBase.ALARM_MINOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B',
                                    'operation': '<=',
                                    'operand2': {
                                        'uve_attribute': 'A.C.D'
                                    },
                                    'variables': ['A.C.N']
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'project',
                    'fq_name': ['default-domain', 'admin', 'alarm8']
                }
            }
        )

        alarm_config9 = self.get_alarm_config_object(
            {
                'name': 'alarm9',
                'uve_keys': ['key9'],
                'alarm_severity': AlarmBase.ALARM_MINOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.*.C',
                                    'operation': '!=',
                                    'operand2': {
                                        'uve_attribute': 'A.*.D'
                                    },
                                    'variables': ['A.__key']
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm9']
                }
            }
        )

        alarm_config10 = self.get_alarm_config_object(
            {
                'name': 'alarm10',
                'uve_keys': ['key10'],
                'alarm_severity': AlarmBase.ALARM_MAJOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B.*.C',
                                    'operation': '!=',
                                    'operand2': {
                                        'uve_attribute': 'A.C'
                                    },
                                    'variables': ['A.B.__key', 'A.B.*.D']
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm10']
                }
            }
        )

        alarm_config11 = self.get_alarm_config_object(
            {
                'name': 'alarm11',
                'uve_keys': ['key11'],
                'alarm_severity': AlarmBase.ALARM_MINOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B',
                                    'operation': '<=',
                                    'operand2': {
                                        'uve_attribute': 'A.C.*.D'
                                    },
                                    'variables': ['A.C.__key', 'A.C.__value']
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm11']
                }
            }
        )

        alarm_config12 = self.get_alarm_config_object(
            {
                'name': 'alarm12',
                'uve_keys': ['key12'],
                'alarm_severity': AlarmBase.ALARM_MAJOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B.__key',
                                    'operation': 'not in',
                                    'operand2': {
                                        'json_value': '["abc", "xyz"]'
                                    },
                                    'variables': ['A.B.__key', 'A.B.__value',
                                        'A.C', 'X.Y']
                                }
                            ],
                        },
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B.__value',
                                    'operation': 'not in',
                                    'operand2': {
                                        'json_value': '["test1", "test2"]'
                                    },
                                    'variables': ['A.B.__key', 'A.B.__value']
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm12']
                }
            }
        )

        alarm_config13 = self.get_alarm_config_object(
            {
                'name': 'alarm13',
                'uve_keys': ['key13'],
                'alarm_severity': AlarmBase.ALARM_MAJOR,
                'alarm_rules': {
                    'or_list': [
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B.C',
                                    'operation': '<',
                                    'operand2': {
                                        'uve_attribute': 'A.B.D'
                                    }
                                },
                                {
                                    'operand1': 'A.B.C',
                                    'operation': '>',
                                    'operand2': {
                                        'json_value': '100'
                                    }
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'operand1': 'A.B.C',
                                    'operation': 'range',
                                    'operand2': {
                                        'json_value': '[500, 1000]'
                                    }
                                }
                            ]
                        }
                    ]
                },
                'kwargs': {
                    'parent_type': 'global-system-config',
                    'fq_name': ['global-syscfg-default', 'alarm13']
                }
            }
        )

        tests = [
            TestCase(name='operand1 not present/null in UVE',
                input=TestInput(alarm_cfg=alarm_config1,
                    uve_key='Table1:host1',
                    uve={}),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '=='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(name='operand2 not present/null in UVE',
                input=TestInput(alarm_cfg=alarm_config2,
                    uve_key='Table1:host1',
                    uve={
                        'A': {
                            'B': {
                                'C': 'xyz'
                            },
                        },
                        'X': {
                            'Z': 12
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B.C',
                                    'operand2': {
                                        'uve_attribute': 'X.Y'
                                    },
                                    'operation': 'not in'
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"xyz"',
                                        'json_operand2_val': 'null'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='Failed to get operand1 from UVE',
                input=TestInput(alarm_cfg=alarm_config2,
                    uve_key='Table2:host1',
                    uve={
                        'A': {
                            'D': 'test' 
                        },
                        'X': {
                            'Y': ['xyz', 'abc']
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='Failed to get operand2 from UVE',
                input=TestInput(alarm_cfg=alarm_config2,
                    uve_key='Table3:host1',
                    uve={
                        'A': {
                            'B': {
                                'C': 'abc'
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='operand1 does not evaluate to list;'
                    'operand2 is not a json value and does not evaluate '
                    'to list - no rule match',
                input=TestInput(alarm_cfg=alarm_config3,
                    uve_key='table2:host1',
                    uve={
                        'A': {
                            'B': 'val1',
                            'C': 'val1'
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='operand1 does not evaluate to list;'
                    'operand2 is not a json value and does not evaluate '
                    'to list',
                input=TestInput(alarm_cfg=alarm_config3,
                    uve_key='table4:host1',
                    uve={
                        'A': {
                            'B': 'val1',
                            'C': 'val2'
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'uve_attribute': 'A.C'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"val1"',
                                        'json_operand2_val': '"val2"'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='operand1 does not evaluate to a list;'
                    'operand2 is a json value - no rule match',
                input=TestInput(alarm_cfg=alarm_config4,
                    uve_key='table3:host2',
                    uve={
                        'A': {
                            'B': 2,
                            'C': 1
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='operand1 does not evaluate to a list;'
                    'operand2 is a json value',
                input=TestInput(alarm_cfg=alarm_config4,
                    uve_key='table6:host2',
                    uve={
                        'A': {
                            'B': 3,
                            'C': 2
                        },
                        'X': {
                            'Y': 'abc'
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'json_value': '2'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '3'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='operand1 evaluates to a list;'
                    'operand2 is a json value - no rule match',
                input=TestInput(alarm_cfg=alarm_config4,
                    uve_key='table7:host2',
                    uve={
                        'A': [
                            {
                                'B': 2,
                                'C': 'abc'
                            },
                            {
                                'B': 2,
                                'C': 'xyz'
                            }
                        ]
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='operand1 evaluates to a list;'
                    'operand2 is a json value',
                input=TestInput(alarm_cfg=alarm_config4,
                    uve_key='table7:host2',
                    uve={
                        'A': [
                            {
                                'B': 1,
                                'C': 'abc'
                            },
                            {
                                'B': 2,
                                'C': 'qst'
                            },
                            {
                                'B': '3',
                                'C': 'xyz'
                            }
                        ]
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'json_value': '2'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '1'
                                    },
                                    {
                                        'json_operand1_val': '3'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='operand1 evaluates to a list;'
                    'operand2 is a uve attribute and is not a list'
                    ' - no rule match',
                input=TestInput(alarm_cfg=alarm_config2,
                    uve_key='table2:host2',
                    uve={
                        'A': {
                            'B': [
                                {
                                    'C': 'abc'
                                }
                            ]
                        },
                        'X': {
                            'Y': ['def', 'abc']
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='operand1 evaluates to a list;'
                    'operand2 is a uve attribute and is not a list',
                input=TestInput(alarm_cfg=alarm_config2,
                    uve_key='table2:host2',
                    uve={
                        'A': {
                            'B': [
                                {
                                    'C': 'abc'
                                },
                                {
                                    'B': 'abc',
                                    'C': 'def'
                                },
                                {
                                    'C': 'xyz'
                                }
                            ]
                        },
                        'X': {
                            'Y': ['def', 'pqr']
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B.C',
                                    'operand2': {
                                        'uve_attribute': 'X.Y'
                                    },
                                    'operation': 'not in'
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"abc"',
                                        'json_operand2_val': '["def", "pqr"]'
                                    },
                                    {
                                        'json_operand1_val': '"xyz"',
                                        'json_operand2_val': '["def", "pqr"]'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='operand1 evaluates to a list; operand2 evaluates '
                    'to a list - no rule match',
                input=TestInput(alarm_cfg=alarm_config3,
                    uve_key='table3:host3',
                    uve={
                        'A': [
                            {
                                'B': 'abc',
                                'C': 'abc'
                            },
                            {
                                'B': 'xyz',
                                'C': 'xyz'
                            },
                            {
                                'D': 1,
                                'E': 2
                            },
                        ]
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='Both operand1 and operand2 evaluates to a list',
                input=TestInput(alarm_cfg=alarm_config3,
                    uve_key='table3:host1',
                    uve={
                        'A': [
                            {
                                'B': 'abc',
                                'C': 'abc'
                            },
                            {
                                'B': 'xyz',
                            },
                            {
                                'C': 'abc'
                            },
                            {
                                'B': 'def',
                                'C': 'def'
                            },
                            {
                                'B': 'def',
                                'C': 'qst'
                            }
                        ],
                        'X': {
                            'A': 123
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'uve_attribute': 'A.C'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"xyz"',
                                        'json_operand2_val': 'null'
                                    },
                                    {
                                        'json_operand1_val': 'null',
                                        'json_operand2_val': '"abc"'
                                    },
                                    {
                                        'json_operand1_val': '"def"',
                                        'json_operand2_val': '"qst"'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='operand1 is not a list; operand2 evaluates to a list'
                    ' - no rule match',
                input=TestInput(alarm_cfg=alarm_config5,
                    uve_key='table5:host2',
                    uve={
                        'A': {
                            'B': 10,
                            'C': [
                                {
                                    'D': 5
                                },
                                {
                                    'D': 2
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='operand1 is not a list; operand2 evaluates to a list',
                input=TestInput(alarm_cfg=alarm_config5,
                    uve_key='table2:host2',
                    uve={
                        'A': {
                            'B': 10,
                            'C': [
                                {
                                    'D': 5
                                },
                                {
                                    'D': 11,
                                    'N': 'abc',
                                    'A': {
                                        'B': 'abc'
                                    }
                                },
                                {
                                    'D': 25
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'uve_attribute': 'A.C.D'
                                    },
                                    'operation': '<=',
                                    'variables': ['A.C.N']
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '10',
                                        'json_operand2_val': '11',
                                        'json_variables': {
                                            'A.C.N': '"abc"'
                                        }
                                    },
                                    {
                                        'json_operand1_val': '10',
                                        'json_operand2_val': '25',
                                        'json_variables': {
                                            'A.C.N': 'null'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='multiple conditions in and_list - '
                    'not all conditions match',
                input=TestInput(alarm_cfg=alarm_config6,
                    uve_key='table6:host2',
                    uve={
                        'A': {
                            'B': 10,
                            'C': [
                                {
                                    'N': 'abc',
                                    'D': 5,
                                    'E': 5
                                },
                                {
                                    'N': 'hjk',
                                    'D': 2,
                                    'E': 4
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='multiple conditions in and_list - all conditions match',
                input=TestInput(alarm_cfg=alarm_config6,
                    uve_key='table6:host3',
                    uve={
                        'A': {
                            'B': 5,
                            'C': [
                                {
                                    'N': 'abc',
                                    'D': 5,
                                    'E': 5
                                },
                                {
                                    'N': 'hjk',
                                    'D': 10,
                                    'E': 4
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'uve_attribute': 'A.C.D'
                                    },
                                    'operation': '<=',
                                    'variables': ['A.C.N']
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '5',
                                        'json_operand2_val': '5',
                                        'json_variables': {
                                            'A.C.N': '"abc"'
                                        }
                                    },
                                    {
                                        'json_operand1_val': '5',
                                        'json_operand2_val': '10',
                                        'json_variables': {
                                            'A.C.N': '"hjk"'
                                        }
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'uve_attribute': 'A.C.E'
                                    },
                                    'operation': '>=',
                                    'variables': ['A.C.N']
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '5',
                                        'json_operand2_val': '5',
                                        'json_variables': {
                                            'A.C.N': '"abc"'
                                        }
                                    },
                                    {
                                        'json_operand1_val': '5',
                                        'json_operand2_val': '4',
                                        'json_variables': {
                                            'A.C.N': '"hjk"'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='mutiple and_list in or_list - no match',
                input=TestInput(alarm_cfg=alarm_config7,
                    uve_key='table7:host2',
                    uve={
                        'A': {
                            'B': 'abc'
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='multiple and_list in or_list - not all and_list '
                    'results in a match',
                input=TestInput(alarm_cfg=alarm_config7,
                    uve_key='table7:host1',
                    uve={
                        'A': {
                            'B': 'def',
                            'D': 'def'
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '{"B": "def", "D": "def"}',
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'uve_attribute': 'A.D'
                                    },
                                    'operation': '=='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"def"',
                                        'json_operand2_val': '"def"'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='multiple and_list in or_list - multiple and_list '
                    'results in a match',
                input=TestInput(alarm_cfg=alarm_config7,
                    uve_key='table7:host1',
                    uve={
                        'A': {
                            'B': 'xyz',
                            'D': 'xyz'
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '{"B": "xyz", "D": "xyz"}',
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'json_value': '["abc", "def"]'
                                    },
                                    'operation': 'not in'
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"xyz"'
                                    }
                                ]
                            }
                        ]
                    },
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A',
                                    'operand2': {
                                        'json_value': 'null'
                                    },
                                    'operation': '!='
                                },
                                'match': [
                                    {
                                        'json_operand1_val':
                                            '{"B": "xyz", "D": "xyz"}',
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'uve_attribute': 'A.D'
                                    },
                                    'operation': '=='
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"xyz"',
                                        'json_operand2_val': '"xyz"'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='alarm config parent type is project. project fqname '
                    'not present in the uve name',
                input=TestInput(alarm_cfg=alarm_config8,
                    uve_key='table8:host1',
                    uve={
                        'A': {
                            'B': 5,
                            'C': [
                                {
                                    'D': 7
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='alarm config parent type is project. project fqname '
                    'present in the uve name',
                input=TestInput(alarm_cfg=alarm_config8,
                    uve_key='table8:default-domain:admin:host1',
                    uve={
                        'A': {
                            'B': 5,
                            'C': [
                                {
                                    'D': 7
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'uve_attribute': 'A.C.D'
                                    },
                                    'operation': '<=',
                                    'variables': ['A.C.N']
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '5',
                                        'json_operand2_val': '7',
                                        'json_variables': {
                                            'A.C.N': 'null'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='virtual-machine alarm configured under project - '
                    'interface_list has matching project name',
                input=TestInput(alarm_cfg=alarm_config8,
                    uve_key='ObjectVMTable:vm1',
                    uve={
                        'A': {
                            'B': 5,
                            'C': [
                                {
                                    'D': 7
                                }
                            ]
                        },
                        'UveVirtualMachineAgent': {
                            'interface_list': [
                                'default-domain:admin:if1',
                                'default-domain:admin:if2'
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'uve_attribute': 'A.C.D'
                                    },
                                    'operation': '<=',
                                    'variables': ['A.C.N']
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '5',
                                        'json_operand2_val': '7',
                                        'json_variables': {
                                            'A.C.N': 'null'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='virtual-machine alarm configured under project - '
                    'interface_list has no matching project name',
                input=TestInput(alarm_cfg=alarm_config8,
                    uve_key='ObjectVMTable:vm1',
                    uve={
                        'A': {
                            'B': 5,
                            'C': [
                                {
                                    'D': 7
                                }
                            ]
                        },
                        'UveVirtualMachineAgent': {
                            'interface_list': [
                                'default-domain:demo:if1'
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='virtual-machine alarm configured under project - '
                    'UveVirtualMachineAgent UVE not present',
                input=TestInput(alarm_cfg=alarm_config8,
                    uve_key='ObjectVMTable:vm1',
                    uve={
                        'A': {
                            'B': 5,
                            'C': [
                                {
                                    'D': 7
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='virtual-machine alarm configured under project - '
                    'interface_list not present in UveVirtualMachineAgent UVE',
                input=TestInput(alarm_cfg=alarm_config8,
                    uve_key='ObjectVMTable:vm1',
                    uve={
                        'A': {
                            'B': 5,
                            'C': [
                                {
                                    'D': 7
                                }
                            ]
                        },
                        'UveVirtualMachineAgent': {
                            'vm_name': 'vm1'
                        }
                    },
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='virtual-machine alarm configured under global - '
                    'UveVirtualMachineAgent UVE not present',
                input=TestInput(alarm_cfg=alarm_config5,
                    uve_key='ObjectVMTable:vm1',
                    uve={
                        'A': {
                            'B': 5,
                            'C': [
                                {
                                    'D': 7,
                                    'N': 'test'
                                }
                            ]
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'uve_attribute': 'A.C.D'
                                    },
                                    'operation': '<=',
                                    'variables': ['A.C.N']
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '5',
                                        'json_operand2_val': '7',
                                        'json_variables': {
                                            'A.C.N': '"test"'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='operand1 and operand2 has * and evaluates to a list;'
                    ' - no rule match',
                input=TestInput(alarm_cfg=alarm_config9,
                    uve_key='table9:host9',
                    uve={
                        'A': {
                            'abc': {
                                'C': 'abc',
                                'D': 'abc'
                            },
                            'xyz': {
                                'C': 'qwe',
                                'D': 'qwe'
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='operand1 and operand2 has * and evaluates to a list',
                input=TestInput(alarm_cfg=alarm_config9,
                    uve_key='table9:host9',
                    uve={
                        'A': {
                            'abc': {
                                'C': 10,
                                'D': 4,
                                'E': 10
                            },
                            'def': {
                                'C': 23,
                                'D': 23,
                                'E': 10
                            },
                            'rst': {
                                'C': 4,
                            },
                            'bnm': {
                                'D': 45,
                                'E': 12
                            },
                            'dfg': {
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.*.C',
                                    'operand2': {
                                        'uve_attribute': 'A.*.D'
                                    },
                                    'operation': '!=',
                                    'variables': ['A.__key']
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '4',
                                        'json_operand2_val': 'null',
                                        'json_variables': {
                                            'A.__key': '"rst"'
                                        }
                                    },
                                    {
                                        'json_operand1_val': '10',
                                        'json_operand2_val': '4',
                                        'json_variables': {
                                            'A.__key': '"abc"'
                                        }
                                    },
                                    {
                                        'json_operand1_val': 'null',
                                        'json_operand2_val': '45',
                                        'json_variables': {
                                            'A.__key': '"bnm"'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='operand1 has * and evaluates to a list;'
                    'operand2 is a uve attribute and not list - no rule match',
                input=TestInput(alarm_cfg=alarm_config10,
                    uve_key='table10:host10',
                    uve={
                        'A': {
                            'B': {
                                'abc': {
                                    'C': 'test1',
                                    'D': 2
                                },
                                'xyz': {
                                    'C': 'test1'
                                }
                            },
                            'C': 'test1'
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='operand1 has * and evaluates to a list;'
                    'operand2 is a uve attribute and not a list',
                input=TestInput(alarm_cfg=alarm_config10,
                    uve_key='table10:host10',
                    uve={
                        'A': {
                            'B': {
                                'abc': {
                                    'C': 'test1'
                                },
                                'def': {
                                    'C': 'test2'
                                }
                            },
                            'C': 'test1'
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B.*.C',
                                    'operand2': {
                                        'uve_attribute': 'A.C'
                                    },
                                    'operation': '!=',
                                    'variables': ['A.B.__key', 'A.B.*.D']
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"test2"',
                                        'json_operand2_val': '"test1"',
                                        'json_variables': {
                                            'A.B.__key': '"def"',
                                            'A.B.*.D': 'null'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='operand1 is not a list and operand2 has * and '
                    'evaluates to a list - no rule match',
                input=TestInput(alarm_cfg=alarm_config11,
                    uve_key='table11:host11',
                    uve={
                        'A': {
                            'B': 5,
                            'C': {
                                'lmo': {
                                    'D': 2,
                                },
                                'xyz': {
                                    'D': 4,
                                    'C': 'qwe'
                                }
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='operand1 is not a list and operand2 has * and '
                    'evaluates to a list',
                input=TestInput(alarm_cfg=alarm_config11,
                    uve_key='table11:host11',
                    uve={
                        'A': {
                            'B': 5,
                            'C': {
                                'lmo': {
                                    'D': 5,
                                },
                                'xyz': {
                                    'D': 10,
                                    'C': 'qwe'
                                }
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B',
                                    'operand2': {
                                        'uve_attribute': 'A.C.*.D'
                                    },
                                    'operation': '<=',
                                    'variables': ['A.C.__key', 'A.C.__value']
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '5',
                                        'json_operand2_val': '10',
                                        'json_variables': {
                                            'A.C.__key': '"xyz"',
                                            'A.C.__value':
                                                '{"C": "qwe", "D": 10}'
                                        }
                                    },
                                    {
                                        'json_operand1_val': '5',
                                        'json_operand2_val': '5',
                                        'json_variables': {
                                            'A.C.__key': '"lmo"',
                                            'A.C.__value': '{"D": 5}'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='operand1 has __key and evaluates to a list;'
                    'operand2 is a json value - no rule match',
                input=TestInput(alarm_cfg=alarm_config12,
                    uve_key='table12:host12',
                    uve={
                        'A': {
                            'B': {
                                'abc': 'test1',
                                'xyz': 'test2'
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='operand1 has __key and evaluates to a list;'
                    'operand2 is a json value',
                input=TestInput(alarm_cfg=alarm_config12,
                    uve_key='table12:host12',
                    uve={
                        'A': {
                            'B': {
                                'abc': 'test3',
                                'def': 'test4'
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B.__key',
                                    'operand2': {
                                        'json_value': '["abc", "xyz"]'
                                    },
                                    'operation': 'not in',
                                    'variables': ['A.B.__key', 'A.B.__value',
                                        'A.C', 'X.Y']
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"def"',
                                        'json_variables': {
                                            'A.B.__key': '"def"',
                                            'A.B.__value': '"test4"',
                                            'A.C': 'null',
                                            'X.Y': 'null'
                                        }
                                    }
                                ]
                            }
                        ]
                    },
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B.__value',
                                    'operand2': {
                                        'json_value': '["test1", "test2"]'
                                    },
                                    'operation': 'not in',
                                    'variables': ['A.B.__key', 'A.B.__value']
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '"test3"',
                                        'json_variables': {
                                            'A.B.__key': '"abc"',
                                            'A.B.__value': '"test3"'
                                        }
                                    },
                                    {
                                        'json_operand1_val': '"test4"',
                                        'json_variables': {
                                            'A.B.__key': '"def"',
                                            'A.B.__value': '"test4"'
                                        }
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='Test operation < and > - no condition match',
                input=TestInput(alarm_cfg=alarm_config13,
                    uve_key='table13:host13',
                    uve={
                        'A': {
                            'B': {
                                'C': 5,
                                'D': 5
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='Test operation < and > - condition match',
                input=TestInput(alarm_cfg=alarm_config13,
                    uve_key='table13:host13',
                    uve={
                        'A': {
                            'B': {
                                'C': 150,
                                'D': 200
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B.C',
                                    'operand2': {
                                        'uve_attribute': 'A.B.D'
                                    },
                                    'operation': '<'
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '150',
                                        'json_operand2_val': '200'
                                    }
                                ]
                            },
                            {
                                'condition': {
                                    'operand1': 'A.B.C',
                                    'operand2': {
                                        'json_value': '100'
                                    },
                                    'operation': '>'
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '150'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            ),
            TestCase(
                name='Test range operation - no condition match',
                input=TestInput(alarm_cfg=alarm_config13,
                    uve_key='table13:host13',
                    uve={
                        'A': {
                            'B': {
                                'C': 1500,
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=None)
            ),
            TestCase(
                name='Test range operation - condition match',
                input=TestInput(alarm_cfg=alarm_config13,
                    uve_key='table13:host13',
                    uve={
                        'A': {
                            'B': {
                                'C': 500,
                            }
                        }
                    }
                ),
                output=TestOutput(or_list=[
                    {
                        'and_list': [
                            {
                                'condition': {
                                    'operand1': 'A.B.C',
                                    'operand2': {
                                        'json_value': '[500, 1000]'
                                    },
                                    'operation': 'range'
                                },
                                'match': [
                                    {
                                        'json_operand1_val': '500'
                                    }
                                ]
                            }
                        ]
                    }
                ])
            )
        ]

        for test in tests:
            logging.info('=== Test: %s ===' % (test.name))
            exp_or_list = None
            if test.output.or_list is not None:
                exp_or_list = []
                for elt in test.output.or_list:
                    and_list = []
                    for and_elt in elt['and_list']:
                        cond = and_elt['condition']
                        match = and_elt['match']
                        and_list.append(AlarmConditionMatch(
                            condition=AlarmCondition(
                                operation=cond['operation'],
                                operand1=cond['operand1'],
                                operand2=SandeshAlarmOperand2(
                                    uve_attribute=cond['operand2'].get(
                                        'uve_attribute'),
                                    json_value=cond['operand2'].get(
                                        'json_value')),
                                variables=cond.get('variables') or []),
                            match=[AlarmMatch(
                                json_operand1_value=m['json_operand1_val'],
                                json_operand2_value=m.get('json_operand2_val'),
                                json_variables=m.get('json_variables') or {}) \
                                    for m in match]))
                    exp_or_list.append(SandeshAlarmAndList(and_list))
            alarm_processor = AlarmProcessor(logging)
            or_list = alarm_processor._evaluate_uve_for_alarms(
                test.input.alarm_cfg, test.input.uve_key, test.input.uve)
            logging.info('exp_or_list: %s' % (str(exp_or_list)))
            logging.info('or_list: %s' % (str(or_list)))
            self.assertEqual(exp_or_list, or_list)
    # end test_05_evaluate_uve_for_alarms

    def test_06_is_new_alarm_same(self):
        TestCase = namedtuple('TestCase', ['name', 'input', 'output'])
        TestInput = namedtuple('TestInput', ['old_alarm', 'new_alarm', 'state'])
        TestOutput = namedtuple('TestOutput', ['result'])

        tests = [
            TestCase(name='old alarm or_list == [] and new alarm or_list == []',
                input=TestInput(
                    old_alarm={
                        'or_list': []
                    },
                    new_alarm={
                        'or_list': []
                    },
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=True)
            ),
            TestCase(name='old alarm or_list == [] and new alarm or_list != []',
                input=TestInput(
                    old_alarm={
                        'or_list': []
                    },
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"val1"',
                                            'json_operand2_val': '"val2"'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list != [] and new alarm or_list == []',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"val1"',
                                            'json_operand2_val': '"val2"'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={
                        'or_list': []
                    },
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list == new alarm or_list; '
                'single OR term and single AND term; '
                'state == UVEAlarmState.Active',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"val1"',
                                            'json_operand2_val': '"val2"'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"val1"',
                                            'json_operand2_val': '"val2"'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=True)
            ),
            TestCase(name='old alarm or_list == new alarm or_list; '
                'single OR term and single AND term; '
                'state != UVEAlarmState.Active',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"val1"',
                                            'json_operand2_val': '"val2"'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"val1"',
                                            'json_operand2_val': '"val2"'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Soak_Idle
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list == new alarm or_list; '
                'single OR term and multiple AND terms; '
                'state == UVEAlarmState.Active',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "def", "D": "def"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.D'
                                        },
                                        'operation': '=='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"def"',
                                            'json_operand2_val': '"def"'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "def", "D": "def"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.D'
                                        },
                                        'operation': '=='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"def"',
                                            'json_operand2_val': '"def"'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=True)
            ),
            TestCase(name='old alarm or_list == new alarm or_list; '
                'multiple OR terms and multiple AND terms; '
                'state == UVEAlarmState.Active',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'json_value': '["abc", "def"]'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}'
                                        },
                                        {
                                            'json_operand1_val':
                                                '{"B": "abc", "D": "abc"}'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'json_value': '["abc", "def"]'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}'
                                        },
                                        {
                                            'json_operand1_val':
                                                '{"B": "abc", "D": "abc"}'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=True)
            ),
            TestCase(name='old alarm or_list == new alarm or_list; '
                'AND terms with <=, <, >=, >, range; '
                'state == UVEAlarmState.Active',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C.D'
                                        },
                                        'operation': '<=',
                                        'variables': ['A.C.N']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '5',
                                            'json_operand2_val': '7',
                                            'json_variables': {
                                                'A.C.N': '"test"'
                                            }
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C.E'
                                        },
                                        'operation': '<',
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '5',
                                            'json_operand2_val': '9',
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'P.Q',
                                        'operand2': {
                                            'uve_attribute': 'P.R.*.S'
                                        },
                                        'operation': '>=',
                                        'variables':
                                            ['P.R.__key', 'P.R.__value']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '100',
                                            'json_operand2_val': '20',
                                            'json_variables': {
                                                'P.R.__key': '"xyz"',
                                                'P.R.__value':
                                                    '{"C": "qwe", "D": 10}'
                                            }
                                        },
                                        {
                                            'json_operand1_val': '3',
                                            'json_operand2_val': '3',
                                            'json_variables': {
                                                'P.R.__key': '"lmo"',
                                                'P.R.__value': '{"D": 5}'
                                            }
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B.C',
                                        'operand2': {
                                            'json_value': '100'
                                        },
                                        'operation': '>'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '150'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B.C',
                                        'operand2': {
                                            'json_value': '[500, 1000]'
                                        },
                                        'operation': 'range'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '500'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C.D'
                                        },
                                        'operation': '<=',
                                        'variables': ['A.C.N']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '7',
                                            'json_operand2_val': '7',
                                            'json_variables': {
                                                'A.C.N': '"test"'
                                            }
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C.E'
                                        },
                                        'operation': '<',
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '8',
                                            'json_operand2_val': '9',
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'P.Q',
                                        'operand2': {
                                            'uve_attribute': 'P.R.*.S'
                                        },
                                        'operation': '>=',
                                        'variables':
                                            ['P.R.__key', 'P.R.__value']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '100',
                                            'json_operand2_val': '90',
                                            'json_variables': {
                                                'P.R.__key': '"xyz"',
                                                'P.R.__value':
                                                    '{"C": "qwe", "D": 10}'
                                            }
                                        },
                                        {
                                            'json_operand1_val': '7',
                                            'json_operand2_val': '7',
                                            'json_variables': {
                                                'P.R.__key': '"lmo"',
                                                'P.R.__value': '{"D": 5}'
                                            }
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B.C',
                                        'operand2': {
                                            'json_value': '100'
                                        },
                                        'operation': '>'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '250'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B.C',
                                        'operand2': {
                                            'json_value': '[500, 1000]'
                                        },
                                        'operation': 'range'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '502'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=True)
            ),
            TestCase(name='old alarm or_list != new alarm or_list; '
                'or_list len mismatch',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'json_value': '["abc", "def"]'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}'
                                        },
                                        {
                                            'json_operand1_val':
                                                '{"B": "abc", "D": "abc"}'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'json_value': '["abc", "def"]'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list != new alarm or_list; '
                'multiple OR terms and multiple AND terms; '
                'mismatch in number of AND terms',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}'
                                        },
                                        {
                                            'json_operand1_val':
                                                '{"B": "abc", "D": "abc"}'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'json_value': '["abc", "def"]'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}'
                                        },
                                        {
                                            'json_operand1_val':
                                                '{"B": "abc", "D": "abc"}'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list != new alarm or_list; '
                'single OR term and single AND term; condition mismatch',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B.C',
                                        'operand2': {
                                            'uve_attribute': 'X.Y'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"',
                                            'json_operand2_val': 'null'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"val1"',
                                            'json_operand2_val': '"val2"'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list != new alarm or_list; '
                'multiple OR terms and multiple AND terms; '
                'condition mismatch',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'json_value': '["abc", "def"]'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}'
                                        },
                                        {
                                            'json_operand1_val':
                                                '{"B": "abc", "D": "abc"}'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'json_value': '["abc", "def"]'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': '5'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}'
                                        },
                                        {
                                            'json_operand1_val':
                                                '{"B": "abc", "D": "abc"}'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list != new alarm or_list; '
                'multiple OR terms and multiple AND terms; '
                'mismatch in the number of match terms',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'json_value': '["abc", "def"]'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}'
                                        },
                                        {
                                            'json_operand1_val':
                                                '{"B": "abc", "D": "abc"}'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'json_value': '["abc", "def"]'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list != new alarm or_list; '
                'multiple OR terms and multiple AND terms; '
                'mismatch in the match list',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'json_value': '["abc", "def"]'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}'
                                        },
                                        {
                                            'json_operand1_val':
                                                '{"B": "abc", "D": "abc"}'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}',
                                        }
                                    ]
                                },
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'json_value': '["abc", "def"]'
                                        },
                                        'operation': 'not in'
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '"xyz"'
                                        }
                                    ]
                                }
                            ]
                        },
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A',
                                        'operand2': {
                                            'json_value': 'null'
                                        },
                                        'operation': '!='
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val':
                                                '{"B": "xyz", "D": "xyz"}'
                                        },
                                        {
                                            'json_operand1_val':
                                                '{"B": "abc", "D": "def"}'
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list != new alarm or_list; '
                'AND term with <=',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C.D'
                                        },
                                        'operation': '<=',
                                        'variables': ['A.C.N']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '5',
                                            'json_operand2_val': '7',
                                            'json_variables': {
                                                'A.C.N': '"test"'
                                            }
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C.D'
                                        },
                                        'operation': '<=',
                                        'variables': ['A.C.N']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '5',
                                            'json_operand2_val': '7',
                                            'json_variables': {
                                                'A.C.N': '"test1"'
                                            }
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list != new alarm or_list; '
                'AND term with <',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C.E'
                                        },
                                        'operation': '<',
                                        'variables': ['A.C.N', 'A.C.M']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '5',
                                            'json_operand2_val': '9',
                                            'json_variables': {
                                                'A.C.N': '"test1"',
                                                'A.C.M': '10'
                                            }
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B',
                                        'operand2': {
                                            'uve_attribute': 'A.C.E'
                                        },
                                        'operation': '<',
                                        'variables': ['A.C.N', 'A.C.M']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '5',
                                            'json_operand2_val': '9',
                                            'json_variables': {
                                                'A.C.N': '"test1"',
                                                'A.C.M': '100'
                                            }
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list != new alarm or_list; '
                'AND term with >=',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'P.Q',
                                        'operand2': {
                                            'uve_attribute': 'P.R.*.S'
                                        },
                                        'operation': '>=',
                                        'variables':
                                            ['P.R.__key', 'P.R.__value']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '100',
                                            'json_operand2_val': '20',
                                            'json_variables': {
                                                'P.R.__key': '"xyz"',
                                                'P.R.__value':
                                                    '{"C": "qwe", "D": 10}'
                                            }
                                        },
                                        {
                                            'json_operand1_val': '3',
                                            'json_operand2_val': '3',
                                            'json_variables': {
                                                'P.R.__key': '"lmo"',
                                                'P.R.__value': '{"D": 5}'
                                            }
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'P.Q',
                                        'operand2': {
                                            'uve_attribute': 'P.R.*.S'
                                        },
                                        'operation': '>=',
                                        'variables':
                                            ['P.R.__key', 'P.R.__value']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '100',
                                            'json_operand2_val': '20',
                                            'json_variables': {
                                                'P.R.__key': '"xyz"',
                                                'P.R.__value':
                                                    '{"C": "qwe", "D": 10}'
                                            }
                                        },
                                        {
                                            'json_operand1_val': '3',
                                            'json_operand2_val': '3',
                                            'json_variables': {
                                                'P.R.__key': '"abc"',
                                                'P.R.__value': '{"D": 5}'
                                            }
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list != new alarm or_list; '
                'AND term with >',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B.C',
                                        'operand2': {
                                            'json_value': '100'
                                        },
                                        'operation': '>',
                                        'variables': ['A.D']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '150',
                                            'json_variables': {
                                                'A.D': '"abc"'
                                            }
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B.C',
                                        'operand2': {
                                            'json_value': '100'
                                        },
                                        'operation': '>',
                                        'variables': ['A.D']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '150',
                                            'json_variables': {
                                                'A.D': '"def"'
                                            }
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            ),
            TestCase(name='old alarm or_list != new alarm or_list; '
                'AND term with range',
                input=TestInput(
                    old_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B.C',
                                        'operand2': {
                                            'json_value': '[500, 1000]'
                                        },
                                        'operation': 'range',
                                        'variables': ['A.B.D']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '500',
                                            'json_variables': {
                                                'A.B.D': 'null'
                                            }
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    new_alarm={'or_list': [
                        {
                            'and_list': [
                                {
                                    'condition': {
                                        'operand1': 'A.B.C',
                                        'operand2': {
                                            'json_value': '[500, 1000]'
                                        },
                                        'operation': 'range',
                                        'variables': ['A.B.D']
                                    },
                                    'match': [
                                        {
                                            'json_operand1_val': '500',
                                            'json_variables': {
                                                'A.B.D': '"test"'
                                            }
                                        }
                                    ]
                                }
                            ]
                        }
                    ]},
                    state=UVEAlarmState.Active
                ),
                output=TestOutput(result=False)
            )
        ]

        for test in tests:
            logging.info('=== Test: %s ===' % (test.name))
            old_alarm_obj = self.get_alarm_object(test.input.old_alarm)
            new_alarm_obj = self.get_alarm_object(test.input.new_alarm)
            asm = AlarmStateMachine(tab=None, uv=None, nm=None, activeTimer=0,
                    idleTimer=None, freqCheck_Times=0, freqCheck_Seconds=0,
                    freqExceededCheck=False, sandesh=self._ag._sandesh)
            asm.set_uai(old_alarm_obj)
            asm.uas.state = test.input.state
            self.assertEqual(test.output.result,
                asm.is_new_alarm_same(new_alarm_obj))
    # end test_06_is_new_alarm_same


# end class TestAlarmGen


def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT, _term_handler)
    unittest.main(verbosity=2, catchbreak=True)
