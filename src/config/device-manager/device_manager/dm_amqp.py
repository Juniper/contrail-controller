# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Device Manager amqp handler
"""

from cfgm_common.vnc_amqp import VncAmqpHandle

from db import DBBaseDM, VirtualNetworkDM, PhysicalRouterDM


class DMAmqpHandle(VncAmqpHandle):

    def __init__(self, logger, reaction_map, args):
        q_name_prefix = 'device_manager'
        super(DMAmqpHandle, self).__init__(logger, DBBaseDM, reaction_map,
               q_name_prefix, args=args)

    def evaluate_dependency(self):
        if not self.dependency_tracker:
            return

        for vn_id in self.dependency_tracker.resources.get('virtual_network',
                                                           []):
            vn = VirtualNetworkDM.get(vn_id)
            if vn is not None:
                vn.update_instance_ip_map()

        for pr_id in self.dependency_tracker.resources.get('physical_router',
                                                           []):
            pr = PhysicalRouterDM.get(pr_id)
            if pr is not None:
                pr.set_config_state()
