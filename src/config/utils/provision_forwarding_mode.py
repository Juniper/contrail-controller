#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import ConfigParser

from vnc_api.vnc_api import *

class ForwardingModeSetup(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._vnc_lib = VncApi(
            self._args.admin_user, self._args.admin_password,
            self._args.admin_tenant_name,
            self._args.api_server_ip,
            self._args.api_server_port, '/')
        
        #import pdb;pdb.set_trace()
        vxlan_id = self._args.vxlan_id
        vn_name = self._args.vn_name
        forwarding_mode = self._args.forwarding_mode
        project_fq_name_str = self._args.project_fq_name
        project_fq_name = project_fq_name_str.split(':')
        
        #Figure out VN
        vni_list = self._vnc_lib.virtual_networks_list(
                        parent_fq_name = project_fq_name)['virtual-networks']
        found = False
        for vni_record in vni_list:
            if (vni_record['fq_name'][0] == project_fq_name[0] and
                vni_record['fq_name'][1] == project_fq_name[1] and 
                vni_record['fq_name'][2] == vn_name):
                vni_obj = self._vnc_lib.virtual_network_read(
                                    id = vni_record['uuid'])
                vni_obj_properties = vni_obj.get_virtual_network_properties()
                if (vxlan_id is not None):
                    vni_obj_properties.set_vxlan_network_identifier(int(vxlan_id))
                if (forwarding_mode is not None):
                    vni_obj_properties.set_forwarding_mode(forwarding_mode)
                vni_obj.set_virtual_network_properties(vni_obj_properties)
                self._vnc_lib.virtual_network_update(vni_obj)
                found = True

        if not found:
            print "No Virtual Network  %s" %(vn_name)
            sys.exit(1)
        
    # end __init__
    
    def _parse_args(self, args_str):
        '''
        Eg. python provision_forwarding_mode.py 
                                        --project_fq_name 'default-domain:admin'
                                        --vn_name vn1
                                        --vxlan_id 100
                                        --forwarding_mode l2
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
            'project_fq_name' : 'default-domain:admin',
        }
        ksopts = {
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'default-domain'
        }

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
            "--vn_name", help="VN Name", required=True)
        parser.add_argument(
            "--project_fq_name", help="Fully qualified name of the Project", required=True)
        parser.add_argument(
            "--vxlan_id", help="VxLan ID", required=True)
        parser.add_argument("--api_server_port", help="Port of api server", required=True)
        parser.add_argument(
            "--forwarding_mode", help="l2_l3 or l2 only", required=True)

        self._args = parser.parse_args(remaining_argv)
    # end _parse_args

# end class ForwardingModeSetup 


def main(args_str=None):
    ForwardingModeSetup(args_str)
# end main

if __name__ == "__main__":
    main()
