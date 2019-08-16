#!/usr/bin/env python

##
## Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
##

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

    def test_1_agents(self):
        print(Fore.CYAN + "\nBring up 400 XMPP agent sessions" +
              Style.RESET_ALL)
        c1 = CAT.add_control_node(self.test, "control-node1")
        c2 = CAT.add_control_node(self.test, "control-node2")
        agents = []
        for i in range(400):
            name = "agent" + str(i+1)
            agent = CAT.add_agent(self.test, name, [c1, c2])
            agents.append(agent)
        ret, ms = CAT.check_connection([c1, c2], agents, 5, 10)
        self.assertTrue(ret, msg=ms)

    def test_2_config(self):
        print(Fore.CYAN + "\nInject 20000 virtual-networks configuration" +
              Style.RESET_ALL)
        conf1 = CAT.create_config_file(self.test, "conf1")
        conf1.add_virtual_network("vn", 20000, "default-domain", "admin")
        c1 = CAT.add_control_node(self.test, "control-node1",
                                  conf=conf1.filename)
        a1 = CAT.add_agent(self.test, "agent1", [c1])
        ret, ms = CAT.check_connection([c1], [a1], 15, 20)
        self.assertTrue(ret, msg=ms)

    def test_3_scale_agents_and_config(self):
        print(Fore.CYAN+"\nBring up 400 sessions + 20000 virtual-networks" +
              Style.RESET_ALL)
        conf1 = CAT.create_config_file(self.test, "conf1")
        conf1.add_virtual_network("cat-vn", 20000, "default-domain", "admin")
        c1 = CAT.add_control_node(self.test, "control-node1",
                                  conf=conf1.filename)
        c2 = CAT.add_control_node(self.test, "control-node2", conf=conf1.filename)
        agents = []
        for i in range(400):
            name = "agent" + str(i+1)
            agent = CAT.add_agent(self.test, name, [c1, c2])
            agents.append(agent)
        ret, ms = CAT.check_connection([c1], agents, 15, 20)
        self.assertTrue(ret, msg=ms)

    def test_4_all_config(self):
        print(Fore.CYAN + "\nInjecting all Configuration" + Style.RESET_ALL)
        conf1 = CAT.create_config_file(self.test, "conf1")
        conf1.add_virtual_network("vn", 1)
        conf1.add_virtual_machine()
        conf1.add_virtual_router("agent1")
        c1 = CAT.add_control_node(self.test, "control-node1",
                                  conf=conf1.filename)
        c2 = CAT.add_control_node(self.test, "control-node2",
                                  conf=conf1.filename)
        c3 = CAT.add_control_node(self.test, "control-node3",
                                  conf=conf1.filename)
        a1 = CAT.add_agent(self.test, "agent1", [c1, c2])
        ret, ms = CAT.check_connection([c1], [a1], 5, 10)
        self.assertTrue(ret, msg=ms)
        conf1.add_virtual_port()
        b1 = conf1.add_bgp_router(c1, c1.name, "127.0.0.1")
        b2 = conf1.add_bgp_router(c2, c2.name, "127.0.0.2")
        b3 = conf1.add_bgp_router(c3, c3.name, "127.0.0.3")
        conf1.connect_bgp_routers(b1, b2)
        conf1.connect_bgp_routers(b1, b3)
        conf1.connect_bgp_routers(b2, b3)
        c1.restart_control_node()
        c2.restart_control_node()
        c3.restart_control_node()
        ret, ms = CAT.check_connection([c1], [a1], 5, 10)
        self.assertTrue(ret, msg=ms)

def main():

    CAT.parse_arguments()
    unittest.main(testRunner=HTMLTestRunner(output=CAT.get_report_dir()))

if __name__ == "__main__":

    main()
