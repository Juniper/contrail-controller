#!/usr/bin/python
#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#

import os
import sys
import argparse
import ConfigParser
import requests

from netaddr.ip import IPNetwork
from vnc_api.vnc_api import *


class ProvisionVgwInterface(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)
        headers = {'content-type': 'application/json'}
        url = "http://localhost:9091/gateway"

        if self._args.oper == "create":
            print "Creating virtual-gateway ..."

            with open("/proc/sys/net/ipv4/ip_forward", "w") as file:
                file.write("1")

            vif_command = '/usr/bin/vif --create ' + self._args.interface
            vif_command += ' --mac 00:00:5e:00:01:00'
            self.execute_command(vif_command)

            ifconfig_command = 'ifconfig ' + self._args.interface + ' up'
            self.execute_command(ifconfig_command)

            for subnet in self._args.subnets:
                route_command = 'route add -net ' + subnet
                route_command += ' dev ' + self._args.interface
                self.execute_command(route_command)

            subnet_list = []
            first = True
            subnets_str = "\"subnets\":["
            for subnet in self._args.subnets:
                net = IPNetwork(subnet)
                if not first:
                    subnets_str += ","
                first = False
                subnets_str += "{\"ip-address\":\"%s\", \"prefix-len\":%d}" % (str(net.ip), net.prefixlen)
            subnets_str += "]"

            route_list = []
            first = True
            routes_str = "\"routes\":["
            for subnet in self._args.routes:
                net = IPNetwork(subnet)
                if not first:
                    routes_str += ","
                first = False
                routes_str += "{\"ip-address\":\"%s\", \"prefix-len\":%d}" % (str(net.ip), net.prefixlen)
            routes_str += "]"

            gw_str = "[{\"interface\":\"%s\", \"routing-instance\":\"%s\", %s, %s}]" %(self._args.interface, self._args.vrf, subnets_str, routes_str)

            try:
                r = requests.post(url, data=gw_str, headers=headers)
            except ConnectionError:
                print "Error: Error adding VGW interface"
                return
            if r.status_code != 200:
                print "Failed to Add VGW interface"
                return
            print "Done creating virtual-gateway..."

        else:
            print "Deleting virtual-gateway ..."
            gw_str = "[{\"interface\":\"%s\"}]" % (self._args.interface)
            try:
                r = requests.delete(url, data=gw_str, headers=headers)
            except ConnectionError:
                print "Error: Error deleting VGW interface"
                return
            if r.status_code != 200:
                print "Failed to Delete VGW interface"
                return
            for subnet in self._args.subnets:
                route_command = 'route del -net ' + subnet
                route_command += ' dev ' + self._args.interface
                self.execute_command(route_command)

            ifconfig_command = 'ifconfig ' + self._args.interface + ' down'
            self.execute_command(ifconfig_command)

            interface_index = self.get_interface_index(self._args.interface)
            if interface_index != -1:
                vif_command = '/usr/bin/vif --delete ' + interface_index
                self.execute_command(vif_command)

            del_cmd = 'ip link del ' + self._args.interface
            self.execute_command(del_cmd)
            print "Done deleting virtual-gateway..."

    # end __init__
    
    def execute_command(self, cmd):
        print cmd
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
                                        --routes 8.8.8.0/24 9.9.9.0/24
                                        --vrf default-domain:admin:vn1:vn1
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'oper': 'create',
            'interface': '',
            'subnets': [],
            'routes': [],
            'vrf': '',
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
            "--interface", help="Name of the gateway interface")
        parser.add_argument(
            "--subnets", nargs='+', 
            help="List of subnets in virtual-network configured for gateway (Ex: 1.1.1.0/24 2.2.2.0/24)")
        parser.add_argument(
            "--routes", nargs='+', 
            help="List of public routes injected into virtual-network routing-instance (Ex: 8.8.8.0/24 9.9.9.0/24)")
        parser.add_argument(
            "--vrf",
            help="Routing instance for virtual-network configured for gateway (as FQDN)")

        self._args = parser.parse_args(remaining_argv)
        if not self._args.interface:
            parser.error('Missing argument interface')
        if not self._args.subnets:
            parser.error('Missing argument subnets')
        if self._args.oper == "create":
            if not self._args.routes:
                parser.error('Missing argument routes')
            if not self._args.vrf:
                parser.error('Missing argument vrf')
    # end _parse_args

# end class ProvisionVgwInterface

def main(args_str=None):
    ProvisionVgwInterface(args_str)
# end main

if __name__ == "__main__":
    main()
