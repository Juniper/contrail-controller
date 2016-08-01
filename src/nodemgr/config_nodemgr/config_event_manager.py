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

from nodemgr.common.event_manager import EventManager, package_installed
from nodemgr.database_nodemgr.common import CassandraManager

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

from nodemgr.common.sandesh.nodeinfo.ttypes import *
from nodemgr.common.sandesh.nodeinfo.cpuinfo.ttypes import *
from nodemgr.common.sandesh.nodeinfo.process_info.ttypes import *
from nodemgr.common.sandesh.nodeinfo.process_info.constants import *
from pysandesh.connection_info import ConnectionState

class ConfigEventManager(EventManager):
    def __init__(self, rule_file, discovery_server,
                 discovery_port, collector_addr,
                 cassandra_repair_interval,
                 cassandra_repair_logdir):
        self.node_type = "contrail-config"
        self.table = "ObjectConfigNode"
        self.module = Module.CONFIG_NODE_MGR
        self.module_id = ModuleNames[self.module]
        self.cassandra_repair_interval = cassandra_repair_interval
        self.cassandra_repair_logdir = cassandra_repair_logdir
        self.cassandra_mgr = CassandraManager(cassandra_repair_logdir)
        self.supervisor_serverurl = "unix:///var/run/supervisord_config.sock"
        self.add_current_process()
        node_type = Module2NodeType[self.module]
        node_type_name = NodeTypeNames[node_type]
        self.sandesh_global = sandesh_global
        EventManager.__init__(
            self, rule_file, discovery_server,
            discovery_port, collector_addr, sandesh_global)
        _disc = self.get_discovery_client()
        sandesh_global.init_generator(
            self.module_id, socket.gethostname(),
            node_type_name, self.instance_id, self.collector_addr,
            self.module_id, 8100, ['nodemgr.common.sandesh'], _disc)
        sandesh_global.set_logging_params(enable_local_log=True)
        ConnectionState.init(sandesh_global, socket.gethostname(),
		self.module_id, self.instance_id,
		staticmethod(ConnectionState.get_process_state_cb),
                NodeStatusUVE, NodeStatus, self.table)
        self.send_system_cpu_info()
        self.third_party_process_dict = {}
    # end __init__

    def process(self):
        if self.rule_file is '':
            self.rule_file = "/etc/contrail/" + \
                "supervisord_config_files/contrail-config.rules"
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

    def do_periodic_events(self):
        db = package_installed('contrail-opesntack-database')
        config_db = package_installed('contrail-database-common')
        if not db and config_db:
            # Record cluster status and shut down cassandra if needed
            self.cassandra_mgr.status()
        self.event_tick_60()
        if not db and config_db:
            # Perform nodetool repair every cassandra_repair_interval hours
            if self.tick_count % (60 * self.cassandra_repair_interval) == 0:
                self.cassandra_mgr.repair()
