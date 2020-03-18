#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from future import standard_library  # noqa
standard_library.install_aliases()  # noqa

import argparse # noqa
# from builtins import str

from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.sandesh_base import Sandesh, SandeshConfig
from six.moves.configparser import SafeConfigParser


def default_options():
    return {
        'use_certs': False,
        'keyfile': '',
        'certfile': '',
        'ca_certs': '',
        'admin_user': 'user1',
        'admin_password': '',
        'admin_tenant_name': 'default-domain',
        'cassandra_user': None,
        'cassandra_password': None,
        'rabbit_server': 'localhost',
        'rabbit_port': '5672',
        'rabbit_user': 'guest',
        'rabbit_password': 'guest',
        'rabbit_vhost': None,
        'rabbit_ha_mode': False,
        'cassandra_server_list': '127.0.0.1:9160',
        'api_server_ip': '127.0.0.1',
        'api_server_port': '8082',
        'api_server_use_ssl': None,
        'analytics_server_ip': '127.0.0.1',
        'analytics_server_port': '8081',
        'analytics_username': None,
        'analytics_password': None,
        'zk_server_ip': '127.0.0.1',
        'zk_server_port': '2181',
        'collectors': None,
        'http_server_port': '8096',
        'http_server_ip': '0.0.0.0',
        'log_local': False,
        'log_level': SandeshLevel.SYS_DEBUG,
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'cluster_id': '',
        'logging_conf': '',
        'logger_class': None,
        'repush_interval': '15',
        'push_mode': 0,
        'repush_max_interval': '600',
        'push_delay_per_kb': '0.01',
        'push_delay_max': '100',
        'push_delay_enable': True,
        'rabbit_use_ssl': False,
        'kombu_ssl_version': '',
        'kombu_ssl_keyfile': '',
        'kombu_ssl_certfile': '',
        'kombu_ssl_ca_certs': '',
        'job_status_retry_timeout': '10',
        'job_status_max_retries': '60',
        'max_job_count': 100,
        'fabric_ansible_conf_file':
            ['/etc/contrail/contrail-keystone-auth.conf',
             '/etc/contrail/contrail-fabric-ansible.conf'],
        'dnsmasq_conf_dir': None,
        'tftp_dir': None,
        'dhcp_leases_file': None,
        'dnsmasq_reload_by_signal': False,
        'ztp_timeout': 600,
        'rabbit_health_check_interval': 0,
        'job_manager_db_conn_retry_timeout': '10',
        'job_manager_db_conn_max_retries': '6',
        'fabric_ansible_dir': '/opt/contrail/fabric_ansible_playbooks',
        'dm_run_mode' : None,
    }
# end default_options


def add_parser_arguments(parser):
    parser.add_argument("--cassandra_server_list",
                        help="List of cassandra servers in IP Address:Port "
                             "format",
                        nargs='+')
    parser.add_argument("--cassandra_use_ssl", action="store_true",
                        help="Enable TLS for cassandra communication")
    parser.add_argument("--cassandra_ca_certs", help="Cassandra CA certs")
    parser.add_argument("--reset_config", action="store_true",
                        help="Warning! Destroy previous configuration and "
                             "start clean")
    parser.add_argument("--api_server_ip",
                        help="IP address of API server")
    parser.add_argument("--api_server_port",
                        help="Port of API server")
    parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
    parser.add_argument("--analytics_server_ip",
                        help="IP address of Analytics server")
    parser.add_argument("--analytics_server_port",
                        help="Port of Analytics server")
    parser.add_argument("--analytics_username",
                        help="Username for Analytics server")
    parser.add_argument("--analytics_password",
                        help="Password for Analytics server")
    parser.add_argument("--zk_server_ip",
                        help="IP address:port of zookeeper server")
    parser.add_argument("--collectors",
                        help="List of VNC collectors in ip:port format",
                        nargs="+")
    parser.add_argument("--http_server_port",
                        help="Port of Introspect HTTP server")
    parser.add_argument("--http_server_ip",
                        help="IP of Introspect HTTP server")
    parser.add_argument("--log_local", action="store_true",
                        help="Enable local logging of sandesh messages")
    parser.add_argument("--log_level",
                        help="Severity level for local logging of sandesh "
                             "messages")
    parser.add_argument("--log_category",
                        help="Category filter for local logging of sandesh "
                             "messages")
    parser.add_argument("--log_file",
                        help="Filename for the logs to be written to")
    parser.add_argument("--use_syslog", action="store_true",
                        help="Use syslog for logging")
    parser.add_argument("--syslog_facility",
                        help="Syslog facility to receive log lines")
    parser.add_argument("--admin_user",
                        help="Name of keystone admin user")
    parser.add_argument("--admin_password",
                        help="Password of keystone admin user")
    parser.add_argument("--admin_tenant_name",
                        help="Tenant name for keystone admin user")
    parser.add_argument("--cluster_id",
                        help="Used for database keyspace separation")
    parser.add_argument("--logging_conf",
                        help=("Optional logging configuration file, default: "
                              "None"))
    parser.add_argument("--logger_class",
                        help=("Optional external logger class, default: None"))
    parser.add_argument("--repush_interval",
                        help="time interval for config re push")
    parser.add_argument("--repush_max_interval",
                        help="max time interval for config re push")
    parser.add_argument("--push_delay_per_kb",
                        help="time delay between two successful commits per "
                             "kb config size")
    parser.add_argument("--push_delay_max",
                        help="max time delay between two successful commits")
    parser.add_argument("--push_delay_enable",
                        help="enable delay between two successful commits")
    parser.add_argument("--cassandra_user",
                        help="Cassandra user name")
    parser.add_argument("--cassandra_password",
                        help="Cassandra password")
    parser.add_argument("--job_status_retry_timeout",
                        help="Timeout between job status check retries")
    parser.add_argument("--job_status_max_retries",
                        help="Max number of job status retries")
    parser.add_argument("--max_job_count", type=int,
                        help="Maximum number of concurrent jobs that can run "
                             "based on the system configuration")
    parser.add_argument("--fabric_ansible_conf_file",
                        help="List of conf files required by fabric "
                             "ansible job manager.", nargs="+")
    parser.add_argument("--dnsmasq_conf_dir",
                        help="Path of the dnsmasq config directory")
    parser.add_argument("--tftp_dir",
                        help="Path of the tftp directory")
    parser.add_argument("--dnsmasq_reload_by_signal", action="store_true",
                        help="Reload the dnsmasq service by sending a signal "
                             "to a dnsmasq process")
    parser.add_argument("--dhcp_leases_file",
                        help="Path of the dhcp leases file")
    parser.add_argument("--ztp_timeout",
                        help="Timeout for the DHCP Lease lookup during ZTP")
    parser.add_argument("--rabbit_health_check_interval",
                        help="Interval between rabbitmq heartbeat checks")
    parser.add_argument("--job_manager_db_conn_retry_timeout",
                        help="Timeout between job manager retries")
    parser.add_argument("--job_manager_db_conn_max_retries",
                        help="Max number of job manager retries")
    parser.add_argument("--fabric_ansible_dir",
                        help="Fabric ansible directory path")
    parser.add_argument("--dm_run_mode",
                        help="Run all classes or just DeviceJobManager and DeviceZtpManager")
    SandeshConfig.add_parser_arguments(parser)
# end add_parser_arguments


def parse_args(args_str):
    """
    Please see the example below.

    python dm_server.py
    --rabbit_server localhost
    --rabbit_port 5672
    --cassandra_server_list 10.1.2.3:9160
    --api_server_ip 10.1.2.3
    --api_server_use_ssl False
    --analytics_server_ip 10.1.2.3
    --zk_server_ip 10.1.2.3
    --zk_server_port 2181
    --collectors 127.0.0.1:8086
    --http_server_port 8090
    [--reset_config]
    """
    # Source any specified config/ini file
    # Turn off help, so we see all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file", action='append',
                             help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = default_options()
    defaults.update(SandeshConfig.get_default_options(['DEFAULTS']))
    defaults.update(SandeshConfig.get_default_options())

    saved_conf_file = args.conf_file
    if args.conf_file:
        config = SafeConfigParser()
        config.read(args.conf_file)
        defaults.update(dict(config.items("DEFAULTS")))
        if ('SECURITY' in config.sections() and
                'use_certs' in config.options('SECURITY')):
            if config.getboolean('SECURITY', 'use_certs'):
                defaults.update(dict(config.items("SECURITY")))
        if 'KEYSTONE' in config.sections():
            defaults.update(dict(config.items("KEYSTONE")))
        if 'CASSANDRA' in config.sections():
            defaults.update(dict(config.items('CASSANDRA')))
        SandeshConfig.update_options(defaults, config)

    # Override with CLI options
    # Don't surpress add_help here so it will handle -h
    parser = argparse.ArgumentParser(
        # Inherit options from config_parser
        parents=[conf_parser],
        # print script description with -h/--help
        description=__doc__,
        # Don't mess with format of description
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.set_defaults(**defaults)

    add_parser_arguments(parser)
    args = parser.parse_args(remaining_argv)
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()
    args.sandesh_config = SandeshConfig.from_parser_arguments(args)
    args.cassandra_use_ssl = (str(args.cassandra_use_ssl).lower() == 'true')
    args.rabbit_use_ssl = (str(args.rabbit_use_ssl).lower() == 'true')
    args.dnsmasq_reload_by_signal = \
        (str(args.dnsmasq_reload_by_signal).lower() == 'true')

    args.conf_file = saved_conf_file
    return args
# end parse_args
