#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

from sandesh_common.vns.ttypes import Module

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo


class AnalyticsEventManager(EventManager):
    def __init__(self, config, unit_names):

        type_info = EventManagerTypeInfo(
            module_type=Module.ANALYTICS_NODE_MGR,
            object_table='ObjectCollectorInfo')
        super(AnalyticsEventManager, self).__init__(
            config, type_info, unit_names)
    # end __init__

# end class AnaltyicsEventManager
