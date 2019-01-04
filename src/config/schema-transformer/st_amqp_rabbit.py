# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""
Sechmatransformer  amqp handler
"""

from cfgm_common.vnc_amqp import VncAmqpHandle
from config_db import DBBaseST, VirtualNetworkST, BgpRouterST


class STAmqpHandleRabbit(VncAmqpHandle):

    def __init__(self, logger, reaction_map, args, host_ip, q_name_prefix, timer_obj=None):
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
        super(STAmqpHandleRabbit, self).__init__(logger._sandesh, logger, DBBaseST,
                                           reaction_map, q_name_prefix,
                                           rabbitmq_cfg, host_ip,
                                           args.trace_file, timer_obj=timer_obj)

    def evaluate_dependency(self):
        if not self.dependency_tracker:
            return
        super(STAmqpHandleRabbit, self).evaluate_dependency()
        for vn_id in self.dependency_tracker.resources.get(
                'virtual_network', []):
            vn = VirtualNetworkST.get(vn_id)
            if vn is not None:
                vn.uve_send()
        for bgp_router_id in \
                self.dependency_tracker.resources.get('bgp_router', []):
            bgp_router = BgpRouterST.get(bgp_router_id)
            if bgp_router is not None:
                bgp_router.update_peering()
