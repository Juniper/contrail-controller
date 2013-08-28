#!/usr/bin/python
#
#Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import ConfigParser

from provision_dns import DnsProvisioner
from requests.exceptions import ConnectionError

class AddVirtualDnsRecord(object):
    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        if self._args.rec_ttl < 0 or self._args.rec_ttl > 2147483647:
            print 'Invalid ttl value ' , self._args.rec_ttl 
            return

        if not DnsProvisioner.is_valid_ipv4_address(self._args.api_server_ip):
            print 'Invalid IPv4 address ', self._args.api_server_ip
            return

        rec_name = self._args.rec_name
        if self._args.rec_type == 'A':
            vstr = self._args.rec_name
            if not DnsProvisioner.is_valid_ipv4_address(self._args.rec_data):
               print 'Invalid Ipv4 address ', self._args.rec_data
               return
        elif self._args.rec_type == 'PTR':
            vstr = self._args.rec_data
            if not rec_name.endswith('.in-addr.arpa'): 
                if not DnsProvisioner.is_valid_ipv4_address(rec_name):
                    print 'Invalid PTR record name ', self._args.rec_name
                    return
        elif self._args.rec_type == 'NS' or self._args.rec_type == 'CNAME':
            vstr = self._args.rec_name


        if not DnsProvisioner.is_valid_dns_name(vstr):
            print 'DNS name requirements are not satisfied by ', vstr
            return

        try:
            dp_obj = DnsProvisioner(self._args.admin_user, self._args.admin_password, 
                                    self._args.admin_tenant_name, 
                                    self._args.api_server_ip, self._args.api_server_port)
        except ConnectionError:
             print 'Connection to API server failed '
             return

        dp_obj.add_virtual_dns_record(self._args.name, self._args.vdns_fqname, 
                                      rec_name, self._args.rec_type, 
                                      self._args.rec_class, self._args.rec_data,
                                      self._args.rec_ttl)
    #end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python add_virtual_dns_record.py --name vdns_rec1 
                                             --vdns_fqname default-domain:vdns1
                                             --rec_name one --rec_type A
                                             --rec_class IN --rec_data 23.0.0.41
                                             --rec_ttl 60000 
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

        parser.add_argument("--name", help = "Virtual DNS Record Name")
        parser.add_argument("--vdns_fqname", help = "Fully qualified Virtal Dns Name")
        parser.add_argument("--rec_name", help = "DNS Host name")
        parser.add_argument("--rec_type", choices=['A', 'AAAA', 'CNAME', 'PTR', 'NS'], help = "DNS Record Type")
        parser.add_argument("--rec_class", choices=['IN'], help = "DNS Record class")
        parser.add_argument("--rec_data", help = "Data for DNS Record")
        parser.add_argument("--rec_ttl", type = int, help = "Time to Live for DNS record")
        parser.add_argument("--api_server_ip", help = "IP address of api server")
        parser.add_argument("--api_server_port", type = int, help = "Port of api server")
        parser.add_argument("--admin_user", help = "Name of keystone admin user")
        parser.add_argument("--admin_password", help = "Password of keystone admin user")
        parser.add_argument("--admin_tenant_name", help = "Tenamt name for keystone admin user")
    
        self._args = parser.parse_args(remaining_argv)

    #end _parse_args

# end class AddVirtualDnsRecord

def main(args_str = None):
    AddVirtualDnsRecord(args_str)
#end main

if __name__ == "__main__":
    main()
