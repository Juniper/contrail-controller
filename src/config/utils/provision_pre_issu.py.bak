#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import time
import argparse
import ConfigParser

from cfgm_common.exceptions import *
from provision_bgp import BgpProvisioner
from vnc_api.vnc_api import *


class ISSUContrailPreProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)
        connected = False
        tries = 0
        while not connected:
            try:
                self._vnc_lib = VncApi(
                    self._args.admin_user, self._args.admin_password,
                    self._args.admin_tenant_name,
                    self._args.v1_api_server_ip,
                    self._args.api_server_port, '/',
                    auth_host=self._args.openstack_ip,
                    api_server_use_ssl=self._args.api_server_use_ssl)
                connected = True
            except ResourceExhaustionError: # haproxy throws 503
                if tries < 10:
                    tries += 1
                    time.sleep(3)
                else:
                    raise

        gsc_obj = self._vnc_lib.global_system_config_read(
            fq_name=['default-global-system-config'])
        self._global_system_config_obj = gsc_obj

        self.add_control_nodes()
    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python provision_pre_issu.py --host_name a3s30.contrail.juniper.net
                                        --host_ip 10.1.1.1
                                        --v1_api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --api_server_use_ssl False
                                        --oper <add | del>
        '''

        defaults = {
            'v1_api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
            'api_server_use_ssl': False,
            'oper': 'add',
            'router_asn': '64512',
        }
        ksopts = {
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'default-domain'
        }

        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        conf_parser = argparse.ArgumentParser(add_help=False)
        conf_parser.add_argument("-c", "--conf_file", action='append',
            help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        if args.conf_file:
            config = ConfigParser.SafeConfigParser()
            config.read(args.conf_file)
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
            "--v1_api_server_ip", help="IP address of api server")
        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenamt name for keystone admin user")
        parser.add_argument(
            "--openstack_ip", help="IP address of openstack node")
        parser.add_argument(
            "--address_families", help="Address family list",
            choices=["route-target", "inet-vpn", "e-vpn", "erm-vpn", "inet6-vpn"],
            nargs="*", default=[])
        parser.add_argument(
            "--md5", help="Md5 config for the node")
        parser.add_argument(
            "--ibgp_auto_mesh", help="Create iBGP mesh automatically", dest='ibgp_auto_mesh', action='store_true')

        args_obj, remaining_argv = parser.parse_known_args(remaining_argv)
        if args.conf_file:
            args_obj.config_section = config

        if type(args_obj.db_host_info) is str:
            json_string=args_obj.db_host_info.replace("'", "\"")
            args_obj.db_host_info=\
                json.loads(json_string)

        if type(args_obj.config_host_info) is str:
            json_string=args_obj.config_host_info.replace("'", "\"")
            args_obj.config_host_info=\
                json.loads(json_string)

        if type(args_obj.analytics_host_info) is str:
            json_string=args_obj.analytics_host_info.replace("'", "\"")
            args_obj.analytics_host_info=\
                json.loads(json_string)

        if type(args_obj.control_host_info) is str:
            json_string=args_obj.control_host_info.replace("'", "\"")
            args_obj.control_host_info=\
                json.loads(json_string)

        self._args = args_obj
    # end _parse_args

    def add_control_nodes(self):
        gsc_obj = self._global_system_config_obj

        rt_inst_obj = self._vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])

        for k,v in self._args.control_host_info.items():
            router_params = BgpRouterParams(router_type='control-node',
                               vendor='contrail' , 
                               autonomous_system=64512,
                               identifier=k,
                               address=k,
                               port=179,hold_time=90)

            bgp_router_obj = BgpRouter(v, rt_inst_obj,
                                   bgp_router_parameters=router_params)

            # Return early with a log if it already exists
            try:
                fq_name = bgp_router_obj.get_fq_name()
                existing_obj = self._vnc_lib.bgp_router_read(fq_name=fq_name)
                if self._args.md5:
                    bgp_params = existing_obj.get_bgp_router_parameters()
                    # set md5
                    print "Setting md5 on the existing uuid"
                    md5 = {'key_items': [ { 'key': self._args.md5 ,"key_id":0 } ], "key_type":"md5"}
                    bgp_params.set_auth_data(md5)
                    existing_obj.set_bgp_router_parameters(bgp_params)
                    self._vnc_lib.bgp_router_update(existing_obj)
                print ("BGP Router " + pformat(fq_name) +
                   " already exists with uuid " + existing_obj.uuid)
                return
            except NoIdError:
                pass

            cur_id = self._vnc_lib.bgp_router_create(bgp_router_obj)
            cur_obj = self._vnc_lib.bgp_router_read(id=cur_id)

            self._vnc_lib.bgp_router_update(cur_obj)

    # end add_node

# end class ISSUContrailPreProvisioner


def main(args_str=None):
    ISSUContrailPreProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
