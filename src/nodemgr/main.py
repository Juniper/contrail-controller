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
import signal
import random
import hashlib
from nodemgr.analytics_nodemgr.analytics_event_manager import AnalyticsEventManager
from nodemgr.control_nodemgr.control_event_manager import ControlEventManager
from nodemgr.config_nodemgr.config_event_manager import ConfigEventManager
from nodemgr.vrouter_nodemgr.vrouter_event_manager import VrouterEventManager
from nodemgr.database_nodemgr.database_event_manager import DatabaseEventManager
from pysandesh.sandesh_base import SandeshSystem, SandeshConfig

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
    sandesh_opts = {
        'sandesh_keyfile': '/etc/contrail/ssl/private/server-privkey.pem',
        'sandesh_certfile': '/etc/contrail/ssl/certs/server.pem',
        'sandesh_ca_cert': '/etc/contrail/ssl/certs/ca-cert.pem',
        'sandesh_ssl_enable': False,
        'introspect_ssl_enable': False,
        'sandesh_dscp_value': 0
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
    if 'DEFAULTS' in config.sections():
        default.update(dict(config.items('DEFAULTS')))
    if 'COLLECTOR' in config.sections():
        try:
            collector = config.get('COLLECTOR', 'server_list')
            default['collectors'] = collector.split()
        except ConfigParser.NoOptionError as e:
            pass
    if 'SANDESH' in config.sections():
        sandesh_opts.update(dict(config.items('SANDESH')))
        if 'sandesh_ssl_enable' in config.options('SANDESH'):
            sandesh_opts['sandesh_ssl_enable'] = config.getboolean(
                'SANDESH', 'sandesh_ssl_enable')
        if 'introspect_ssl_enable' in config.options('SANDESH'):
            sandesh_opts['introspect_ssl_enable'] = config.getboolean(
                'SANDESH', 'introspect_ssl_enable')
        if 'sandesh_dscp_value' in config.options('SANDESH'):
            try:
                sandesh_opts['sandesh_dscp_value'] = config.getint(
                    'SANDESH', 'sandesh_dscp_value')
            except:
                pass
    parser = argparse.ArgumentParser(parents=[node_parser],
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    default.update(sandesh_opts)
    parser.set_defaults(**default)
    parser.add_argument("--rules",
                        help='Rules file to use for processing events')
    parser.add_argument("--collectors",
                        nargs='+',
                        help='Collector addresses in format' +
                             'ip1:port1 ip2:port2')
    parser.add_argument("--sandesh_send_rate_limit", type=int,
            help="Sandesh send rate limit in messages/sec")
    parser.add_argument("--sandesh_keyfile",
                        help="Sandesh ssl private key")
    parser.add_argument("--sandesh_certfile",
                        help="Sandesh ssl certificate")
    parser.add_argument("--sandesh_ca_cert",
                        help="Sandesh CA ssl certificate")
    parser.add_argument("--sandesh_ssl_enable", action="store_true",
                        help="Enable ssl for sandesh connection")
    parser.add_argument("--introspect_ssl_enable", action="store_true",
                        help="Enable ssl for introspect connection")
    parser.add_argument("--sandesh_dscp_value", type=int,
                        help="DSCP bits for IP header of Sandesh messages")
    if (node_type == 'contrail-database'):
        parser.add_argument("--minimum_diskGB",
                            type=int,
                            dest='minimum_diskgb',
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
    collector_addr = _args.collectors
    sys.stderr.write("Collector address: " + str(collector_addr) + "\n")

    # randomize collector list
    _args.chksum = ""
    if _args.collectors:
        _args.chksum = hashlib.md5("".join(_args.collectors)).hexdigest()
        _args.random_collectors = random.sample(_args.collectors, len(_args.collectors))
        _args.collectors = _args.random_collectors

    collector_addr = _args.collectors
    sys.stderr.write("Random Collector address: " + str(collector_addr) + "\n")

    if _args.sandesh_send_rate_limit is not None:
        SandeshSystem.set_sandesh_send_rate_limit(_args.sandesh_send_rate_limit)
    sandesh_config = SandeshConfig(_args.sandesh_keyfile,
        _args.sandesh_certfile, _args.sandesh_ca_cert,
        _args.sandesh_ssl_enable, _args.introspect_ssl_enable,
        _args.sandesh_dscp_value)
    # done parsing arguments

    prog = None
    if (node_type == 'contrail-analytics'):
        if not rule_file:
            rule_file = "/etc/contrail/supervisord_analytics_files/" + \
                "contrail-analytics.rules"
        unit_names = ['contrail-collector.service',
                      'contrail-analytics-api.service',
                      'contrail-snmp-collector.service',
                      'contrail-query-engine.service',
                      'contrail-alarm-gen.service',
                      'contrail-topology.service',
                      'contrail-analytics-nodemgr.service',
                     ]
        prog = AnalyticsEventManager(
            rule_file, unit_names, collector_addr, sandesh_config)
    elif (node_type == 'contrail-config'):
        if not rule_file:
            rule_file = "/etc/contrail/supervisord_config_files/" + \
                "contrail-config.rules"
        unit_names = ['contrail-api.service',
                      'contrail-schema.service',
                      'contrail-svc-monitor.service',
                      'contrail-device-manager.service',
                      'contrail-config-nodemgr.service',
                      'ifmap.service',
                     ]
        cassandra_repair_interval = _args.cassandra_repair_interval
	cassandra_repair_logdir = _args.cassandra_repair_logdir
        prog = ConfigEventManager(
            rule_file, unit_names, collector_addr, sandesh_config,
            cassandra_repair_interval, cassandra_repair_logdir)
    elif (node_type == 'contrail-control'):
        if not rule_file:
            rule_file = "/etc/contrail/supervisord_control_files/" + \
                "contrail-control.rules"
        unit_names = ['contrail-control.service',
                      'contrail-dns.service',
                      'contrail-named.service',
                      'contrail-control-nodemgr.service',
                     ]
        prog = ControlEventManager(
            rule_file, unit_names, collector_addr, sandesh_config)
    elif (node_type == 'contrail-vrouter'):
        if not rule_file:
            rule_file = "/etc/contrail/supervisord_vrouter_files/" + \
                "contrail-vrouter.rules"
        unit_names = ['contrail-vrouter-agent.service',
                      'contrail-vrouter-nodemgr.service',
                     ]
        prog = VrouterEventManager(
            rule_file, unit_names, collector_addr, sandesh_config)
    elif (node_type == 'contrail-database'):
        if not rule_file:
            rule_file = "/etc/contrail/supervisord_database_files/" + \
                "contrail-database.rules"
        unit_names = ['contrail-database.service',
                      'kafka.service',
                      'contrail-database-nodemgr.service',
                     ]
        hostip = _args.hostip
        minimum_diskgb = _args.minimum_diskgb
        contrail_databases = _args.contrail_databases
        cassandra_repair_interval = _args.cassandra_repair_interval
	cassandra_repair_logdir = _args.cassandra_repair_logdir
        prog = DatabaseEventManager(
            rule_file, unit_names, collector_addr, sandesh_config, 
            hostip, minimum_diskgb, contrail_databases,
	    cassandra_repair_interval, cassandra_repair_logdir)
    else:
        sys.stderr.write("Node type" + str(node_type) + "is incorrect" + "\n")
        return

    prog.process()
    prog.send_nodemgr_process_status()
    prog.send_process_state_db(prog.group_names)
    prog.config_file = config_file
    prog.collector_chksum = _args.chksum

    """ @sighup
    Reconfig of collector list
    """
    gevent.signal(signal.SIGHUP, prog.nodemgr_sighup_handler)

    gevent.joinall([gevent.spawn(prog.runforever),
        gevent.spawn(prog.run_periodically(prog.do_periodic_events, 60))])

if __name__ == '__main__':
    main()
