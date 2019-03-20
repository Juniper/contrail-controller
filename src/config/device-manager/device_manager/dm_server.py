#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
"""
Device Manager Server.
"""

import gevent
from gevent import monkey
monkey.patch_all()

import os
import random
import requests
import signal
import socket
import sys
import time

from attrdict import AttrDict
from cfgm_common import vnc_cgitb
from cfgm_common.exceptions import ResourceExhaustionError
from cfgm_common.kombu_amqp import KombuAmqpClient
from cfgm_common.zkclient import ZookeeperClient
from cassandra import DMCassandraDB
from distutils.util import strtobool
from device_job_manager import DeviceJobManager
from device_manager import DeviceManager
from device_ztp_manager import DeviceZtpManager
from dm_amqp import dm_amqp_factory
from dm_server_args import parse_args
from etcd import DMEtcdDB
from logger import DeviceManagerLogger
from vnc_api.vnc_api import VncApi

_amqp_client = None
_zookeeper_client = None
_object_db = None


def initialize_amqp_client(logger, args):
    amqp_client = None
    try:
        kombu_cfg = AttrDict(
            ssl_version=args.kombu_ssl_version,
            ssl_keyfile=args.kombu_ssl_keyfile,
            ssl_certfile=args.kombu_ssl_certfile,
            ssl_ca_certs=args.kombu_ssl_ca_certs)
        if use_etcd(args, 'notification_driver'):
            kombu_cfg.transport = 'etcd'
            kombu_cfg.servers = args.etcd_server
            kombu_cfg.port = args.etcd_port
            kombu_cfg.user = args.etcd_user
            kombu_cfg.password = args.etcd_password
            # 'vhost' is used as etcd prefix
            # "kombu" postfix added by Kombu etcd transport Channel
            kombu_cfg.vhost = (
                args.etcd_kv_store + '/device-manager/').lstrip('/')
            kombu_cfg.use_ssl = args.etcd_use_ssl
        else:
            kombu_cfg.servers = args.rabbit_server
            kombu_cfg.port = args.rabbit_port
            kombu_cfg.user = args.rabbit_user
            kombu_cfg.password = args.rabbit_password
            kombu_cfg.vhost = args.rabbit_vhost
            kombu_cfg.ha_mode = args.rabbit_ha_mode
            kombu_cfg.use_ssl = args.rabbit_use_ssl
        amqp_client = KombuAmqpClient(
            logger.log, kombu_cfg, heartbeat=args.rabbit_health_check_interval)
        amqp_client.run()
    except Exception as e:
        logger.error("Error while initializing the AMQP"
                     " client %s" % repr(e))
        if amqp_client is not None:
            amqp_client.stop()
    return amqp_client
# end initialize_amqp_client


def initialize_vnc_lib(logger, args):
    connected = False
    vnc_lib = None
    while not connected:
        try:
            vnc_lib = VncApi(
                args.admin_user,
                args.admin_password,
                args.admin_tenant_name,
                args.api_server_ip.split(','),
                args.api_server_port,
                api_server_use_ssl=args.api_server_use_ssl)
            connected = True
        except requests.exceptions.ConnectionError as e:
            # Update connection info
            logger.error(
                "Error while initializing the VNC LIB %s" % repr(e))
            time.sleep(3)
        except ResourceExhaustionError:  # haproxy throws 503
            time.sleep(3)
    return vnc_lib
# end initialize_vnc_lib


def use_etcd(args, key):
    try:
        driver = getattr(args, key, '')
        return driver == 'etcd'
    except AttributeError:
        return False
# end use_etcd


def run_device_manager(dm_logger, args):
    global _amqp_client
    global _object_db

    dm_logger.notice("Elected master Device Manager node. Initializing... ")
    dm_logger.introspect_init()
    DeviceZtpManager.get_instance().set_active()
    DeviceManager(dm_logger, _object_db, _amqp_client, args)
    if _amqp_client._consumer_gl is not None:
        gevent.joinall([_amqp_client._consumer_gl])
# end run_device_manager


def sighup_handler():
    if DeviceManager.get_instance() is not None:
        DeviceManager.get_instance().sighup_handler()
# end sighup_handler


def sigterm_handler():
    global _amqp_client
    global _object_db

    DeviceManager.destroy_instance()
    DeviceZtpManager.destroy_instance()
    DeviceJobManager.destroy_instance()

    if isinstance(_object_db, DMEtcdDB):
        DMEtcdDB.clear_instance()
    else:
        DMCassandraDB.clear_instance()

    if _amqp_client is not None:
        _amqp_client.stop()
# end sigterm_handler


def main(args_str=None):
    global _amqp_client
    global _zookeeper_client
    global _object_db

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
    vnc_amqp = dm_amqp_factory(dm_logger, {}, args)
    vnc_amqp.establish()
    vnc_amqp.close()
    dm_logger.debug("Removed remaining AMQP queue from previous run")

    if 'host_ip' in args:
        host_ip = args.host_ip
    else:
        host_ip = socket.gethostbyname(socket.getfqdn())

    _amqp_client = initialize_amqp_client(dm_logger, args)
    _zookeeper_client = ZookeeperClient(client_pfx + "device-manager",
                                        args.zk_server_ip, host_ip)
    if use_etcd(args, 'db_driver'):
        vnc_lib = initialize_vnc_lib(dm_logger, args)
        _object_db = DMEtcdDB.get_instance(args, vnc_lib, dm_logger)
    else:
        _object_db = DMCassandraDB.get_instance(_zookeeper_client, args,
                                                dm_logger)
    try:
        # Initialize the device job manager
        DeviceJobManager(_object_db, _amqp_client, _zookeeper_client, args,
                         dm_logger)
    except Exception as e:
        dm_logger.error("Error while initializing the device job "
                        "manager %s" % repr(e))

    try:
        # Initialize the device ztp manager
        DeviceZtpManager(_amqp_client, args, host_ip, dm_logger)
    except Exception as e:
        dm_logger.error("Error while initializing the device ztp "
                        "manager %s" % repr(e))

    gevent.signal(signal.SIGHUP, sighup_handler)
    gevent.signal(signal.SIGTERM, sigterm_handler)
    gevent.signal(signal.SIGINT, sigterm_handler)

    dm_logger.notice("Waiting to be elected as master...")
    _zookeeper_client.master_election(zk_path_pfx + "/device-manager",
                                      os.getpid(), run_device_manager,
                                      dm_logger, args)
# end main


def server_main():
    vnc_cgitb.enable(format='text')
    main()
# end server_main


if __name__ == '__main__':
    server_main()
