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
        qos_cmd = '/usr/bin/qosmap --set-queue ' + self.ifname + ' --dcbx ' + self.dcbx + ' --pg ' + self.priority_group
        qos_cmd = qos_cmd + ' --bw ' + self.bandwidth + ' --strict ' + self.scheduling + ' --tc ' + self.traffic_class
        self.execute_command(qos_cmd)

    # end __init__

    def _parse_args(self):

        # Use agent config file /etc/contrail/contrail-vrouter-agent.conf
        self.ifname = ""
        self.dcbx = "ieee"
        self.priority_group = ""
        self.bandwidth = ""
        self.scheduling = ""
        self.traffic_class = ""
        scheduling = ""
        bandwidth = ""
        config = ConfigParser.SafeConfigParser()
        config.read([self.conf_file])
        self.ifname = config.get('VIRTUAL-HOST-INTERFACE', 'physical_interface') 
        for section in config.sections():
            if "QUEUE-" in section:
                self.traffic_class = self.traffic_class + section.strip('[]').split('-')[1] + ','
                scheduling = config.get(section, 'scheduling')
                if scheduling == 'strict':
                    self.scheduling = self.scheduling + '1'
                else:
                    self.scheduling = self.scheduling + '0'
                bandwidth = config.get(section, 'bandwidth')
                self.bandwidth = self.bandwidth + bandwidth + ','

        # For one to one mapping priority group is same as traffic class
        self.priority_group = self.traffic_class

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

