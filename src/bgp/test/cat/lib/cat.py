##
## Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
##

import datetime
import json
import os
import shutil
import signal
import subprocess
import sys
import time

objects = []

##############################################################
#                                                            #
#       Main Control-node Agent Test framework Class         #
#                                                            #
##############################################################

class CAT:

    direc = ""

    @staticmethod
    def get_report_dir():
        user = os.environ.get('USER')
        udir = '/cs-shared/CAT/reports/' + user
        if not os.path.exists(udir):
            os.makedirs(udir)
        return udir

    @staticmethod
    def clean_up():
        print("Called clean-up")
        for item in objects:
            os.kill(item.pid, signal.SIGKILL)
            # delete file from /tmp/CAT/<user-id>/pid.txt

    @staticmethod
    def sig_cleanup(sig, frame):
        print("Called signal handler here")
        CAT.clean_up()

    @staticmethod
    def initialize():
        main = "/tmp/CAT/"
        user = main + os.environ.get('USER')
        date = user + "/" + str(datetime.datetime.now())

        if not os.path.exists(date):
            os.makedirs(date)

        CAT.direc = date # /tmp/CAT/UNAME/DATE

        signal.signal(signal.SIGINT, CAT.sig_cleanup)
        signal.signal(signal.SIGTERM, CAT.sig_cleanup)

    @staticmethod
    def add_control_node(test, name, port):
        con = ControlNode(test, name, CAT.direc)
        con.create_object(port)
        objects.append(con)
        return con

    @staticmethod
    def add_agent(test, name, objs):
        agent = Agent(test, name, CAT.direc)
        agent.create_agent(objs)
        objects.append(agent)
        return agent

    @staticmethod
    def delete_all_files():
        print("Removing all directories and files")
        direc = CAT.direc
        shutil.rmtree(direc, ignore_errors=False)

    @staticmethod
    def __check_connection_internal(control_nodes, agents):
        for control_node in control_nodes:
            for agent in agents:
                url = ("curl -s \'http://127.0.0.1:" +
                    str(control_node.http_port) + "/Snh_BgpNeighborReq?x=" +
                    agent.name + "\' | xmllint --format  - | grep -i state | "
                    + "grep -i Established")
                try:
                    r = subprocess.check_output(url, stderr=subprocess.STDOUT,
                        shell=True)
                except subprocess.CalledProcessError as event:
                    ret = (str(control_node.name) + " not connected to " +
                        str(agent.name))
                    return False, ret
        return True, None

    @staticmethod
    def check_connection(cons, agents, retry_time=3, retry_tries=3):
        while retry_tries >= 0:
            ret, e = CAT.__check_connection_internal(cons, agents)
            if ret == False:
                print("Retrying. retry-time " + str(retry_time) + "seconds")
                retry_tries -= 1
                time.sleep(retry_time)
            else:
                return ret, e
        return ret, e

class Component:
    def create_component(self, read_port):
	new_pid = os.fork()
        if new_pid == 0:
            self.pid = os.getpid()
            self.execute_child()
        else:
            self.pid = new_pid
            if read_port:
            	self.read_port_numbers()

    def create_directory(self, component):
        directory = self.user
        test = directory + "/" + self.test
        self.comp = test + "/" + component
	self.user_dir = self.comp + "/" + self.name

	self.conf_file_dir = self.user_dir + "/conf"
	if not os.path.exists(self.conf_file_dir):
            os.makedirs(self.conf_file_dir)

        self.log_files = self.user_dir + "/log"
        if not os.path.exists(self.log_files):
            os.makedirs(self.log_files)

##############################################################
#                                                            #
#            Class for Control-Node                          #
#                                                            #
##############################################################

class ControlNode(Component):
    def __init__(self, test, name, home):
        self.name = name
        self.test = test
        self.pid = 0
        self.http_port = 0
        self.bgp_port = 0
        self.xmpp_port = 0
        self.filename = ""
        self.user = home
        self.user_dir = ""
        self.comp = ""
        self.conf_file_dir = ""
        self.log_files = ""
        self.create_directory("control_node")

    def create_object(self, port):
        self.http_port = port
        self.create_component(True)

    def read_port_numbers(self):
        self.filename = self.user_dir + "/" + str(self.http_port) + ".txt"
        read = 10
        while(read):
            if os.path.exists(self.filename):
                with open(self.filename) as f:
                    data = json.load(f)
                    for p in data['ControllerDetails']:
                        self.xmpp_port = p['XMPPPORT']
                        self.bgp_port = p['BGPPORT']
                read = 0
            else:
		time.sleep(1)
		read-=1
            continue

    def execute_child(self):

        c1 = "/cs-shared/CAT/binaries/bgp_ifmap_xmpp_integration_test"

        env = { "USER": self.user, "BGP_IFMAP_XMPP_INTEGRATION_TEST_SELF_NAME":
            "overcloud-contrailcontroller-1",
            "BGP_IFMAP_XMPP_INTEGRATION_TEST_INTROSPECT": str(self.http_port),
            "BGP_IFMAP_XMPP_INTEGRATION_TEST_PAUSE": "1", "LOG_DISABLE": "1",
            "USER_DIR": str(self.user_dir)}

        args = []
        os.execve(c1, args, env)


##############################################################
#                                                            #
#            Class for Agent                                 #
#                                                            #
##############################################################

class Agent(Component):
    def __init__(self, test, name, home):
        self.test = test
        self.pid = 0
        self.xmpp_port = 0
        self.conf_file = ""
        self.name = name
        self.user = home
        self.user_dir = ""
        self.comp = ""
        self.conf_file_dir = ""
        self.log_files = ""
        self.create_directory("agent")

    def create_agent(self, objs):
        ports = []
        for i in objs:
            ports.append(i.xmpp_port)
        self.xmpp_ports = ports
        self.create_conf()
        self.create_component(False)

    def execute_child(self):
        c1 = "/cs-shared/CAT/binaries/contrail-vrouter-agent"
        arg = [c1, "--config_file=" + self.conf_file]

        os.execv(c1, arg)

    def create_conf(self):
        sample_conf = "/cs-shared/CAT/configs/contrail-vrouter-agent.conf"
        conf = open(sample_conf, 'r+')
        new_conf = []

        for line in conf:
            if 'xmpp_port' in line:
                line_c = line.split('=')
                line_c[1] = ""
                for p in self.xmpp_ports:
                    line_c[1] = line_c[1] + "127.0.0.1:" + str(p) + " "
                updated_line = "=".join(line_c)
                new_conf.append(updated_line)
            elif 'agent_name' in line:
                line_s = line.split('=')
                line_s[1] = str(self.name)
                updated_line = "=".join(line_s)
                new_conf.append(updated_line)
            elif 'log_file' in line:
                line_s = line.split('=')
                line_s[1] = (str(self.log_files) + "/" +
                            "contrail-vrouter-agent.log")
                updated_line = "=".join(line_s)
                new_conf.append(updated_line)
            else:
                new_conf.append(line)

        new_conf_file = (self.conf_file_dir + "/contrail-vrouter-agent_" +
                        self.name + ".conf")
        new_conf_f = open(new_conf_file, 'w')
        new_conf_f.writelines(new_conf)
        conf.close()
        new_conf_f.close()
        self.conf_file = new_conf_file
