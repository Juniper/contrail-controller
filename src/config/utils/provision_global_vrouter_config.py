#!/usr/bin/python
#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
from future import standard_library
standard_library.install_aliases()
from builtins import object
import sys
import argparse
import configparser

from cfgm_common.exceptions import RefsExistError
from vnc_api.vnc_api import *
from vnc_admin_api import VncApiAdmin


class GlobalVrouterConfigProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._vnc_lib = VncApiAdmin(
            self._args.use_admin_api,
            self._args.admin_user, self._args.admin_password,
            self._args.admin_tenant_name,
            self._args.api_server_ip,
            self._args.api_server_port, '/',
            api_server_use_ssl=self._args.api_server_use_ssl)

        port_trans_pool_list = []
        if self._args.snat_list:
            port=None
            protocol=None
            for proto_port in self._args.snat_list:
                port_count=''
                port_range_obj=None
                protocol = proto_port.split(':')[0]
                port = proto_port.split(':')[1]
                if '-' in port:
                    port_range_obj = PortType(start_port=int(port.split('-')[0]),
                                              end_port=int(port.split('-')[1]))
                else:
                    port_count = port

                port_trans_pool_obj = PortTranslationPool(protocol = protocol,
                                                          port_range = port_range_obj,
                                                          port_count = port_count)
                port_trans_pool_list.append(port_trans_pool_obj)

        port_trans_pools_obj = PortTranslationPools(port_trans_pool_list)

        try:
            current_config=self._vnc_lib.global_vrouter_config_read(
                                fq_name=['default-global-system-config',
                                         'default-global-vrouter-config'])
        except Exception as e:
            try:
                if self._args.oper == "add":
                    conf_obj=GlobalVrouterConfig(flow_export_rate=self._args.flow_export_rate)
                    conf_obj.set_port_translation_pools(port_trans_pools_obj)
                    result=self._vnc_lib.global_vrouter_config_create(conf_obj)
                    print('Created.UUID is %s'%(result))
                return
            except RefsExistError:
                print("Already created!")

        existing_snat_pools = current_config.get_port_translation_pools()
        if not existing_snat_pools:
             existing_snat_pools = PortTranslationPools([])
        if port_trans_pool_list:
            for snat_pool in port_trans_pool_list:
                if not self.check_dup_snat_pool(snat_pool, existing_snat_pools):
                    existing_snat_pools.add_port_translation_pool(snat_pool)

        if self._args.oper != "add":
            conf_obj=GlobalVrouterConfig()
        else:
            conf_obj=GlobalVrouterConfig(flow_export_rate=self._args.flow_export_rate)
            conf_obj.set_port_translation_pools(existing_snat_pools)

        result=self._vnc_lib.global_vrouter_config_update(conf_obj)
        print('Updated.%s'%(result))
    # end __init__

    def check_dup_snat_pool(self, snat_pool, existing_snat_pools):
        for pool_obj in existing_snat_pools.get_port_translation_pool():
            if snat_pool.get_port_count() and pool_obj.get_port_count():
                if snat_pool.get_port_count() == pool_obj.get_port_count() and \
                    snat_pool.get_protocol() == pool_obj.get_protocol():
                    return True
            elif snat_pool.get_port_range() and pool_obj.get_port_range():
                if snat_pool.get_protocol() == pool_obj.get_protocol() and \
                    snat_pool.get_port_range().start_port == pool_obj.get_port_range().start_port and \
                    snat_pool.get_port_range().end_port == pool_obj.get_port_range().end_port:
                    return True
        return False

    def _parse_args(self, args_str):
        '''
        Eg. python provision_global_vrouter_config.py
                                        --api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --api_server_use_ssl False
                                        --flow_export_rate 10
                                        --oper <add | delete>
                                        --snat_list tcp:6000-7000 udp:500
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
            'api_server_use_ssl': False,
            'oper': 'add',
            'snat_list': None
        }
        ksopts = {
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'admin'
        }

        if args.conf_file:
            config = configparser.SafeConfigParser()
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
            formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        )
        defaults.update(ksopts)
        parser.set_defaults(**defaults)

        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
        parser.add_argument(
            "--flow_export_rate", type=int, required=True,
            help="Flow export rate to the collector")
        parser.add_argument(
            "--oper", default='add', choices=['add', 'delete'],
            help="Provision operation to be done(add or delete)")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenant name for keystone admin user")
        parser.add_argument(
            "--snat_list", help="Protocol port range or port count list for distributed snat",
            nargs='+', type=str)
        group = parser.add_mutually_exclusive_group()
        group.add_argument(
            "--api_server_ip", help="IP address of api server")
        group.add_argument("--use_admin_api",
                            default=False,
                            help = "Connect to local api-server on admin port",
                            action="store_true")
        self._args = parser.parse_args(remaining_argv)
    # end _parse_args

# end class GlobalVrouterConfigProvisioner


def main(args_str=None):
    GlobalVrouterConfigProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
