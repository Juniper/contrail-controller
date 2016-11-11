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

    def _vnc_subscribe_callback(self, oper_info):
        self._db_resync_done.wait()
        try:
            self.oper_info = oper_info
            self.vnc_subscribe_actions()

        except Exception:
            string_buf = cStringIO.StringIO()
            cgitb_hook(file=string_buf, format="text")
            self.logger.error(string_buf.getvalue())

            self.msgbus_store_err_msg(string_buf.getvalue())
            try:
                with open(self._args.trace_file, 'a') as err_file:
                    err_file.write(string_buf.getvalue())
            except IOError:
                pass
        finally:
            try:
                self.msgbus_trace_msg()
            except Exception:
                pass
            del self.oper_info
            del self.obj_type
            del self.obj_class
            del self.obj
            del self.dependency_tracker

