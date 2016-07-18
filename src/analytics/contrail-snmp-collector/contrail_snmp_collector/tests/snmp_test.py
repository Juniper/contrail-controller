#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import unittest
import tempfile

import sys, os, mock, gevent
#sys.path.insert(0, os.path.abspath(".."))
#sys.path.append('../../tools/sandesh/library/python')

mock_pkg = mock.MagicMock(name='mock_vnc_api')
mock_mod = mock.MagicMock(name='mock_vnc_api_mod')
mock_cls = mock.MagicMock(name='mock_VncApi')
mock_mod.VncApi = mock_cls
mock_pkg.vnc_api = mock_mod

sys.modules['vnc_api'] = mock_pkg
sys.modules['vnc_api.vnc_api'] = mock_mod

from contrail_snmp_collector.device_config import DeviceConfig
from contrail_snmp_collector.snmpctrlr import MaxNinTtime


class SnmpTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        pass
        #if (os.getenv('LD_LIBRARY_PATH', '').find('build/lib') < 0):
        #    if (os.getenv('DYLD_LIBRARY_PATH', '').find('build/lib') < 0):
        #        assert(False)

        #cls.cassandra_port = AnalyticsTest.get_free_port()
        #mockcassandra.start_cassandra(cls.cassandra_port)

    @classmethod
    def tearDownClass(cls):
        pass
        #mockcassandra.stop_cassandra(cls.cassandra_port)

    def write_file(self, s):
        f = tempfile.NamedTemporaryFile(delete=False)
        f.write(s)
        f.close()
        return f.name

    def write_cfg_file(self):
        return self.write_file('[DEFAULTS]\nfile = dev.ini')

    def write_dev_file(self):
        return self.write_file('[1.1.1.1]\nCommunity = public\nVersion = 2')

    def delete_file(self, filename):
        if os.path.exists(filename):
            os.unlink(filename)

    def systestsetUp(self):
        self.cfgfl = self.write_cfg_file()
        self.devfl = self.write_dev_file()
        self.http_port = 9042
        self.dfgp = CfgParser(
                '-c %s --file %s --http_server_port %d' % (self.cfgfl,
                    self.devfl, self.http_port))
        self.tasks = []
        self.vizd_obj = None


    def systesttearDown(self):
        self.delete_file(self.cfgfl)
        self.delete_file(self.cfgfl)

    def test_000_snmp_devcfg(self):
        #logging.info("%%% test_000_snmp_devcfg %%%")
        self.assertEqual(1, 1)

class MaxNinTtimeTest(unittest.TestCase):
    def setUp(self):
        self.n = 5
        self.t = 5
        self.mntt = MaxNinTtime(self.n, self.t)

    def test_000_addone(self):
        self.assertEqual(len(self.mntt._slots), self.n)
        self.mntt.add()
        self.assertEqual(self.mntt._pointer, 1)

    def test_010_addmore(self):
        ts = [self.mntt.add() for i in range(self.n + self.n/2)]
        self.assertEqual(len(filter(lambda p: p>self.t/2, [t-ts[
                        i-1] for i,t in enumerate(ts)][1:])), 1) #one jump

    def test_020_fs(self):
        ts = [self.mntt.add() for i in range(self.n + self.n/2)]
        self.assertFalse(self.mntt.ready4full_scan())
        gevent.sleep(self.t + .1)
        self.assertTrue(self.mntt.ready4full_scan())

if __name__ == '__main__':
    unittest.main(catchbreak=True)
