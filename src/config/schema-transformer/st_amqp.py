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
        rabbitmq_cfg = {
            'servers': args.rabbit_server, 'port': args.rabbit_port,
            'user': args.rabbit_user, 'password': args.rabbit_password,
            'vhost': args.rabbit_vhost, 'ha_mode': args.rabbit_ha_mode,
            'use_ssl': args.rabbit_use_ssl,
            'ssl_version': args.kombu_ssl_version,
            'ssl_keyfile': args.kombu_ssl_keyfile,
            'ssl_certfile': args.kombu_ssl_certfile,
            'ssl_ca_certs': args.kombu_ssl_ca_certs
        }
        super(STAmqpHandle, self).__init__(logger._sandesh, logger, DBBaseST,
                                           reaction_map, q_name_prefix,
                                           rabbitmq_cfg, args.trace_file)

    def evaluate_dependency(self):
        if not self.dependency_tracker:
            return
        super(STAmqpHandle, self).evaluate_dependency()
        for vn_id in self.dependency_tracker.resources.get(
                'virtual_network', []):
            vn = VirtualNetworkST.get(vn_id)
            if vn is not None:
                vn.uve_send()
