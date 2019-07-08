##
## Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
##

#!/usr/bin/env python

import argparse
from pyunitreport import HTMLTestRunner
import os
import sys
import unittest

sys.path.append('controller/src/bgp/test/cat/lib')
from cat import *

class TestCAT(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        CAT.initialize()
        return

    @classmethod
    def tearDownClass(cls):
        CAT.delete_all_files()
        return

    def setUp(self):
        self.test = unittest.TestCase.id(self)

    def tearDown(self):
        CAT.clean_up()

    def test_5_two_controlnodes_scale_agents(self):
        c1 = CAT.add_control_node(self.test, "con1")
        #c2 = CAT.add_control_node(self.test, "con2")
        agents = []
        for i in range(200):
            name = "agent" + str(i+1)
            agent = CAT.add_agent(self.test, name, [c1])
            agents.append(agent)
        ret, ms = CAT.check_connection([c1], agents, 5, 3)
        self.assertTrue(ret, msg=ms)

def main():

    CAT.parse_arguments()
    unittest.main(testRunner=HTMLTestRunner(output=CAT.get_report_dir()))

if __name__ == "__main__":

    main()
