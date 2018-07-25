#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()
import os
import sys
import socket
import json
import time
import datetime
import select
import gevent

from nodemgr.common.event_manager import EventManager
from nodemgr.common.cassandra_manager import CassandraManager

from supervisor import childutils

from pysandesh.sandesh_base import *
from pysandesh.sandesh_session import SandeshWriter
from pysandesh.sandesh_logger import SandeshLogger
from pysandesh.gen_py.sandesh_trace.ttypes import SandeshTraceRequest
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, NodeTypeNames,\
    Module2NodeType, SERVICE_CONTRAIL_DATABASE, UVENodeTypeNames

from nodemgr.common.sandesh.nodeinfo.ttypes import *
from nodemgr.common.sandesh.nodeinfo.cpuinfo.ttypes import *
from nodemgr.common.sandesh.nodeinfo.process_info.ttypes import *
from nodemgr.common.sandesh.nodeinfo.process_info.constants import *
from pysandesh.connection_info import ConnectionState
from database.sandesh.database.ttypes import \
    CassandraStatusData, CassandraCompactionTask

class DatabaseEventManager(EventManager):
    def __init__(self, rule_file, discovery_server,
                 discovery_port, collector_addr,
                 hostip, db_port, minimum_diskgb, contrail_databases,
                 cassandra_repair_interval,
                 cassandra_repair_logdir):
        self.node_type = "contrail-database"
        self.uve_node_type = UVENodeTypeNames[NodeType.DATABASE]
        self.table = "ObjectDatabaseInfo"
        self.module = Module.DATABASE_NODE_MGR
        self.module_id = ModuleNames[self.module]
        self.hostip = hostip
        self.db_port = db_port
        self.minimum_diskgb = minimum_diskgb
        self.contrail_databases = contrail_databases
        self.cassandra_repair_interval = cassandra_repair_interval
        self.cassandra_repair_logdir = cassandra_repair_logdir
        self.cassandra_mgr = CassandraManager(self.cassandra_repair_logdir,
                                              'analyticsDb', self.table,
                                              self.contrail_databases,
                                              self.hostip, self.minimum_diskgb,
                                              self.db_port)
        if os.path.exists('/tmp/supervisord_database.sock'):
            self.supervisor_serverurl = "unix:///tmp/supervisord_database.sock"
        else:
            self.supervisor_serverurl = "unix:///var/run/supervisord_database.sock"
        self.add_current_process()
        node_type = Module2NodeType[self.module]
        node_type_name = NodeTypeNames[node_type]
        self.sandesh_global = sandesh_global
        EventManager.__init__(
            self, rule_file, discovery_server,
            discovery_port, collector_addr, sandesh_global, send_build_info = True)
        self.sandesh_global = sandesh_global
        if self.rule_file is '':
            self.rule_file = "/etc/contrail/" + \
                "supervisord_database_files/contrail-database.rules"
        json_file = open(self.rule_file)
        self.rules_data = json.load(json_file)
        _disc = self.get_discovery_client()
        sandesh_global.init_generator(
            self.module_id, socket.gethostname(), node_type_name,
            self.instance_id, self.collector_addr, self.module_id, 8103,
            ['database.sandesh', 'nodemgr.common.sandesh'], _disc)
        sandesh_global.set_logging_params(enable_local_log=True)
        ConnectionState.init(sandesh_global, socket.gethostname(), self.module_id,
            self.instance_id,
            staticmethod(ConnectionState.get_process_state_cb),
            NodeStatusUVE, NodeStatus, self.table)
        self.send_system_cpu_info()
        self.third_party_process_dict = {}
        self.third_party_process_dict["cassandra"] = "Dcassandra-pidfile=.*cassandra\.pid"
        self.third_party_process_dict["zookeeper"] = "org.apache.zookeeper.server.quorum.QuorumPeerMain"
    # end __init__

    def msg_log(self, msg, level):
        self.sandesh_global.logger().log(SandeshLogger.get_py_logger_level(
                            level), msg)

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

    def get_failbits_nodespecific_desc(self, fail_status_bits):
        description = ""
        if fail_status_bits & self.FAIL_STATUS_DISK_SPACE:
            description += "Disk for analytics db is too low," + \
                " cassandra stopped."
        if fail_status_bits & self.FAIL_STATUS_SERVER_PORT:
            if description != "":
                description += " "
            description += "Cassandra state detected DOWN."
        if fail_status_bits & self.FAIL_STATUS_DISK_SPACE_NA:
            description += "Disk space for analytics db not retrievable."
        return description

    def runforever(self, test=False):
        self.prev_current_time = int(time.time())
        while 1:
            # we explicitly use self.stdin, self.stdout, and self.stderr
            # instead of sys.* so we can unit test this code
            headers, payload = self.listener_nodemgr.wait(
                self.stdin, self.stdout)

            # self.stderr.write("headers:\n" + str(headers) + '\n')
            # self.stderr.write("payload:\n" + str(payload) + '\n')

            pheaders, pdata = childutils.eventdata(payload + '\n')
            # self.stderr.write("pheaders:\n" + str(pheaders)+'\n')
            # self.stderr.write("pdata:\n" + str(pdata))

            # check for process state change events
            if headers['eventname'].startswith("PROCESS_STATE"):
                self.event_process_state(pheaders, headers)
            # check for flag value change events
            if headers['eventname'].startswith("PROCESS_COMMUNICATION"):
                self.event_process_communication(pdata)
            # do periodic events
            if headers['eventname'].startswith("TICK_60"):
                self.cassandra_mgr.database_periodic(self)
                self.event_tick_60()
            self.listener_nodemgr.ok(self.stdout)

    def process(self):
        self.cassandra_mgr.process(self)
    # end process
