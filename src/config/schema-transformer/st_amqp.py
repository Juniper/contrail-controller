# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Sechmatransformer  amqp handler
"""

from cfgm_common.vnc_amqp import VncAmqpHandle
from config_db import DBBaseST, VirtualNetworkST
from schema_transformer.sandesh.traces.ttypes import MessageBusNotifyTrace,\
                DependencyTrackerResource


class STAmqpHandle(VncAmqpHandle):

    def __init__(self, logger, reaction_map, args, timer_obj=None):
        q_name_prefix = 'schema_transformer'
        super(STAmqpHandle, self).__init__(logger, DBBaseST, reaction_map,
                q_name_prefix, args=args, timer_obj=timer_obj)

    def create_msgbus_trace(self, request_id, oper, uuid):
        self.msg_tracer = MessageBusNotifyTrace(request_id=request_id,
                                                operation=oper, uuid=uuid)

    def msgbus_store_err_msg(self, msg):
        self.msg_tracer.error = msg

    def msgbus_trace_msg(self):
            self.msg_tracer.trace_msg(name='MessageBusNotifyTraceBuf',
                                      sandesh=self.logger._sandesh)

    def init_msgbus_fq_name(self):
        self.msg_tracer.fq_name = self.obj.name

    def init_msgbus_dtr(self):
        self.msg_tracer.dependency_tracker_resources = []

    def add_msgbus_dtr(self, res_type, res_id_list):
        dtr = DependencyTrackerResource(obj_type=res_type,
                                        obj_keys=res_id_list)
        self.msg_tracer.dependency_tracker_resources.append(dtr)

    def evaluate_dependency(self):
        if not self.dependency_tracker:
            return
        super(STAmqpHandle, self).evaluate_dependency()
        for vn_id in self.dependency_tracker.resources.get(
                'virtual_network', []):
            vn = VirtualNetworkST.get(vn_id)
            if vn is not None:
                vn.uve_send()
