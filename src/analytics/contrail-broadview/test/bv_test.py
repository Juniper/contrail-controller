#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
import unittest
import tempfile

import sys, os

class BroadviewTest(unittest.TestCase):
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

    def test_000_bv(self):
        #logging.info("%%% test_000_snmp_devcfg %%%")
        self.assertEqual(1, 1)


if __name__ == '__main__':
    unittest.main(catchbreak=True)
