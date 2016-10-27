# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Sechmatransformer  amqp handler
"""

from cfgm_common.vnc_amqp import VncAmqpHandle
from config_db import DBBaseST, VirtualNetworkST


class STAmqpHandle(VncAmqpHandle):

    def __init__(self, logger, reaction_map, args):
        q_name_prefix = 'schema_transformer'
        super(STAmqpHandle, self).__init__(logger, DBBaseST, reaction_map,
                                           q_name_prefix, args=args)

    def evaluate_dependency(self):
        if not self.dependency_tracker:
            return
        super(STAmqpHandle, self).evaluate_dependency()
        for vn_id in self.dependency_tracker.resources.get(
                'virtual_network', []):
            vn = VirtualNetworkST.get(vn_id)
            if vn is not None:
                vn.uve_send()
