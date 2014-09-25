#!/usr/bin/python
#
#Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import ConfigParser

from provision_dns import DnsProvisioner
from requests.exceptions import ConnectionError

class AddVirtualDns(object):
    def __init__(self, args_str = None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        if not self._args.ttl:
            self._args.ttl = 86400

        if self._args.ttl < 0 or self._args.ttl > 2147483647:
            print 'Invalid ttl value ' , self._args.ttl 
            return

        if not DnsProvisioner.is_valid_ipv4_address(self._args.api_server_ip):
            print 'Invalid IPv4 address ', self._args.api_server_ip
            return

        if not DnsProvisioner.is_valid_dns_name(self._args.dns_domain):
            print 'Domain name does not satisfy DNS name requirements: ', self._args.dns_domain
            return

        try:
            dp_obj = DnsProvisioner(self._args.admin_user, self._args.admin_password, 
                                    self._args.admin_tenant_name, 
                                    self._args.api_server_ip, self._args.api_server_port)
        except ConnectionError:
             print 'Connection to API server failed '
             return
  
        if self._args.dyn_updates:
            dyn_updates = 'true'
        else:
            dyn_updates = 'false'
        
        dp_obj.add_virtual_dns(self._args.name, self._args.domain_name, 
                               self._args.dns_domain, dyn_updates, 
                               self._args.record_order, self._args.ttl, 
                               self._args.next_vdns,
                               self._args.floating_ip_record)
    #end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python add_virtual_dns.py --name vdns1 --domain_name default-domain
                                      --dns_domain example.com --dyn-updates
                                      --record_order fixed --ttl 20000 
                                      --next_vdns default-domain:vdns2
                                      --floating_ip_record vm_name
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

        parser.add_argument("--name", help = "Virtual DNS Name")
        parser.add_argument("--domain_name", help = "Domain Name")
        parser.add_argument("--dns_domain", help = "DNS Domain name")
        parser.add_argument("--dyn_updates", help = "Enable Dynamic DNS updates", action="store_true")
        parser.add_argument("--record_order", choices=['fixed', 'random', 'round-robin'], help = "Order used for DNS resolution")
        parser.add_argument("--ttl", type = int, help = "Time to Live for DNS records")
        parser.add_argument("--next_vdns", help = "Next Virtual DNS Server")
        parser.add_argument("--floating_ip_record",
                            choices=['dashed-ip', 'dashed-ip-tenant-name', 'vm-name', 'vm-name-tenant-name'],
                            help = "Name format for floating IP record")
        parser.add_argument("--api_server_ip", help = "IP address of api server")
        parser.add_argument("--api_server_port", type = int, help = "Port of api server")
        parser.add_argument("--admin_user", help = "Name of keystone admin user")
        parser.add_argument("--admin_password", help = "Password of keystone admin user")
        parser.add_argument("--admin_tenant_name", help = "Tenamt name for keystone admin user")
    
        self._args = parser.parse_args(remaining_argv)
        
    #end _parse_args

# end class AddVirtualDns

def main(args_str = None):
    AddVirtualDns(args_str)
#end main

if __name__ == "__main__":
    main()
