#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from gevent import monkey
monkey.patch_all()

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from nodemgr.vrouter_nodemgr.vrouter_process_stat import VrouterProcessStat
from pysandesh.sandesh_base import sandesh_global
from sandesh_common.vns.ttypes import Module
from loadbalancer_stats import LoadbalancerStatsUVE


class VrouterEventManager(EventManager):
    def __init__(self, config, rule_file, unit_names):
        type_info = EventManagerTypeInfo(
            module_type=Module.COMPUTE_NODE_MGR,
            object_table='ObjectVRouter',
            sandesh_packages=['vrouter.loadbalancer'])
        super(VrouterEventManager, self).__init__(config, type_info, rule_file,
                sandesh_global, unit_names, update_process_list=True)
        self.lb_stats = LoadbalancerStatsUVE(self.logger)
    # end __init__

    def get_process_stat_object(self, pname):
        return VrouterProcessStat(pname, self.logger)
    # end get_process_stat_object

    def do_periodic_events(self):
        self.event_tick_60()
        # loadbalancer processing
        self.lb_stats.send_loadbalancer_stats()
    # end do_periodic_events

    def nodemgr_sighup_handler(self):
        self.update_current_process()
        super(VrouterEventManager, self).nodemgr_sighup_handler()
    # end nodemgr_sighup_handler

# end class VrouterEventManager
