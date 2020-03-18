from __future__ import absolute_import
#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import os
import random
import signal
import socket
import sys
import subprocess, psutil

from attrdict import AttrDict
from cfgm_common import vnc_cgitb
from cfgm_common.kombu_amqp import KombuAmqpClient
from cfgm_common.vnc_object_db import VncObjectDBClient
from cfgm_common.zkclient import ZookeeperClient
import gevent
from gevent import monkey

from .device_job_manager import DeviceJobManager
from .device_ztp_manager import DeviceZtpManager
from .dm_amqp import DMAmqpHandle
from .dm_server_args import parse_args
from .logger import DeviceManagerLogger

from .device_manager import DeviceManager # noqa
monkey.patch_all() # noqa


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
        amqp_client = KombuAmqpClient(
            logger.log, rabbitmq_cfg,
            heartbeat=0)
        amqp_client.run()
    except Exception as e:
        logger.error("Error while initializing the AMQP"
                     " client %s" % repr(e))
        if amqp_client is not None:
            amqp_client.stop()
    return amqp_client
# end initialize_amqp_client


def initialize_db_connection(logger, args):
    credential = None
    if args.cassandra_user and args.cassandra_password:
        credential = {
            'username': args.cassandra_user,
            'password': args.cassandra_password
        }

    timeout = int(args.job_manager_db_conn_retry_timeout)
    max_retries = int(args.job_manager_db_conn_max_retries)

    retry_count = 1
    while True:
        try:
            return VncObjectDBClient(
                args.cassandra_server_list, args.cluster_id, None, None,
                logger.log, credential=credential,
                ssl_enabled=args.cassandra_use_ssl,
                ca_certs=args.cassandra_ca_certs)
        except Exception as e:
            if retry_count >= max_retries:
                raise e
            logger.error("Error while initializing db connection, "
                         "retrying: %s" % str(e))
            gevent.sleep(timeout)
        finally:
            retry_count = retry_count + 1
# end initialize_db_connection


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

def run_full_dm():
    # won master election, destroy existing process
    running_procs = [x for x in psutil.process_iter()]
    for proc in running_procs:
        if proc.pid == 1:
            continue
        elif 'contrail-device-manager' in ' '.join(proc.cmdline()):
            proc.kill()
    script_to_run = sys.argv
    script_to_run.append('--dm_run_mode Full')
    proc = subprocess.Popen(script_to_run, close_fds=True)
    gevent.joinall([gevent.spawn(dummy_gl, proc)])
# end run_full_dm

def dummy_gl(proc):
    while True:
        gevent.sleep(10)
        if proc.poll() == None:
            # proc is still running
            pass
        elif proc.poll() == 0:
            # proc is terminated, kill self
            os._exit(2)
        else:
            # proc completed, this shouldnt happen
            os._exit(2)
# end dummy_gl

def run_partial_dm():
    # if we are not master, start only DeviceJobManager
    is_master = False
    running_procs = [x for x in psutil.process_iter()]
    for proc in running_procs:
        if proc.pid == 1:
            continue
        elif 'contrail-device-manager' in ' '.join(proc.cmdline()):
            is_master = True
    if not is_master:
        # start dm with Partial flag
        script_to_run = sys.argv
        script_to_run.append('--dm_run_mode Partial')
        proc = subprocess.Popen(script_to_run, close_fds=True)
        for x in range(3):
            gevent.sleep(5)
            if proc.poll() == None:
                # proc is still running
                pass
            else:
                # proc completed, this shouldnt happen
                os._exit(2)
# end master_watcher

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

def run_job_ztp_manager(dm_logger, _zookeeper_client, args):
    # Initialize AMQP handler then close it to be sure remain queue of a
    # precedent run is cleaned
    vnc_amqp = DMAmqpHandle(dm_logger, {}, args)
    vnc_amqp.establish()
    vnc_amqp.close()
    dm_logger.debug("Removed remaining AMQP queue from previous run")

    global _amqp_client
    _amqp_client = initialize_amqp_client(dm_logger, args)
    _db_conn = initialize_db_connection(dm_logger, args)

    try:
        # Initialize the device job manager
        DeviceJobManager(_amqp_client, _zookeeper_client, _db_conn, args,
                         dm_logger)
        # Allow kombu client to connect consumers
        gevent.sleep(0.5)
    except Exception as e:
        dm_logger.error("Error while initializing the device job "
                        "manager %s" % str(e))
        raise e

    try:
        # Initialize the device ztp manager
        DeviceZtpManager(_amqp_client, _db_conn, args, dm_logger)
        # Allow kombu client to connect consumers
        gevent.sleep(0.5)
    except Exception as e:
        dm_logger.error("Error while initializing the device ztp "
                        "manager %s" % str(e))
        raise e

    if args.dm_run_mode == 'Partial':
        dm_logger.notice("Process %s started in Partial mode..." %os.getpid())
        if _amqp_client._consumer_gl is not None:
            gevent.joinall([_amqp_client._consumer_gl])
# end run_job_ztp_manager

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

    args.log_level = str(args.log_level)

    # Initialize logger without introspect thread
    #dm_logger = DeviceManagerLogger(args, http_server_port=-1)

    ## Initialize AMQP handler then close it to be sure remain queue of a
    ## precedent run is cleaned
    #vnc_amqp = DMAmqpHandle(dm_logger, {}, args)
    #vnc_amqp.establish()
    #vnc_amqp.close()
    #dm_logger.debug("Removed remaining AMQP queue from previous run")

    if 'host_ip' not in args:
        args.host_ip = socket.gethostbyname(socket.getfqdn())

    #dm_logger = None
    #_amqp_client = initialize_amqp_client(dm_logger, args)
    _zookeeper_client = ZookeeperClient(client_pfx + "device-manager",
                                        args.zk_server_ip, args.host_ip)
    #_db_conn = initialize_db_connection(dm_logger, args)

    #try:
    #    # Initialize the device job manager
    #    DeviceJobManager(_amqp_client, _zookeeper_client, _db_conn, args,
    #                     dm_logger)
    #    # Allow kombu client to connect consumers
    #    gevent.sleep(0.5)
    #except Exception as e:
    #    dm_logger.error("Error while initializing the device job "
    #                    "manager %s" % str(e))
    #    raise e

    #try:
    #    # Initialize the device ztp manager
    #    DeviceZtpManager(_amqp_client, _db_conn, args, dm_logger)
    #    # Allow kombu client to connect consumers
    #    gevent.sleep(0.5)
    #except Exception as e:
    #    dm_logger.error("Error while initializing the device ztp "
    #                    "manager %s" % str(e))
    #    raise e

    gevent.signal(signal.SIGHUP, sighup_handler)
    gevent.signal(signal.SIGTERM, sigterm_handler)
    gevent.signal(signal.SIGINT, sigterm_handler)

    #dm_logger.notice("Waiting to be elected as master...")

    if args.dm_run_mode == 'Full':
        dm_logger = DeviceManagerLogger(args, http_server_port=-1)
        run_job_ztp_manager(dm_logger, _zookeeper_client, args)
        run_device_manager(dm_logger, args)
    elif args.dm_run_mode == 'Partial':
        dm_logger = DeviceManagerLogger(args, http_server_port=-1)
        dm_logger.notice("Process %s prepared to run in Partial mode..." %os.getpid())
        run_job_ztp_manager(dm_logger, _zookeeper_client, args)
    else:
        run_partial_dm()
        _zookeeper_client.master_election(zk_path_pfx + "/device-manager",
                                          os.getpid(), run_full_dm)
# end main


def server_main():
    vnc_cgitb.enable(format='text')
    main()
# end server_main


if __name__ == '__main__':
    server_main()
