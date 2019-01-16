#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo, \
    package_installed
from nodemgr.common.cassandra_manager import CassandraManager
from pysandesh.sandesh_logger import SandeshLogger


from pysandesh.sandesh_base import sandesh_global
from sandesh_common.vns.ttypes import Module

class ConfigEventManager(EventManager):
    def __init__(self, rule_file,unit_names,discovery_server,
                 discovery_port, collector_addr,
                 hostip, db_port, minimum_diskgb, contrail_databases,
                 cassandra_repair_interval,
                 cassandra_repair_logdir):
        type_info = EventManagerTypeInfo(package_name = 'contrail-config',
            module_type = Module.CONFIG_NODE_MGR,
            object_table = 'ObjectConfigNode',
            supervisor_serverurl = 'unix:///var/run/supervisord_config.sock',
            unit_names = unit_names)
        EventManager.__init__(
            self, type_info, rule_file, discovery_server,
            discovery_port, collector_addr, sandesh_global)
        self.cassandra_repair_interval = cassandra_repair_interval
        self.cassandra_repair_logdir = cassandra_repair_logdir
        self.hostip = hostip
        self.db_port = db_port
        self.minimum_diskgb = minimum_diskgb
        self.sandesh_global = sandesh_global #AddedJan24
        self.contrail_databases = contrail_databases
        self.cassandra_mgr = CassandraManager(self.cassandra_repair_logdir,
                                              'configDb',self.contrail_databases,
                                              self.hostip, self.minimum_diskgb,
                                              self.db_port)

        self.db = package_installed('contrail-openstack-database')
        self.config_db = package_installed('contrail-database-common')
        self.third_party_process_dict["cassandra"] = "Dcassandra-pidfile=.*cassandra\.pid"
        self.third_party_process_dict["zookeeper"] = "org.apache.zookeeper.server.quorum.QuorumPeerMain"
    # end __init__

    def msg_log(self, msg, level):
        self.sandesh_global.logger().log(SandeshLogger.get_py_logger_level(
                            level), msg)

    def do_periodic_events(self):
        if not self.db and self.config_db:
            self.cassandra_mgr.database_periodic(self)
            # Perform nodetool repair every cassandra_repair_interval hours
            if self.tick_count % (60 * self.cassandra_repair_interval) == 0:
                self.cassandra_mgr.repair()
        self.event_tick_60()
     # end do_periodic_events

    def get_failbits_nodespecific_desc(self, fail_status_bits):
        description = ""
        if fail_status_bits & self.FAIL_STATUS_DISK_SPACE:
            description += "Disk for config db is too low," + \
                " cassandra stopped."
        if fail_status_bits & self.FAIL_STATUS_SERVER_PORT:
            if description != "":
                description += " "
            description += "Cassandra state detected DOWN."
        if fail_status_bits & self.FAIL_STATUS_DISK_SPACE_NA:
            description += "Disk space for config db not retrievable."
        return description
