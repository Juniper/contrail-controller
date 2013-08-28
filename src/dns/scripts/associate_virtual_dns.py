#!/usr/bin/python
#
#Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import ConfigParser

from vnc_api.vnc_api import *
from provision_dns import DnsProvisioner
from requests.exceptions import ConnectionError

class AssociateVirtualDns(object):
    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        if self._args.ipam_dns_method == "none" or self._args.ipam_dns_method == "default-dns-server":
            if self._args.vdns_fqname:
                print 'vdns_fqname should not be configured when ipam_dns_method is none/default-dns-server'
                return
            if self._args.tenant_dns_servers:
                print 'tenant_dns_servers should not be configured when ipam_dns_method is none/default-dns-server'
                return
        
        ipam_dns_srv_obj = None
        if self._args.ipam_dns_method == "virtual-dns-server":
            if not self._args.vdns_fqname:
                print 'vdns_fqname is mandatory for ipam method of virtual-dns-server'
                return
            ipam_dns_srv_obj = IpamDnsAddressType(virtual_dns_server_name=self._args.vdns_fqname)
        elif self._args.ipam_dns_method == "tenant-dns-server":
            if not self._args.tenant_dns_servers:
                print 'tenant-dns-servers is mandatory for ipam method of tenant-dns-server'
                return
            dict_servers = {}
            dict_servers['ip_address']=self._args.tenant_dns_servers
            ipam_dns_srv_obj = IpamDnsAddressType(tenant_dns_server_address=dict_servers, virtual_dns_server_name=None)

        try:
            dp_obj = DnsProvisioner(self._args.admin_user, self._args.admin_password, 
                                    self._args.admin_tenant_name, 
                                    self._args.api_server_ip, self._args.api_server_port)
        except ConnectionError:
             print 'Connection to API server failed '
             return

        dp_obj.associate_vdns_with_ipam(self._args.ipam_fqname, self._args.ipam_dns_method, ipam_dns_srv_obj)
    #end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python associate_virtual_dns.py --ipam_fqname default-domain:demo:ipam1
                                            --vdns_fqname default-domain:vdns2
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help = False)
        
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            'api_server_ip' : '127.0.0.1',
            'api_server_port' : '8082',
            'admin_user': None,
            'admin_password': None,
            'admin_tenant_name': None
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

        parser.add_argument("--ipam_fqname", help = "Fully qualified IPAM Name")
        parser.add_argument("--ipam_dns_method", choices=['none', 'default-dns-server', 'tenant-dns-server', 'virtual-dns-server'], help = "Type of Ipam DNS method")
        parser.add_argument("--vdns_fqname", help = "Fully qualified Virtual DNS Name")
        parser.add_argument("--tenant_dns_servers", nargs='+', help = "List of IPs DNS Servers")
        parser.add_argument("--api_server_ip", help = "IP address of api server")
        parser.add_argument("--api_server_port", help = "Port of api server")
        parser.add_argument("--admin_user", help = "Name of keystone admin user")
        parser.add_argument("--admin_password", help = "Password of keystone admin user")
        parser.add_argument("--admin_tenant_name", help = "Tenamt name for keystone admin user")
    
        self._args = parser.parse_args(remaining_argv)

    #end _parse_args

# end class AssociateVirtualDns

def main(args_str = None):
    AssociateVirtualDns(args_str)
#end main

if __name__ == "__main__":
    main()
