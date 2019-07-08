##
## Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
##

import argparse
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
#    Main Class for Control-node Agent Test framework(CAT)   #
#                                                            #
##############################################################

class CAT:

    direc = ""
    pause = 0
    verbose = False
    p = 'none'

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
        CAT.__pause_exec()
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

        #signal.signal(signal.SIGINT, CAT.sig_cleanup)
        signal.signal(signal.SIGTERM, CAT.sig_cleanup)

    @staticmethod
    def add_control_node(test, name, port = 0):
        con = ControlNode(test, name, CAT.direc)
        con.logs = CAT.verbose
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
    def parse_arguments():
        parser = argparse.ArgumentParser()
        parser.add_argument('-v', '--verbose', help='Enable logs on Control Node',
                       action='store_true')
        parser.add_argument('-p', '--pause', type=int,
                       help='pause test-case for the given time(secs)')
        parser.add_argument('unittest_args', nargs='*')

        args = parser.parse_args()
        CAT.pause = args.pause
        CAT.verbose = args.verbose

        sys.argv[1:] = args.unittest_args

    @staticmethod
    def __pause_exec():
        if not CAT.pause:
            return
        cmd = ['/usr/bin/python']
        try:
            CAT.p = subprocess.Popen(cmd, stderr=subprocess.STDOUT, shell=False).wait()
        except KeyboardInterrupt:
            print("p is " + str(CAT.p))
            return

    @staticmethod
    def __check_connection_internal(control_node, agent, retry_time, retry_tries):
        while retry_tries >= 0:
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
                retry_tries -= 1
                time.sleep(retry_time)
                print ret
                print("Retrying. retry-time " + str(retry_time) + " seconds" + ". Retries " + str(retry_tries))
                continue
            return True, None
        return False, ret

    @staticmethod
    def check_connection(cons, agents, retry_time=3, retry_tries=3):
        for control_node in cons:
            for agent in agents:
                ret, e = CAT.__check_connection_internal(control_node,
                         agent, retry_time, retry_tries)
                if ret == False:
                    return ret, e
        return ret, e

##############################################################
#                                                            #
#         Parent Class for Components(Controller/Agent)      #
#                                                            #
##############################################################

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
        self.logs = False
        self.create_directory("control_node")

    def create_object(self, port):
        self.http_port = port
        self.create_component(True)

    def read_port_numbers(self):
        self.filename = self.user_dir + "/conf/" + str(self.pid) + ".txt"
        read = 10
        while(read):
            if os.path.exists(self.filename):
                with open(self.filename) as f:
                    data = json.load(f)
                    for p in data['ControllerDetails']:
                        self.xmpp_port = p['XMPPPORT']
                        self.bgp_port = p['BGPPORT']
                        self.http_port = p['HTTPPORT']
                read = 0
            else:
                time.sleep(1)
                read-=1

    def execute_child(self):
        # c1 = "/cs-shared/CAT/binaries/bgp_ifmap_xmpp_integration_test"
        c1 = "build/debug/bgp/test/bgp_ifmap_xmpp_integration_test"
        env = { "USER": self.user, "BGP_IFMAP_XMPP_INTEGRATION_TEST_SELF_NAME":
            "overcloud-contrailcontroller-1",
            "CAT_BGP_PORT": str(self.bgp_port),
            "CAT_XMPP_PORT": str(self.xmpp_port),
            "BGP_IFMAP_XMPP_INTEGRATION_TEST_INTROSPECT": str(self.http_port),
            "BGP_IFMAP_XMPP_INTEGRATION_TEST_PAUSE": "1",
            "" if self.logs else "LOG_DISABLE" : "1",
            "BGP_IFMAP_XMPP_INTEGRATION_TEST_DATA_FILE":
            "/cs-shared/CAT/configs/bulk_sync_2.json",
            "LD_LIBRARY_PATH": "build/lib",
            "CONTRAIL_CAT_FRAMEWORK": "1",
            "USER_DIR": str(self.user_dir)}

        args = []
        os.execve(c1, args, env)

    def restart_control_node(self):
        os.kill(self.pid, signal.SIGKILL)
        new_pid = os.fork()
        if new_pid == 0:
            self.execute_child()
        else:
            self.pid = new_pid

    def send_sighup(self):
        os.kill(self.pid, signal.SIGHUP)


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
        # c1 = "/cs-shared/CAT/binaries/contrail-vrouter-agent"
        c1 = "build/debug/vnsw/agent/contrail/contrail-vrouter-agent"
        env = { "LD_LIBRARY_PATH": "build/lib", "LOGNAME": os.environ.get('USER') }
        arg = [c1, "--config_file=" + self.conf_file]

        os.execve(c1, arg, env)

    def create_conf(self):
        sample_conf = \
            "controller/src/bgp/test/cat/lib/contrail-vrouter-agent.conf"
        conf = open(sample_conf, 'r')
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
