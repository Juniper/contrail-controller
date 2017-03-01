#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import os
from gevent import monkey
monkey.patch_all()

from pysandesh.sandesh_base import sandesh_global
from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from sandesh_common.vns.ttypes import Module

class AnalyticsEventManager(EventManager):
    def __init__(self, rule_file, unit_names, collector_addr, sandesh_config):

        if os.path.exists('/tmp/supervisord_analytics.sock'):
            supervisor_serverurl = "unix:///tmp/supervisord_analytics.sock"
        else:
            supervisor_serverurl = "unix:///var/run/supervisord_analytics.sock"
        type_info = EventManagerTypeInfo(package_name = 'contrail-analytics',
            module_type = Module.ANALYTICS_NODE_MGR,
            object_table = 'ObjectCollectorInfo',
            supervisor_serverurl = supervisor_serverurl,
            unit_names = unit_names)
        EventManager.__init__(
            self, type_info, rule_file, collector_addr, sandesh_global, 
            sandesh_config)
    # end __init__

# end class AnaltyicsEventManager
