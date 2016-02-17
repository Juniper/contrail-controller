#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

#!/usr/bin/python


doc = """\
Node manager listens to process state change events and
other flag value change events to provide advanced service
management functionality.

Rules files looks like following:
====================
{ "Rules": [
    {"process_name": "contrail-query-engine",
     "process_state": "PROCESS_STATE_FATAL",
     "action": "supervisorctl -s http://localhost:9002 """ + \
    """\stop contrail-analytics-api"},
    {"process_name": "contrail-query-engine",
     "process_state": "PROCESS_STATE_STOPPED",
     "action": "supervisorctl -s http://localhost:9002 """ + \
    """\stop contrail-analytics-api"},
    {"processname": "contrail-collector",
     "process_state": "PROCESS_STATE_RUNNING",
     "action": "/usr/bin/echo collector is starting >> /tmp/log"},
    {"flag_name": "test", "flag_value":"true",
     "action": "/usr/bin/echo flag test is set true >> /tmp/log.1"}
     ]
}
====================

"""


from gevent import monkey
monkey.patch_all()
import os
import os.path
import sys
import argparse
import socket
import gevent
import ConfigParser
from nodemgr.analytics_nodemgr.analytics_event_manager import AnalyticsEventManager
from nodemgr.control_nodemgr.control_event_manager import ControlEventManager
from nodemgr.config_nodemgr.config_event_manager import ConfigEventManager
from nodemgr.vrouter_nodemgr.vrouter_event_manager import VrouterEventManager
from nodemgr.database_nodemgr.database_event_manager import DatabaseEventManager
from pysandesh.sandesh_base import SandeshSystem

def usage():
    print doc
    sys.exit(255)


def main(args_str=' '.join(sys.argv[1:])):
    # Parse Arguments
    node_parser = argparse.ArgumentParser(add_help=False)
    node_parser.add_argument("--nodetype",
                             default='contrail-analytics',
                             help='Type of node which nodemgr is managing')
    try:
        args, remaining_argv = node_parser.parse_known_args(args_str.split())
    except:
        usage()
    disc_options = {'server': socket.gethostname(), 'port': 5998}
    default = {'rules': '',
               'collectors': [],
               'hostip': '127.0.0.1',
               'minimum_diskgb': 256,
               'contrail_databases': 'config analytics',
               'cassandra_repair_interval': 24,
               'cassandra_repair_logdir': '/var/log/contrail/',
               'sandesh_send_rate_limit': \
                    SandeshSystem.get_sandesh_send_rate_limit(),
              }
    node_type = args.nodetype
    if (node_type == 'contrail-analytics'):
        config_file = '/etc/contrail/contrail-analytics-nodemgr.conf'
    elif (node_type == 'contrail-config'):
        config_file = '/etc/contrail/contrail-config-nodemgr.conf'
    elif (node_type == 'contrail-control'):
        config_file = '/etc/contrail/contrail-control-nodemgr.conf'
    elif (node_type == 'contrail-vrouter'):
        config_file = '/etc/contrail/contrail-vrouter-nodemgr.conf'
    elif (node_type == 'contrail-database'):
        config_file = '/etc/contrail/contrail-database-nodemgr.conf'
    else:
        sys.stderr.write("Node type" + str(node_type) + "is incorrect" + "\n")
        return
    if (os.path.exists(config_file) == False):
        sys.stderr.write("config file " + config_file + " is not present" + "\n")
        return
    config = ConfigParser.SafeConfigParser()
    config.read([config_file])
    if 'DEFAULT' in config.sections():
        default.update(dict(config.items('DEFAULT')))
    if 'DISCOVERY' in config.sections():
        disc_options.update(dict(config.items('DISCOVERY')))
    disc_options['discovery_server'] = disc_options.pop('server')
    disc_options['discovery_port'] = disc_options.pop('port')
    if 'COLLECTOR' in config.sections():
        try:
            collector = config.get('COLLECTOR', 'server_list')
            default['collectors'] = collector.split()
        except ConfigParser.NoOptionError as e:
            pass
    parser = argparse.ArgumentParser(parents=[node_parser],
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    default.update(disc_options)
    parser.set_defaults(**default)
    parser.add_argument("--rules",
                        help='Rules file to use for processing events')
    parser.add_argument("--discovery_server",
                        help='IP address of Discovery Server')
    parser.add_argument("--discovery_port",
                        type=int,
                        help='Port of Discovery Server')
    parser.add_argument("--collectors",
                        nargs='+',
                        help='Collector addresses in format' +
                             'ip1:port1 ip2:port2')
    parser.add_argument("--sandesh_send_rate_limit", type=int,
            help="Sandesh send rate limit in messages/sec")
    if (node_type == 'contrail-database'):
        parser.add_argument("--minimum_diskgb",
                            type=int,
                            help="Minimum disk space in GB's")
        parser.add_argument("--contrail_databases",
                            nargs='+',
                            help='Contrail databases on this node' +
                                 'in format: config analytics' )
        parser.add_argument("--hostip",
                            help="IP address of host")
        parser.add_argument("--cassandra_repair_interval", type=int,
                            help="Time in hours to periodically run "
                            "nodetool repair for cassandra maintenance")
        parser.add_argument("--cassandra_repair_logdir",
                            help="Directory for storing repair logs")
    try:
        _args = parser.parse_args(remaining_argv)
    except:
        usage()
    rule_file = _args.rules
    discovery_server = _args.discovery_server
    sys.stderr.write("Discovery server: " + discovery_server + "\n")
    discovery_port = _args.discovery_port
    sys.stderr.write("Discovery port: " + str(discovery_port) + "\n")
    collector_addr = _args.collectors
    sys.stderr.write("Collector address: " + str(collector_addr) + "\n")
    if _args.sandesh_send_rate_limit is not None:
        SandeshSystem.set_sandesh_send_rate_limit(_args.sandesh_send_rate_limit)
    # done parsing arguments

    if not 'SUPERVISOR_SERVER_URL' in os.environ:
        sys.stderr.write('Node manager must be run as a supervisor event '
                         'listener\n')
        sys.stderr.flush()
        return
    prog = None
    if (node_type == 'contrail-analytics'):
        prog = AnalyticsEventManager(
            rule_file, discovery_server,
            discovery_port, collector_addr)
    elif (node_type == 'contrail-config'):
        prog = ConfigEventManager(
            rule_file, discovery_server,
            discovery_port, collector_addr)
    elif (node_type == 'contrail-control'):
        prog = ControlEventManager(
            rule_file, discovery_server,
            discovery_port, collector_addr)
    elif (node_type == 'contrail-vrouter'):
        prog = VrouterEventManager(
            rule_file, discovery_server,
            discovery_port, collector_addr)
    elif (node_type == 'contrail-database'):
        hostip = _args.hostip
        minimum_diskgb = _args.minimum_diskgb
        contrail_databases = _args.contrail_databases
        cassandra_repair_interval = _args.cassandra_repair_interval
	cassandra_repair_logdir = _args.cassandra_repair_logdir
        prog = DatabaseEventManager(
            rule_file, discovery_server,
            discovery_port, collector_addr,
            hostip, minimum_diskgb, contrail_databases,
	    cassandra_repair_interval, cassandra_repair_logdir)
    else:
        sys.stderr.write("Node type" + str(node_type) + "is incorrect" + "\n")
        return
    prog.process()
    prog.send_nodemgr_process_status()
    prog.send_process_state_db(prog.group_names)
    gevent.joinall([gevent.spawn(prog.runforever)])

if __name__ == '__main__':
    main()
