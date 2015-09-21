#!/usr/bin/env python

#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import sys
import gevent
import json
import signal
import logging
import mock
import unittest
import collections
from collections import namedtuple
from kafka.common import OffsetAndMessage,Message


class MockSnmpUve(mock.MagicMock):
    def __init__(self, *a, **kw):
        super(MockContrailVRouterApi, self).__init__()
        self._args = a
        self._dict = kw
        self._vifs = {}

mock_pkg = mock.MagicMock(name='mock_snmpuve')
mock_cls = mock.MagicMock(name='MockSnmpUve',
                                  side_effect=MockSnmpUve)
mock_pkg.SnmpUve = mock_cls

sys.modules['contrail_snmp_collector.snmpuve'] = mock_pkg
sys.modules['contrail_snmp_collector.snmpuve.SnmpUve'] = mock_cls

sys.modules['netsnmp'] = mock.MagicMock(name='mock_netsnmp')


from contrail_snmp_collector.snmpctrlr import MaxNinTtime


logging.basicConfig(level=logging.DEBUG,
    format='%(asctime)s %(levelname)s %(message)s')
logging.getLogger("stevedore.extension").setLevel(logging.WARNING)

# Tests for the PartitionHandler class
class TestMaxMinTime(unittest.TestCase):

    def setUp(self):
        self.mnt = MaxNinTtime(3, 1)

    def tearDown(self):
        pass

    #@unittest.skip('Skipping PartHandler test')
    def test_00_init(self):
        self.assertIsNotNone(self.mnt)

    def test_01_add(self):
        [self.mnt.add() for x in range(5)]
        self.assertFalse(self.mnt.ready4full_scan())

    def test_02_fs(self):
        [self.mnt.add() for x in range(3)]
        gevent.sleep(1)
        self.assertTrue(self.mnt.ready4full_scan())

def _term_handler(*_):
    raise IntSignal()

if __name__ == '__main__':
    gevent.signal(signal.SIGINT, _term_handler)
    unittest.main(verbosity=2, catchbreak=True)
