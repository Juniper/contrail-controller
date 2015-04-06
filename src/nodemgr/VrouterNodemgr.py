doc = " "

from gevent import monkey; monkey.patch_all()
import os
import sys
import socket
import subprocess
import json
import time
import datetime
import platform
import select
import gevent
import ConfigParser

from nodemgr.EventManager import EventManager
from nodemgr.vrouter_process_stat import process_stat

from ConfigParser import NoOptionError

from supervisor import childutils

from pysandesh.sandesh_base import *
from pysandesh.sandesh_session import SandeshWriter
from pysandesh.gen_py.sandesh_trace.ttypes import SandeshTraceRequest
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames,\
    Module2NodeType, INSTANCE_ID_DEFAULT
from subprocess import Popen, PIPE
from StringIO import StringIO

from vrouter.vrouter.ttypes import \
    NodeStatusUVE, NodeStatus
from vrouter.vrouter.process_info.ttypes import \
    ProcessStatus, ProcessState, ProcessInfo
from vrouter.vrouter.process_info.constants import \
    ProcessStateNames

def usage():
    print doc
    sys.exit(255)

class VrouterEventManager(EventManager):
    def __init__(self, rule_file, discovery_server, discovery_port, collector_addr):
        EventManager.__init__(self, rule_file, discovery_server, discovery_port, collector_addr)
        self.node_type = "contrail-vrouter"
        self.module = Module.COMPUTE_NODE_MGR
        self.module_id =  ModuleNames[self.module]

        os_nova_comp = process_stat('openstack-nova-compute')
        (os_nova_comp_state, error_value) = Popen("openstack-status | grep openstack-nova-compute | cut -d ':' -f2", shell=True, stdout=PIPE).communicate()
        os_nova_comp.process_state = os_nova_comp_state.strip()
        if (os_nova_comp.process_state == 'active'):
            os_nova_comp.process_state = 'PROCESS_STATE_RUNNING'
            os_nova_comp.start_time = str(int(time.time()*1000000))
            os_nova_comp.start_count += 1
        if (os_nova_comp.process_state == 'dead'):
            os_nova_comp.process_state = 'PROCESS_STATE_FATAL'
        sys.stderr.write('Openstack Nova Compute status:' + os_nova_comp.process_state + "\n")
        self.process_state_db['openstack-nova-compute'] = os_nova_comp

        self.supervisor_serverurl = "unix:///tmp/supervisord_vrouter.sock"
        self.add_current_process(process_stat)
    #end __init__

    def process(self):
        if self.rule_file is '':
            self.rule_file = "/etc/contrail/supervisord_vrouter_files/contrail-vrouter.rules"
        json_file = open(self.rule_file)
        self.rules_data = json.load(json_file)
        node_type = Module2NodeType[self.module]
        node_type_name = NodeTypeNames[node_type]
        config_file = '/etc/contrail/contrail-vrouter-nodemgr.conf'
        Config = self.read_config_data(config_file)
        self.get_collector_list(Config)
        _disc = self.get_discovery_client(Config)
        sandesh_global.init_generator(self.module_id, socket.gethostname(),
            node_type_name, self.instance_id, self.collector_addr,
            self.module_id, 8102, ['vrouter.vrouter'],_disc)
        #sandesh_global.set_logging_params(enable_local_log=True)
        self.sandesh_global = sandesh_global

    def send_nodemgr_process_status(self):
        if (self.prev_fail_status_bits != self.fail_status_bits):
            self.prev_fail_status_bits = self.fail_status_bits
            fail_status_bits = self.fail_status_bits
            if fail_status_bits:
                state = ProcessStateNames[ProcessState.NON_FUNCTIONAL]
                description = ""
                if fail_status_bits & self.FAIL_STATUS_DISK_SPACE:
                    description += "Disk for analytics db is too low, cassandra stopped."
                if fail_status_bits & self.FAIL_STATUS_SERVER_PORT:
                    if description != "":
                        description += " "
                    description += "Cassandra state detected DOWN."
                if fail_status_bits & self.FAIL_STATUS_NTP_SYNC:
                    if description != "":
                        description += " "
                    description += "NTP state unsynchronized."
            else:
                state = ProcessStateNames[ProcessState.FUNCTIONAL]
                description = ''
            process_status = ProcessStatus(module_id = self.module_id, instance_id = self.instance_id, state = state,
                description = description)
            process_status_list = []
            process_status_list.append(process_status)
            node_status = NodeStatus(name = socket.gethostname(),
                process_status = process_status_list)
            node_status_uve = NodeStatusUVE(data = node_status)
            sys.stderr.write('Sending UVE:' + str(node_status_uve))
            node_status_uve.send()

    def send_process_state_db(self, group_names):
        self.send_process_state_db_base(group_names, ProcessInfo, NodeStatus, NodeStatusUVE)

    def runforever(self, test=False):
        prev_current_time = int(time.time())
        while 1:
            gevent.sleep(1)
            # we explicitly use self.stdin, self.stdout, and self.stderr
            # instead of sys.* so we can unit test this code
            headers, payload = self.listener_nodemgr.wait(self.stdin, self.stdout)

            #self.stderr.write("headers:\n" + str(headers) + '\n')
            #self.stderr.write("payload:\n" + str(payload) + '\n')

            pheaders, pdata = childutils.eventdata(payload+'\n')
            #self.stderr.write("pheaders:\n" + str(pheaders)+'\n')
            #self.stderr.write("pdata:\n" + str(pdata))

            # check for process state change events
            if headers['eventname'].startswith("PROCESS_STATE"):
                self.event_process_state(pheaders, headers)
            # check for flag value change events
            if headers['eventname'].startswith("PROCESS_COMMUNICATION"):
                self.event_process_communication(pdata)
            # do periodic events
            if headers['eventname'].startswith("TICK_60"):
                os_nova_comp = self.process_state_db['openstack-nova-compute']
                (os_nova_comp_state, error_value) = Popen("openstack-status | grep openstack-nova-compute | cut -d ':' -f2", shell=True, stdout=PIPE).communicate()
                if (os_nova_comp_state.strip() == 'active'):
                    os_nova_comp_state = 'PROCESS_STATE_RUNNING'
                if (os_nova_comp_state.strip() == 'dead'):
                    os_nova_comp_state = 'PROCESS_STATE_FATAL'
                if (os_nova_comp_state.strip() == 'inactive'):
                    os_nova_comp_state = 'PROCESS_STATE_STOPPED'
                if (os_nova_comp.process_state != os_nova_comp_state):
                    os_nova_comp.process_state = os_nova_comp_state.strip()
                    sys.stderr.write('Openstack Nova Compute status changed to:' + os_nova_comp.process_state + "\n")
                    if (os_nova_comp.process_state == 'PROCESS_STATE_RUNNING'):
                        os_nova_comp.start_time = str(int(time.time()*1000000))
                        os_nova_comp.start_count += 1
                    if (os_nova_comp.process_state == 'PROCESS_STATE_FATAL'):
                        os_nova_comp.exit_time = str(int(time.time()*1000000))
                        os_nova_comp.exit_count += 1
                    if (os_nova_comp.process_state == 'PROCESS_STATE_STOPPED'):
                        os_nova_comp.stop_time = str(int(time.time()*1000000))
                        os_nova_comp.stop_count += 1
                    self.process_state_db['openstack-nova-compute'] = os_nova_comp
                    self.send_process_state_db('vrouter_group')
                else:
                    sys.stderr.write('Openstack Nova Compute status unchanged at:' + os_nova_comp.process_state + "\n")
                self.process_state_db['openstack-nova-compute'] = os_nova_comp
                prev_current_time = self.event_tick_60(prev_current_time)
            self.listener_nodemgr.ok(self.stdout)
