#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import gevent
from gevent import monkey
monkey.patch_all()

import os
import random
import signal
import socket
import sys

from attrdict import AttrDict
from cfgm_common import vnc_cgitb
from cfgm_common.kombu_amqp import KombuAmqpClient
from cfgm_common.zkclient import ZookeeperClient
from distutils.util import strtobool
from device_job_manager import DeviceJobManager
from device_manager import DeviceManager
from device_ztp_manager import DeviceZtpManager
from dm_amqp import DMAmqpHandle
from dm_server_args import parse_args
from logger import DeviceManagerLogger


_amqp_client = None
_zookeeper_client = None


def initialize_amqp_client(logger, args):
    amqp_client = None
    try:
        # prepare rabbitMQ params
        rabbitmq_cfg = AttrDict(
            servers=args.rabbit_server,
            port=args.rabbit_port,
            user=args.rabbit_user,
            password=args.rabbit_password,
            vhost=args.rabbit_vhost,
            ha_mode=args.rabbit_ha_mode,
            use_ssl=args.rabbit_use_ssl,
            ssl_version=args.kombu_ssl_version,
            ssl_keyfile=args.kombu_ssl_keyfile,
            ssl_certfile=args.kombu_ssl_certfile,
            ssl_ca_certs=args.kombu_ssl_ca_certs
        )
        amqp_client = KombuAmqpClient(logger.log, rabbitmq_cfg,
            heartbeat=args.rabbit_health_check_interval)
        amqp_client.run()
    except Exception as e:
        logger.error("Error while initializing the AMQP"
                     " client %s" % repr(e))
        if amqp_client is not None:
            amqp_client.stop()
    return amqp_client
# end initialize_amqp_client


def run_device_manager(dm_logger, args):
    global _amqp_client
    global _zookeeper_client

    dm_logger.notice("Elected master Device Manager node. Initializing... ")
    dm_logger.introspect_init()
    DeviceZtpManager.get_instance().set_active()
    DeviceManager(dm_logger, args, _zookeeper_client, _amqp_client)
    if _amqp_client._consumer_gl is not None:
        gevent.joinall([_amqp_client._consumer_gl])
# end run_device_manager


def sighup_handler():
    if DeviceManager.get_instance() is not None:
        DeviceManager.get_instance().sighup_handler()
# end sighup_handler


def sigterm_handler():
    global _amqp_client

    DeviceManager.destroy_instance()
    DeviceZtpManager.destroy_instance()
    DeviceJobManager.destroy_instance()

    if _amqp_client is not None:
        _amqp_client.stop()
# end sigterm_handler


def main(args_str=None):
    global _amqp_client
    global _zookeeper_client

    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)
    if args.cluster_id:
        client_pfx = args.cluster_id + '-'
        zk_path_pfx = args.cluster_id + '/'
    else:
        client_pfx = ''
        zk_path_pfx = ''

    # randomize collector list
    args.random_collectors = args.collectors
    if args.collectors:
        args.random_collectors = random.sample(args.collectors,
                                               len(args.collectors))

    # Initialize logger without introspect thread
    dm_logger = DeviceManagerLogger(args, http_server_port=-1)

    # Initialize AMQP handler then close it to be sure remain queue of a
    # precedent run is cleaned
    vnc_amqp = DMAmqpHandle(dm_logger, {}, args)
    vnc_amqp.establish()
    vnc_amqp.close()
    dm_logger.debug("Removed remaining AMQP queue from previous run")

    if 'host_ip' not in args:
        args.host_ip = socket.gethostbyname(socket.getfqdn())

    _amqp_client = initialize_amqp_client(dm_logger, args)
    _zookeeper_client = ZookeeperClient(client_pfx+"device-manager",
                                        args.zk_server_ip, args.host_ip)

    try:
        # Initialize the device job manager
        DeviceJobManager(_amqp_client, _zookeeper_client, args,
                         dm_logger)
        # Allow kombu client to connect consumers
        gevent.sleep(0.5)
    except Exception as e:
        dm_logger.error("Error while initializing the device job "
                        "manager %s" % str(e))
        raise e

    try:
        # Initialize the device ztp manager
        DeviceZtpManager(_amqp_client, args, dm_logger)
        # Allow kombu client to connect consumers
        gevent.sleep(0.5)
    except Exception as e:
        dm_logger.error("Error while initializing the device ztp "
                        "manager %s" % str(e))
        raise e

    gevent.signal(signal.SIGHUP, sighup_handler)
    gevent.signal(signal.SIGTERM, sigterm_handler)
    gevent.signal(signal.SIGINT, sigterm_handler)

    dm_logger.notice("Waiting to be elected as master...")
    _zookeeper_client.master_election(zk_path_pfx+"/device-manager",
                                      os.getpid(), run_device_manager,
                                      dm_logger, args)
# end main

def server_main():
    vnc_cgitb.enable(format='text')
    main()
# end server_main

if __name__ == '__main__':
    server_main()
