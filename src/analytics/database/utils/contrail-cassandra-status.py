#!/usr/bin/env python
#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import logging
import subprocess
import sys
import platform
import argparse
import os
import time

# Parses nodetool status output and returns a dict
# containing the nodes and their status
def parse_nodetool_status_output(output):
    """ Following is sample nodetool status output:

        Datacenter: datacenter1
        =======================
        Status=Up/Down
        |/ State=Normal/Leaving/Joining/Moving
        --  Address      Load       Tokens  Owns   Host ID                               Rack
        UN  10.84.27.27  28.06 GB   256     30.3%  2905fcf3-b702-4a62-9eb9-9f2396f17665  rack1
        UN  10.84.27.8   28.41 GB   256     32.2%  46999810-e412-41a5-8d50-fe43c5933945  rack1
        UN  10.84.27.9   29.61 GB   256     37.5%  205d5521-1ccc-40b1-98fb-fb2256d776de  rack1
    """
    # Extract the nodes (Find the header and start from there)
    olines = output.splitlines()
    olcounter = 0
    for line in olines:
        line_info = line.split()
        if (len(line_info) >= 3 and line_info[1] == "Address" and
                line_info[2] == "Load"):
            olcounter += 1
            break
        olcounter += 1
    if olcounter == 0:
        logging.error("FAILED to parse: {output}".format(output=output))
        return {}
    nodes = olines[olcounter:]
    # Create a node status dict indexed by Host ID (column 6 or column 5
    # depending on the output)
    """
       UN  10.84.27.8   28.41 GB   256     32.2%  46999810-e412-41a5-8d50-fe43c5933945  rack1
       DN  10.84.23.59  ?          256     30.3%  315f045a-ea54-42f7-9c05-72c8ac4b34b6  rack1
    """
    nodes_status = {}
    for node in nodes:
        node_info = node.split()
        node_info_len = len(node_info)
        if node_info_len == 8:
            node_id = node_info[6]
        elif node_info_len  == 7:
            node_id = node_info[5]
        else:
            # Not an error, can contain other stuff
            continue
        # Node status is column 0
        nodes_status[node_id] = node_info[0]
    return nodes_status
#end parse_nodetool_status_output

# Determine the number of UP nodes and verify that they are
# greater than or equal to RF/2 + 1 for QUORUM reads/writes
# to succeed. If RF is not passed, assumption is that RF is
# equal to number of nodes.
def is_cluster_partitioned(options):
    cmd = [options.nodetool, "-h", options.host, "status"]
    success, cmd, stdout, stderr = run_command(*cmd)
    if not success or not stdout:
        logging.error("FAILED: {cmd}".format(cmd=cmd))
        logging.error(stderr)
        return True
    nodes_status = parse_nodetool_status_output(stdout)
    if options.replication_factor:
        num_nodes = options.replication_factor
    else:
        num_nodes = len(nodes_status)
    nodes_up_status = dict((node_id, node_status) for \
        node_id, node_status in nodes_status.items() if 'U' in node_status)
    num_up_nodes = len(nodes_up_status)
    if num_up_nodes < (num_nodes/2) + 1:
        return True
    else:
        return False
#end is_cluster_partitioned

def get_cassandra_secs_since_up(options):
    secs_since_up = 0
    if options.status_up_file and os.path.exists(options.status_up_file):
       statinfo = os.stat(options.status_up_file)
       last_up_secs = int(statinfo.st_atime)
       current_time_secs = int(time.time())
       secs_since_up = current_time_secs - last_up_secs
    return secs_since_up
#end get_cassandra_secs_since_up

def update_status(options):
    # Find the node ID from nodetool info
    cmd = [options.nodetool, "-h", options.host, "info",
           "|", "grep", "ID", "|", "awk \'{print $3}\'"]
    success, cmd, stdout, stderr = run_command(*cmd)
    if not success or not stdout:
        logging.error("FAILED: {cmd}".format(cmd=cmd))
        logging.error(stderr)
        return 1
    node_id = stdout.strip()
    # Run nodetool status and check the status of node ID
    cmd = [options.nodetool, "-h", options.host, "status",
           "|", "grep", node_id, "|", "awk \'{print $1}\'"]
    success, cmd, stdout, stderr = run_command(*cmd)
    if not success or not stdout:
        logging.error("FAILED: {cmd}".format(cmd=cmd))
        logging.error(stderr)
        return 2
    self_status = stdout.strip()
    # Update status_up_file if the status is UP and the cluster is not
    # partitioned
    partitioned = is_cluster_partitioned(options)
    if 'U' in self_status and not partitioned and options.status_up_file:
        cmd = ["touch", options.status_up_file]
        success, cmd, _, stderr = run_command(*cmd)
        if not success:
            logging.error("FAILED: {cmd}".format(cmd=cmd))
            logging.error(stderr)
            return 3
    if options.debug:
        logging.debug("STATUS: {status}, PARTITIONED: {partitioned}".format(
            status=self_status, partitioned=partitioned))
    return 0
#end update_status

def verify_up_status(options):
    # Verify if the status has NOT being UP for max_allowed_down_seconds, then
    # stop cassandra
    secs_since_up = get_cassandra_secs_since_up(options)
    if secs_since_up >= options.max_allowed_down_seconds:
        cmd = ["service", "contrail-database", "stop"]
        success, cmd, _, stderr = run_command(*cmd)
        if not success:
            logging.error("FAILED: {cmd}".format(cmd=cmd))
            logging.error(stderr)
            return 4
    if options.debug:
        logging.debug("SECS SINCE UP: {secs}".format(secs=secs_since_up))
    return 0
#end verify_up_status

def status(options):
    update_status(options)
    ret = verify_up_status(options)
    return ret
#end status

def run_command(*command):
    """Execute a shell command and return the output
    :param command: the command to be run and all of the arguments
    :returns: success_boolean, command_string, stdout, stderr
    """
    cmd = " ".join(command)
    logging.debug("run_command: " + cmd)
    proc = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    stdout, stderr = proc.communicate()
    return proc.returncode == 0, cmd, stdout, stderr
#end run_command

def setup_logging(option_group):
    """Sets up logging in a syslog format by log level
    :param option_group: options as returned by the OptionParser
    """
    stderr_log_format = "%(levelname) -10s %(asctime)s %(funcName) -20s line:%(lineno) -5d: %(message)s"
    file_log_format = "%(asctime)s - %(levelname)s - %(message)s"
    logger = logging.getLogger()
    if option_group.debug:
        logger.setLevel(level=logging.DEBUG)
    elif option_group.verbose:
        logger.setLevel(level=logging.INFO)
    else:
        logger.setLevel(level=logging.WARNING)

    handlers = []
    if option_group.syslog:
        handlers.append(logging.SyslogHandler(facility=option_group.syslog))
        # Use standard format here because timestamp and level will be added by syslogd.
    if option_group.logfile:
        handlers.append(logging.FileHandler(option_group.logfile))
        handlers[0].setFormatter(logging.Formatter(file_log_format))
    if not handlers:
        handlers.append(logging.StreamHandler())
        handlers[0].setFormatter(logging.Formatter(stderr_log_format))
    for handler in handlers:
        logger.addHandler(handler)
#end setup_logging

def main():
    """Validate arguments and check status
    """
    parser = argparse.ArgumentParser(
                # print script description with -h/--help
                description=__doc__,
                formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument("-H", "--host", dest="host", default='127.0.0.1',
                        metavar="HOST",
                        help="Hostname to check status")

    parser.add_argument("-n", "--nodetool", dest="nodetool", default="nodetool",
                        metavar="NODETOOL",
                        help="Path to nodetool")

    parser.add_argument("--replication-factor", dest="replication_factor",
                        metavar="NUM", type=int,
                        help="Maximum replication factor of any keyspace")

    parser.add_argument("--max-allowed-down-seconds", dest="max_allowed_down_seconds",
                        metavar="SECONDS", type=int, default=int(864000*0.9),
                        help="Maximum seconds allowed for cassandra status to"
                        " not be UP before stopping cassandra")

    parser.add_argument("--status-up-file", dest="status_up_file",
                        metavar="FILENAME", default="/var/log/cassandra/status-up",
                        help="Record up status to file")

    parser.add_argument("-v", "--verbose", dest="verbose", action='store_true',
                        default=False, help="Verbose output")

    parser.add_argument("-d", "--debug", dest="debug", action='store_true',
                        default=False, help="Debugging output")

    parser.add_argument("--syslog", dest="syslog", metavar="FACILITY",
                        help="Send log messages to the syslog")

    parser.add_argument("--log-file", dest="logfile", metavar="FILENAME",
                        help="Send log messages to a file")

    options = parser.parse_args()

    setup_logging(options)
    ret = status(options)
    exit(ret)
#end main

if __name__ == "__main__":
    main()
