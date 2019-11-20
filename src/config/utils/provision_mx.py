#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
from future import standard_library
standard_library.install_aliases()
from builtins import object
import sys
import argparse
import configparser

from provision_bgp import BgpProvisioner
from vnc_api.vnc_api import *
from cfgm_common.exceptions import *
from vnc_admin_api import VncApiAdmin

class MxProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        self._peer_list = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)
        if self._args.peer_list:
            self._peer_list = self._args.peer_list.split(',')

        self._vnc_lib = VncApiAdmin(
            self._args.use_admin_api, self._args.admin_user, self._args.admin_password,
            self._args.admin_tenant_name, self._args.api_server_ip,
            self._args.api_server_port, '/', self._args.api_server_use_ssl)

        self.add_bgp_router()
        if self._args.oper == 'add':
            self.add_physical_device()
        elif self._args.oper == 'del':
            self.delete_physical_device()

    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python provision_mx.py --router_name mx1
                                   --router_ip 10.1.1.1
                                   --loopback_ip 1.1.1.1
                                   --product_name MX80
                                   --device_user root
                                   --device_password pwd
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
            'loopback_ip': None,
            'vendor_name': 'Juniper',
            'product_name': 'MX80',
            'device_user': None,
            'device_password': None,
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
            'api_server_use_ssl': False,
            'oper': 'add',
            'role': None,
            'admin_user': None,
            'admin_password': None,
            'admin_tenant_name': None,
            'sub_cluster_name': None,
            'peer_list': None,
        }

        if args.conf_file:
            config = configparser.SafeConfigParser()
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
        parser.add_argument("--loopback_ip", help="Loopback IP address of MX")
        parser.add_argument(
            "--product_name", default='MX80', help="Product name of the MX", required=True)
        parser.add_argument(
            "--device_user", help="Username for MX login")
        parser.add_argument(
            "--device_password", help="Password for MX login")
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
        parser.add_argument(
            "--sub_cluster_name", help="sub cluster to associate to",
            required=False)
        parser.add_argument(
            "--peer_list", help="list of control node names to peer",
            required=False)
        group = parser.add_mutually_exclusive_group()
        group.add_argument(
            "--api_server_ip", help="IP address of api server",
            nargs='+', type=str)
        group.add_argument("--use_admin_api",
                            default=False,
                            help = "Connect to local api-server on admin port",
                            action="store_true")

        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

    def _get_rt_inst_obj(self):
        vnc_lib = self._vnc_lib

        # TODO pick fqname hardcode from common
        rt_inst_obj = vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])

        return rt_inst_obj
    #end _get_rt_inst_obj

    def add_bgp_router(self):

        bp_obj = BgpProvisioner(
            self._args.admin_user, self._args.admin_password,
            self._args.admin_tenant_name, self._args.api_server_ip,
            self._args.api_server_port, self._args.api_server_use_ssl,
            self._args.use_admin_api, peer_list=self._peer_list,
            sub_cluster_name=self._args.sub_cluster_name)

        if self._args.oper == 'add':
            bp_obj.add_bgp_router('router', self._args.router_name,
                                  self._args.router_ip, self._args.router_asn,
                                  self._args.address_families)
        elif self._args.oper == 'del':
            bp_obj.del_bgp_router(self._args.router_name)
        else:
            print("Unknown operation %s. Only 'add' and 'del' supported"\
                % (self._args.oper))
    # end add_bgp_router

    def add_physical_device(self):
        pr = PhysicalRouter(self._args.router_name)
        pr.physical_router_management_ip = self._args.router_ip
        if self._args.loopback_ip:
            pr.physical_router_dataplane_ip = self._args.loopback_ip
        pr.physical_router_vendor_name = self._args.vendor_name
        pr.physical_router_product_name = self._args.product_name
        pr.physical_router_vnc_managed = True
        if self._args.role:
            pr.physical_router_role = self._args.role
        if self._args.device_user and self._args.device_password:
            uc = UserCredentials(self._args.device_user, self._args.device_password)
            pr.set_physical_router_user_credentials(uc)

        rt_inst_obj = self._get_rt_inst_obj()
        fq_name = rt_inst_obj.get_fq_name() + [self._args.router_name]
        bgp_router = self._vnc_lib.bgp_router_read(fq_name=fq_name)

        pr.set_bgp_router(bgp_router)

        self._vnc_lib.physical_router_create(pr)
    # end add_physical_device

    def del_physical_device(self):
        pr_check=GetDevice(self._vnc_lib, self._args.router_name)
        uuid=pr_check.Get()
        if uuid:
            self._vnc_lib.physical_router_delete(id=uuid)
        else:
            print('No device found with Name : %s' %(self._args.device_name))
    # end del_physical_device

# end class MxProvisioner


def main(args_str=None):
    MxProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
