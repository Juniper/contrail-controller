#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import ConfigParser

from provision_bgp import BgpProvisioner
from vnc_api.vnc_api import *


class ControlProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        if self._args.router_asn and not self._args.oper:
            self._vnc_lib = VncApi(
            self._args.admin_user, self._args.admin_password, self._args.admin_tenant_name,
            self._args.api_server_ip,
            self._args.api_server_port, '/')
            # Update global system config also with this ASN
            gsc_obj = self._vnc_lib.global_system_config_read(
                  fq_name=['default-global-system-config'])
            gsc_obj.set_autonomous_system(self._args.router_asn)
            gsc_obj.set_ibgp_auto_mesh(True)
            self._vnc_lib.global_system_config_update(gsc_obj)
            return 

        bp_obj = BgpProvisioner(
            self._args.admin_user, self._args.admin_password,
            self._args.admin_tenant_name,
            self._args.api_server_ip, self._args.api_server_port)

        if self._args.oper == 'add':
            bp_obj.add_bgp_router('contrail', self._args.host_name,
                                  self._args.host_ip, self._args.router_asn)
        elif self._args.oper == 'del':
            bp_obj.del_bgp_router(self._args.host_name)
        else:
            print "Unknown operation %s. Only 'add' and 'del' supported"\
                % (self._args.oper)

    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python provision_control.py --host_name a3s30.contrail.juniper.net
                                        --host_ip 10.1.1.1
                                        --router_asn 64512
                                        --api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --oper <add | del>
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'router_asn': '64512',
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
            'oper': None,
            'admin_user': None,
            'admin_password': None,
            'admin_tenant_name': None
        }

        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read([args.conf_file])
            defaults.update(dict(config.items("DEFAULTS")))

        # Override with CLI options
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
            "--host_name", help="hostname name of control-node")
        parser.add_argument("--host_ip", help="IP address of control-node")
        parser.add_argument(
            "--router_asn", help="AS Number the control-node is in", required=True)
        parser.add_argument(
            "--api_server_ip", help="IP address of api server", required=True)
        parser.add_argument("--api_server_port", help="Port of api server", required=True)
        parser.add_argument(
            "--oper", 
            help="Provision operation to be done(add or del)")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenamt name for keystone admin user")

        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

# end class ControlProvisioner


def main(args_str=None):
    ControlProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
