#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import ConfigParser

from provision_bgp import BgpProvisioner


class MxProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        bp_obj = BgpProvisioner(
            self._args.admin_user, self._args.admin_password,
            self._args.admin_tenant_name, self._args.api_server_ip,
            self._args.api_server_port, self._args.api_server_use_ssl,
            self._args.use_admin_api)

        if self._args.oper == 'add':
            bp_obj.add_bgp_router('router', self._args.router_name,
                                  self._args.router_ip, self._args.router_asn,
                                  self._args.address_families)
        elif self._args.oper == 'del':
            bp_obj.del_bgp_router(self._args.router_name)
        else:
            print "Unknown operation %s. Only 'add' and 'del' supported"\
                % (self._args.oper)

    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python provision_mx.py --router_name mx1
                                   --router_ip 10.1.1.1
                                   --router_asn 64512
                                   --api_server_ip 127.0.0.1
                                   --api_server_port 8082
                                   --api_server_use_ssl False
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
            'api_server_use_ssl': False,
            'oper': 'add',
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

        parser.add_argument("--router_name", help="System name of MX")
        parser.add_argument("--router_ip", help="IP address of MX")
        parser.add_argument("--router_asn", help="AS Number the MX is in")
        parser.add_argument(
            "--address_families", help="Address family list",
            choices=["route-target", "inet-vpn", "e-vpn", "erm-vpn", "inet6-vpn"],
            nargs="*", default=[])
        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
        parser.add_argument(
            "--oper", default='add',
            help="Provision operation to be done(add or del)")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenamt name for keystone admin user")
        group = parser.add_mutually_exclusive_group()
        group.add_argument(
            "--api_server_ip", help="IP address of api server")
        group.add_argument("--use_admin_api",
                            default=False,
                            help = "Connect to local api-server on admin port",
                            action="store_true")

        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

# end class MxProvisioner


def main(args_str=None):
    MxProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
