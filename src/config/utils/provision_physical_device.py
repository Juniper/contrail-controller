#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import time
import argparse
import ConfigParser
from vnc_api.vnc_api import *
from cfgm_common.exceptions import *
from vnc_admin_api import VncApiAdmin


class VrouterProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        connected = False
        tries = 0
        while not connected:
            try:
                self._vnc_lib = VncApiAdmin(
                    self._args.use_admin_api,
                    self._args.admin_user, self._args.admin_password,
                    self._args.admin_tenant_name,
                    self._args.api_server_ip,
                    self._args.api_server_port, '/',
                    api_server_use_ssl=self._args.api_server_use_ssl,
                    auth_host=self._args.openstack_ip)
                connected = True
            except ResourceExhaustionError: # haproxy throws 503
                if tries < 10:
                    tries += 1
                    time.sleep(3)
                else:
                    raise
        #self.phy_obj = self._vnc_lib.physical_routers_list()
        if self._args.oper == 'add':
            self.add_physical_device()
        elif self._args.oper == 'del':
            self.del_physical_device()
        else:
            print "Unknown operation %s. Only 'add' and 'del' supported"\
                % (self._args.oper)

    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg: python provision_physical_device.py --device_name my_router 
                                                --vendor_name Juniper  
                                                --product_name QFX5100
                                                --device_mgmt_ip 10.204.217.39
                                                --device_tunnel_ip 34.34.34.34
                                                --device_tor_agent nodec45-1 
                                                --device_tsn nodec45 
                                                --api_server_ip 10.204.221.33 
                                                --api_server_port 8082 
                                                --api_server_use_ssl False
                                                --oper <add | del>
                                                --admin_user admin 
                                                --admin_password contrail123 
                                                --admin_tenant_name admin  
                                                --openstack_ip 10.204.221.34
                                                --snmp_monitor
                                                --local_port 161
                                                --v2_community public
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
            'oper': 'add',
        }
        ksopts = {
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'default-domain'
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
            "--device_name", help="Name of physical device", required=True)
        parser.add_argument(
            "--vendor_name", help="Vendor type of the device", required=True)
        parser.add_argument(
            "--product_name", default='', help="Product name of the device")
        parser.add_argument(
            "--device_mgmt_ip", help="Management IP of the device")
        parser.add_argument(
            "--device_tunnel_ip", help="Tunnel IP of the device")
        parser.add_argument(
            "--device_tor_agent", help="Tor Agent Name for the device")
        parser.add_argument(
            "--device_tsn", help="TSN Name for the device")
        parser.add_argument(
            "--snmp_monitor", help="monitor through snmp", action='store_true')
        parser.add_argument(
            "--local_port", help="snmp port to connect to")
        parser.add_argument(
            "--v2_community", help="community string for snmp")
        parser.add_argument(
            "--api_server_port", help="Port of api server")
        parser.add_argument("--api_server_use_ssl",
            help="Use SSL to connect with API server")
        parser.add_argument(
            "--openstack_ip", help="Openstack node ip")
        parser.add_argument(
            "--oper", default='add', help="Provision operation to be done(add or del)")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenant name for keystone admin user")
        group = parser.add_mutually_exclusive_group(required=True)
        group.add_argument(
            "--api_server_ip", help="IP address of api server")
        group.add_argument("--use_admin_api",
                            default=False,
                            help = "Connect to local api-server on admin port",
                            action="store_true")
        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

    def add_physical_device(self):
        pr = PhysicalRouter(self._args.device_name)
        pr.physical_router_dataplane_ip = self._args.device_tunnel_ip
        pr.physical_router_management_ip = self._args.device_mgmt_ip
        pr.physical_router_vendor_name = self._args.vendor_name
        pr.physical_router_product_name = self._args.product_name
        pr_check=GetDevice(self._vnc_lib, self._args.device_name)
        if pr_check.Get():
            pr_id = self._vnc_lib.physical_router_update(pr)
        else:
            pr_id = self._vnc_lib.physical_router_create(pr)
        
        # Associate TSN and Tor agent with Physical Device
        for member in [self._args.device_tsn, self._args.device_tor_agent]:
            vrouter_tmp = GetVrouter(self._vnc_lib, member)
            vrouter = vrouter_tmp.Get()
            if vrouter:
                pr.add_virtual_router(vrouter)

        if self._args.snmp_monitor:
            local_port = 161
            if self._args.local_port:
                local_port = self._args.local_port
            v2_community = 'public'
            if self._args.v2_community:
                v2_community = self._args.v2_community
            snmp_credentials = SNMPCredentials(local_port=local_port, v2_community=v2_community)
            pr.set_physical_router_snmp_credentials(snmp_credentials)

        self._vnc_lib.physical_router_update(pr)
    # end add_physical_device

    def del_physical_device(self):
        pr_check=GetDevice(self._vnc_lib, self._args.device_name)
        uuid=pr_check.Get()
        if uuid:
            self._vnc_lib.physical_router_delete(id=uuid)
        else:
            print 'No device found with Name : %s' %(self._args.device_name)
    # end del_physical_device

# end class VrouterProvisioner


class GetVrouter():
    def __init__(self, handle, name = ''):
        self.vrouter_name = name
        self.handle = handle

    def Get(self):
        vrouter = None
        vrouter_list=self.handle.virtual_routers_list()
        for i in range(len(vrouter_list['virtual-routers'])):
            if unicode(self.vrouter_name) == vrouter_list['virtual-routers'][i]['fq_name'][1]:
                self.uuid=vrouter_list['virtual-routers'][i]['uuid']
                vrouter = self.handle.virtual_router_read(id = self.uuid)
        if not vrouter:
            print 'No router found with VRouter Name : %s' %(self.vrouter_name)
        return vrouter
# end class GetVrouter

class GetDevice():
    def __init__(self, handle, name = ''):
        self.physical_device_name = name
        self.handle = handle
    def Get(self):
        uuid=''
        phy_rt_list=self.handle.physical_routers_list()
        for i in range(len(phy_rt_list['physical-routers'])):
            if unicode(self.physical_device_name) == phy_rt_list['physical-routers'][i]['fq_name'][1]: 
                uuid=phy_rt_list['physical-routers'][i]['uuid']
        return uuid
# end class GetDevice

  

def main(args_str=None):
    VrouterProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
