#!/usr/bin/python
#This is a python based script for configuring user-defined-log-statistics
# from commandline
# Usage :
# ./provision_user_defined_log_statistics.py --conf /etc/contrail/contrail-schema.conf --conf /etc/contrail/contrail-keystone-auth.conf -h
# usage: user_defined_log_statistics.py [-h] [-c FILE]
#                                       [--api_server_ip API_SERVER_IP]
#                                       [--api_server_port API_SERVER_PORT]
#                                       [--admin_user ADMIN_USER]
#                                       [--admin_password ADMIN_PASSWORD]
#                                       [--admin_tenant_name ADMIN_TENANT_NAME]
#                                       {add,delete,list} ...
#
# positional arguments:
#   {add,delete,list}     Operations (add|delete|list)
#
# optional arguments:
#   -h, --help            show this help message and exit
#   -c FILE, --conf_file FILE
#                         Specify config file
#   --api_server_ip API_SERVER_IP
#                         IP address of api server
#   --api_server_port API_SERVER_PORT
#                         Port of api server
#   --admin_user ADMIN_USER
#                         Name of keystone admin user
#   --admin_password ADMIN_PASSWORD
#                         Password of keystone admin user
#   --admin_tenant_name ADMIN_TENANT_NAME
#                         Tenamt name for keystone admin user
#
#
# ./provision_user_defined_log_statistics.py --conf /etc/contrail/contrail-schema.conf --conf /etc/contrail/contrail-keystone-auth.conf list
# ls ->
# Configured:
# Name: "HostName", Pattern: "a5s318"
# Name: "MyIp", Pattern: "10.84.14.38"
# ./provision_user_defined_log_statistics.py --conf /etc/contrail/contrail-schema.conf --conf /etc/contrail/contrail-keystone-auth.conf add statname 'foo.*bar'
# Add ->  statname, foo.*bar
# ./provision_user_defined_log_statistics.py --conf /etc/contrail/contrail-schema.conf --conf /etc/contrail/contrail-keystone-auth.conf list
# ls ->
# Configured:
# Name: "statname", Pattern: "foo.*bar"
# Name: "HostName", Pattern: "a5s318"
# Name: "MyIp", Pattern: "10.84.14.38"
# ./provision_user_defined_log_statistics.py --conf /etc/contrail/contrail-schema.conf --conf /etc/contrail/contrail-keystone-auth.conf delete statname
# Delete ->  statname
# ./provision_user_defined_log_statistics.py --conf /etc/contrail/contrail-schema.conf --conf /etc/contrail/contrail-keystone-auth.conf list
# ls ->
# Configured:
# Name: "HostName", Pattern: "a5s318"
# Name: "MyIp", Pattern: "10.84.14.38"
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import argparse
import ConfigParser
import sys

from vnc_api.vnc_api import *
from vnc_api.gen.resource_xsd import UserDefinedLogStat
from vnc_api.gen.resource_client import GlobalSystemConfig
from vnc_admin_api import VncApiAdmin


class VncProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._vnc_lib = VncApiAdmin(self._args.use_admin_api,
                               self._args.admin_user,
                               self._args.admin_password,
                               self._args.admin_tenant_name,
                               self._args.api_server_ip,
                               self._args.api_server_port, '/')
        vnc = self._vnc_lib
        gsc_uuid = vnc.global_system_configs_list()['global-system-configs'][
                                                    0]['uuid']
        gsc = vnc.global_system_config_read(id=gsc_uuid)

        if hasattr(self._args, 'add'):
            print 'Add -> ', ', '.join(self._args.add)
            g=GlobalSystemConfig()
            g.add_user_defined_log_statistics(UserDefinedLogStat(
                                                        *self._args.add))
            vnc.global_system_config_update(g)
        elif hasattr(self._args, 'delete'):
            print 'Delete -> ', ', '.join(self._args.delete)
            if gsc.user_defined_log_statistics:
                gsc.user_defined_log_statistics.statlist = filter(
                        lambda x: x.name not in self._args.delete,
                        gsc.user_defined_log_statistics.statlist)
                gsc.set_user_defined_log_statistics(
                        gsc.user_defined_log_statistics)
                vnc.global_system_config_update(gsc)
        elif hasattr(self._args, 'list'):
            print 'ls -> ', ', '.join(self._args.list)
            print 'Configured:'
            if gsc.user_defined_log_statistics:
                for x in gsc.user_defined_log_statistics.statlist:
                    if self._chk2print(x.name):
                        print 'Name: "%s", Pattern: "%s"' % (x.name, x.pattern)
    # end __init__

    def _chk2print(self, n):
        if self._args.list:
            return n in self._args.list
        return True

    def _parse_args(self, args_str):
        '''
        Eg. python provision_physical_router.py
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
            #'public_vn_name': 'default-domain:'
            #'default-project:default-virtual-network',
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
        }
        ksopts = {
            'admin_user': 'admin',
            'admin_password': 'c0ntrail123',
            'admin_tenant_name': 'default-domain'
        }

        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read([args.conf_file])
            if 'DEFAULTS' in config.sections():
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

        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenamt name for keystone admin user")

        subparsers = parser.add_subparsers(help='Operations (add|delete|list)')
        add_p = subparsers.add_parser('add')
        del_p = subparsers.add_parser('delete')
        lst_p = subparsers.add_parser('list')

        add_p.add_argument("add", nargs=2, help="name 'pattern'")
        del_p.add_argument("delete", nargs='+', help="name [name ...]")
        lst_p.add_argument("list", nargs='*', help="[name ...]")
        group = parser.add_mutually_exclusive_group()
        group.add_argument(
            "--api_server_ip", help="IP address of api server")
        group.add_argument("--use_admin_api",
                            default=False,
                            help = "Connect to local api-server on admin port",
                            action="store_true")

        self._args = parser.parse_args(remaining_argv)
    # end _parse_args

# end class VncProvisioner


def main(args_str=None):
    VncProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
