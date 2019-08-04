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
import uuid
from colorama import Fore, Back, Style

objects = []

##############################################################
#                                                            #
#    Main Class for Control-node Agent Test framework(CAT)   #
#                                                            #
##############################################################

class CAT:

    directory = ""
    pause = False
    verbose = False
    net_id = 0
    ip = ''

    @staticmethod
    def get_report_dir():
        if not os.path.exists(CAT.reportdir):
            os.makedirs(CAT.reportdir)
        return CAT.reportdir

    @staticmethod
    def clean_up():
        print("Called clean-up")
        CAT.__pause_exec()
        CAT.net_id = 0
        for item in objects:
            os.kill(item.pid, signal.SIGKILL)

    @staticmethod
    def sig_cleanup(sig, frame):
        print("Called signal handler here")
        CAT.clean_up()

    @staticmethod
    def __get_ip_from_string(ips):
        ips = ips.split()
        for i in range(len(ips)):
            ip = ips[i-1]
            split = ip.split(".")
            if split[0] is '172' or split[0] is '192' or split[0] is '127':
                continue
            else:
                return ip

    @staticmethod
    def initialize():
        logdir = CAT.logdir+"/"+str(datetime.datetime.now()).replace(" ","_")
        if not os.path.exists(logdir):
            os.makedirs(logdir)
        print(Fore.YELLOW+ "Logs are in " + logdir + Style.RESET_ALL)
        CAT.logdir = logdir

        ips = os.popen('hostname -i').read()
        CAT.ip = CAT.__get_ip_from_string(ips)

        #signal.signal(signal.SIGINT, CAT.sig_cleanup)
        signal.signal(signal.SIGTERM, CAT.sig_cleanup)

    @staticmethod
    def add_control_node(test, name, port = 0, conf=""):
        con = ControlNode(test, name, CAT.logdir)
        con.logs = CAT.verbose
        con.create_object(port, conf)
        objects.append(con)
        return con

    @staticmethod
    def add_agent(test, name, objs):
        agent = Agent(test, name, CAT.logdir)
        agent.create_agent(objs)
        objects.append(agent)
        return agent

    @staticmethod
    def create_config_file(test, name):
        conf = Configuration(test, name, CAT.logdir)
        return conf

    @staticmethod
    def delete_all_files():
        return
        print("Removing all directories and files")
        directory = CAT.logdir
        shutil.rmtree(directory, ignore_errors=True)

    @staticmethod
    def parse_arguments():
        parser = argparse.ArgumentParser()
        parser.add_argument('-v', '--verbose', help='Enable logs on Control '
                       'Node', action='store_true')
        parser.add_argument('-p', '--pause', action='store_true',
                       help='pause every test-case after each run')
        parser.add_argument('-l', '--logdir',
                            default="/tmp/CAT/" + os.environ['USER'],
                            help='Redirect log files to this directory')
        parser.add_argument('-r', '--reportdir',
            default="/cs-shared/CAT/reports/" + os.environ['USER'],
            help='Generate test report in this directory')
        parser.add_argument('unittest_args', nargs='*')

        args = parser.parse_args()
        CAT.pause = args.pause
        CAT.verbose = args.verbose
        CAT.reportdir = args.reportdir
        CAT.logdir = args.logdir

        sys.argv[1:] = args.unittest_args

    @staticmethod
    def __pause_exec():
        if CAT.pause is False:
            return
        cmd = ['/usr/bin/python']
        try:
            subprocess.call(cmd, stderr=subprocess.STDOUT, shell=True)
        except KeyboardInterrupt:
            return

    @staticmethod
    def __check_connection_internal(control_node, agent, retry_time,
                                    retry_tries):
        ret = ""
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
                print("Retrying. retry-time " + str(retry_time) + " seconds" +
                      ". Retries " + str(retry_tries))
                continue
            ret = (str(control_node.name) + " connected to " + str(agent.name))
            return True, ret
        return False, ret

    @staticmethod
    def check_connection(cons, agents, retry_time=3, retry_tries=3):
        for control_node in cons:
            for agent in agents:
                ret, msg = CAT.__check_connection_internal(control_node,
                         agent, retry_time, retry_tries)
                if ret == False:
                    #print msg
                    return ret, msg
        return ret, msg

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

        if component is "Configuration":
            if not os.path.exists(self.user_dir):
                os.makedirs(self.user_dir)
            return

        self.conf_file_dir = self.user_dir + "/conf"
        if not os.path.exists(self.conf_file_dir):
                os.makedirs(self.conf_file_dir)

        self.log_files = self.user_dir + "/log"
        if not os.path.exists(self.log_files):
            os.makedirs(self.log_files)

    def redirect_stdouterr(self):
            old = os.dup(1)
            os.close(1)
            f = self.log_files + "/" + self.name + ".log"
            open(f, 'w')
            os.open(f, os.O_WRONLY)

            old = os.dup(2)
            os.close(2)
            f = self.log_files + "/" + self.name + ".log"
            open(f, 'w')
            os.open(f, os.O_WRONLY)

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
        self.conf_file = ""

    def create_object(self, port, conf):
        self.http_port = port
        self.conf_file = conf
        self.create_component(True)

    def read_port_numbers(self):
        self.filename = self.user_dir + "/conf/" + str(self.pid) + ".json"
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
        print(Fore.YELLOW+ "Control-Node Introspect for " + self.name + " is " +
              "http://" + CAT.ip + ":" + str(self.http_port) + Style.RESET_ALL)

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
            "" if not self.conf_file else 
            "BGP_IFMAP_XMPP_INTEGRATION_TEST_DATA_FILE": str(self.conf_file),
            "LD_LIBRARY_PATH": "build/lib",
            "CONTRAIL_CAT_FRAMEWORK": "1",
            "USER_DIR": str(self.user_dir)}

        self.redirect_stdouterr()
        os.execve(c1, [], env)

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
        env = {
                  "LD_LIBRARY_PATH": "build/lib",
                  "LOGNAME": os.environ.get('USER')
              }
        arg = [c1, "--config_file=" + self.conf_file]
        self.redirect_stdouterr()
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

##############################################################
#                                                            #
#            Class for Configuration                         #
#                                                            #
##############################################################

class Configuration(Component):
    def __init__(self, test, name, home):
        self.name = name
        self.test = test
        self.filename = ""
        self.user = home
        self.user_dir = ""
        self.comp = ""
        self.create_directory("Configuration")

    def __create_basic_template(self):
        basic_template = str("[{ \"operation\": \"db_sync\", \"OBJ_FQ_NAME_TABLE"
                         +"\": { \"bgp_router\": {}, \"virtual_network\":{},"+
                         "\"virtual_router\":{}, \"project\":{}, " +
                         "\"virtual_machine\":{},  \"domain\":{}," +
                         "\"network_ipam\":{}, \"routing_instance\": {}, " +
                         "\"route_aggregate\":{} }," +
                         "\"db\": {} }]")
        return json.loads(basic_template)

    def __get_or_create_conf_file(self):
        bas_temp = ""
        if not self.filename:
            #print("No conf file yet. Creating one.")
            bas_temp = self.__create_basic_template()
            self.filename = str(self.user_dir + "/" + self.name + ".conf")
        else:
            #print("Loading Conf file")
            with open(self.filename) as json_f:
                bas_temp = json.load(json_f)

        return bas_temp

    def add_virtual_network(self, name, num=1, domain='default-domain',
                           project='default-project'):

        bas_temp = self.__get_or_create_conf_file()
        if num is 1:
            vn = Virtual_Network(bas_temp[0], name, domain, project)
            bas_temp[0] = vn.create_virtual_network()
        else:
            while num>0:
                int_name = name + "_" + str(num)
                vn = Virtual_Network(bas_temp[0], int_name, domain, project)
                bas_temp[0] = vn.create_virtual_network()
                num -= 1

        print(self.filename)
        with open(self.filename, 'w') as out_file:
            json.dump(bas_temp, out_file, indent=4)

##############################################################
#                                                            #
#            Class for Virtual-Network                       #
#                                                            #
##############################################################

class Virtual_Network:
    def __init__(self, base_temp, name, domain, project):
        self.base_temp = base_temp
        self.name = name
        self.domain = domain
        self.project = project
        self.project_uid = ''
        self.route_inst = ''
        self.key = ''
        self.uid = ''

    def create_virtual_network(self):
        table = self.base_temp['OBJ_FQ_NAME_TABLE']
        db = self.base_temp['db']
        virt_net = table['virtual_network']

        self.__virt_net_set_key_uuid()
        virt_net.update({self.key: "null"})
        prop = self.__get_virt_net_properties()
        db.update({str(self.uid): prop})
        return self.base_temp

    def __virt_net_set_key_uuid(self):
        self.uid = uuid.uuid3(uuid.NAMESPACE_DNS, self.name)
        self.key = str(self.domain + ":" + self.project + ":" + self.name + ":"
                   + str(self.uid))

    def __get_virt_net_properties(self):
        prop = {}
        prop.update({"META:latest_col_ts": "null"})

        route_ins = str("children:routing_instance:" +
                    str(Routing_Instance.get_or_create_routing_instance
                    (self)))
        prop.update({route_ins :"null"})

        fq_name = str("[\"" + self.domain + "\", \"" + self.project + "\", \""
                  + self.name + "\"]")
        prop.update({"fq_name": fq_name})

        self.project_uid = Project.get_or_create_project(self)
        project = str("parent:project:" + str(self.project_uid))
        prop.update({project : "null"})

        prop.update({"parent_type": "\"project\""})
        perms2 = str("{\"owner\": \"cloud-admin\", \"owner_access\": 7, " +
                 "\"global_access\": 0, \"share\": []}")
        prop.update({"prop:perms2": perms2})
        prop.update({"type": "\"virtual_network\""})

        perms = str("{\"enable\": true, \"uuid\": {\"uuid_mslong\": " +
                str((self.uid).int>>64) + ", \"uuid_lslong\": " +
                str((self.uid).int & ((1<<64)-1)) + "}, \"creator\": null," +
                " \"created\": \"" +
                str(datetime.datetime.now()).replace(' ', 'T') +
                "\", \"user_visible\": true, \"last_modified\": \"" +
                str(datetime.datetime.now()).replace(' ', 'T') +
                "\", \"permissions\": {\"owner\": \"cloud-admin\", " +
                "\"owner_access\": 7, \"other_access\": 7, \"group\": \"" +
                "cloud-admin-group\", \"group_access\": 7}, " +
                "\"description\": null}")
        prop.update({"prop:id_perms": perms})

        CAT.net_id+=1
        prop.update({"prop:virtual_network_network_id": str(CAT.net_id)})

        return prop

##############################################################
#                                                            #
#            Class for Routing Instance                      #
#                                                            #
##############################################################

class Routing_Instance:

    def __init__(self, base_temp, domain, project, name, uid):
        self.uid = ''
        self.domain = domain
        self.project = project
        self.vn_name = name
        self.vn_uid = uid
        self.base_temp = base_temp
        self.key = ''
        self.route_tar=''

    @staticmethod
    def get_or_create_routing_instance(virt_net):
        if virt_net.route_inst is '':
            route_i = Routing_Instance(virt_net.base_temp, virt_net.domain,
                          virt_net.project, virt_net.name, virt_net.uid)
            route_i.create_routing_instance()
            return route_i.uid
        else:
            return route_inst.uid

    def create_routing_instance(self):

        table = self.base_temp['OBJ_FQ_NAME_TABLE']
        db = self.base_temp['db']
        route_inst = table['routing_instance']

        self.__route_inst_set_key_uuid()
        route_inst.update({self.key: "null"})
        prop = self.__get_route_inst_properties()
        db.update({str(self.uid): prop})
        return

    def __route_inst_set_key_uuid(self):
        self.uid = uuid.uuid1()
        self.key = str(self.domain + ":" + self.project + ":" + self.vn_name +
                   ":" + self.vn_name + ":" + str(self.uid))

    def __get_route_inst_properties(self):
        prop = {}
        prop.update({"META:latest_col_ts": "null"})

        fq_name = str("[\"" + self.domain + "\", \"" + self.project + "\", \""
                  + self.vn_name + "\", \"" + self.vn_name + "\"]")
        prop.update({"fq_name": fq_name})

        parent = str("parent:virtual_network:" + str(self.vn_uid))
        prop.update({parent : "null"})

        prop.update({"parent_type": "\"virtual_network\""})
        perms2 = str("{\"owner\": \"cloud-admin\", \"owner_access\": 7, " +
                 "\"global_access\": 0, \"share\": []}")
        prop.update({"prop:perms2": perms2})
        prop.update({"type": "\"routing_instance\""})
        prop.update({"prop:routing_instance_has_pnf": "false"})
        prop.update({"prop:routing_instance_is_default": "true"})
        prop.update({"prop:static_route_entries": "{\"route\": []}"})

        route_tar = str("ref:route_target:" +
                    str(Route_Target.get_or_create_route_target
                    (self)))
        prop.update({route_tar :"{\"attr\": {\"import_export\": null}}"})

        perms = str("{\"enable\": true, \"uuid\": {\"uuid_mslong\": " +
                str((self.uid).int>>64) + ", \"uuid_lslong\": " +
                str((self.uid).int & ((1<<64)-1)) + "}, \"creator\": null," +
                " \"created\": \"" +
                str(datetime.datetime.now()).replace(' ', 'T') +
                "\", \"user_visible\": true, \"last_modified\": \"" +
                str(datetime.datetime.now()).replace(' ', 'T') +
                "\", \"permissions\": {\"owner\": \"cloud-admin\", " +
                "\"owner_access\": 7, \"other_access\": 7, \"group\": \"" +
                "cloud-admin-group\", \"group_access\": 7}, " +
                "\"description\": null}")
        prop.update({"prop:id_perms": perms})

        return prop


##############################################################
#                                                            #
#            Class for Route-Target                          #
#                                                            #
##############################################################

class Route_Target:

    def __init__(self):
        self.uid = ''

    @staticmethod
    def get_or_create_route_target(route_inst):
        if route_inst.route_tar is '':
            #table = self.base_temp['OBJ_FQ_NAME_TABLE']
            #rout_inst = table['routing_instance']
            #if rout_inst:

            rou_tar = Route_Target()
            rou_tar.create_route_target()
            return rou_tar.uid
        else:
            return rou_tar.uid

    def create_route_target(self):
        self.uid = uuid.uuid1()
        return
"""
    def __init__(self, base_temp, domain, project, name, uid):
        self.uid = ''
        self.domain = domain
        self.project = project
        self.vn_name = name
        self.vn_uid = uid
        self.base_temp = base_temp
        self.key = ''
        self.route_tar=''

    @staticmethod
    def get_or_create_route_target(route_inst):
        if virt_net.route_inst is '':
            route_i = Routing_Instance(virt_net.base_temp, virt_net.domain,
                          virt_net.project, virt_net.name, virt_net.uid)
            route_i.create_routing_instance()
            return route_i.uid
        else:
            return route_inst.uid

    def create_route_target(self):

        table = self.base_temp['OBJ_FQ_NAME_TABLE']
        db = self.base_temp['db']
        route_inst = table['routing_instance']

        self.__route_inst_set_key_uuid()
        route_inst.update({self.key: "null"})
        prop = self.__get_route_inst_properties()
        db.update({str(self.uid): prop})
        return

    def __route_target_set_key_uuid(self):
        self.uid = uuid.uuid1()
        self.key = str(self.domain + ":" + self.project + ":" + self.vn_name +
                   ":" + self.vn_name + ":" + str(self.uid))

    def __get_route_target_properties(self):
        prop = {}
        prop.update({"META:latest_col_ts": "null"})

        fq_name = str("[\"" + self.domain + "\", \"" + self.project + "\", \""
                  + self.vn_name + "\", \"" + self.vn_name + "\"]")
        prop.update({"fq_name": fq_name})

        parent = str("parent:virtual_network:" + str(self.vn_uid))
        prop.update({parent : "null"})

        prop.update({"parent_type": "\"virtual_network\""})
        perms2 = str("{\"owner\": \"cloud-admin\", \"owner_access\": 7, " +
                 "\"global_access\": 0, \"share\": []}")
        prop.update({"prop:perms2": perms2})
        prop.update({"type": "\"routing_instance\""})
        prop.update({"prop:routing_instance_has_pnf": "false"})
        prop.update({"prop:routing_instance_is_default": "true"})
        prop.update({"prop:static_route_entries": "{\"route\": []}"})

        route_tar = str("ref:route_target:" +
                    str(Route_Target.get_or_create_route_target
                    (self)))
        prop.update({route_tar :"{\"attr\": {\"import_export\": null}}"})

        perms = str("{\"enable\": true, \"uuid\": {\"uuid_mslong\": " +
                str((self.uid).int>>64) + ", \"uuid_lslong\": " +
                str((self.uid).int & ((1<<64)-1)) + "}, \"creator\": null," +
                " \"created\": \"" +
                str(datetime.datetime.now()).replace(' ', 'T') +
                "\", \"user_visible\": true, \"last_modified\": \"" +
                str(datetime.datetime.now()).replace(' ', 'T') +
                "\", \"permissions\": {\"owner\": \"cloud-admin\", " +
                "\"owner_access\": 7, \"other_access\": 7, \"group\": \"" +
                "cloud-admin-group\", \"group_access\": 7}, " +
                "\"description\": null}")
        prop.update({"prop:id_perms": perms})

        return prop
"""
##############################################################
#                                                            #
#            Class for Project                               #
#                                                            #
##############################################################

class Project:

    def __init__(self, base_temp, domain, project, uid):
        self.uid = ''
        self.domain_uid = ''
        self.domain = domain
        self.project = project
        self.vn_uid = uid
        self.base_temp = base_temp
        self.key = ''

    @staticmethod
    def get_or_create_project(virt_net):
        proj = Project(virt_net.base_temp, virt_net.domain,
                       virt_net.project, virt_net.uid)
        proj.create_project()
        return proj.uid

    def create_project(self):

        table = self.base_temp['OBJ_FQ_NAME_TABLE']
        db = self.base_temp['db']
        project = table['project']

        self.__project_set_key_uuid()
        project.update({self.key: "null"})
        prop = self.__get_project_properties()
        db.update({str(self.uid): prop})
        return

    def __project_set_key_uuid(self):
        self.uid = uuid.uuid1()
        self.key = str(self.domain + ":" + self.project + ":" + str(self.uid))

    def __get_project_properties(self):
        prop = {}
        prop.update({"META:latest_col_ts": "null"})

        fq_name = str("[\"" + self.domain + "\", \"" + self.project + "\"]")
        prop.update({"fq_name": fq_name})

        self.domain_uid = Domain.get_or_create_domain(self)
        parent = str("parent:domain:" + str(self.domain_uid))
        prop.update({parent : "null"})

        prop.update({"parent_type": "\"domain\""})
        perms2 = str("{\"owner\": \"cloud-admin\", \"owner_access\": 7, " +
                 "\"global_access\": 0, \"share\": []}")
        prop.update({"prop:perms2": perms2})
        prop.update({"type": "\"project\""})
        prop.update({"prop:quota": "{\"defaults\": -1}"})

        child = str("children:virtual_network:" + str(self.vn_uid))
        prop.update({child : "null"})

        perms = str("{\"enable\": true, \"uuid\": {\"uuid_mslong\": " +
                str((self.uid).int>>64) + ", \"uuid_lslong\": " +
                str((self.uid).int & ((1<<64)-1)) + "}, \"creator\": null," +
                " \"created\": \"" +
                str(datetime.datetime.now()).replace(' ', 'T') +
                "\", \"user_visible\": true, \"last_modified\": \"" +
                str(datetime.datetime.now()).replace(' ', 'T') +
                "\", \"permissions\": {\"owner\": \"cloud-admin\", " +
                "\"owner_access\": 7, \"other_access\": 7, \"group\": \"" +
                "cloud-admin-group\", \"group_access\": 7}, " +
                "\"description\": null}")
        prop.update({"prop:id_perms": perms})

        return prop

##############################################################
#                                                            #
#            Class for Domain                                #
#                                                            #
##############################################################

class Domain:

    def __init__(self, base_temp, domain, project):
        self.uid = ''
        self.domain = domain
        self.project = project
        self.base_temp = base_temp
        self.key = ''

    @staticmethod
    def get_or_create_domain(project):
        dom = Domain(project.base_temp, project.domain, project.uid)
        dom.create_domain()
        return dom.uid

    def create_domain(self):

        table = self.base_temp['OBJ_FQ_NAME_TABLE']
        db = self.base_temp['db']
        domain = table['domain']

        self.__domain_set_key_uuid()
        domain.update({self.key: "null"})
        prop = self.__get_domain_properties()
        db.update({str(self.uid): prop})
        return

    def __domain_set_key_uuid(self):
        self.uid = uuid.uuid1()
        self.key = str(self.domain + ":" + str(self.uid))

    def __get_domain_properties(self):
        prop = {}
        prop.update({"META:latest_col_ts": "null"})

        fq_name = str("[\"" + self.domain + "\"]")
        prop.update({"fq_name": fq_name})

        perms2 = str("{\"owner\": \"cloud-admin\", \"owner_access\": 7, " +
                 "\"global_access\": 0, \"share\": [{\"tenant_access\": 6," +
                 " \"tenant\": \"domain:" + str(self.uid) +"\"}]}")
        prop.update({"prop:perms2": perms2})
        prop.update({"type": "\"domain\""})

        child = str("children:project:" + str(self.project))
        prop.update({child : "null"})

        perms = str("{\"enable\": true, \"uuid\": {\"uuid_mslong\": " +
                str((self.uid).int>>64) + ", \"uuid_lslong\": " +
                str((self.uid).int & ((1<<64)-1)) + "}, \"creator\": null," +
                " \"created\": \"" +
                str(datetime.datetime.now()).replace(' ', 'T') +
                "\", \"user_visible\": true, \"last_modified\": \"" +
                str(datetime.datetime.now()).replace(' ', 'T') +
                "\", \"permissions\": {\"owner\": \"cloud-admin\", " +
                "\"owner_access\": 7, \"other_access\": 7, \"group\": \"" +
                "cloud-admin-group\", \"group_access\": 7}, " +
                "\"description\": null}")
        prop.update({"prop:id_perms": perms})

        return prop
