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
    def __init__(self, config, rule_file, unit_names):

        if os.path.exists('/tmp/supervisord_analytics.sock'):
            supervisor_serverurl = "unix:///tmp/supervisord_analytics.sock"
        else:
            supervisor_serverurl = "unix:///var/run/supervisord_analytics.sock"
        type_info = EventManagerTypeInfo(package_name = 'contrail-analytics',
            module_type = Module.ANALYTICS_NODE_MGR,
            object_table = 'ObjectCollectorInfo',
            supervisor_serverurl = supervisor_serverurl,
            unit_names = unit_names)
        super(AnalyticsEventManager, self).__init__(config, type_info,
                rule_file, sandesh_global)
    # end __init__

# end class AnaltyicsEventManager
