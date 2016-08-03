#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()
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

from nodemgr.common.event_manager import EventManager
from nodemgr.vrouter_nodemgr.vrouter_process_stat import VrouterProcessStat

from ConfigParser import NoOptionError

from supervisor import childutils

from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.sandesh_session import SandeshWriter
from pysandesh.sandesh_logger import SandeshLogger
from pysandesh.gen_py.sandesh_trace.ttypes import SandeshTraceRequest
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames,\
    Module2NodeType, INSTANCE_ID_DEFAULT
from subprocess import Popen, PIPE
from StringIO import StringIO

from nodemgr.common.sandesh.nodeinfo.ttypes import *
from nodemgr.common.sandesh.nodeinfo.cpuinfo.ttypes import *
from nodemgr.common.sandesh.nodeinfo.process_info.ttypes import *
from nodemgr.common.sandesh.nodeinfo.process_info.constants import *
from pysandesh.connection_info import ConnectionState

from loadbalancer_stats import LoadbalancerStatsUVE


class VrouterEventManager(EventManager):
    def __init__(self, rule_file, discovery_server,
                 discovery_port, collector_addr):
        self.module = Module.COMPUTE_NODE_MGR
        self.module_id = ModuleNames[self.module]

        node_type = Module2NodeType[self.module]
        node_type_name = NodeTypeNames[node_type]
        self.sandesh_global = sandesh_global
        EventManager.__init__(self, rule_file, discovery_server,
                              discovery_port, collector_addr, sandesh_global)
        self.node_type = "contrail-vrouter"
        self.table = "ObjectVRouter"
        _disc = self.get_discovery_client()
        sandesh_global.init_generator(
            self.module_id, socket.gethostname(),
            node_type_name, self.instance_id, self.collector_addr,
            self.module_id, 8102, ['vrouter.loadbalancer',
                'nodemgr.common.sandesh'], _disc)
        sandesh_global.set_logging_params(enable_local_log=True)
        self.supervisor_serverurl = "unix:///var/run/supervisord_vrouter.sock"
        self.add_current_process()
        ConnectionState.init(sandesh_global, socket.gethostname(), self.module_id,
            self.instance_id,
            staticmethod(ConnectionState.get_process_state_cb),
            NodeStatusUVE, NodeStatus, self.table)

        self.lb_stats = LoadbalancerStatsUVE()
        self.send_system_cpu_info()
        self.third_party_process_dict = {}
    # end __init__

    def msg_log(self, msg, level):
        self.sandesh_global.logger().log(SandeshLogger.get_py_logger_level(
                            level), msg)

    def process(self):
        if self.rule_file is '':
            self.rule_file = \
                "/etc/contrail/supervisord_vrouter_files/" + \
                "contrail-vrouter.rules"
        json_file = open(self.rule_file)
        self.rules_data = json.load(json_file)

    def send_process_state_db(self, group_names):
        self.send_process_state_db_base(
            group_names, ProcessInfo)

    def send_nodemgr_process_status(self):
        self.send_nodemgr_process_status_base(
            ProcessStateNames, ProcessState, ProcessStatus)

    def get_node_third_party_process_dict(self):
        return self.third_party_process_dict 

    def get_process_state(self, fail_status_bits):
        return self.get_process_state_base(
            fail_status_bits, ProcessStateNames, ProcessState)

    def get_process_stat_object(self, pname):
        return VrouterProcessStat(pname)

    # overridden delete_process_handler -
    def delete_process_handler(self, deleted_process):
        super(VrouterEventManager,
              self).delete_process_handler(deleted_process)
    # end delete_process_handler

    def runforever(self, test=False):
        self.prev_current_time = int(time.time())
        while 1:
            # we explicitly use self.stdin, self.stdout, and self.stderr
            # instead of sys.* so we can unit test this code
            headers, payload = \
                self.listener_nodemgr.wait(self.stdin, self.stdout)

            # self.stderr.write("headers:\n" + str(headers) + '\n')
            # self.stderr.write("payload:\n" + str(payload) + '\n')

            pheaders, pdata = childutils.eventdata(payload + '\n')
            # self.stderr.write("pheaders:\n" + str(pheaders)+'\n')
            # self.stderr.write("pdata:\n" + str(pdata))

            # check for process state change events
            if headers['eventname'].startswith("PROCESS_STATE"):
                self.event_process_state(pheaders, headers)
                # check for addition / deletion of processes in the node.
                # Tor Agent process can get added / deleted based on need.
                self.update_current_process()

            # check for flag value change events
            if headers['eventname'].startswith("PROCESS_COMMUNICATION"):
                self.event_process_communication(pdata)
            # do periodic events
            if headers['eventname'].startswith("TICK_60"):
                self.event_tick_60()

                # loadbalancer processing
                self.lb_stats.send_loadbalancer_stats()

            self.listener_nodemgr.ok(self.stdout)
