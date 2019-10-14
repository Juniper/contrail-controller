#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey

from sandesh_common.vns.ttypes import Module

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo

monkey.patch_all()


class ControlEventManager(EventManager):
    def __init__(self, config, unit_names):

        type_info = EventManagerTypeInfo(
            module_type=Module.CONTROL_NODE_MGR,
            object_table='ObjectBgpRouter')
        super(ControlEventManager, self).__init__(config, type_info, unit_names)
    # end __init__

# end class ControlEventManager
