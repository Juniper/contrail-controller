#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function

from future import standard_library
standard_library.install_aliases()
import argparse
from six.moves import configparser
import sys
from pysandesh.sandesh_base import Sandesh, SandeshSystem, SandeshConfig
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from sandesh_common.vns.constants import (HttpPortMesosManager,ApiServerPort,\
                                          DiscoveryServerPort)
from enum import Enum

class MandatoryArgs(Enum):
    """
    Enum of mandatory arguments to mesos-manager.
    mesos-manager arguments will be validated against these arguments to
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
    POD_TASK_SUBNET = {
        "arg_str": "pod_task_subnets",
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
        'log_file': '/var/log/contrail/contrail-mesos-manager.log',
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
        'cassandra_use_ssl': False,
        'cassandra_ca_certs': None,
        'cluster_id': '',
        'vnc_endpoint_ip': '[127.0.0.1]',
        'vnc_endpoint_port': ApiServerPort,
        'admin_user' : '',
        'admin_password' : '',
        'admin_tenant' : '',
        'public_fip_pool': '{}',
        'zk_server_ip': '127.0.0.1:2181',
    }

    sandesh_opts = SandeshConfig.get_default_options()

    mesos_opts = {
        'mesos_cni_server': 'localhost',
        'mesos_cni_port': 6991,
        'mesos_cni_secure_port': 8443,
        'mesos_cni_secure_ip': None,
        MandatoryArgs.POD_TASK_SUBNET.value['arg_str']: None,
        MandatoryArgs.IP_FABRIC_SUBNET.value['arg_str']: None,
        'mesos_cluster_owner': 'mesos',
        'mesos_cluster_domain' : 'default-domain',
        'mesos_cluster_name': 'mesos',
        'cluster_project' : "{}",
        'cluster_network' : "{}",
        'cluster_pod_task_network' : None,
        'ip_fabric_forwarding': False,
        'ip_fabric_snat': False,
        'mesos_agent_retry_sync_hold_time': 2,
        'mesos_agent_retry_sync_count': 6,
    }

    auth_opts = {
        'auth_token_url': None,
        'auth_user': 'admin',
        'auth_password': 'admin',
        'auth_tenant': 'admin',
    }

    config = configparser.ConfigParser()
    if args.config_file:
        config.read(args.config_file)
        if 'VNC' in config.sections():
            vnc_opts.update(dict(config.items("VNC")))
        if 'MESOS' in config.sections():
            mesos_opts.update(dict(config.items("MESOS")))
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
    defaults.update(mesos_opts)
    defaults.update(sandesh_opts)
    defaults.update(auth_opts)
    parser.set_defaults(**defaults)
    args = parser.parse_args(args_str)

    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.cassandra_use_ssl) is str:
        args.cassandra_use_ssl = args.cassandra_use_ssl.lower() == 'true'
    if type(args.pod_task_subnets) is str:
        args.pod_task_subnets = args.pod_task_subnets.split()
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
