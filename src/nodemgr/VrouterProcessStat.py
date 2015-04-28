import os
from StringIO import StringIO
import ConfigParser
import sys
import socket

from nodemgr.ProcessStat import ProcessStat

class VrouterProcessStat(ProcessStat):
    def __init__(self, pname):
        ProcessStat.__init__(self, pname)
        (self.group, self.name) = self.get_vrouter_process_info(pname)

    def get_vrouter_process_info(self, proc_name):
        for root, dirs, files in os.walk("/etc/contrail/supervisord_vrouter_files"):
            for file in files:
                if file.endswith(".ini"):
                    filename = '/etc/contrail/supervisord_vrouter_files/' + file
                    data = StringIO('\n'.join(line.strip() for line in open(filename)))
                    Config = ConfigParser.SafeConfigParser()
                    Config.readfp(data)
                    sections = Config.sections()
                    if not sections[0]:
                        sys.stderr.write("Section not present in the ini file : " + filename + "\n")
                        continue
                    name = sections[0].split(':')
                    if len(name) < 2:
                        sys.stderr.write("Incorrect section name in the ini file : " + filename + "\n")
                        continue
                    if name[1] == proc_name:
                        command = Config.get(sections[0], "command")
                        if not command:
                            sys.stderr.write("Command not present in the ini file : " + filename + "\n")
                            continue
                        args = command.split()
                        if (args[0] == '/usr/bin/contrail-tor-agent'):
                            try:
                                index = args.index('--config_file')
                                agent_name = self.get_vrouter_tor_agent_name(args[index + 1])
                                return (proc_name, agent_name)
                            except Exception, err:
                                sys.stderr.write("Tor Agent command does not have config file : " + command + "\n")
        return ('vrouter_group', socket.gethostname())
    #end get_vrouter_process_info

    # Read agent_name from vrouter-tor-agent conf file
    def get_vrouter_tor_agent_name(self, conf_file):
        tor_agent_name = None
        if conf_file:
            try:
                data = StringIO('\n'.join(line.strip() for line in open(conf_file)))
                Config = ConfigParser.SafeConfigParser()
                Config.readfp(data)
            except Exception, err:
                sys.stderr.write("Error reading file : " + conf_file +
                                 " Error : " + str(err) + "\n")
                return tor_agent_name
            tor_agent_name = Config.get("DEFAULT", "agent_name")
        return tor_agent_name
    #end get_vrouter_tor_agent_name
