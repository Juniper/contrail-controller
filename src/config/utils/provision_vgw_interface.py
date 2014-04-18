#!/usr/bin/python
#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

import os
import sys
import argparse
import ConfigParser

from vnc_api.vnc_api import *

class ProvisionVgwInterface(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        if self._args.oper == "create":
            print "Creating interface..."

            with open("/proc/sys/net/ipv4/ip_forward", "w") as file:
                file.write("1")

            vif_command = '/usr/bin/vif --create ' + self._args.interface + ' --mac 00:01:00:5e:00:00'
            print vif_command
            self.execute_command(vif_command)

            ifconfig_command = 'ifconfig ' + self._args.interface + ' up'
            print ifconfig_command
            self.execute_command(ifconfig_command)

            for subnet in self._args.subnets:
                route_command = 'route add -net ' + subnet + ' dev ' + self._args.interface
                print route_command
                self.execute_command(route_command)

            print "Done creating interface..."

        else:
            print "Deleting interface..."

            for subnet in self._args.subnets:
                route_command = 'route del -net ' + subnet + ' dev ' + self._args.interface
                print route_command
                self.execute_command(route_command)

            ifconfig_command = 'ifconfig ' + self._args.interface + ' down'
            print ifconfig_command
            self.execute_command(ifconfig_command)

            interface_index = self.get_interface_index(self._args.interface)
            if interface_index != -1:
                vif_command = '/usr/bin/vif --delete ' + interface_index
                print vif_command
                self.execute_command(vif_command)

            print "Done deleting interface..."

    # end __init__
    
    def execute_command(self, cmd):
        out = os.system(cmd)
        if out != 0:
            print "Error executing : " + cmd
    #end execute_command

    def get_interface_index(self, interface):
        import subprocess
        proc = subprocess.Popen(["/usr/bin/vif", "--list"], stdout=subprocess.PIPE)
        vif_list, err = proc.communicate()

        vif_match = 'OS: ' + interface
        lines = [line for line in vif_list.split('\n') if line.endswith(vif_match)]
        for line in lines:
            lineitems = line.split(' ')
            first = lineitems[0]
            index = first.split('/')
            return index[1]
        return -1
    #end get_interface_index

    def _parse_args(self, args_str):
        '''
        Eg. python provision_vgw_interface.py 
                                        --oper <create | delete>
                                        --interface vgw1
                                        --subnets 1.2.3.0/24 7.8.9.0/24
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'oper': 'create',
            'interface': '',
            'subnets': [],
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
            "--oper", help="Operation : create / delete")
        parser.add_argument(
            "--interface", help="Name of the interface")
        parser.add_argument(
            "--subnets", nargs='+', help="list of subnets in the address/plen format")

        self._args = parser.parse_args(remaining_argv)
        if not self._args.interface:
            parser.error('interface is required')
        if not self._args.subnets:
            parser.error('subnets are required')

    # end _parse_args

# end class ProvisionVgwInterface

def main(args_str=None):
    ProvisionVgwInterface(args_str)
# end main

if __name__ == "__main__":
    main()
