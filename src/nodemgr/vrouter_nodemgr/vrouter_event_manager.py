#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import os
from gevent import monkey
monkey.patch_all()

from nodemgr.common.event_manager import EventManager, EventManagerTypeInfo
from nodemgr.vrouter_nodemgr.vrouter_process_stat import VrouterProcessStat
from pysandesh.sandesh_base import sandesh_global
from sandesh_common.vns.ttypes import Module
from loadbalancer_stats import LoadbalancerStatsUVE

class VrouterEventManager(EventManager):
    def __init__(self, rule_file, unit_names,
                 collector_addr, sandesh_config):

        if os.path.exists('/tmp/supervisord_vrouter.sock'):
            supervisor_serverurl = "unix:///tmp/supervisord_vrouter.sock"
        else:
            supervisor_serverurl = "unix:///var/run/supervisord_vrouter.sock"

        type_info = EventManagerTypeInfo(
            package_name = 'contrail-vrouter-common',
            module_type = Module.COMPUTE_NODE_MGR,
            object_table = 'ObjectVRouter',
            supervisor_serverurl = supervisor_serverurl,
            unit_names = unit_names)
        EventManager.__init__(self, type_info, rule_file,
                              collector_addr, sandesh_global,
                              sandesh_config, update_process_list = True)
        self.lb_stats = LoadbalancerStatsUVE()
    # end __init__

    def get_process_stat_object(self, pname):
        return VrouterProcessStat(pname)
    # end get_process_stat_object

    def do_periodic_events(self):
        self.event_tick_60()
        # loadbalancer processing
        self.lb_stats.send_loadbalancer_stats()
    # end do_periodic_events

    def nodemgr_sighup_handler(self):
        self.update_current_process()
        config = ConfigParser.SafeConfigParser()
        config.read(self.config_file)
        if 'COLLECTOR' in config.sections():
            try:
                collector = config.get('COLLECTOR', 'server_list')
                collector_list = collector.split()
            except ConfigParser.NoOptionError as e:
                pass

        if collector_list:
            new_chksum = hashlib.md5("".join(collector_list)).hexdigest()
            if new_chksum != self.collector_chksum:
                self.collector_chksum = new_chksum
                self.random_collectors = \
                    random.sample(collector_list, len(collector_list))
            # Reconnect to achieve load-balance irrespective of list
            self.sandesh_instance.reconfig_collectors(self.random_collectors)
    # end nodemgr_sighup_handler

# end class VrouterEventManager
