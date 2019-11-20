#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
from future import standard_library
standard_library.install_aliases()
from builtins import range
from builtins import object
import os
import subprocess
import sys
import argparse
import configparser
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
        if (self.qos_scheduling_config):
            self.execute_qosmap_cmd()
        self.set_xps_cpu(self.ifname_list)

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
        self.qos_scheduling_config = False
        self.bandwidth = []
        self.scheduling = []
        scheduling = ""
        bandwidth = ""
        config = configparser.SafeConfigParser()
        config.read([self.conf_file])
        if self._args.interface_list:
            self.ifname_list = self._args.interface_list
        else:
            self.ifname_list.append(config.get('VIRTUAL-HOST-INTERFACE', 'physical_interface'))

        for i in range(8):
            self.bandwidth.append('0')
            self.scheduling.append('0')

        for section in config.sections():

            if "PG-" in section:
                # For one to one mapping priority group is same as traffic class
                self.qos_scheduling_config = True
                priority_id = section.strip('[]').split('-')[1]
                scheduling = config.get(section, 'scheduling')
                if scheduling == 'strict':
                    self.scheduling[int(priority_id)] = '1'
                else:
                    bandwidth = config.get(section, 'bandwidth')
                    self.bandwidth[int(priority_id)] = bandwidth

        self.bandwidth = ",".join(self.bandwidth)
        self.scheduling = "".join(self.scheduling)
        if (not self.qos_scheduling_config):
            print("Priority group configuration not found")

    # end _parse_args

    def execute_command(self, cmd):
        print(cmd)
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True)
        (out, err) = proc.communicate()
        print(out)
        outwithoutreturn = out.rstrip('\n')
        if err:
            print("Error executing : " + cmd + "\n Cmd output " + out)
            return False

        return outwithoutreturn
    #end execute_command

    def replace(self, file_path, pattern, subst):
        #Create temp file
        stat = os.stat(file_path)
        mode, uid, gid = stat[0], stat[4], stat[5]
        fh, abs_path = tempfile.mkstemp()
        with open(abs_path,'w') as new_file:
            with open(file_path) as old_file:
                for line in old_file:
                    if line.strip().startswith(pattern):
                        line = "%s%s\n" % (pattern, subst)
                        new_file.write(line)
                    else:
                        new_file.write(line)
        os.close(fh)
        #Remove original file
        os.remove(file_path)
        move(abs_path, file_path)
        os.chmod(file_path, mode)
        os.chown(file_path, uid, gid)

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

    def set_xps_cpu(self, intf_list):

        for intf in intf_list:
            file_path = "/sys/class/net/" + intf + "/queues/"
            if os.path.exists(file_path):
                    num_tx_queue_cmd = "cd %s;ls -l | grep tx- | wc -l " % (file_path)
                    num_tx_queues = self.execute_command(num_tx_queue_cmd)
            else:
                    print("Path for interface queue %s does not exist" % file_path)
                    return True
            if (num_tx_queues and num_tx_queues !='0'):
                for i in range(int(num_tx_queues)):
                    file_str = "tx-%s/xps_cpus" % i
                    filename = file_path + file_str
                    xps_cpu_file = os.path.isfile(filename)
                    if (not xps_cpu_file):
                        print("xps_cpu file not found %s on compute %s while disabling Xmit-Packet-Steering" % (xps_cpu_file, compute_host_string))
                        return True
                    cmd = "echo 0 > %s" % filename
                    self.execute_command(cmd)
            else:
                print("Error: No tx queues found for file paths %s " % (file_path))
                return True

# end class QosmapProv

def main(args_str=None):
    conf_file = "/etc/contrail/contrail-vrouter-agent.conf"
    QosmapProv(conf_file, args_str)
# end main

if __name__ == "__main__":
    main()

