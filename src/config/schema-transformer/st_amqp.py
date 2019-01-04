# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

import socket

from st_amqp_etcd import STAmqpHandleEtcd
from st_amqp_rabbit import STAmqpHandleRabbit


def st_amqp_factory(logger, reaction_map, args, timer_obj=None):
    """st_amqp factory function"""
    q_name_prefix = 'schema_transformer'

    if 'host_ip' in args:
        host_ip = args.host_ip
    else:
        host_ip = socket.gethostbyname(socket.getfqdn())

    if hasattr(args, "notification_driver") and args.notification_driver == "etcd":
        # Initialize etcd
        return STAmqpHandleEtcd(logger, reaction_map, args, host_ip, q_name_prefix)

    # Initialize cassandra
    return STAmqpHandleRabbit(logger, reaction_map, args, host_ip, q_name_prefix)