# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import socket

from cfgm_common import vnc_etcd
from cfgm_common.vnc_amqp import VncAmqpHandle
from config_db import DBBaseST, VirtualNetworkST, BgpRouterST

"""
SchemaTransformer amqp handlers
"""

def st_amqp_factory(logger, reaction_map, args, timer_obj=None):
    """st_amqp factory function"""
    q_name_prefix = 'schema_transformer'

    if 'host_ip' in args:
        host_ip = args.host_ip
    else:
        host_ip = socket.gethostbyname(socket.getfqdn())

    if hasattr(args, "notification_driver") and args.notification_driver == "etcd":
        # Initialize etcd
        return STAmqpHandleEtcd(logger, reaction_map, args, host_ip, q_name_prefix, timer_obj)

    # Initialize cassandra
    return STAmqpHandleRabbit(logger, reaction_map, args, host_ip, q_name_prefix, timer_obj)


class STAmqpEvaluateDependencyMixin(object):
    def evaluate_dependency(self):
        if not self.dependency_tracker:
            return
        super(self.__class__, self).evaluate_dependency()
        for vn_id in self.dependency_tracker.resources.get(
                "virtual_network", []):
            vn = VirtualNetworkST.get(vn_id)
            if vn is not None:
                vn.uve_send()
        for bgp_router_id in self.dependency_tracker.resources.get("bgp_router", []):
            bgp_router = BgpRouterST.get(bgp_router_id)
            if bgp_router is not None:
                bgp_router.update_peering()


class STAmqpHandleEtcd(vnc_etcd.VncEtcdWatchHandle, STAmqpEvaluateDependencyMixin):

    def __init__(self, logger, reaction_map, args, host_ip, q_name_prefix, timer_obj=None):
        etcd_cfg = vnc_etcd.etcd_args(args)
        super(STAmqpHandleEtcd, self).__init__(logger._sandesh, logger, DBBaseST,
                                               reaction_map, etcd_cfg, host_ip=host_ip,
                                               timer_obj=timer_obj)


class STAmqpHandleRabbit(VncAmqpHandle, STAmqpEvaluateDependencyMixin):

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
