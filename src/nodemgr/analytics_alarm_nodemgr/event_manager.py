#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

from pysandesh.sandesh_base import sandesh_global
from sandesh_common.vns.ttypes import Module

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo


class AnalyticsAlarmEventManager(EventManager):
    def __init__(self, config, unit_names):

        type_info = EventManagerTypeInfo(
            module_type=Module.ANALYTICS_ALARM_NODE_MGR,
            object_table='ObjectAnalyticsAlarmInfo')
        super(AnalyticsAlarmEventManager, self).__init__(
            config, type_info, sandesh_global, unit_names)
    # end __init__

# end class AnalyticsAlarmEventManager
