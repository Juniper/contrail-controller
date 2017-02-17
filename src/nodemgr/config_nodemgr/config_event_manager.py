#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import os
from gevent import monkey
monkey.patch_all()

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo, \
    package_installed
from nodemgr.database_nodemgr.common import CassandraManager
from pysandesh.sandesh_base import sandesh_global
from sandesh_common.vns.ttypes import Module

class ConfigEventManager(EventManager):
    def __init__(self, rule_file, unit_names, collector_addr,
                 sandesh_config,
                 cassandra_repair_interval,
                 cassandra_repair_logdir):

        if os.path.exists('/tmp/supervisord_config.sock'):
            supervisor_serverurl = "unix:///tmp/supervisord_config.sock"
        else:
            supervisor_serverurl = "unix:///var/run/supervisord_config.sock"
        type_info = EventManagerTypeInfo(package_name = 'contrail-config',
            module_type = Module.CONFIG_NODE_MGR,
            object_table = 'ObjectConfigNode',
            supervisor_serverurl = supervisor_serverurl,
            unit_names = unit_names)
        EventManager.__init__(
            self, type_info, rule_file, collector_addr, sandesh_global,
            sandesh_config)
        self.cassandra_repair_interval = cassandra_repair_interval
        self.cassandra_repair_logdir = cassandra_repair_logdir
        self.cassandra_mgr = CassandraManager(cassandra_repair_logdir)
    # end __init__

    def do_periodic_events(self):
        db = package_installed('contrail-openstack-database')
        config_db = package_installed('contrail-database-common')
        if not db and config_db:
            # Record cluster status and shut down cassandra if needed
            self.cassandra_mgr.status()
            # Perform nodetool repair every cassandra_repair_interval hours
            if self.tick_count % (60 * self.cassandra_repair_interval) == 0:
                self.cassandra_mgr.repair()
        self.event_tick_60()
    # end do_periodic_events

# end class ConfigEventManager
