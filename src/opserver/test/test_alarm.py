#!/usr/bin/env python

#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import gevent
import json
import signal
import logging
import mock
import unittest
import collections
from utils.util import retry
from collections import namedtuple
from kafka.common import OffsetAndMessage,Message

from opserver.uveserver import UVEServer
from opserver.partition_handler import PartitionHandler, UveStreamProc, \
    UveStreamer, UveStreamPart, PartInfo
from opserver.alarmgen import Controller
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
    def __init__(self, partno, logger, cb, pi, rpass):
        self._cb = cb
        self._partno = partno
        self._pi = pi
        self._started = False

    def start(self):
        self._started = True

    def kill(self):
        self._started = False

    def __call__(self, key, type, value):
        if self._started:
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
        self.ustr = UveStreamer(logging, None, None, self.mock_agp, 2, None, Mock_usp)
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
                ["ObjectXX","uve1","type1","xx"],\
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
                ["ObjectXX","uve1","type1","xx"],\
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


    @mock.patch('opserver.alarmgen.Controller.send_agg_uve')
    @mock.patch.object(UVEServer, 'get_part')
    @mock.patch.object(UVEServer, 'get_uve')
    @mock.patch('opserver.partition_handler.SimpleConsumer', autospec=True)
    # Test partition Initialization, including boot-straping using UVEServer
    # Test partition shutdown as well
    def test_00_init(self,
            mock_SimpleConsumer,
            mock_get_uve, mock_get_part, mock_send_agg_uve):

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
        

    @mock.patch('opserver.alarmgen.Controller.send_agg_uve')
    @mock.patch.object(UVEServer, 'get_part')
    @mock.patch.object(UVEServer, 'get_uve')
    @mock.patch('opserver.partition_handler.SimpleConsumer', autospec=True)
    # Test initialization followed by read from Kafka
    # Also test for deletetion of a boot-straped UVE
    def test_01_rxmsg(self,
            mock_SimpleConsumer,
            mock_get_uve, mock_get_part, mock_send_agg_uve):

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
                    message=Message(magic=0, attributes=0, key='',
                    value=('{"message":"UVEUpdate","key":"ObjectYY:uve2",'
                           '"type":"type2","gen":"gen1","coll":'
                           '"127.0.0.1:0","value":{}}')))
        mock_SimpleConsumer.return_value.get_messages.side_effect = \
            m_get_messages

        self._ag.disc_cb_coll([{"ip-address":"127.0.0.1","pid":0}])
        self._ag.libpart_cb([1])
        self.assertTrue(self.checker_dict([1, "ObjectXX", "uve1"], self._ag.ptab_info, False))
        self.assertTrue(self.checker_dict([1, "ObjectYY", "uve2"], self._ag.ptab_info))
        self.assertTrue(self.checker_exact(\
            self._ag.ptab_info[1]["ObjectYY"]["uve2"].values(), {"type2" : {"yy": 1}}))

    @mock.patch('opserver.alarmgen.Controller.send_agg_uve')
    @mock.patch.object(UVEServer, 'get_part')
    @mock.patch.object(UVEServer, 'get_uve')
    @mock.patch('opserver.partition_handler.SimpleConsumer', autospec=True)
    # Test late bringup of collector
    # Also test collector shutdown
    def test_02_collectorha(self,
            mock_SimpleConsumer,
            mock_get_uve, mock_get_part, mock_send_agg_uve):

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
                    message=Message(magic=0, attributes=0, key='',
                    value=('{"message":"UVEUpdate","key":"ObjectYY:uve2",'
                           '"type":"type2","gen":"gen1","coll":'
                           '"127.0.0.5:0","value":{} }')))
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
                    message=Message(magic=0, attributes=0, key='',
                    value=('{"message":"UVEUpdate","key":"ObjectYY:uve2",'
                           '"type":"type2","gen":"gen1","coll":'
                           '"127.0.0.5:0","value":{}}')))
        self.assertTrue(self.checker_dict([1, "ObjectYY", "uve2"], self._ag.ptab_info))

        
        # Withdraw collector 127.0.0.1
        self.assertTrue(self.checker_dict([1, "ObjectXX", "uve1"], self._ag.ptab_info))
        del m_get_uve["ObjectXX:uve1"]
        self._ag.disc_cb_coll([{"ip-address":"127.0.0.5","pid":0}])
        self.assertTrue(self.checker_dict([1, "ObjectXX", "uve1"], self._ag.ptab_info, False))

def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT, _term_handler)
    unittest.main(verbosity=2, catchbreak=True)
