#!/usr/bin/env python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from future import standard_library
standard_library.install_aliases()
from builtins import object
import os
import re
import ast
import logging
import argparse
from six.moves import configparser

log = logging.getLogger('contrail_vrouter_provisioning.cmdparser')


class ComputeArgsParser(object):
    def __init__(self, args_str=None):
        self._args = None

        conf_file = '/etc/contrailctl/agent.conf'
        if '--conf_file' in args_str:
            conf_file = args_str[args_str.index('--conf_file') + 1]
        self.parse_config_file(conf_file)

        self.global_defaults = {
            'conf_file': '/etc/contrailctl/agent.conf',
            'cfgm_ip': self.get_config('GLOBAL', 'cfgm_ip', '127.0.0.1'),
            'keystone_ip': self.get_config('KEYSTONE', 'ip', '127.0.0.1'),
            'keystone_admin_user': self.get_config(
                'KEYSTONE', 'admin_user', 'admin'),
            'keystone_admin_tenant_name': self.get_config(
                'KEYSTONE', 'admin_tenant_name', 'admin'),
            'keystone_admin_password': self.get_config(
                'KEYSTONE', 'admin_password', 'contrail123'),
            'keystone_auth_protocol': self.get_config(
                'KEYSTONE', 'auth_protocol', 'http'),
            'keystone_auth_port': self.get_config(
                'KEYSTONE', 'admin_port', '5000'),
            'neutron_password': self.get_config('NEUTRON', 'password', None),
            'apiserver_auth_protocol': self.get_config(
                'APISERVER', 'auth_protocol', 'http'),
            'self_ip': self.get_config('AGENT', 'ip', '127.0.0.1'),
            'non_mgmt_ip': self.get_config('AGENT', 'ctrl_ip', None),
            'non_mgmt_gw': self.get_config('AGENT', 'ctrl_gateway', None),
            'vgw_intf': self.get_config('VGW', 'interface', None),
            'mode': self.get_config('HYPERVISOR', 'mode', None),
            'cpu_mode': None,
            'cpu_model': None,
            'dpdk': self.get_dpdk_config(False),
            'vrouter_module_params': self.get_vrouter_module_config(None),
            'esxi_vm': False,
            'vmware': None,
            'vmware_username': 'root',
            'vmware_passwd': 'c0ntrail123',
            'vmware_vmpg_vswitch': 'c0ntrail123',
            'vmware_vmpg_vswitch_mtu': None,
            'vmware_datanic_mtu': None,
            'vcenter_server': None,
            'vcenter_username': None,
            'vcenter_password': None,
            'vcenter_cluster': None,
            'vcenter_dvswitch': None,
            'hypervisor': self.get_config('HYPERVISOR', 'type', 'kvm'),
            'compute_as_gateway': '',
            'default_hw_queue_qos': False,
            'qos_priority_tagging': '',
            'qos_logical_queue': None,
            'qos_queue_id': None,
            'priority_id': None,
            'priority_scheduling': None,
            'priority_bandwidth': None,
            'tsn_mode': None,
            'collectors': self.get_config_list(
                'GLOBAL', 'analytics_nodes', ['127.0.0.1']),
            'control_nodes': self.get_config_list(
                'GLOBAL', 'controller_nodes', ['127.0.0.1']),
            'xmpp_auth_enable': self.get_config(
                'GLOBAL', 'xmpp_auth_enable', False),
            'xmpp_dns_auth_enable': self.get_config(
                'GLOBAL', 'xmpp_dns_auth_enable', False),
            'sandesh_ssl_enable': self.get_config(
                'GLOBAL', 'sandesh_ssl_enable', False),
            'introspect_ssl_enable': self.get_config(
                'GLOBAL', 'introspect_ssl_enable', False),
            'register': False,
            'flow_thread_count': '2',
            'metadata_secret': '',
            'tsn_evpn_mode': False,
            'tsn_servers': None,
            'vrouter_1G_hugepages': '0',
            'vrouter_2M_hugepages': '0',
            'backup_file_count': '3',
            'backup_idle_timeout': '10000',
            'restore_audit_timeout': '15000'
        }

        self.parse_args(args_str)

    def parse_config_file(self, conf_file):
        if not os.path.exists(conf_file):
            conf_dir = os.path.dirname(conf_file)
            if not os.path.exists(conf_dir):
                os.makedirs(conf_dir)
            with open(conf_file, 'w') as fd:
                fd.write('')
        self.config = configparser.SafeConfigParser()
        self.config.read([conf_file])

    def evaluate(self, data):
        if isinstance(data, str):
            if re.match(r"^\[.*\]$", data) or re.match(r"^\{.*\}$", data):
                return ast.literal_eval(data)
            elif data.lower() in ("yes", "true", "no", "false"):
                return data.lower() in ("yes", "true")
            else:
                return data
        else:
            return data

    def get_config(self, section, option, default):
        if self.config.has_option(section, option):
            return self.evaluate(self.config.get(section, option))
        else:
            return default

    def get_config_list(self, section, option, default):
        if self.config.has_option(section, option):
            return self.evaluate(self.config.get(section, option)).split(',')
        else:
            return default

    def get_dpdk_config(self, default):
        if self.config.has_section('dpdk'):
            if self.config.has_option('dpdk', 'hugepages'):
                dpdk_params = 'hugepages=%s' % self.config.get('hugepages')
            else:
                log.error("Hugepages not provided for dpdk")
                raise RuntimeError("Hugepages not provided for dpdk")
            if self.config.has_option('dpdk', 'coremask'):
                dpdk_params += ',coremask=%s' % self.config.get('coremask')
            else:
                log.error("coremask not provided for dpdk")
                raise RuntimeError("coremask not provided for dpdk")
            if self.config.has_option('dpdk', 'uio_driver'):
                dpdk_params += ',uio_dri_er=%s' % self.config.get('uio_driver')
            return dpdk_params
        else:
            return default

    def get_vrouter_module_config(self, default):
        # TODO by kiran
        # Implement vrouter_module_config
        return default

    def parse_args(self, args_str):
        '''
        Eg. setup-vnc-vrouter --cfgm_ip 10.1.5.11 --keystone_ip 10.1.5.12
                   --self_ip 10.1.5.12 --service_token 'c0ntrail123'
                   --internal_vip 10.1.5.200
                   --collectors 10.1.5.11 10.1.5.12
                   --control-nodes 10.1.5.11 10.1.5.12
        '''
        parser = argparse.ArgumentParser()

        parser.add_argument("--conf_file", help="Agent containers config file")
        parser.add_argument("--cfgm_ip", help="IP Address of the config node")
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
                "--keystone_auth_protocol",
                help="Auth protocol used to talk to keystone")
        parser.add_argument(
                "--apiserver_auth_protocol",
                help="Auth protocol used to talk to api-server")
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
        parser.add_argument("--tsn_mode", help="tsn mode")
        parser.add_argument(
                "--compute_as_gateway", help="Compute's acting as gateway",
                type=str)
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
        parser.add_argument(
                "--qos_priority_tagging",
                help="Knob to configure priority tagging when in DCB mode",
                type=str)
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
        parser.add_argument(
                "--xmpp_dns_auth_enable", help="Enable DNS xmpp auth",
                action="store_true")
        parser.add_argument(
                "--sandesh_ssl_enable",
                help="Enable SSL for Sandesh connection",
                action="store_true")
        parser.add_argument(
                "--introspect_ssl_enable",
                help="Enable SSL for introspect connection",
                action="store_true")
        parser.add_argument(
                "--register",
                help="create virtual-router object in api-server",
                action="store_true")
        parser.add_argument(
                "--flow_thread_count",
                help="Number of threads for flow setup")
        parser.add_argument(
                "--metadata_use_ssl",
                help="Enable (true) ssl support for metadata proxy service",
                action="store_true")
        parser.add_argument(
                "--tsn_evpn_mode",
                help="Enable (true) to enable tor-service-node HA",
                action="store_true")
        parser.add_argument(
                "--tsn_servers",
                help="List of tsn nodes working in active/backup mode \
                      when agent runs in tsn-no-forwarding mode.",
                nargs='+', type=str)
        parser.add_argument(
                "--vrouter_1G_hugepages",
                help="Number of 1G hugepages for vrouter kernel mode",
                type=str)
        parser.add_argument(
                "--vrouter_2M_hugepages",
                help="Number of 2M hugepages for vrouter kernel mode",
                type=str)
        parser.add_argument(
                "--resource_backup_restore",
                help="Enable/Disable backup of config and resource files",
                action="store_true")
        parser.add_argument(
                "--backup_idle_timeout",
                help="Agent avoids generating backup file if change is \
                     detected within this time",
                type=str)
        parser.add_argument(
                "--restore_audit_timeout",
                help="Audit time for config/resource read from file",
                type=str)
        parser.add_argument(
                "--backup_file_count",
                help="Number of backup files",
                type=str)

        parser.set_defaults(**self.global_defaults)
        self._args = parser.parse_args(args_str)
