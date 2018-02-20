#!/usr/bin/python
#
#Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import ConfigParser

from requests.exceptions import ConnectionError
from vnc_api.vnc_api import *

class ConfigureFatFlow(object):
    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        if not self._parse_args(args_str):
            return

        self._vnc_lib = VncApi(self._args.admin_user, self._args.admin_password,
                               self._args.admin_tenant_name,
                               self._args.api_server_ip, self._args.api_server_port, '/')

        vmi = None
        try:
            vmi = self._vnc_lib.virtual_machine_interface_read(fq_name_str = self._args.vmi_fq_name)
        except NoIdError:
            print "Virtual Machine Interface %s does NOT exist" %(self._args.vmi_fq_name)
            return
       
        proto_list = [] 
        if not self._args.clear:
            proto = ProtocolType(protocol=self._args.protocol, port=self._args.port, ignore_address=self._args.ignore_address)
            proto_list = [proto]
        fat_proto = FatFlowProtocols(fat_flow_protocol=proto_list)
        vmi.set_virtual_machine_interface_fat_flow_protocols(fat_proto)
        self._vnc_lib.virtual_machine_interface_update(vmi)

    #end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python fat_flow.py --vmi_fq_name default-domain:admin:9a8b0557-bd5a-433b-b2f6-25dd84fdc271 --protocol 1 --port=0 --ignore_address="remote"
            python fat_flow.py --vmi_fq_name default-domain:admin:9a8b0557-bd5a-433b-b2f6-25dd84fdc271 --clear
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help = False)

        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'api_server_ip' : '10.204.216.20',
            'api_server_port' : '8082',
            'admin_user': 'admin',
            'admin_password': 'c0ntrail123',
            'admin_tenant_name': 'admin'
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

        parser.add_argument("--vmi_fq_name", help = "FQ Name of Virtual Machine Interface")
        parser.add_argument("--protocol", help = "Protocol number")
        parser.add_argument("--port", type = int, default = 0, help = "Port number")
        parser.add_argument("--ignore_address", choices=['none', 'remote', 'source', 'destination'], help = "Ignore address type")
        parser.add_argument("--clear", help = "clear fat-flow config on interface", action="store_true")
        parser.add_argument("--api_server_ip", help = "IP address of api server")
        parser.add_argument("--api_server_port", type = int, help = "Port of api server")
        parser.add_argument("--admin_user", help = "Name of keystone admin user")
        parser.add_argument("--admin_password", help = "Password of keystone admin user")
        parser.add_argument("--admin_tenant_name", help = "Tenamt name for keystone admin user")

        self._args = parser.parse_args(remaining_argv)

        ret_value  = True
        if not self._args.vmi_fq_name:
            print "VMI FQ-name is missing"
            ret_value = False

        if not self._args.clear:
            if not self._args.protocol:
                print "Protocol is mandatory"
                ret_value = False
            if not self._args.ignore_address: 
                print "Ignore address is mandatory"
                ret_value = False
        return ret_value
    #end _parse_args

#end class ConfigureFatFlow

def main(args_str = None):
    ConfigureFatFlow(args_str)
#end main

if __name__ == "__main__":
    main()
