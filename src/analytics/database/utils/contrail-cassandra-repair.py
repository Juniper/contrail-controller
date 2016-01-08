#!/usr/bin/env python
#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import logging
import subprocess
import sys
import platform
import os
import argparse
from sandesh_common.vns.constants import RepairNeededKeyspaces

def repair(options):
    returncode = 0
    for keyspace in RepairNeededKeyspaces:
        keyspace_repair_logfile = "/var/log/cassandra/repair-" + keyspace + ".log"
        keyspace_repair_running = "/var/log/cassandra/repair-" + keyspace + "-running"
        if os.path.exists(keyspace_repair_running):
            logging.debug("REPAIR for {keyspace} is still running".format(keyspace=keyspace))
            returncode = 1
            continue
        # Create repair running to indicate repair is running for keyspace
        with open(keyspace_repair_running, "w"):
            # Run repair for the keyspace
            cmd = [options.nodetool, "-h", options.host, "repair", "-pr", keyspace]
            with open(keyspace_repair_logfile, "a") as repair_file:
                success = run_command(cmd, repair_file, repair_file)
                if not success:
                    returncode = 2
        os.remove(keyspace_repair_running)
    return returncode
#end repair

def run_command(command, stdout, stderr):
    """Execute a command and return success or failure
    :param command: the command to be run and all of the arguments
    :returns: success_boolean
    """
    cmd = " ".join(command)
    logging.debug("run_command: " + cmd)
    try:
        subprocess.check_call(command, stdout=stdout, stderr=stderr)
        return True
    except subprocess.CalledProcessError as cpe:
        logging.error("FAILED: {cmd}".format(cmd=cmd))
        logging.error(str(cpe))
        return False
    except OSError as ose:
        logging.error("FAILED: {cmd}".format(cmd=cmd))
        logging.error(str(ose))
        return False
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
                      metavar="HOST", help="Hostname to check status")

    parser.add_argument("-n", "--nodetool", dest="nodetool", default="nodetool",
                      metavar="NODETOOL", help="Path to nodetool")

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
    ret = repair(options)
    exit(ret)
#end main

if __name__ == "__main__":
    main()
