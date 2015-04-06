#!/usr/bin/python

from gevent import monkey; monkey.patch_all()
import os
import sys
import argparse
import socket
import gevent
from nodemgr.AnalyticsNodemgr import AnalyticsEventManager
from nodemgr.ControlNodemgr import ControlEventManager
from nodemgr.ConfigNodemgr import ConfigEventManager
from nodemgr.VrouterNodemgr import VrouterEventManager
from nodemgr.DatabaseNodemgr import DatabaseEventManager

def main(argv=sys.argv):
# Parse Arguments
    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    parser.add_argument("--rules",
                        default = '',
                        help = 'Rules file to use for processing events')
    parser.add_argument("--nodetype",
                        default = 'contrail-analytics',
                        help = 'Type of node which nodemgr is managing')
    parser.add_argument("--discovery_server",
                        default = socket.gethostname(),
                        help = 'IP address of Discovery Server')
    parser.add_argument("--collectors",
                        default = '',
                        help = 'Collector addresses in format ip1:port1 ip2:port2')
    parser.add_argument("--discovery_port",
                        type = int,
                        default = 5998,
                        help = 'Port of Discovery Server')
    try:
        _args = parser.parse_args()
    except:
        usage()
    rule_file = _args.rules
    node_type = _args.nodetype
    discovery_server = _args.discovery_server
    sys.stderr.write("Discovery server: " + discovery_server + "\n")
    discovery_port = _args.discovery_port
    sys.stderr.write("Discovery port: " + str(discovery_port) + "\n")
    if _args.collectors is "":
        collector_addr = []
    else:
        collector_addr = _args.collectors.split()
    sys.stderr.write("Collector address: " + str(collector_addr) + "\n")
    #done parsing arguments

    if not 'SUPERVISOR_SERVER_URL' in os.environ:
        sys.stderr.write('Node manager must be run as a supervisor event '
                         'listener\n')
        sys.stderr.flush()
        return
    prog = None
    if (node_type == 'contrail-analytics'):
        prog = AnalyticsEventManager(rule_file, discovery_server, discovery_port, collector_addr)
    elif (node_type == 'contrail-config'):
        prog = ConfigEventManager(rule_file, discovery_server, discovery_port, collector_addr)
    elif (node_type == 'contrail-control'):
        prog = ControlEventManager(rule_file, discovery_server, discovery_port, collector_addr)
    elif (node_type == 'contrail-vrouter'):
        prog = VrouterEventManager(rule_file, discovery_server, discovery_port, collector_addr)
    elif (node_type == 'contrail-database'):
        prog = DatabaseEventManager(rule_file, discovery_server, discovery_port, collector_addr)
    else:
       return
    prog.process()
    prog.send_nodemgr_process_status()
    prog.send_process_state_db(prog.group_names)
    gevent.joinall([gevent.spawn(prog.runforever)])

if __name__ == '__main__':
    main()
