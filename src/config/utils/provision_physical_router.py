#This is a python based script for configuring required MX router resources in the contrail controller. It uses the VNC Rest API provided by contrail controller.
#Usage : # python provision_physical_router.py --api_server_ip <127.0.0.1> --api_server_port <8082> --admin_user <user1> --admin_password <password1> --admin_tenant_name default-domain --op {add_basic|remove_basic}  {--public_vrf_test [True|False]} {--vxlan <vxlan-identifier>}
#Note: make sure, api server authentication is disabled in contrail api server to run this script.
#      To disable: Please set "multi_tenancy=False" in /etc/contrail/contrail-api.conf  and restart API server
#      Please update right MX ip address in the script. 
#File name:   provision_physical_router.py
#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import argparse
import ConfigParser

import json
import copy
from netaddr import IPNetwork

from vnc_api.vnc_api import *


def get_ip(ip_w_pfx):
    return str(IPNetwork(ip_w_pfx).ip)
# end get_ip


class VncProvisioner(object):

    def __init__(self, args_str=None):
        self._args = None
        if not args_str:
            args_str = ' '.join(sys.argv[1:])
        self._parse_args(args_str)

        self._vnc_lib = VncApi(self._args.admin_user,
                               self._args.admin_password,
                               self._args.admin_tenant_name,
                               self._args.api_server_ip,
                               self._args.api_server_port, '/')
        vnc_lib = self._vnc_lib

        if self._args.op is None or self._args.op == 'add_basic':
            router_external = False
            if self._args.public_vrf_test is not None and self._args.public_vrf_test == 'True':
                router_external = True
            vxlan = None 
            if self._args.vxlan is not None: 
                vxlan = self._args.vxlan 
            self.add_physical_router_config(router_external, vxlan)
        elif self._args.op == 'delete_basic':
            print 'calling delete_physical_router_config\n'
            self.delete_physical_router_config()
    # end __init__

    def _get_ip_fabric_ri_obj(self):
        # TODO pick fqname hardcode from common
        rt_inst_obj = self._vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])

        return rt_inst_obj
    # end _get_ip_fabric_ri_obj

    def create_router(self, name, mgmt_ip):
        bgp_router = BgpRouter(name, parent_obj=self._get_ip_fabric_ri_obj())
        params = BgpRouterParams()
        params.address = mgmt_ip
        params.address_families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn',
                                             'inet6-vpn'])
        params.autonomous_system = 64512
        params.vendor = 'mx'
        params.identifier = mgmt_ip
        bgp_router.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_create(bgp_router)

        pr = PhysicalRouter(name)
        pr.physical_router_management_ip = mgmt_ip
        pr.physical_router_vendor_name = 'juniper'
        pr.physical_router_product_name = 'mx'
        pr.physical_router_vnc_managed = True 
        uc = UserCredentials('root', 'Embe1mpls')
        pr.set_physical_router_user_credentials(uc)
        pr.set_bgp_router(bgp_router)
        pr_id = self._vnc_lib.physical_router_create(pr)
        return bgp_router, pr

    def add_physical_router_config(self, router_external = False, vxlan = None):
        import pdb; pdb.set_trace()

        ipam_obj = None
        try:
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=[u'default-domain', u'default-project', u'ipam1'])
        except NoIdError:
            pass
        if ipam_obj is None:
            ipam_obj = NetworkIpam('ipam1')
            self._vnc_lib.network_ipam_create(ipam_obj)

        vn1_obj = None
        try:
            vn1_obj = self._vnc_lib.virtual_network_read(fq_name=[u'default-domain', u'default-project', u'vn1'])
        except NoIdError:
            pass
 
        if vn1_obj is None: 
            vn1_obj = VirtualNetwork('vn1')
            if router_external == True:
                vn1_obj.set_router_external(True)

            if vxlan is not None:
                vn1_obj_properties = VirtualNetworkType()
                vn1_obj_properties.set_vxlan_network_identifier(int(vxlan))
                vn1_obj.set_virtual_network_properties(vn1_obj_properties)

            vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType([IpamSubnetType(SubnetType("10.0.0.0", 24))]))
            vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)

        pr = None
        try:
            pr = self._vnc_lib.physical_router_read(fq_name=[u'default-global-system-config', u'a7-mx-80'])
        except NoIdError:
            pass
 
        if pr is None:
            bgp_router, pr = self.create_router('a7-mx-80', '10.84.63.133')
            pr.set_virtual_network(vn1_obj)
            self._vnc_lib.physical_router_update(pr)

        pi = None
        try:
            pi = self._vnc_lib.physical_interface_read(fq_name=[u'default-global-system-config', u'a7-mx-80', u'ge-1/0/5'])
        except NoIdError:
            pass
        if pi is None:
            pi = PhysicalInterface('ge-1/0/5', parent_obj = pr)
            pi_id = self._vnc_lib.physical_interface_create(pi)

        fq_name = ['default-domain', 'default-project', 'vmi1']
        default_project = self._vnc_lib.project_read(fq_name=[u'default-domain', u'default-project'])
        vmi = None
        try:
            vmi = self._vnc_lib.virtual_machine_interface_read(fq_name=[u'default-domain', u'default-project', u'vmi1'])
        except NoIdError:
            pass
        if vmi is None:
            vmi = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
            vmi.set_virtual_network(vn1_obj)
            self._vnc_lib.virtual_machine_interface_create(vmi)

        li = None
        try:
            li = self._vnc_lib.logical_interface_read(fq_name=[u'default-global-system-config', u'a7-mx-80', u'ge-1/0/5', u'ge-1/0/5.0'])
        except NoIdError:
            pass
       
        if li is None:
            li = LogicalInterface('ge-1/0/5.0', parent_obj = pi)
            li.vlan_tag = 100
            li.set_virtual_machine_interface(vmi)
            li_id = self._vnc_lib.logical_interface_create(li)

    # end 

    def delete_physical_router_config(self):

        print 'delete_physical_router_config\n'
        li = None
        try:
            li = self._vnc_lib.logical_interface_read(fq_name=[u'default-global-system-config', u'a7-mx-80', u'ge-1/0/5', u'ge-1/0/5.0'])
        except NoIdError:
            pass
       
        if li is not None:
            self._vnc_lib.logical_interface_delete(li.get_fq_name())

        vmi = None
        try:
            vmi = self._vnc_lib.virtual_machine_interface_read(fq_name=[u'default-domain', u'default-project', u'vmi1'])
        except NoIdError:
            pass
        if vmi is not None:
            self._vnc_lib.virtual_machine_interface_delete(vmi.get_fq_name())

        pi = None
        try:
            pi = self._vnc_lib.physical_interface_read(fq_name=[u'default-global-system-config', u'a7-mx-80', u'ge-1/0/5'])
        except NoIdError:
            pass
        if pi is not None:
            pi_id = self._vnc_lib.physical_interface_delete(pi.get_fq_name())

        pr = None
        try:
            pr = self._vnc_lib.physical_router_read(fq_name=[u'default-global-system-config', u'a7-mx-80'])
        except NoIdError:
            pass
 
        if pr is not None:
            self._vnc_lib.physical_router_delete(pr.get_fq_name())

        br = None
        try:
            br = self._vnc_lib.bgp_router_read(fq_name=[u'default-domain', u'default-project', u'ip-fabric', u'__default__', u'a7-mx-80'])
        except NoIdError:
            pass
 
        if br is not None:
            self._vnc_lib.bgp_router_delete(br.get_fq_name())

        vn1_obj = None
        try:
            vn1_obj = self._vnc_lib.virtual_network_read(fq_name=[u'default-domain', u'default-project', u'vn1'])
        except NoIdError:
            pass
 
        if vn1_obj is not None: 
            vn1_uuid = self._vnc_lib.virtual_network_delete(vn1_obj.get_fq_name())

        ipam_obj = None
        try:
            ipam_obj = self._vnc_lib.network_ipam_read(fq_name=[u'default-domain', u'default-project', u'ipam1'])
        except NoIdError:
            pass
        if ipam_obj is not None:
            self._vnc_lib.network_ipam_delete(ipam_obj.get_fq_name())

    # end


    def _parse_args(self, args_str):
        '''
        Eg. python provision_physical_router.py 
                                   --api_server_ip 127.0.0.1
                                   --api_server_port 8082
        '''

        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        args, remaining_argv = conf_parser.parse_known_args(args_str.split())

        defaults = {
            #'public_vn_name': 'default-domain:'
            #'default-project:default-virtual-network',
            'api_server_ip': '127.0.0.1',
            'api_server_port': '8082',
        }
        ksopts = {
            'admin_user': 'admin',
            'admin_password': 'c0ntrail123',
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
            "--api_server_ip", help="IP address of api server", required=True)
        parser.add_argument("--api_server_port", help="Port of api server", required=True)
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user", required=True)
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user", required=True)
        parser.add_argument(
            "--admin_tenant_name", help="Tenamt name for keystone admin user", required=True)

        parser.add_argument(
            "--op", help="operation (add_basic, delete_basic)", required=True)

        parser.add_argument(
            "--public_vrf_test", help="operation (False, True)", required=False)
        parser.add_argument(
            "--vxlan", help="vxlan identifier", required=False)

        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

# end class VncProvisioner


def main(args_str=None):
    VncProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
