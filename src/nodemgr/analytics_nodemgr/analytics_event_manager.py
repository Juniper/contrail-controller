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
import gevent
import ConfigParser

from nodemgr.common.event_manager import EventManager

from pysandesh.sandesh_base import *
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames,\
    Module2NodeType
from subprocess import Popen, PIPE

from analytics.ttypes import \
    NodeStatusUVE, NodeStatus
from pysandesh.connection_info import ConnectionState
from analytics.process_info.ttypes import \
    ProcessStatus, ProcessState, ProcessInfo, DiskPartitionUsageStats
from analytics.process_info.constants import \
    ProcessStateNames


class AnalyticsEventManager(EventManager):
    def __init__(self, rule_file, discovery_server,
                 discovery_port, collector_addr):
        EventManager.__init__(
            self, rule_file, discovery_server,
            discovery_port, collector_addr, sandesh_global)
        self.node_type = 'contrail-analytics'
        self.module = Module.ANALYTICS_NODE_MGR
        self.module_id = ModuleNames[self.module]
        self.supervisor_serverurl = "unix:///tmp/supervisord_analytics.sock"
        self.add_current_process()
        node_type = Module2NodeType[self.module]
        node_type_name = NodeTypeNames[node_type]
        _disc = self.get_discovery_client()
        sandesh_global.init_generator(
            self.module_id, socket.gethostname(),
            node_type_name, self.instance_id, self.collector_addr,
            self.module_id, 8104, ['analytics'], _disc)
        sandesh_global.set_logging_params(enable_local_log=True)
        ConnectionState.init(sandesh_global, socket.gethostname(), self.module_id,
            self.instance_id,
            staticmethod(ConnectionState.get_process_state_cb),
            NodeStatusUVE, NodeStatus)
        self.send_sys_cpu_info()
    # end __init__

    def process(self):
        if self.rule_file is '':
            self.rule_file = "/etc/contrail/" + \
                "supervisord_analytics_files/contrail-analytics.rules"
        json_file = open(self.rule_file)
        self.rules_data = json.load(json_file)

    def send_process_state_db(self, group_names):
        self.send_process_state_db_base(
            group_names, ProcessInfo, NodeStatus, NodeStatusUVE)

    def send_nodemgr_process_status(self):
        self.send_nodemgr_process_status_base(
            ProcessStateNames, ProcessState, ProcessStatus,
            NodeStatus, NodeStatusUVE)

    def get_node_status(self):
        return NodeStatus

    def get_node_status_uve(self):
        return NodeStatusUVE

    def send_sys_cpu_info(self):
        self.send_sys_cpu_info_base(NodeStatus, NodeStatusUVE)

    def send_sys_mem_cpu_info(self):
        self.send_sys_mem_cpu_info_base(NodeStatus, NodeStatusUVE)

    def send_process_mem_cpu_info(self, process_mem_cpu_info, pstat):
        self.send_process_mem_cpu_info_base(process_mem_cpu_info, pstat)

    def get_process_state(self, fail_status_bits):
        return self.get_process_state_base(
            fail_status_bits, ProcessStateNames, ProcessState)

    def send_disk_usage_info(self):
        self.send_disk_usage_info_base(
            NodeStatusUVE, NodeStatus, DiskPartitionUsageStats)
