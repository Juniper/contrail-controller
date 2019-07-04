##
## Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
##

from pyunitreport import HTMLTestRunner
import os
import sys
import unittest

sys.path.append(os.path.abspath(os.path.dirname(__file__))+'/../lib')

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

    def test_multi_controlnodes_single_agent(self):
        return
        c1 = CAT.add_control_node(self.test, "con1", 5946)
        c2 = CAT.add_control_node(self.test, "con2", 5956)
        a1 = CAT.add_agent(self.test, "agent1", [c1, c2])
        ret, ms = CAT.check_connection([c1, c2], [a1], 2, 3)
        self.assertTrue(ret, msg=ms)

    def test_single_controlnode_multi_agents(self):
        c1 = CAT.add_control_node(self.test, "con1", 5966)
        a1 = CAT.add_agent(self.test, "agent1", [c1])
        a2 = CAT.add_agent(self.test, "agent2", [c1])
        a3 = CAT.add_agent(self.test, "agent3", [c1])
        ret, ms = CAT.check_connection([c1], [a1, a2, a3], 2, 3)
        self.assertTrue(ret, msg=ms)
	c1.restart_control_node()
        ret, ms = CAT.check_connection([c1], [a1, a2, a3], 2, 3)
        self.assertTrue(ret, msg=ms)

    def test_multi_controlnodes_multi_agents(self):
        return
        c1 = CAT.add_control_node(self.test, "con1", 5976)
        c2 = CAT.add_control_node(self.test, "con2", 5979)
        c3 = CAT.add_control_node(self.test, "con3", 5996)
        a1 = CAT.add_agent(self.test, "agent1", [c1, c2])
        a2 = CAT.add_agent(self.test, "agent2", [c1, c3])
        ret, ms = CAT.check_connection([c1, c2], [a1], 2, 3)
        self.assertTrue(ret, msg=ms)
        ret, ms = CAT.check_connection([c1, c3], [a1, a2], 2, 3)
        self.assertFalse(ret, msg=ms)

def main():
    unittest.main(testRunner=HTMLTestRunner(output=CAT.get_report_dir()))

if __name__ == "__main__":
    main()
