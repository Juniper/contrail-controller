#!/usr/bin/env python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import logging
import argparse


log = logging.getLogger('contrail_vrouter_provisioning.cmdparser')


class ComputeArgsParser(object):
    def __init__(self, args_str=None):
        self._args = None

        self.global_defaults = {
            'cfgm_ip': '127.0.0.1',
            'cfgm_user': 'root',
            'cfgm_passwd': 'c0ntrail123',
            'keystone_ip': '127.0.0.1',
            'self_ip': '127.0.0.1',
            'non_mgmt_ip': None,
            'non_mgmt_gw': None,
            'public_subnet': None,
            'public_vn_name': None,
            'vgw_intf': None,
            'gateway_routes': None,
            'haproxy': False,
            'keystone_admin_user': None,
            'keystone_admin_passwd': None,
            'neutron_password': None,
            'mode': None,
            'cpu_mode': None,
            'cpu_model': None,
            'dpdk': False,
            'vrouter_module_params': None,
            'vmware': False,
            'esxi_vm': False,
            'vmware': None,
            'vmware_username': 'root',
            'vmware_passwd': 'c0ntrail123',
            'vmware_vmpg_vswitch': 'c0ntrail123',
            'vmware_vmpg_vswitch_mtu': None,
            'vmware_datanic_mtu': None,
            'mode': None,
            'vcenter_server': None,
            'vcenter_username': None,
            'vcenter_password': None,
            'vcenter_cluster': None,
            'vcenter_dvswitch': None,
            'hypervisor': 'libvirt',
            'gateway_server_list': '',
            'default_hw_queue_qos': False,
            'qos_logical_queue': None,
            'qos_queue_id': None,
            'priority_id': None,
            'priority_scheduling': None,
            'priority_bandwidth': None,
            'collectors': ['127.0.0.1'],
            'control_nodes': ['127.0.0.1'],
            'xmpp_auth_enable': False,
        }

        self.parse_args(args_str)

    def parse_args(self, args_str):
        '''
        Eg. setup-vnc-vrouter --cfgm_ip 10.1.5.11 --keystone_ip 10.1.5.12
                   --self_ip 10.1.5.12 --service_token 'c0ntrail123'
                   --haproxy --internal_vip 10.1.5.200
                   --collectors 10.1.5.11 10.1.5.12
                   --control-nodes 10.1.5.11 10.1.5.12
        '''
        parser = argparse.ArgumentParser()

        parser.add_argument("--cfgm_ip", help="IP Address of the config node")
        parser.add_argument("--cfgm_user", help="Sudo User in the config node")
        parser.add_argument(
                "--cfgm_passwd",
                help="Password of the Sudo user in the config node")
        parser.add_argument(
                "--keystone_ip", help="IP Address of the keystone node")
        parser.add_argument(
                "--self_ip", help="IP Address of this(compute) node")
        parser.add_argument(
                "--hypervisor",
                help="Hypervisor to be provisioned in this(compute) node")
        parser.add_argument(
                "--mgmt_self_ip", help="Management IP Address of this system")
        parser.add_argument(
                "--non_mgmt_ip",
                help="IP Address of non-management interface(fabric network)"
                     "on the compute  node")
        parser.add_argument(
                "--non_mgmt_gw",
                help="Gateway Address of the non-management interface"
                     "(fabric network) on the compute node")
        parser.add_argument(
                "--public_subnet",
                help="Subnet of the virtual network used for public access")
        parser.add_argument(
                "--physical_interface",
                help="Name of the physical interface to use")
        parser.add_argument(
                "--vgw_intf", help="Virtual gateway intreface name")
        parser.add_argument(
                "--vgw_public_subnet",
                help="Subnet of the virtual network used for public access")
        parser.add_argument(
                "--vgw_public_vn_name",
                help="Fully-qualified domain name (FQDN) of the"
                     "routing-instance that needs public access")
        parser.add_argument(
                "--vgw_intf_list", help="List of virtual getway intreface")
        parser.add_argument(
                "--vgw_gateway_routes",
                help="Static route to be configured in agent configuration"
                     "for VGW")
        parser.add_argument(
                "--public_vn_name",
                help="Fully-qualified domain name (FQDN) of the"
                     "routing-instance that needs public access")
        parser.add_argument(
                "--gateway_routes",
                help="List of route need to be added in agent"
                     "configuration for virtual gateway")
        parser.add_argument(
                "--haproxy", help="Enable haproxy", action="store_true")
        parser.add_argument(
                "--keystone_auth_protocol",
                help="Auth protocol used to talk to keystone")
        parser.add_argument(
                "--keystone_auth_port", help="Port of Keystone to talk to")
        parser.add_argument(
                "--keystone_admin_user",
                help="Keystone admin tenants user name")
        parser.add_argument(
                "--keystone_admin_password",
                help="Keystone admin user's password")
        parser.add_argument(
                "--keystone_admin_tenant_name",
                help="Keystone admin tenant name")
        parser.add_argument(
                "--neutron_password", help="Password of Neutron user")
        parser.add_argument(
                "--internal_vip",
                help="Internal VIP Address of openstack nodes")
        parser.add_argument(
                "--external_vip",
                help="External VIP Address of openstack nodes")
        parser.add_argument(
                "--contrail_internal_vip", help="VIP Address of config  nodes")
        parser.add_argument(
                "--cpu_mode",
                help="VM cpu_mode, can be none|host-model|host-passthrough|"
                     "custom'")
        parser.add_argument(
                "--cpu_model",
                help="VM cpu_model, required if cpu_mode is 'custom'."
                     "eg. 'Nehalem'")
        parser.add_argument(
                "--vmware", help="The Vmware ESXI IP")
        parser.add_argument(
                "--vmware_username", help="The Vmware ESXI username")
        parser.add_argument(
                "--vmware_passwd", help="The Vmware ESXI password")
        parser.add_argument(
                "--vmware_vmpg_vswitch", help="The Vmware VMPG vswitch name")
        parser.add_argument(
                "--vmware_vmpg_vswitch_mtu",
                help="The Vmware VMPG vswitch MTU")
        parser.add_argument(
                "--vmware_datanic_mtu", help="The Vmware data interface MTU")
        parser.add_argument(
                "--mode", help="mode - openstack or vcenter")
        parser.add_argument(
                "--vcenter_server", help="The vcenter server IP")
        parser.add_argument(
                "--vcenter_username", help="The vcenter server username")
        parser.add_argument(
                "--vcenter_password", help="The vcenter server password")
        parser.add_argument(
                "--vcenter_cluster", help="The cluster on vcenter")
        parser.add_argument(
                "--vcenter_dvswitch", help="The dvswitch on vcenter")
        parser.add_argument(
                "--dpdk", help="vRouter/DPDK mode.")
        parser.add_argument(
                "--vrouter_module_params", help="vRouter module parameters.")
        parser.add_argument("--sriov", help="sriov configuration")
        parser.add_argument(
                "--gateway_server_list", help="Compute's acting as gateway",
                nargs='+', type=str)
        parser.add_argument(
                "--qos_logical_queue", help="Logical queue for qos",
                nargs='+', type=str)
        parser.add_argument(
                "--qos_queue_id", help="Hardware queue id",
                nargs='+', type=str)
        parser.add_argument(
                "--default_hw_queue_qos",
                help="Default hardware queue is defined for qos",
                action="store_true")
        parser.add_argument("--priority_id", help="Priority group id",
                            nargs='+', type=str)
        parser.add_argument(
                "--priority_scheduling",
                help="Scheduling algorithm for priority group",
                nargs='+', type=str)
        parser.add_argument(
                "--priority_bandwidth",
                help="Maximum bandwidth for priority group",
                nargs='+', type=str)
        parser.add_argument(
                "--collectors",
                help="List of IP:port of the VNC collectors",
                nargs='+', type=str)
        parser.add_argument(
                "--control-nodes",
                help="List of IP:port of the VNC control-nodes",
                nargs='+', type=str)
        parser.add_argument(
                "--metadata_secret",
                help="Metadata Proxy secret from openstack node")
        parser.add_argument(
                "--xmpp_auth_enable", help="Enable xmpp auth",
                action="store_true")

        parser.set_defaults(**self.global_defaults)
        self._args = parser.parse_args(args_str)
