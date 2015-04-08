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
from nodemgr.process_stat import process_stat

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

from cfgm_common.uve.cfgm_cpuinfo.ttypes import \
    NodeStatusUVE, NodeStatus
from cfgm_common.uve.cfgm_cpuinfo.process_info.ttypes import \
    ProcessStatus, ProcessState, ProcessInfo, DiskPartitionUsageStats
from cfgm_common.uve.cfgm_cpuinfo.process_info.constants import \
    ProcessStateNames

def usage():
    print doc
    sys.exit(255)

class ConfigEventManager(EventManager):
    def __init__(self, rule_file, discovery_server, discovery_port, collector_addr):
        EventManager.__init__(self, rule_file, discovery_server, discovery_port, collector_addr)
        self.node_type = "contrail-config"
        self.module = Module.CONFIG_NODE_MGR
        self.module_id =  ModuleNames[self.module]
        self.supervisor_serverurl = "unix:///tmp/supervisord_config.sock"
        self.add_current_process(process_stat)
    #end __init__

    def process(self):
        if self.rule_file is '':
            self.rule_file = "/etc/contrail/supervisord_config_files/contrail-config.rules"
        json_file = open(self.rule_file)
        self.rules_data = json.load(json_file)
        node_type = Module2NodeType[self.module]
        node_type_name = NodeTypeNames[node_type]
        config_file = '/etc/contrail/contrail-config-nodemgr.conf'
        Config = self.read_config_data(config_file)
        self.get_collector_list(Config)
        _disc = self.get_discovery_client(Config)
        sandesh_global.init_generator(self.module_id, socket.gethostname(),
            node_type_name, self.instance_id, self.collector_addr,
            self.module_id, 8100, ['cfgm_common.uve'],_disc)
        #sandesh_global.set_logging_params(enable_local_log=True)
        self.sandesh_global = sandesh_global

    def send_process_state_db(self, group_names):
        self.send_process_state_db_base(group_names, ProcessInfo, NodeStatus, NodeStatusUVE)

    def send_nodemgr_process_status(self):
        self.send_nodemgr_process_status_base(ProcessStateNames, ProcessState, ProcessStatus, NodeStatus, NodeStatusUVE)

    def get_process_state(self, fail_status_bits):
        return self.get_process_state_base(fail_status_bits, ProcessStateNames, ProcessState)

    def send_disk_usage_info(self):
        self.send_disk_usage_info_base(NodeStatusUVE, NodeStatus, DiskPartitionUsageStats)
