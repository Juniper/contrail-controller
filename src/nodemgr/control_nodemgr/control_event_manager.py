#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import os
from gevent import monkey
monkey.patch_all()

from pysandesh.sandesh_base import sandesh_global
from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from sandesh_common.vns.ttypes import Module

class ControlEventManager(EventManager):
    def __init__(self, config, rule_file, unit_names):

        if os.path.exists('/tmp/supervisord_control.sock'):
            supervisor_serverurl = "unix:///tmp/supervisord_control.sock"
        else:
            supervisor_serverurl = "unix:///var/run/supervisord_control.sock"
        type_info = EventManagerTypeInfo(package_name = 'contrail-control',
            module_type = Module.CONTROL_NODE_MGR,
            object_table = 'ObjectBgpRouter',
            supervisor_serverurl = supervisor_serverurl)
        super(ControlEventManager, self).__init__(config, type_info, rule_file,
            sandesh_global, unit_names)
    # end __init__

# end class ControlEventManager
