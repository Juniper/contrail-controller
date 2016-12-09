#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import os
import sys
import argparse
import ConfigParser
import tempfile
from shutil import move

class QosmapProv(object):

    def __init__(self, conf_file, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self.conf_file = conf_file
        self._parse_agent_conf()
        self.execute_qosmap_cmd()

    # end __init__

    def execute_qosmap_cmd(self):

        for intf in self.ifname_list:
            qos_cmd = '/usr/bin/qosmap --set-queue ' + intf + ' --dcbx ' + self.dcbx
            qos_cmd = qos_cmd + ' --bw ' + self.bandwidth + ' --strict ' + self.scheduling
            self.execute_command(qos_cmd)

    def _parse_agent_conf(self):

        # Use agent config file /etc/contrail/contrail-vrouter-agent.conf
        self.ifname_list = []
        self.dcbx = "ieee"
        self.priority_group = []
        self.bandwidth = []
        self.scheduling = []
        scheduling = ""
        bandwidth = ""
        config = ConfigParser.SafeConfigParser()
        config.read([self.conf_file])
        if self._args.interface_list:
            self.ifname_list = self._args.interface_list
        else:
            self.ifname_list.append(config.get('VIRTUAL-HOST-INTERFACE', 'physical_interface'))

        for i in range(8):
            self.priority_group.append('0')
            self.bandwidth.append('0')
            self.scheduling.append('0')

        for section in config.sections():

            if "PG-" in section:
                # For one to one mapping priority group is same as traffic class
                priority_id = section.strip('[]').split('-')[1]
                self.priority_group[int(priority_id)] = priority_id
                scheduling = config.get(section, 'scheduling')
                if scheduling == 'strict':
                    self.scheduling[int(priority_id)] = '1'
                else:
                    bandwidth = config.get(section, 'bandwidth')
                    self.bandwidth[int(priority_id)] = bandwidth

        self.priority_group = [elem for elem in self.priority_group]
        self.bandwidth = ",".join(self.bandwidth)
        self.scheduling = "".join(self.scheduling)
        if (self.priority_group == ""):
            print "Please configure priority groups"

        self.replace("/etc/contrail/agent_param.tmpl", "qos_intf", self._args.interface_list)
        self.replace("/etc/contrail/agent_param.tmpl", "qos_bw", self.bandwidth)
        self.replace("/etc/contrail/agent_param.tmpl", "qos_strictness", self.scheduling)

    # end _parse_args

    def execute_command(self, cmd):
        print cmd
        out = os.system(cmd)
        if out != 0:
            print "Error executing : " + cmd
    #end execute_command

    def replace(self, file_path, pattern, subst):
        #Create temp file
        fh, abs_path = tempfile.mkstemp()
        with open(abs_path,'w') as new_file:
            with open(file_path) as old_file:
                for line in old_file:
                    if line.strip().startswith(pattern):
                        line = "%s=%s\n" % (pattern, subst)
                    #line = re.sub(r"^%s" % pattern, "%s=%s" % (pattern, subst), line)
                    new_file.write(line)
        os.close(fh)
        #Remove original file
        os.remove(file_path)
        move(abs_path, file_path)

    def _parse_args(self, args_str):
        '''
        Eg. python qosmap.py --interface_list p1p1
        The --interface_list option can be ignored,
        Then interface is picked from file:
        /etc/contrail/contrail-vrouter-agent.conf.

        '''

        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'interface_list': None,
        }

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

        parser.add_argument(
            "--interface_list", help="name of physical interfaces",
            nargs='+', type=str)

        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

# end class QosmapProv

def main(args_str=None):
    conf_file = "/etc/contrail/contrail-vrouter-agent.conf"
    QosmapProv(conf_file, args_str)
# end main

if __name__ == "__main__":
    main()

