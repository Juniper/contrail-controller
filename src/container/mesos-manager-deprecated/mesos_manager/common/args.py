#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

# Standard library import
import argparse
import ConfigParser
import sys

# Application library import
from pysandesh.sandesh_base import Sandesh, SandeshSystem, SandeshConfig
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
import mesos_manager.mesos_consts as mesos_consts
from sandesh_common.vns.constants import (HttpPortMesosManager,\
                                          DiscoveryServerPort)


def parse_args(args_str=None):
    if not args_str:
        args_str = sys.argv[1:]
    conf_parser = argparse.ArgumentParser(add_help=False)
    conf_parser.add_argument("-c", "--config-file", action='append',
                             help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str)

    defaults = {
        'mesos_api_server': mesos_consts._WEB_HOST,
        'mesos_api_port': mesos_consts._WEB_PORT,
        'http_server_port': HttpPortMesosManager,
        'worker_id': '0',
        'collectors': None,
        'logger_class': None,
        'logging_conf': '',
        'log_local': False,
        'log_category': '',
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'disc_server_ip': 'localhost',
        'disc_server_port': DiscoveryServerPort,
        'log_level': SandeshLevel.SYS_DEBUG,
    }
    defaults.update(SandeshConfig.get_default_options(['DEFAULTS']))

    vnc_opts = {
        'admin_user': 'admin',
        'admin_password': 'admin',
        'admin_tenant': 'admin',
        'rabbit_server': 'localhost',
        'rabbit_port': '5672',
        'rabbit_user': 'guest',
        'rabbit_password': 'guest',
        'rabbit_vhost': None,
        'rabbit_ha_mode': False,
        'rabbit_use_ssl': False,
        'kombu_ssl_version': '',
        'kombu_ssl_keyfile': '',
        'kombu_ssl_certfile': '',
        'kombu_ssl_ca_certs': '',
        'cassandra_server_ip': mesos_consts._CASSANDRA_HOST,
        'cassandra_server_port': mesos_consts._CASSANDRA_PORT,
        'cassandra_max_retries': mesos_consts._CASSANDRA_MAX_RETRIES,
        'cassandra_timeout': mesos_consts._CASSANDRA_TIMEOUT,
        'cassandra_user': None,
        'cassandra_password': None,
        'cluster_id': '',
    }

    sandesh_opts = SandeshConfig.get_default_options()

    mesos_opts = {
        'mesos_api_server': 'localhost',
        'mesos_api_port': '8080',
        'mesos_api_secure_port': 8443,
        'mesos_api_secure_ip': None,
        'mesos_service_name': 'mesos',
        'service_subnets': '',
        'app_subnets': ''
    }

    config = ConfigParser.SafeConfigParser()
    if args.config_file:
        config.read(args.config_file)
        if 'VNC' in config.sections():
            vnc_opts.update(dict(config.items("VNC")))
        if 'MESOS' in config.sections():
            mesos_opts.update(dict(config.items("MESOS")))
        SandeshConfig.update_options(sandesh_opts, config)
        if 'DEFAULTS' in config.sections():
            defaults.update(dict(config.items("DEFAULTS")))

    parser = argparse.ArgumentParser(
        parents=[conf_parser],
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    defaults.update(vnc_opts)
    defaults.update(mesos_opts)
    defaults.update(sandesh_opts)
    parser.set_defaults(**defaults)
    args = parser.parse_args(args_str)

    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.app_subnets) is str:
        args.app_subnets = args.app_subnets.split()
    if type(args.service_subnets) is str:
        args.service_subnets = args.service_subnets.split()
    args.sandesh_config = SandeshConfig.from_parser_arguments(args)
    return args


def rabbitmq_args(args):
    return {
        'servers': args.rabbit_server, 'port': args.rabbit_port,
        'user': args.rabbit_user, 'password': args.rabbit_password,
        'vhost': args.rabbit_vhost, 'ha_mode': args.rabbit_ha_mode,
        'use_ssl': args.rabbit_use_ssl,
        'ssl_version': args.kombu_ssl_version,
        'ssl_keyfile': args.kombu_ssl_keyfile,
        'ssl_certfile': args.kombu_ssl_certfile,
        'ssl_ca_certs': args.kombu_ssl_ca_certs
    }
