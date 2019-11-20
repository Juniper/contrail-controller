#!/usr/bin/python
#
#Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
from future import standard_library
standard_library.install_aliases()
from builtins import object
import sys
import argparse
import configparser

from requests.exceptions import ConnectionError
from vnc_api.vnc_api import *

class VrouterIpam(object):
    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        if not self._parse_args(args_str):
            return

        self._vnc_lib = VncApi(self._args.admin_user, self._args.admin_password,
                               self._args.admin_tenant_name,
                               self._args.api_server_ip, self._args.api_server_port, '/')

        if self._args.oper == 'add':
            self.HandleAdd()
        elif self._args.oper == 'delete':
            self.HandleDelete()
        elif self._args.oper == 'associate':
            self.HandleAssociate()
        elif self._args.oper == 'disassociate':
            self.HandleDisassociate()
    #end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python vrouter_ipam.py --name ipam1 --oper=add --prefix="20.0.0.0" --prefix_len=16
            python vrouter_ipam.py --name ipam1 --oper=delete
            python vrouter_ipam.py --name ipam1 --oper=associate --vrouter_name="default-global-system-config:nodea28" --prefix="20.0.1.0" --prefix_len=24
            python vrouter_ipam.py --name ipam1 --oper=disassociate --vrouter_name="default-global-system-config:nodea28"
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

        parser.add_argument("--name", help = "Name of Network-Ipam", required=True)
        parser.add_argument("--oper", choices=['add', 'delete', 'associate', 'disassociate'], help = "operation type - create/remove/associate/disassociate Network-Ipam", required=True)
        parser.add_argument("--vrouter_name", help = "Fully qualified name of virtual-router object")
        parser.add_argument("--prefix", help = "Subnet in dotted decimal notation")
        parser.add_argument("--prefix_len", type = int, help = "Subnet prefix length")
        parser.add_argument("--api_server_ip", help = "IP address of api server")
        parser.add_argument("--api_server_port", type = int, help = "Port of api server")
        parser.add_argument("--admin_user", help = "Name of keystone admin user")
        parser.add_argument("--admin_password", help = "Password of keystone admin user")
        parser.add_argument("--admin_tenant_name", help = "Tenamt name for keystone admin user")

        self._args = parser.parse_args(remaining_argv)

        ret_value  = True
        if self._args.oper == 'associate' or self._args.oper == 'disassociate':
            if not self._args.vrouter_name:
                print("virtual-router FQ-name required for --oper=associate/disassociate")
                ret_value = False
        else:
            if not self._args.name:
                print("Argument --name required for --oper=add or --oper=delete")
                ret_value = False
            if not self._args.prefix and self._args.oper == 'add':
                print("Argument --prefix required for --oper=add")
                ret_value = False
            if not self._args.prefix_len and self._args.oper == 'add':
                print("Argument --prefix_len required for --oper=add")
                ret_value = False
        return ret_value
    #end _parse_args

    def HandleAdd(self):
        ipam_fq_name = "default-domain:default-project:" + self._args.name
        ipam_obj = None
        try:
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name = None, fq_name_str=ipam_fq_name)
        except NoIdError:
            pass
        if ipam_obj is not None:
            print("Ipam %s already present" %(ipam_fq_name))
            return
        ipam_subnets_obj = IpamSubnets()
        ipam_subnets_obj.add_subnets(IpamSubnetType(SubnetType(self._args.prefix, self._args.prefix_len)))
        ipam_obj = NetworkIpam(name = self._args.name, parent_obj = None, network_ipam_mgmt=None, ipam_subnets=ipam_subnets_obj, ipam_subnet_method="flat-subnet")
        self._vnc_lib.network_ipam_create(ipam_obj)
    #end HandleAdd

    def HandleDelete(self):
        ipam_fq_name = "default-domain:default-project:" + self._args.name
        ipam_obj = None
        try:
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name = None, fq_name_str=ipam_fq_name)
        except NoIdError:
            print("Ipam %s does not exist" %(ipam_fq_name))
            return
        if ipam_obj is not None:
            self._vnc_lib.network_ipam_delete(ipam_obj.get_fq_name())
    #end HandleDelete

    def HandleAssociate(self):
        ipam_fq_name = "default-domain:default-project:" + self._args.name
        ipam_obj = None
        try:
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name = None, fq_name_str=ipam_fq_name)
        except NoIdError:
            print("Ipam %s does not exist" %(ipam_fq_name))
            return
        vr_obj = None
        try:
            vr_obj = self._vnc_lib.virtual_router_read(fq_name = None, fq_name_str=self._args.vrouter_name)
        except NoIdError:
            print("Vrouter %s does not exist" %(self._args.vrouter_name))
            return
        pool_data = AllocationPoolType(start=None, end=None, vrouter_specific_pool=True)
        subnet_data = SubnetType(self._args.prefix, self._args.prefix_len)
        data = VirtualRouterNetworkIpamType(allocation_pools=[pool_data], subnet=[subnet_data])
        vr_obj.add_network_ipam(ipam_obj, data)
        self._vnc_lib.virtual_router_update(vr_obj)
    #end HandleAssociate

    def HandleDisassociate(self):
        ipam_fq_name = "default-domain:default-project:" + self._args.name
        ipam_obj = None
        try:
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name = None, fq_name_str=ipam_fq_name)
        except NoIdError:
            print("Ipam %s does not exist" %(ipam_fq_name))
            return
        vr_obj = None
        try:
            vr_obj = self._vnc_lib.virtual_router_read(fq_name = None, fq_name_str=self._args.vrouter_name)
        except NoIdError:
            print("Vrouter %s does not exist" %(self._args.vrouter_name))
            return
        vr_obj.del_network_ipam(ipam_obj)
        self._vnc_lib.virtual_router_update(vr_obj)
    #end HandleDisassociate

#end class VrouterIpam

def main(args_str = None):
    VrouterIpam(args_str)
#end main

if __name__ == "__main__":
    main()
