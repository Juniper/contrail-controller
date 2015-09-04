#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import ConfigParser
import netaddr	

from vnc_api.vnc_api import *
from vnc_api.gen.resource_xsd import RouteType
from vnc_api.gen.resource_xsd import RouteTableType
from vnc_api.gen.resource_client import InterfaceRouteTable
from netaddr import *


class StaticRouteProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._vnc_lib = VncApi(
            self._args.user, self._args.password,
            self._args.tenant_name,
            self._args.api_server_ip,
            self._args.api_server_port, '/')
        
        prefix = self._args.prefix
        vmi_id_got = self._args.virtual_machine_interface_id
        route_table_name = self._args.route_table_name

        try:
            ip_nw = IPNetwork(prefix)
        except AddrFormatError:
            print 'Invalid ip address format'
            sys.exit(1)

        project_fq_name_str = 'default-domain:'+ self._args.tenant_name
        project_fq_name = project_fq_name_str.split(':')
        project_obj = self._vnc_lib.project_read(fq_name=project_fq_name)
        
        route_table = RouteTableType(route_table_name)
        route_table.set_route([])
        intf_route_table = InterfaceRouteTable(
                                interface_route_table_routes = route_table,
                                parent_obj=project_obj, 
                                name=route_table_name)
        try:
            route_table_obj = self._vnc_lib.interface_route_table_read(
                                    fq_name = intf_route_table.get_fq_name())
            intf_route_table_id = route_table_obj.uuid
        except NoIdError:
            if self._args.oper == 'del':
                print "Route table %s does not exist" %(route_table_name)
                sys.exit(1)
            print "Creating Route table"
            intf_route_table_id = self._vnc_lib.interface_route_table_create(
                                    intf_route_table)
        intf_route_table_obj = self._vnc_lib.interface_route_table_read(
                                    id = intf_route_table_id) 
        if self._args.oper == 'add':
            intf_route_table_obj = self.add_route(intf_route_table_obj, prefix)
        elif self._args.oper == 'del':
            intf_route_table_obj = self.del_route(intf_route_table_obj, prefix)
        self._vnc_lib.interface_route_table_update(intf_route_table_obj)
        
        #Update the VMI Object now
        vmi_obj = self._vnc_lib.virtual_machine_interface_read(id = vmi_id_got)
        if self._args.oper == 'add':
            vmi_obj.add_interface_route_table(intf_route_table_obj)
        elif self._args.oper == 'del':
            if self.is_route_table_empty(intf_route_table_obj):
                vmi_obj.del_interface_route_table(intf_route_table_obj)
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)

    # end __init__
    
    def add_route(self, intf_route_table_obj, prefix):
        rt_routes = intf_route_table_obj.get_interface_route_table_routes()
        routes = rt_routes.get_route()
        found = False
        for route in routes:
            if route.prefix == prefix:
                print "Prefix already present in Interface Route Table, not adding"
                found = True
                sys.exit(0) 
        if not found:
   	    rt1 = RouteType(prefix = prefix)
        routes.append(rt1)
        intf_route_table_obj.set_interface_route_table_routes(rt_routes)
        return intf_route_table_obj
    #end add_route 
     
    def del_route(self, intf_route_table_obj, prefix):
#        routes = intf_route_table_obj['interface_route_table_routes']['route']
        rt_routes = intf_route_table_obj.get_interface_route_table_routes()
        routes = rt_routes.get_route()
        found = False
        for route in routes:
            if route.prefix == prefix:
                found = True
                routes.remove(route)
        if not found : 
            print "Prefix %s not found in Route table %s!" %( prefix, intf_route_table_obj.name)
            sys.exit(1)
        intf_route_table_obj.set_interface_route_table_routes(rt_routes)
        return intf_route_table_obj
    
    def is_route_table_empty(self, intf_route_table_obj):
        rt_routes = intf_route_table_obj.get_interface_route_table_routes()
        if len(rt_routes.get_route()) == 0 :
            return True
        else:
            return False
    #end is_route_table_empty

    def _parse_args(self, args_str):
        '''
        Eg. python provision_static_route.py 
                                        --api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --prefix 2.2.2.0/24
                                        --virtual_machine_interface_id 242717c9-8e78-4c67-94a8-5fbef1f2f096 
                                        --route_table_name "MyRouteTable" 
                                        --tenant_name "admin"
                                        --oper <add | del>
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
            'oper': 'add',
            'control_names': [],
            'route_table_name': 'CustomRouteTable',
        }
        ksopts = {
            'user': 'user1',
            'password': 'password1',
            'tenant_name': 'default-domain'
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
            "--prefix", help="IP Destination prefix to be updated in the Route", required=True)
        parser.add_argument(
            "--api_server_ip", help="IP address of api server")
        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument(
            "--oper", default='add',
            help="Provision operation to be done(add or del)")
        parser.add_argument(
            "--virtual_machine_interface_id", help="Next hop which is the UUID of the VMI(aka port-id)")
        parser.add_argument(
            "--tenant_name", help="Tenant name for keystone admin user")
        parser.add_argument(
            "--user", help="Name of keystone admin user")
        parser.add_argument(
            "--password", help="Password of keystone admin user")
        parser.add_argument(
            "--route_table_name", help="Route Table name. Default : CustomRouteTable")

        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

# end class StaticRouteProvisioner


def main(args_str=None):
    st=StaticRouteProvisioner(args_str)
    #st.add_route()
# end main

if __name__ == "__main__":
    main()
