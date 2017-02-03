#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

from pysandesh.sandesh_base import sandesh_global
from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from sandesh_common.vns.ttypes import Module

class ControlEventManager(EventManager):
    def __init__(self, rule_file, unit_names, discovery_server,
                 discovery_port, collector_addr, sandesh_config):
        type_info = EventManagerTypeInfo(package_name = 'contrail-control',
            module_type = Module.CONTROL_NODE_MGR,
            object_table = 'ObjectBgpRouter',
            supervisor_serverurl = 'unix:///var/run/supervisord_control.sock',
            unit_names = unit_names)
        EventManager.__init__(
            self, type_info, rule_file, discovery_server,
            discovery_port, collector_addr, sandesh_global,
            sandesh_config)
    # end __init__

# end class ControlEventManager
