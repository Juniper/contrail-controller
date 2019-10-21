#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from nodemgr.common.cassandra_manager import CassandraManager
from sandesh_common.vns.ttypes import Module

monkey.patch_all()


class ConfigDatabaseEventManager(EventManager):
    def __init__(self, config, unit_names):
        table = 'ObjectConfigDatabaseInfo'
        type_info = EventManagerTypeInfo(
            object_table=table,
            module_type=Module.CONFIG_DATABASE_NODE_MGR,
            sandesh_packages=['database.sandesh'])
        super(ConfigDatabaseEventManager, self).__init__(
            config, type_info, unit_names)
        self.cassandra_repair_interval = config.cassandra_repair_interval
        self.cassandra_mgr = CassandraManager(
            config.cassandra_repair_logdir, 'config', table,
            config.hostip, config.minimum_diskgb,
            config.db_port, config.db_jmx_port, config.db_use_ssl,
            config.db_user, config.db_password,
            self.process_info_manager, hostname=config.hostname)

    def get_failbits_nodespecific_desc(self, fail_status_bits):
        return self.cassandra_mgr.get_failbits_nodespecific_desc(
            self, fail_status_bits)

    def do_periodic_events(self):
        self.cassandra_mgr.database_periodic(self)
        # Perform nodetool repair every cassandra_repair_interval hours
        if self.tick_count % (60 * self.cassandra_repair_interval) == 0:
            self.cassandra_mgr.repair(self)
        super(ConfigDatabaseEventManager, self).do_periodic_events()
