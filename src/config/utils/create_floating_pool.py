#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import argparse
import ConfigParser

import json
import copy
from netaddr import IPNetwork

from vnc_api.vnc_api import *


def get_ip(ip_w_pfx):
    return str(IPNetwork(ip_w_pfx).ip)
# end get_ip


class VncProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._vnc_lib = VncApi(self._args.admin_user,
                               self._args.admin_password,
                               self._args.admin_tenant_name,
                               self._args.api_server_ip,
                               self._args.api_server_port, '/')
        vnc_lib = self._vnc_lib

        fq_name = self._args.public_vn_name.split(':')
        vn_obj = vnc_lib.virtual_network_read(fq_name=fq_name)

        self.add_floating_ip_pool(vn_obj, self._args.floating_ip_pool_name)
    # end __init__

    def add_floating_ip_pool(self, pub_vn_obj, fip_pool_name):
        vnc_lib = self._vnc_lib

        fip_pool_obj = FloatingIpPool(fip_pool_name, pub_vn_obj)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)

    # end add_route_target

    def _parse_args(self, args_str):
        '''
        Eg. python create_floating_pool.py --public_vn_name dom:proj:pub-vn
                                   --floating_ip_pool_name fip_pool
                                   --api_server_ip 127.0.0.1
                                   --api_server_port 8082
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'public_vn_name': 'default-domain:'
            'default-project:default-virtual-network',
            'floating_ip_pool_name': 'fip_pool',
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
        }
        ksopts = {
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'default-domain'
        }

        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read([args.conf_file])
            defaults.update(dict(config.items("DEFAULTS")))
            if 'KEYSTONE' in config.sections():
                ksopts.update(dict(config.items("KEYSTONE")))

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
        defaults.update(ksopts)
        parser.set_defaults(**defaults)

        parser.add_argument(
            "--public_vn_name", help="Colon separated fully qualified name", required=True)
        parser.add_argument(
            "--floating_ip_pool_name", help="Name of the floating IP pool", required=True)
        parser.add_argument(
            "--api_server_ip", help="IP address of api server", required=True)
        parser.add_argument("--api_server_port", help="Port of api server", required=True)
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user", required=True)
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user", required=True)
        parser.add_argument(
            "--admin_tenant_name", help="Tenamt name for keystone admin user", required=True)

        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

# end class VncProvisioner


def main(args_str=None):
    VncProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
