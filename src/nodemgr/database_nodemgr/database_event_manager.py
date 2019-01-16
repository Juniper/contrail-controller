#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()
import os
import socket

import yaml

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from nodemgr.common.cassandra_manager import CassandraManager


from pysandesh.sandesh_base import sandesh_global
from pysandesh.sandesh_logger import SandeshLogger

from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ThreadPoolNames

from database.sandesh.database.ttypes import \
    CassandraStatusData, CassandraCompactionTask

class DatabaseEventManager(EventManager):
    def __init__(self, rule_file,unit_names, discovery_server,
                 discovery_port, collector_addr,sandesh_config,
                 hostip, db_port, minimum_diskgb, contrail_databases,
                 cassandra_repair_interval,
                 cassandra_repair_logdir):
        type_info = EventManagerTypeInfo(
            package_name = 'contrail-database-common',
            object_table = "ObjectDatabaseInfo",
            module_type = Module.DATABASE_NODE_MGR,
            supervisor_serverurl = "unix:///var/run/supervisord_database.sock",
            third_party_processes =  {
                "cassandra" : "Dcassandra-pidfile=.*cassandra\.pid",
                "zookeeper" : "org.apache.zookeeper.server.quorum.QuorumPeerMain"
            },
            sandesh_packages = ['database.sandesh'],
            unit_names = unit_names)
        EventManager.__init__(
            self, type_info, rule_file, discovery_server,
            discovery_port, collector_addr, sandesh_global, sandesh_config)
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
        self.send_system_cpu_info()
        # Initialize tpstat structures
        self.cassandra_status_old = CassandraStatusData()
        self.cassandra_status_old.cassandra_compaction_task = CassandraCompactionTask()
        self.cassandra_status_old.thread_pool_stats = []
    # end __init__






    def get_failbits_nodespecific_desc(self, fail_status_bits):
        description = ""
        proc = subprocess.Popen(cqlsh_cmd, shell=True, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, close_fds=True)
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



    def process(self):
        self.load_rules_data()
                    (ret_value, error_value) = subprocess.Popen(
                        cmd_str, shell=True, stdout=subprocess.PIPE,
                        close_fds=True).communicate()
        self.cassandra_mgr.process(self)
    # end process

    def do_periodic_events(self):
        self.database_periodic()
        self.event_tick_60()
    # end do_periodic_events
# end class DatabaseEventManager
