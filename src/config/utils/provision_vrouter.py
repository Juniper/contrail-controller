#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
from future import standard_library
standard_library.install_aliases()
from builtins import object
import sys
import time
import argparse
from six.moves import configparser

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
                    auth_host=self._args.openstack_ip,
                    api_server_use_ssl=self._args.api_server_use_ssl)
                connected = True
            except ResourceExhaustionError: # haproxy throws 503
                if tries < 10:
                    tries += 1
                    time.sleep(3)
                else:
                    raise
        gsc_obj = self._vnc_lib.global_system_config_read(
            fq_name=['default-global-system-config'])
        self._global_system_config_obj = gsc_obj

        rt_inst_obj = self._vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        self._fab_rt_inst_obj = rt_inst_obj

        if self._args.oper == 'add':
            self.add_vrouter()
            if not self._args.disable_vhost_vmi:
                self.add_vhost0_vmi()
        elif self._args.oper == 'del':
            self.del_vhost0_vmi()
            self.del_vrouter()
        else:
            print("Unknown operation %s. Only 'add' and 'del' supported"\
                % (self._args.oper))

    # end __init__

    def _parse_args(self, args_str):
        '''
        Eg. python provision_vrouter.py --host_name a3s30.contrail.juniper.net
                                        --host_ip 10.1.1.1
                                        --api_server_ip 127.0.0.1
                                        --api_server_port 8082
                                        --api_server_use_ssl False
                                        --oper <add | del>
                                        [--ip_fabric_subnet 192.168.10.0/24]
                                        [--dpdk-enabled]
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
            'control_names': [],
            'router_type': None,
            'dpdk_enabled': False,
            'disable_vhost_vmi': False,
            'enable_vhost_vmi_policy': False,
            'sub_cluster_name': None,
            'ip_fabric_subnet': None
        }
        ksopts = {
            'admin_user': 'user1',
            'admin_password': 'password1',
            'admin_tenant_name': 'default-domain'
        }

        if args.conf_file:
            config = configparser.SafeConfigParser()
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
            "--host_name", help="hostname name of compute-node", required=True)
        parser.add_argument("--host_ip", help="IP address of compute-node", required=True)
        parser.add_argument(
            "--control_names",
            help="List of control-node names compute node connects to")
        parser.add_argument("--api_server_port", help="Port of api server")
        parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
        parser.add_argument(
            "--oper", default='add',
            help="Provision operation to be done(add or del)")
        parser.add_argument(
            "--admin_user", help="Name of keystone admin user")
        parser.add_argument(
            "--admin_password", help="Password of keystone admin user")
        parser.add_argument(
            "--admin_tenant_name", help="Tenamt name for keystone admin user")
        parser.add_argument(
            "--openstack_ip", help="IP address of openstack node")
        parser.add_argument(
            "--router_type", help="Type of the virtual router (tor-service-node,embedded or none)")
        parser.add_argument(
            "--dpdk_enabled", action="store_true", help="Whether forwarding mode on vrouter is DPDK based")
        parser.add_argument(
            "--disable_vhost_vmi", action="store_true", help="Do not create vhost0 vmi if flag is set")
        parser.add_argument(
            "--enable_vhost_vmi_policy", action="store_true", help="Enable vhost0 vmi policy if flag is set")
        parser.add_argument(
            "--sub_cluster_name", help="Sub cluster this vrouter to be part of")
        parser.add_argument("--ip_fabric_subnet",
                            help = "Add the ip_fabric_subnet")
        group = parser.add_mutually_exclusive_group(required=True)
        group.add_argument(
            "--api_server_ip", help="IP address of api server",
            nargs='+', type=str)
        group.add_argument("--use_admin_api",
                            default=False,
                            help = "Connect to local api-server on admin port",
                            action="store_true")

        self._args = parser.parse_args(remaining_argv)

    # end _parse_args

    def add_vrouter(self):
        gsc_obj = self._global_system_config_obj

        if self._args.ip_fabric_subnet:
            ip_subnet, mask = self._args.ip_fabric_subnet.split('/')
            so = SubnetType(ip_subnet, int(mask))
            sl = gsc_obj.get_ip_fabric_subnets()
            if sl is None:
                gsc_obj.set_ip_fabric_subnets(SubnetListType([so]))
                self._vnc_lib.global_system_config_update(gsc_obj)
            elif so not in sl.subnet:
                sl.subnet.append(so)
                gsc_obj.set_ip_fabric_subnets(sl)
                self._vnc_lib.global_system_config_update(gsc_obj)

        vrouter_obj = VirtualRouter(
            self._args.host_name, gsc_obj,
            virtual_router_ip_address=self._args.host_ip)
        vrouter_exists = True

        try:
            vrouter_obj = self._vnc_lib.virtual_router_read(
                fq_name=vrouter_obj.get_fq_name())
        except NoIdError:
            vrouter_exists = False

        if self._args.sub_cluster_name:
            sub_cluster_obj = SubCluster(self._args.sub_cluster_name)
            try:
                sub_cluster_obj = self._vnc_lib.sub_cluster_read(
                    fq_name=sub_cluster_obj.get_fq_name())
            except NoIdError:
                raise RuntimeError("Sub cluster has to be provisioned first")
            vrouter_obj.add_sub_cluster(sub_cluster_obj)

        # Configure router type
        if self._args.router_type:
            vrouter_obj.set_virtual_router_type(self._args.router_type)
        if self._args.dpdk_enabled:
            vrouter_obj.set_virtual_router_dpdk_enabled(True)
        else:
            vrouter_obj.set_virtual_router_dpdk_enabled(False)
        if vrouter_exists:
            self.vrouter_fq_name = vrouter_obj.get_fq_name()
            self._vnc_lib.virtual_router_update(vrouter_obj)
        else:
            try:
                self.vrouter_fq_name = vrouter_obj.get_fq_name()
                self._vnc_lib.virtual_router_create(vrouter_obj)
            except RefsExistError:
                print("Already created!")

    # end add_vrouter

    def add_vhost0_vmi(self):
        vrouter_exists = True
        try:
            vrouter_obj = self._vnc_lib.virtual_router_read(
                fq_name=self.vrouter_fq_name)
        except NoIdError:
            vrouter_exists = False

        if not vrouter_exists:
            print("No vrouter object found cannot add vhost0 vmi !")
            return

        try:
            vhost0_vmi_fq_name = self.vrouter_fq_name
            vhost0_vmi_fq_name.append('vhost0')
            vhost0_vmi = self._vnc_lib.virtual_machine_interface_read(
                fq_name = vhost0_vmi_fq_name)
            vhost0_vmi_exists = True
        except NoIdError:
            vhost0_vmi_exists = False
            vhost0_vmi =  VirtualMachineInterface(name="vhost0", parent_obj = vrouter_obj)
            ip_fab_vn = self._vnc_lib.virtual_network_read(fq_name = [u'default-domain', u'default-project', u'ip-fabric'])
            vhost0_vmi.set_virtual_network(ip_fab_vn)

        # Enable/Disable policy on the vhost0 vmi
        if self._args.enable_vhost_vmi_policy:
            vhost0_vmi.set_virtual_machine_interface_disable_policy(False)
        else:
            vhost0_vmi.set_virtual_machine_interface_disable_policy(True)

        if vhost0_vmi_exists:
           self._vnc_lib.virtual_machine_interface_update(vhost0_vmi)
        else:
            try:
                self._vnc_lib.virtual_machine_interface_create(vhost0_vmi)
            except RefsExistError:
                print("vhost0 vmi already created!")

    # end add_vhost0_vmi

    def del_vrouter(self):
        gsc_obj = self._global_system_config_obj
        vrouter_obj = VirtualRouter(self._args.host_name, gsc_obj)
        vrouter_exists = True
        try:
            vrouter = self._vnc_lib.virtual_router_read(
                fq_name=vrouter_obj.get_fq_name())
        except NoIdError:
            vrouter_exists = False

        if vrouter_exists:
            self._vnc_lib.virtual_router_delete(
                fq_name=vrouter_obj.get_fq_name())
        else:
            print(" vrouter object not found ")

    # end del_vrouter

    def del_vhost0_vmi(self):
        gsc_obj = self._global_system_config_obj
        vrouter_obj = VirtualRouter(self._args.host_name, gsc_obj)
        vhost0_vmi_fq_name = vrouter_obj.get_fq_name()
        vhost0_vmi_fq_name.append('vhost0')
        vhost0_vmi_exists = True
        try:
            vhost0_vmi = self._vnc_lib.virtual_machine_interface_read(
                fq_name = vhost0_vmi_fq_name)
        except NoIdError:
            vhost0_vmi_exists = False

        if vhost0_vmi_exists:
            self._vnc_lib.virtual_machine_interface_delete(fq_name=vhost0_vmi_fq_name)
            print(" Deleted vhost0 vmi %s " % vhost0_vmi_fq_name)
        else:
            print(" No vhost0 vmi found ")

    # end del_vhost0_vmi

# end class VrouterProvisioner


def main(args_str=None):
    VrouterProvisioner(args_str)
# end main

if __name__ == "__main__":
    main()
