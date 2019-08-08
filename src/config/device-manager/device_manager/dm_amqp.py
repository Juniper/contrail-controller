# vim: tabstop=4 shiftwidth=4 softtabstop=4
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

"""Device Manager amqp handler."""
from __future__ import absolute_import

import socket

from cfgm_common.vnc_amqp import VncAmqpHandle

from .db import DBBaseDM, PhysicalRouterDM, VirtualNetworkDM


class DMAmqpHandle(VncAmqpHandle):

    def __init__(self, logger, reaction_map, args):
        """Initialize the amqp handler."""
        q_name_prefix = 'device_manager'
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
        if 'host_ip' in args:
            host_ip = args.host_ip
        else:
            host_ip = socket.gethostbyname(socket.getfqdn())
        super(DMAmqpHandle, self).__init__(logger._sandesh, logger, DBBaseDM,
                                           reaction_map, q_name_prefix,
                                           rabbitmq_cfg, host_ip,
                                           register_handler=False)

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
                pr.uve_send()
