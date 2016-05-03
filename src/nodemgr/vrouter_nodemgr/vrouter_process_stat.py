#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import os
from StringIO import StringIO
import ConfigParser
import sys
import socket

from nodemgr.common.process_stat import ProcessStat


class VrouterProcessStat(ProcessStat):
    def __init__(self, pname):
        ProcessStat.__init__(self, pname)
        (self.group, self.name) = self.get_vrouter_process_info(pname)

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
                        sys.stderr.write(msg + filename + "\n")
                        continue
                    Config = ConfigParser.SafeConfigParser()
                    Config.readfp(data)
                    sections = Config.sections()
                    if not sections[0]:
                        msg = "Section not present in the ini file : "
                        sys.stderr.write(msg + filename + "\n")
                        continue
                    name = sections[0].split(':')
                    if len(name) < 2:
                        msg = "Incorrect section name in the ini file : "
                        sys.stderr.write(msg + filename + "\n")
                        continue
                    if name[1] == proc_name:
                        command = Config.get(sections[0], "command")
                        if not command:
                            msg = "Command not present in the ini file : "
                            sys.stderr.write(msg + filename + "\n")
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
                                sys.stderr.write(msg + command + "\n")
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
                sys.stderr.write("Error reading file : " + conf_file +
                                 " Error : " + str(err) + "\n")
                return tor_agent_name
            tor_agent_name = Config.get("DEFAULT", "agent_name")
        return tor_agent_name
    # end get_vrouter_tor_agent_name
