#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import os
from gevent import monkey
monkey.patch_all()

import os
import subprocess
import socket
from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from nodemgr.common.cassandra_manager import CassandraManager
from pysandesh.sandesh_base import sandesh_global
from sandesh_common.vns.ttypes import Module

class DatabaseEventManager(EventManager):
    def __init__(self, config, rule_file, unit_names):

        if os.path.exists('/tmp/supervisord_database.sock'):
            supervisor_serverurl = "unix:///tmp/supervisord_database.sock"
        else:
            supervisor_serverurl = "unix:///var/run/supervisord_database.sock"
        self.table = 'ObjectDatabaseInfo'
        type_info = EventManagerTypeInfo(
            package_name = 'contrail-database-common',
            object_table = self.table,
            module_type = Module.DATABASE_NODE_MGR,
            supervisor_serverurl = supervisor_serverurl,
            third_party_processes =  {
                "cassandra" : "Dcassandra-pidfile=.*cassandra\.pid",
                "zookeeper" : "org.apache.zookeeper.server.quorum.QuorumPeerMain"
            },
            sandesh_packages = ['database.sandesh'],
            unit_names = unit_names)
        super(DatabaseEventManager, self).__init__(config, type_info, rule_file,
                sandesh_global)
        self.hostip = config.hostip
        self.db_port = config.db_port
        self.db_user = config.db_user
        self.db_password = config.db_password
        self.minimum_diskgb = config.minimum_diskgb
        self.contrail_databases = config.contrail_databases
        self.cassandra_repair_interval = config.cassandra_repair_interval
        self.cassandra_repair_logdir = config.cassandra_repair_logdir
        self.cassandra_mgr = CassandraManager(self.cassandra_repair_logdir,
                                              'analyticsDb', self.table,
                                              self.contrail_databases,
                                              self.hostip, self.minimum_diskgb,
                                              self.db_port, self.db_user, self.db_password)
    # end __init__

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

    def do_periodic_events(self):
        self.cassandra_mgr.database_periodic(self)
        self.event_tick_60()
    # end do_periodic_events

    def process(self):
        self.cassandra_mgr.process(self)
    # end process
# end class DatabaseEventManager
