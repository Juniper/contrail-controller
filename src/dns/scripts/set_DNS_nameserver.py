#!/usr/bin/python
#
#Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import argparse
import netaddr
import sys

from vnc_api.vnc_api import *

class AddDnsNameServer(object):
    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        server_list =  []
        for ip in self._args.name_server_ips or []:
            server_list.append(str(netaddr.IPAddress(ip)))

        self._vnc_lib = VncApi(self._args.admin_user,
                               self._args.admin_password,
                               self._args.project_name,
                               self._args.api_server_ip,
                               self._args.api_server_port, '/')

        ipam_fq_name = [self._args.domain_name,
                        self._args.project_name,
                        self._args.ipam_network_name]
        ipam_obj = self._vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
        
        props = IpamType()
        props.ipam_dns_method = 'tenant-dns-server'
        props.ipam_dns_server = IpamDnsAddressType(
            tenant_dns_server_address=IpAddressesType(server_list))
        ipam_obj.set_network_ipam_mgmt(props)
        self._vnc_lib.network_ipam_update(ipam_obj)


    def _parse_args(self, args_str):
        '''
        Eg. python set_DNS_nameserver.py --domain-name default-domain
                                         --project-name default-project
                                         --ipam-network-name default-network-ipam
                                         --name-server-ips []
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help = False)
        
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'api_server_ip' : '127.0.0.1',
            'api_server_port' : '8082',
            'admin_user': None,
            'admin_password': None
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

        parser.add_argument("--api-server-ip", help="IP address of api server")
        parser.add_argument("--api-server-port", type=int, help="Port of api server")
        parser.add_argument("--admin-user", help="Name of keystone admin user")
        parser.add_argument("--admin-password", help="Password of keystone admin user")
        parser.add_argument("--domain-name", help="Domain Name", default='default-domain')
        parser.add_argument("--project-name", help="Project Name", default='default-project')
        parser.add_argument("--ipam-network-name", help="IPAM network name", default='default-network-ipam')
        parser.add_argument("--name-server-ips", nargs='+', type=str, help="DNS nameserver IP list", default=[])
    
        self._args = parser.parse_args(remaining_argv)

def main(args_str = None):
    AddDnsNameServer(args_str)
#end main

if __name__ == "__main__":
    main()
