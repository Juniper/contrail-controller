#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import socket
from gevent import monkey

from sandesh_common.vns.ttypes import Module
from nodemgr.vrouter_nodemgr.loadbalancer_stats import LoadbalancerStatsUVE

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from nodemgr.vrouter_nodemgr.process_stat import VrouterProcessStat

monkey.patch_all()


class VrouterEventManager(EventManager):
    def __init__(self, config, unit_names):
        type_info = EventManagerTypeInfo(
            module_type=Module.COMPUTE_NODE_MGR,
            object_table='ObjectVRouter',
            sandesh_packages=['vrouter.loadbalancer'])
        super(VrouterEventManager, self).__init__(
            config, type_info, unit_names, update_process_list=True)
        self.host_ip = config.hostip
        self.hostname = socket.getfqdn(self.hostip) \
            if config.hostname is None else config.hostname
        self.lb_stats = LoadbalancerStatsUVE(self.logger, self.host_ip,
                                             hostname=self.hostname)

    def get_process_stat_object(self, pname):
        return VrouterProcessStat(pname, self.host_ip, self.logger,
                                  hostname=self.hostname)

    def do_periodic_events(self):
        super(VrouterEventManager, self).do_periodic_events()
        # loadbalancer processing
        self.lb_stats.send_loadbalancer_stats()

    def nodemgr_sighup_handler(self):
        self.update_current_process()
        super(VrouterEventManager, self).nodemgr_sighup_handler()
