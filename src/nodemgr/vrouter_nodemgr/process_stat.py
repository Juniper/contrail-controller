#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import os
from StringIO import StringIO
import ConfigParser
import sys
import socket

from nodemgr.common.process_stat import ProcessStat

from pysandesh.sandesh_logger import SandeshLogger
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel

class VrouterProcessStat(ProcessStat):
    def __init__(self, pname, sandesh_logger):
        self.logger = sandesh_logger
        ProcessStat.__init__(self, pname)
        (self.group, self.name) = self.get_vrouter_process_info(pname)

    def msg_log(self, msg, level):
        self.logger.log(SandeshLogger.get_py_logger_level(level), msg)

    def get_vrouter_process_info(self, proc_name):
        vrouter_file = "/etc/contrail/supervisord_vrouter_files"
        for root, dirs, files in os.walk(vrouter_file):
            for file in files:
                if file.endswith(".ini"):
                    filename = \
                        '/etc/contrail/supervisord_vrouter_files/' + file
                    try:
                        data = StringIO('\n'.join(line.strip()
                                    for line in open(filename)))
                    except IOError:
                        msg = "This file does not exist anymore so continuing:  "
                        self.msg_log(msg + filename, SandeshLevel.SYS_ERR)
                        continue
                    Config = ConfigParser.SafeConfigParser()
                    Config.readfp(data)
                    sections = Config.sections()
                    if not sections[0]:
                        msg = "Section not present in the ini file : "
                        self.msg_log(msg + filename, SandeshLevel.SYS_ERR)
                        continue
                    name = sections[0].split(':')
                    if len(name) < 2:
                        msg = "Incorrect section name in the ini file : "
                        self.msg_log(msg + filename, SandeshLevel.SYS_ERR)
                        continue
                    if name[1] == proc_name:
                        command = Config.get(sections[0], "command")
                        if not command:
                            msg = "Command not present in the ini file : "
                            self.msg_log(msg + filename, SandeshLevel.SYS_ERR)
                            continue
                        args = command.split()
                        if (args[0] == '/usr/bin/contrail-tor-agent'):
                            try:
                                index = args.index('--config_file')
                                args_val = args[index + 1]
                                agent_name = \
                                    self.get_vrouter_tor_agent_name(args_val)
                                return (proc_name, agent_name)
                            except Exception, err:
                                msg = "Tor Agent command does " + \
                                      "not have config file : "
                                self.msg_log(msg + command, SandeshLevel.SYS_ERR)
        return ('vrouter_group', socket.gethostname())
    # end get_vrouter_process_info

    # Read agent_name from vrouter-tor-agent conf file
    def get_vrouter_tor_agent_name(self, conf_file):
        tor_agent_name = None
        if conf_file:
            try:
                data = StringIO('\n'.join(line.strip()
                                for line in open(conf_file)))
                Config = ConfigParser.SafeConfigParser()
                Config.readfp(data)
            except Exception, err:
                self.msg_log("Error reading file : " + conf_file + " Error : " + str(err),
                                SandeshLevel.SYS_ERR)
                return tor_agent_name
            tor_agent_name = Config.get("DEFAULT", "agent_name")
        return tor_agent_name
    # end get_vrouter_tor_agent_name
