#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from nodemgr.common.cassandra_manager import CassandraManager
from pysandesh.sandesh_base import sandesh_global
from sandesh_common.vns.ttypes import Module


class AnalyticsDatabaseEventManager(EventManager):
    def __init__(self, config, unit_names):
        table = 'ObjectDatabaseInfo'
        type_info = EventManagerTypeInfo(
            object_table=table,
            module_type=Module.DATABASE_NODE_MGR,
            sandesh_packages=['database.sandesh'])
        super(AnalyticsDatabaseEventManager, self).__init__(
            config, type_info, sandesh_global, unit_names)
        # TODO: try to understand is next needed here and use it or remove
        #self.cassandra_repair_interval = config.cassandra_repair_interval
        self.cassandra_mgr = CassandraManager(
            config.cassandra_repair_logdir, 'analytics', table,
            config.hostip, config.minimum_diskgb,
            config.db_port, config.db_jmx_port,
            config.db_user, config.db_password,
            self.process_info_manager)

    def get_failbits_nodespecific_desc(self, fail_status_bits):
        return self.cassandra_mgr.get_failbits_nodespecific_desc(
            self, fail_status_bits)

    def do_periodic_events(self):
        self.cassandra_mgr.database_periodic(self)
        super(AnalyticsDatabaseEventManager, self).do_periodic_events()
