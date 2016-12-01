#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import os
import sys
import ConfigParser

class QosmapProv(object):

    def __init__(self, conf_file):

        self.conf_file = conf_file
        self._parse_args()
        qos_cmd = '/usr/bin/qosmap --set-queue ' + self.ifname + ' --dcbx ' + self.dcbx
        qos_cmd = qos_cmd + ' --bw ' + self.bandwidth + ' --strict ' + self.scheduling
        self.execute_command(qos_cmd)

    # end __init__

    def _parse_args(self):

        # Use agent config file /etc/contrail/contrail-vrouter-agent.conf
        self.ifname = ""
        self.dcbx = "ieee"
        self.priority_group = []
        self.bandwidth = []
        self.scheduling = []
        scheduling = ""
        bandwidth = ""
        config = ConfigParser.SafeConfigParser()
        config.read([self.conf_file])
        self.ifname = config.get('VIRTUAL-HOST-INTERFACE', 'physical_interface')
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

    # end _parse_args

    def execute_command(self, cmd):
        print cmd
        out = os.system(cmd)
        if out != 0:
            print "Error executing : " + cmd
    #end execute_command

# end class QosmapProv

def main():
    conf_file = "/etc/contrail/contrail-vrouter-agent.conf"
    QosmapProv(conf_file)
# end main

if __name__ == "__main__":
    main()

