#!/usr/bin/python
#
#Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import ConfigParser

from requests.exceptions import ConnectionError
from vnc_api.vnc_api import *

def auto_int(x):
    return int(x, 0)

class SetDefaultGlobalQos(object):
    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        if self._args.control < 0 or self._args.control > 255:
            print 'Invalid control DSCP value ' , self._args.control
            return

        if self._args.analytics < 0 or self._args.analytics > 255:
            print 'Invalid Analytics DSCP value ' , self._args.analytics
            return

        if self._args.dns < 0 or self._args.dns > 255:
            print 'Invalid DNS DSCP value ' , self._args.dns
            return
        
        print "Control ", hex(self._args.control)
        print "Analytics ", hex(self._args.analytics)
        print "DNS ", hex(self._args.dns)
        self._vnc_lib = VncApi(self._args.admin_user, self._args.admin_password, 
                               self._args.admin_tenant_name, 
                               self._args.api_server_ip, self._args.api_server_port, '/')
        name = 'default-global-system-config:default-global-qos-config'
        try:
            qos_obj = self._vnc_lib.global_qos_config_read(fq_name_str=name)
        except NoIdError:
            print 'Global Qos Config ' + name + ' not found!'
            return
 
        value = ControlTrafficDscpType(control=self._args.control, analytics=self._args.analytics, dns=self._args.dns)
        qos_obj.set_control_traffic_dscp(value)
        self._vnc_lib.global_qos_config_update(qos_obj)

        try:
            qos_obj = self._vnc_lib.global_qos_config_read(fq_name_str=name)
        except NoIdError:
            print 'Global Qos Config ' + name + ' not found!'
            return
 
        print "Updated global qos config ", str(qos_obj)

    #end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python update_default_global_qos_config.py --control 0x2c --analytics 0x38 --dns 0x40 
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help = False)
        
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'api_server_ip' : '127.0.0.1',
            'api_server_port' : '8082',
            'admin_user': 'admin',
            'admin_password': 'contrail123',
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

        parser.add_argument("--control", type = auto_int, help = "DSCP value for control traffic")
        parser.add_argument("--analytics", type = auto_int, help = "DSCP value for analytics traffic")
        parser.add_argument("--dns", type = auto_int, help = "DSCP value for DNS traffic")
        parser.add_argument("--api_server_ip", help = "IP address of api server")
        parser.add_argument("--api_server_port", type = int, help = "Port of api server")
        parser.add_argument("--admin_user", help = "Name of keystone admin user")
        parser.add_argument("--admin_password", help = "Password of keystone admin user")
        parser.add_argument("--admin_tenant_name", help = "Tenamt name for keystone admin user")
    
        self._args = parser.parse_args(remaining_argv)
        
    #end _parse_args

# end class SetDefaultGlobalQos

def main(args_str = None):
    SetDefaultGlobalQos(args_str)
#end main

if __name__ == "__main__":
    main()
