#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import sys

import argparse
from vnc_api.vnc_api import *
from pysandesh.sandesh_base import Sandesh, SandeshSystem, SandeshConfig
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.constants import (HttpPortKubeManager,ApiServerPort,\
    DiscoveryServerPort)

def parse_args(args_str=None):
    if not args_str:
        args_str = sys.argv[1:]
    conf_parser = argparse.ArgumentParser(add_help=False)
    conf_parser.add_argument("-c", "--config-file", action='append',
        help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str)

    defaults = {
        'http_server_port': HttpPortKubeManager,
        'worker_id': '0',
        'collectors': None,
        'logger_class': None,
        'logging_conf': '',
        'log_local': False,
        'log_category': '',
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'kube_object_cache': 'True',
        'disc_server_ip': 'localhost',
        'disc_server_port': DiscoveryServerPort,
        'log_level': SandeshLevel.SYS_DEBUG,
        'log_file': '/var/log/contrail/contrail-kube-manager.log',
        'api_service_link_local' : 'True',
        'orchestrator' : 'kubernetes',
        'token' : '',
        'nested_mode': '0',
    }
    defaults.update(SandeshConfig.get_default_options(['DEFAULTS']))

    vnc_opts = {
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
        'cassandra_user': None,
        'cassandra_password': None,
        'cassandra_server_list': '',
        'cluster_id': '',
        'vnc_endpoint_ip': 'localhost',
        'vnc_endpoint_port': ApiServerPort,
        'admin_user' : '',
        'admin_password' : '',
        'admin_tenant' : '',
        'public_network_project':'default-project',
        'public_network':'__public__',
        'public_fip_pool':'__fip_pool_public__',
    }

    k8s_opts = {
        'kubernetes_api_server': 'localhost',
        'kubernetes_api_port': '8080',
        'kubernetes_api_secure_port': 8443,
        'kubernetes_api_secure_ip': None,
        'kubernetes_service_name': 'kubernetes',
        'service_subnets': '',
        'pod_subnets': '',
        'kubernetes_cluster_owner': 'k8s',
        'kubernetes_cluster_domain' : 'default-domain',
        'cluster_name': None,
        'cluster_project' : None,
        'cluster_network' : None,
    }

    sandesh_opts = SandeshConfig.get_default_options()

    auth_opts = {
        'auth_token_url': None
    }

    config = ConfigParser.SafeConfigParser()
    if args.config_file:
        config.read(args.config_file)
        if 'VNC' in config.sections():
            vnc_opts.update(dict(config.items("VNC")))
        if 'KUBERNETES' in config.sections():
            k8s_opts.update(dict(config.items("KUBERNETES")))
        SandeshConfig.update_options(sandesh_opts, config)
        if 'AUTH' in config.sections():
            auth_opts.update(dict(config.items("AUTH")))
        if 'DEFAULTS' in config.sections():
            defaults.update(dict(config.items("DEFAULTS")))

    parser = argparse.ArgumentParser(
        parents=[conf_parser],
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    defaults.update(vnc_opts)
    defaults.update(k8s_opts)
    defaults.update(sandesh_opts)
    defaults.update(auth_opts)
    parser.set_defaults(**defaults)
    args = parser.parse_args(args_str)

    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.pod_subnets) is str:
        args.pod_subnets = args.pod_subnets.split()
    if type(args.service_subnets) is str:
        args.service_subnets = args.service_subnets.split()
    args.sandesh_config = SandeshConfig.from_parser_argumentsg(args)
    return args
