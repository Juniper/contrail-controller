#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

from pysandesh.sandesh_base import sandesh_global
from sandesh_common.vns.ttypes import Module

from nodemgr.common.cassandra_manager import CassandraManager
from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo


class ConfigEventManager(EventManager):
    def __init__(self, config, unit_names):
        type_info = EventManagerTypeInfo(
            module_type=Module.CONFIG_NODE_MGR,
            object_table='ObjectConfigNode',
            sandesh_packages=['database.sandesh'])
        super(ConfigEventManager, self).__init__(config, type_info,
            sandesh_global, unit_names)
        self.cassandra_repair_interval = config.cassandra_repair_interval
        self.cassandra_mgr = CassandraManager(
            config.cassandra_repair_logdir, 'config',
            config.contrail_databases, config.hostip, config.minimum_diskgb,
            config.db_port, config.db_jmx_port,
            config.db_user, config.db_password,
            self.process_info_manager)

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

    def do_periodic_events(self):
        self.cassandra_mgr.database_periodic(self)
        # Perform nodetool repair every cassandra_repair_interval hours
        if self.tick_count % (60 * self.cassandra_repair_interval) == 0:
            self.cassandra_mgr.repair()
        super(ConfigEventManager, self).do_periodic_events()
