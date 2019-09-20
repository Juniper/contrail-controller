#!/usr/bin/python
#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#


doc = """\
Node manager listens to process state change events and
other flag value change events to provide advanced service
management functionality.

"""

from gevent import monkey

import argparse
from six.moves.configparser import ConfigParser, SafeConfigParser, NoOptionError
import gevent
import os
import platform
import socket
import sys

from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from pysandesh.sandesh_base import Sandesh, SandeshConfig

from nodemgr.analytics_nodemgr.event_manager import AnalyticsEventManager
from nodemgr.analytics_alarm_nodemgr.event_manager import AnalyticsAlarmEventManager
from nodemgr.analytics_snmp_nodemgr.event_manager import AnalyticsSNMPEventManager
from nodemgr.config_nodemgr.event_manager import ConfigEventManager
from nodemgr.control_nodemgr.event_manager import ControlEventManager
from nodemgr.analytics_database_nodemgr.event_manager import AnalyticsDatabaseEventManager
from nodemgr.config_database_nodemgr.event_manager import ConfigDatabaseEventManager
from nodemgr.vrouter_nodemgr.event_manager import VrouterEventManager

monkey.patch_all()


node_properties = {
    'contrail-analytics': {
        'config_file': '/etc/contrail/contrail-analytics-nodemgr.conf',
        'event_manager': AnalyticsEventManager,
        'unit_names': [
            'contrail-collector',
            'contrail-analytics-api',
            'contrail-analytics-nodemgr'
        ],
    },
    'contrail-analytics-snmp': {
        'config_file': '/etc/contrail/contrail-analytics-snmp-nodemgr.conf',
        'event_manager': AnalyticsSNMPEventManager,
        'unit_names': [
            'contrail-snmp-collector',
            'contrail-topology',
            'contrail-analytics-snmp-nodemgr',
        ],
    },
    'contrail-analytics-alarm': {
        'config_file': '/etc/contrail/contrail-analytics-alarm-nodemgr.conf',
        'event_manager': AnalyticsAlarmEventManager,
        'unit_names': [
            'contrail-alarm-gen',
            'kafka',
            'contrail-analytics-alarm-nodemgr',
        ],
    },
    'contrail-config': {
        'config_file': '/etc/contrail/contrail-config-nodemgr.conf',
        'event_manager': ConfigEventManager,
        'unit_names': [
            'contrail-api',
            'contrail-schema',
            'contrail-svc-monitor',
            'contrail-device-manager',
            'contrail-config-nodemgr'
        ],
    },
    'contrail-config-database': {
        'config_file': '/etc/contrail/contrail-config-database-nodemgr.conf',
        'event_manager': ConfigDatabaseEventManager,
        'unit_names': [
            'cassandra',
            'zookeeper',
            'contrail-config-database-nodemgr'
        ],
    },
    'contrail-control': {
        'config_file': '/etc/contrail/contrail-control-nodemgr.conf',
        'event_manager': ControlEventManager,
        'unit_names': [
            'contrail-control',
            'contrail-dns',
            'contrail-named',
            'contrail-control-nodemgr'
        ],
    },
    'contrail-vrouter': {
        'config_file': '/etc/contrail/contrail-vrouter-nodemgr.conf',
        'event_manager': VrouterEventManager,
        'unit_names': [
            'contrail-vrouter-agent',
            'contrail-vrouter-nodemgr'
        ],
    },
    'contrail-database': {
        'config_file': '/etc/contrail/contrail-database-nodemgr.conf',
        'event_manager': AnalyticsDatabaseEventManager,
        'unit_names': [
            'cassandra',
            'contrail-query-engine',
            'contrail-database-nodemgr'
        ],
    },
}


def print_usage_and_exit():
    print(doc)
    sys.exit(255)


def main(args_str=' '.join(sys.argv[1:])):
    # Parse Arguments
    node_parser = argparse.ArgumentParser(add_help=False)
    node_parser.add_argument("--nodetype",
                             default='contrail-analytics',
                             help='Type of node which nodemgr is managing')
    try:
        args, remaining_argv = node_parser.parse_known_args(args_str.split())
    except Exception:
        print_usage_and_exit()
    default = {'rules': '',
               'collectors': [],
               'db_port': '9042',
               'db_jmx_port': '7199',
               'db_user': None,
               'db_password': None,
               'db_use_ssl': False,
               'minimum_diskgb': 256,
               'corefile_path': '/var/crashes',
               'cassandra_repair_interval': 24,
               'cassandra_repair_logdir': '/var/log/contrail/',
               'log_local': False,
               'log_level': SandeshLevel.SYS_DEBUG,
               'log_category': '',
               'log_file': Sandesh._DEFAULT_LOG_FILE,
               'use_syslog': False,
               'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
               'hostname': None
               }
    try:
        default['hostip'] = socket.gethostbyname(socket.getfqdn())
    except Exception:
        pass
    default.update(SandeshConfig.get_default_options(['DEFAULTS']))
    sandesh_opts = SandeshConfig.get_default_options()
    node_type = args.nodetype

    if node_type not in node_properties:
        sys.stderr.write("Node type '" + str(node_type) + "' is incorrect\n")
        sys.exit(1)

    config_file_path = ""
    config_file_path += node_properties[node_type]['config_file']
    if (os.path.exists(config_file_path) is False):
        sys.stderr.write("config file '" + config_file_path + "' is not present\n")
        sys.exit(1)
    config = SafeConfigParser()
    config.read([config_file_path])
    if 'DEFAULTS' in config.sections():
        default.update(dict(config.items('DEFAULTS')))
    if 'COLLECTOR' in config.sections():
        try:
            collector = config.get('COLLECTOR', 'server_list')
            default['collectors'] = collector.split()
        except NoOptionError:
            pass
    SandeshConfig.update_options(sandesh_opts, config)
    parser = argparse.ArgumentParser(
        parents=[node_parser],
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    default.update(sandesh_opts)
    parser.set_defaults(**default)
    parser.add_argument("--rules",
                        help='Rules file to use for processing events')
    parser.add_argument("--collectors",
                        nargs='+',
                        help='Collector addresses in format'
                             + 'ip1:port1 ip2:port2')
    parser.add_argument("--log_local", action="store_true",
                        help="Enable local logging of sandesh messages")
    parser.add_argument(
        "--log_level",
        help="Severity level for local logging of sandesh messages")
    parser.add_argument(
        "--log_category",
        help="Category filter for local logging of sandesh messages")
    parser.add_argument("--log_file",
                        help="Filename for the logs to be written to")
    parser.add_argument("--use_syslog", action="store_true",
                        help="Use syslog for logging")
    parser.add_argument("--syslog_facility",
                        help="Syslog facility to receive log lines")
    parser.add_argument("--corefile_path",
                        help="Location where coredump files are stored")
    parser.add_argument("--hostname",
                        help="Custom Hostname")
    SandeshConfig.add_parser_arguments(parser, add_dscp=True)
    if (node_type == 'contrail-database'
            or node_type == 'contrail-config-database'):
        parser.add_argument("--minimum_diskGB",
                            type=int,
                            dest='minimum_diskgb',
                            help="Minimum disk space in GB's")
        parser.add_argument("--hostip",
                            help="IP address of host")
        parser.add_argument("--db_port",
                            help="Cassandra DB cql port")
        parser.add_argument("--db_jmx_port",
                            help="Cassandra DB jmx port")
        parser.add_argument("--db_user",
                            help="Cassandra DB cql username")
        parser.add_argument("--db_password",
                            help="Cassandra DB cql password")
        parser.add_argument("--db_use_ssl",
                            help="Cassandra DB behind SSL or not")
        parser.add_argument("--cassandra_repair_interval", type=int,
                            help="Time in hours to periodically run "
                            "nodetool repair for cassandra maintenance")
        parser.add_argument("--cassandra_repair_logdir",
                            help="Directory for storing repair logs")
    try:
        _args = parser.parse_args(remaining_argv)
    except Exception:
        print_usage_and_exit()

    _args.config_file_path = config_file_path
    _args.db_use_ssl = (str(_args.db_use_ssl).lower() == 'true')
    # done parsing arguments

    # TODO: restore rule_file logic somehow if needed for microservices
    # rule_file = _args.rules

    unit_names = node_properties[node_type]['unit_names']
    event_manager = node_properties[node_type]['event_manager'](_args, unit_names)
    event_manager.send_init_data()

    gevent.joinall([
        gevent.spawn(event_manager.runforever),
        gevent.spawn(
            event_manager.run_periodically(
                event_manager.do_periodic_events, 60))
    ])


if __name__ == '__main__':
    main()
