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
import gevent
import ConfigParser

from nodemgr.process_stat import process_stat
from nodemgr.EventManager import EventManager

from pysandesh.sandesh_base import *
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames,\
    Module2NodeType
from subprocess import Popen, PIPE

from analytics.ttypes import \
    NodeStatusUVE, NodeStatus
from analytics.process_info.ttypes import \
    ProcessStatus, ProcessState, ProcessInfo
from analytics.process_info.constants import \
    ProcessStateNames

def usage():
    print doc
    sys.exit(255)

class AnalyticsEventManager(EventManager):
    def __init__(self, rule_file, discovery_server, discovery_port, collector_addr):
        EventManager.__init__(self, rule_file, discovery_server, discovery_port, collector_addr)
        self.node_type = 'contrail-analytics'
        self.module = Module.ANALYTICS_NODE_MGR
        self.module_id =  ModuleNames[self.module]
        self.supervisor_serverurl = "unix:///tmp/supervisord_analytics.sock"
        self.add_current_process(process_stat)
    #end __init__

    def process(self):
        if self.rule_file is '':
            self.rule_file = "/etc/contrail/supervisord_analytics_files/contrail-analytics.rules"
        json_file = open(self.rule_file)
        self.rules_data = json.load(json_file)
        node_type = Module2NodeType[self.module]
        node_type_name = NodeTypeNames[node_type]
        config_file = '/etc/contrail/contrail-analytics-nodemgr.conf'
        Config = self.read_config_data(config_file)
        self.get_collector_list(Config)
        _disc = self.get_discovery_client(Config)
        try:
            from opserver.sandesh.analytics.ttypes import *
            sandesh_pkg_dir = 'opserver.sandesh'
        except:
            from analytics.ttypes import *
            sandesh_pkg_dir = 'analytics'
        sandesh_global.init_generator(self.module_id, socket.gethostname(),
            node_type_name, self.instance_id, self.collector_addr,
            self.module_id, 8104, [sandesh_pkg_dir],_disc)
        sandesh_global.set_logging_params(enable_local_log=True)
        self.sandesh_global = sandesh_global

    def send_process_state_db(self, group_names):
        self.send_process_state_db_base(group_names, ProcessInfo, NodeStatus, NodeStatusUVE)

    def send_nodemgr_process_status(self):
        self.send_nodemgr_process_status_base(ProcessStateNames, ProcessState, ProcessStatus, NodeStatus, NodeStatusUVE)

    def get_process_state(self, fail_status_bits):
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
        return state, description
