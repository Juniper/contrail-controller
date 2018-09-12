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
from enum import Enum

class MandatoryArgs(Enum):
    """
    Enum of mandatory arguments to kube-manager.
    Kube-manager arguments will be validated against these arguments to
    enforce the presence of these mandatory arguments and optionally to
    enforce the correctness/validity of the supplied value for an argument.

    Each mandatory argument is represented by an enum member and the following
    info is captured for each argument, as a dictionary:
    a. arg_str - String which identifies the argument in config file.
    b. validatefn (optional) - Pointer to function that validates configured
                               value for an argument.

    A validate function (if specified) can be any custom function that returns
    a value that evaluates to bool True when validation is successful.
    It should return bool False if its validation fails.

    Example:

    An argumennt "foo" is configured in the config file as follows:

        foo = foo_value

    It can be enforced as mandatory argument by added the following member to
    this enum.

        FOO = {"arg_str": "foo", "validatefn": foo_fn()}

    If a validation function is not required then:

        FOO = {"arg_str": "foo"}

    """
    POD_SUBNET = {
        "arg_str": "pod_subnets",
        "validatefn": lambda x: x
    }

    SERVICE_SUBNET = {
        "arg_str": "service_subnets",
        "validatefn": lambda x: x
    }

    IP_FABRIC_SUBNET = {
        "arg_str": "ip_fabric_subnets",
        "validatefn": lambda x: x
    }

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
        'collectors': '',
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
        'global_tags': '1',
        'aps_name': '',
        'kube_timer_interval': '60',
        'secure_project': 'True'
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
        'db_driver': 'cassandra',
        'cassandra_user': None,
        'cassandra_password': None,
        'cassandra_server_list': '',
        'etcd_user': None,
        'etcd_password': None,
        'etcd_server': '',
        'etcd_prefix': '/contrail',
        'cluster_id': '',
        'vnc_endpoint_ip': '[127.0.0.1]',
        'vnc_endpoint_port': ApiServerPort,
        'admin_user': '',
        'admin_password': '',
        'admin_tenant': '',
        'public_fip_pool': '{}',
        'zk_server_ip': '127.0.0.1:2181',
    }

    k8s_opts = {
        'kubernetes_api_server': 'localhost',
        'kubernetes_api_port': '8080',
        'kubernetes_api_secure_port': 8443,
        'kubernetes_service_name': 'kubernetes',
        MandatoryArgs.SERVICE_SUBNET.value['arg_str']: None,
        MandatoryArgs.POD_SUBNET.value['arg_str']: None,
        MandatoryArgs.IP_FABRIC_SUBNET.value['arg_str']: None,
        'kubernetes_cluster_owner': 'k8s',
        'kubernetes_cluster_domain' : 'default-domain',
        'cluster_name': None,
        'cluster_project' : "{}",
        'cluster_network' : "{}",
        'cluster_pod_network' : None,
        'cluster_service_network' : None,
        'ip_fabric_forwarding': False,
        'ip_fabric_snat': False,
    }

    sandesh_opts = SandeshConfig.get_default_options()

    auth_opts = {
        'auth_token_url': None,
        'auth_user': 'admin',
        'auth_password': 'admin',
        'auth_tenant': 'admin',
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
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()
    if type(args.pod_subnets) is str:
        args.pod_subnets = args.pod_subnets.split()
    if type(args.service_subnets) is str:
        args.service_subnets = args.service_subnets.split()
    if type(args.ip_fabric_subnets) is str:
        args.ip_fabric_subnets = args.ip_fabric_subnets.split()
    if type(args.ip_fabric_forwarding) is str:
        if args.ip_fabric_forwarding.lower() == 'true':
            args.ip_fabric_forwarding = True
        else:
            args.ip_fabric_forwarding = False
    if type(args.ip_fabric_snat) is str:
        if args.ip_fabric_snat.lower() == 'true':
            args.ip_fabric_snat = True
        else:
            args.ip_fabric_snat = False
    args.sandesh_config = SandeshConfig.from_parser_arguments(args)

    # Validate input argumnents.
    validate_mandatory_args(args)

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

def validate_mandatory_args(args):
    for mandatory_arg in MandatoryArgs:
        arg_name = mandatory_arg.value['arg_str']
        if not hasattr(args, arg_name):
            print("Mandatory Argument %s not found in config"
                % arg_name)
            sys.exit("Mandatory argument [%s] not found in config" % arg_name)

        validatefn = mandatory_arg.value.get('validatefn', None)
        arg_value = getattr(args, arg_name)
        if validatefn and not validatefn(arg_value):
            sys.exit("Validation of mandatory argument [%s] configured with"\
                " value [%s] failed." % (arg_name, arg_value))
