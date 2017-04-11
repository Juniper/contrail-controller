#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import argparse
import ConfigParser

from cfgm_common.exceptions import RefsExistError
from vnc_api.vnc_api import *
from vnc_admin_api import VncApiAdmin

class MetadataProvisioner(object):

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
        
        linklocal_obj=LinklocalServiceEntryType(
                 linklocal_service_name=self._args.linklocal_service_name,
                 linklocal_service_ip=self._args.linklocal_service_ip,
                 linklocal_service_port=self._args.linklocal_service_port,
                 ip_fabric_DNS_service_name=self._args.ipfabric_dns_service_name,
                 ip_fabric_service_port=self._args.ipfabric_service_port)
        if self._args.ipfabric_service_ip:
            linklocal_obj.ip_fabric_service_ip=[self._args.ipfabric_service_ip]

        try:
            current_config=self._vnc_lib.global_vrouter_config_read(
                                fq_name=['default-global-system-config',
                                         'default-global-vrouter-config'])
        except Exception as e:
            try:
                if self._args.oper == "add":
                    linklocal_services_obj=LinklocalServicesTypes([linklocal_obj])
                    conf_obj=GlobalVrouterConfig(linklocal_services=linklocal_services_obj)
                    result=self._vnc_lib.global_vrouter_config_create(conf_obj)
                    print 'Created.UUID is %s'%(result)
                return
            except RefsExistError:
                print "Already created!"

        current_linklocal=current_config.get_linklocal_services()
        if current_linklocal is None:
            obj = {'linklocal_service_entry': []}
        else:
            obj = current_linklocal.__dict__
        new_linklocal=[]
        for key, value in obj.iteritems():
            found=False
            for vl in value:
                entry = vl.__dict__
                if ('linklocal_service_name' in entry and
                    entry['linklocal_service_name'] == self._args.linklocal_service_name):
                    if self._args.oper == "add":
                        new_linklocal.append(linklocal_obj)
                    found=True
                else:
                    new_linklocal.append(vl)
            if not found and self._args.oper == "add":
                new_linklocal.append(linklocal_obj)
            obj[key] = new_linklocal
        
        conf_obj=GlobalVrouterConfig(linklocal_services=obj)
        result=self._vnc_lib.global_vrouter_config_update(conf_obj)
        print 'Updated.%s'%(result)

    # end __init__
    
    def _parse_args(self, args_str):
        '''
        Eg. python provision_metadata.py 
                                        --api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --api_server_use_ssl False
                                        --linklocal_service_name name
                                        --linklocal_service_ip 1.2.3.4
                                        --linklocal_service_port 1234
                                        --ipfabric_dns_service_name fabric_server_name
                                        --ipfabric_service_ip 10.1.1.1
                                        --ipfabric_service_port 5775
                                        --oper <add | delete>
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
            'linklocal_service_name': '',
            'linklocal_service_ip': '',
            'linklocal_service_port': 0,
            'ipfabric_dns_service_name': '',
            'ipfabric_service_ip': [],
            'ipfabric_service_port': 0,
            'oper': 'add',
        }
        ksopts = {
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'admin'
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

        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
        parser.add_argument(
            "--linklocal_service_name", help="Service Name")
        parser.add_argument(
            "--linklocal_service_ip", help="Link Local Service IP")
        parser.add_argument(
            "--linklocal_service_port", type=int, help="Link Local Service Port")
        parser.add_argument(
            "--ipfabric_dns_service_name", help="IP Fabric DNS Service Name")
        parser.add_argument(
            "--ipfabric_service_ip", help="IP Fabric Service IP")
        parser.add_argument(
            "--ipfabric_service_port", type=int, help="IP Fabric Service Port")
        parser.add_argument(
            "--oper", default='add', help="Provision operation to be done(add or delete)")
        parser.add_argument(
            "--admin_tenant_name", help="Tenant to create the Link Local Service")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        group = parser.add_mutually_exclusive_group()
        group.add_argument(
            "--api_server_ip", help="IP address of api server")
        group.add_argument("--use_admin_api",
                            default=False,
                            help = "Connect to local api-server on admin port",
                            action="store_true")

        self._args = parser.parse_args(remaining_argv)
        if not self._args.linklocal_service_name:
            parser.error('linklocal_service_name is required')

    # end _parse_args

# end class MetadataProvisioner


def main(args_str=None):
    MetadataProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
