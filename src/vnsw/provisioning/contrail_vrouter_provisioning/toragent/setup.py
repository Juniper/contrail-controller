#!/usr/bin/python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import os
import sys
import argparse
import platform
import socket

from contrail_vrouter_provisioning import local
from contrail_vrouter_provisioning.base import ContrailSetup
from contrail_vrouter_provisioning.toragent.templates import tor_agent_conf
from contrail_vrouter_provisioning.toragent.templates import tor_agent_ini
from contrail_vrouter_provisioning.toragent.templates import tor_agent_service
from distutils.version import LooseVersion

(PLATFORM, VERSION, EXTRA) = platform.linux_distribution()

class TorAgentBaseSetup(ContrailSetup):
    def __init__(self, tor_agent_args, args_str=None):
        super(TorAgentBaseSetup, self).__init__()
        self._args = tor_agent_args
        # if the non_mgmt_ip is set use this as control ip
        if self._args.non_mgmt_ip:
            self._args.self_ip = self._args.non_mgmt_ip
        if self._args.tor_agent_name is None:
            self._args.tor_agent_name = socket.getfqdn()+ '-' + str(self._args.tor_id)
    def fixup_tor_agent(self):
        #if self._args.tor_ovs_protocol.lower() == 'pssl':
        self.ssl_cacert = '/etc/contrail/ssl/certs/tor-ca-cert.pem'
        self.ssl_cert = '/etc/contrail/ssl/certs/tor.' + self._args.tor_id + '.cert.pem'
        self.ssl_privkey = '/etc/contrail/ssl/private/tor.' + self._args.tor_id + '.private.pem'
        control_servers = ' '.join('%s:%s' % (server, '5269')
                                   for server in self._args.control_nodes)
        collector_servers = ' '.join('%s:%s' % (server, '8086')
                                     for server in self._args.collectors)
        dns_servers = ' '.join('%s:%s' % (server, '53')
                                for server in self._args.control_nodes)
        template_vals = {
                '__contrail_control_ip__': self._args.self_ip,
                '__contrail_tor_name__': self._args.tor_name,
                '__contrail_http_server_port__': self._args.http_server_port,
                '__contrail_tor_ip__': self._args.tor_ip,
                '__contrail_tor_id__': self._args.tor_id,
                '__contrail_tsn_ovs_port__': self._args.tor_ovs_port,
                '__contrail_tsn_ip__': self._args.tsn_ip,
                '__contrail_tor_ovs_protocol__': self._args.tor_ovs_protocol,
                '__contrail_tor_agent_ovs_ka__': self._args.tor_agent_ovs_ka,
                '__contrail_tor_agent_name__': self._args.tor_agent_name,
                '__contrail_tor_tunnel_ip__': self._args.tor_tunnel_ip,
                '__contrail_tor_product_name__': self._args.tor_product_name,
                '__contrail_tor_vendor_name__': self._args.tor_vendor_name,
                '__contrail_tor_ssl_cert__': self.ssl_cert,
                '__contrail_tor_ssl_privkey__': self.ssl_privkey,
                '__contrail_tor_ssl_cacert__': self.ssl_cacert,
                '__contrail_control_servers__': control_servers,
                '__contrail_collector_servers__': collector_servers,
                '__contrail_dns_servers__': dns_servers,
                '__xmpp_dns_auth_enable__': self._args.xmpp_dns_auth_enable,
                '__xmpp_auth_enable__': self._args.xmpp_auth_enable,
                }
        self._template_substitute_write(
                tor_agent_conf.template, template_vals,
                self._temp_dir_name + '/tor_agent_conf')
        self.tor_file_name = ('contrail-tor-agent-' + self._args.tor_id +
                              '.conf')
        local("sudo mv %s/tor_agent_conf /etc/contrail/%s" %
              (self._temp_dir_name, self.tor_file_name))

    def fixup_tor_ini(self):
        self.tor_process_name = 'contrail-tor-agent-' + self._args.tor_id
        self.tor_log_file_name = self.tor_process_name + '-stdout.log'

        template_vals = {
                '__contrail_tor_agent__': self.tor_process_name,
                '__contrail_tor_agent_conf_file__': self.tor_file_name,
                '__contrail_tor_agent_log_file__': self.tor_log_file_name
                        }
        self._template_substitute_write(
                tor_agent_ini.template, template_vals,
                self._temp_dir_name + '/tor_agent_ini')
        self.tor_ini_file_name = self.tor_process_name + '.ini'
        src = "%s/tor_agent_ini" % self._temp_dir_name
        dst = "/etc/contrail/supervisord_vrouter_files/%s" %\
              self.tor_ini_file_name
        local("sudo mv %s %s" % (src, dst))

    def fixup_tor_service(self):
        self.tor_process_name = 'contrail-tor-agent-' + self._args.tor_id

        template_vals = {
                '__contrail_tor_agent_conf_file__': self.tor_file_name,
                        }
        self._template_substitute_write(
                tor_agent_service.template, template_vals,
                self._temp_dir_name + '/tor_agent_service')
        self.tor_service_file_name = self.tor_process_name + '.service'
        src = "%s/tor_agent_service" % self._temp_dir_name
        dst = "/lib/systemd/system/%s" %\
              self.tor_service_file_name
        local("sudo mv %s %s" % (src, dst))

    def create_init_file(self):
        local("sudo cp /etc/init.d/contrail-vrouter-agent /etc/init.d/%s" %(self.tor_process_name))

    def add_vnc_config(self):
        cmd = "sudo python /opt/contrail/utils/provision_vrouter.py"
        cmd += " --host_name %s" % self._args.tor_agent_name
        cmd += " --host_ip %s" % self._args.self_ip
        cmd += " --api_server_ip %s" % self._args.cfgm_ip
        cmd += " --admin_user %s" % self._args.admin_user
        cmd += " --admin_password %s"  % self._args.admin_password
        cmd += " --admin_tenant_name %s" % self._args.admin_tenant_name
        cmd += " --openstack_ip %s" % self._args.authserver_ip
        cmd += " --router_type tor-agent"
        if self._args.auth_protocol == 'https':
            cmd += " --api_server_use_ssl True"
        cmd += " --oper add "
        local(cmd)

    def add_physical_device(self):
        cmd = "sudo python /opt/contrail/utils/provision_physical_device.py"
        cmd += " --device_name %s" % self._args.tor_name
        cmd += " --vendor_name %s" % self._args.tor_vendor_name
        cmd += " --device_mgmt_ip %s" % self._args.tor_ip
        cmd += " --device_tunnel_ip %s" % self._args.tor_tunnel_ip
        cmd += " --device_tor_agent %s" % self._args.tor_agent_name
        cmd += " --device_tsn %s" % self._args.tor_tsn_name
        cmd += " --api_server_ip %s" % self._args.cfgm_ip
        cmd += " --admin_user %s"  % self._args.admin_user
        cmd += " --admin_password %s" % self._args.admin_password
        cmd += " --admin_tenant_name %s" % self._args.admin_tenant_name
        cmd += " --openstack_ip %s" % self._args.authserver_ip
        if self._args.auth_protocol == 'https':
            cmd += " --api_server_use_ssl True"
        if self._args.tor_product_name:
            cmd += " --product_name %s" %(self._args.tor_product_name)
        cmd += " --oper add "
        local(cmd)
    
    def update_supervisor(self):
        if self._args.restart:
            local("sudo supervisorctl -c /etc/contrail/supervisord_vrouter.conf update", warn_only=True)

    def run_services(self):
        if self._args.restart:
            cmd = "sudo service %s start" % self.tor_process_name
            local(cmd)
            cmd = "sudo systemctl enable %s" % self.tor_process_name
            local(cmd)

    def setup(self):
        self.fixup_tor_agent()
        if (('ubuntu' in PLATFORM.lower()) and
            (LooseVersion(VERSION) >= LooseVersion('16.04'))):
            self.fixup_tor_service()
            self.add_vnc_config()
            self.add_physical_device()
            self.run_services()
        else:
            self.fixup_tor_ini()
            self.create_init_file()
            self.add_vnc_config()
            self.add_physical_device()
            self.update_supervisor()

class TorAgentSetup(ContrailSetup):
    def __init__(self, args_str=None):
        super(TorAgentSetup, self).__init__()
        self._args = None
        self.global_defaults = {
            'cfgm_ip': '127.0.0.1',
            'authserver_ip': '127.0.0.1',
            'admin_user':None,
            'admin_passwd':None,
            'admin_tenant_name':'admin',
            'auth_protocol':None,
            'tor_agent_name':None,
            'tor_vendor_name':'',
            'tor_product_name':'',
            'http_server_port':9090,
            'tor_agent_ovs_ka':10000,
            'restart':True,
            'xmpp_auth_enable': False,
            'xmpp_dns_auth_enable': False,
        }

        self.parse_args(args_str)

    def parse_args(self, args_str):
        '''
        Eg. setup-vnc-tor-agent --agent_name contrail-tor-1
            --http_server_port 9090 --discovery_server_ip 10.204.217.39
            --tor_id 1 --tor_ip 10.204.221.35 --tor_ovs_port 9999
            --tsn_ip 10.204.221.33 --tor_ovs_protocol tcp
            --tor_agent_ovs_ka 10000
        '''
        parser = argparse.ArgumentParser() 
        parser.add_argument("--cfgm_ip", help = "IP Address of the config node")
        parser.add_argument("--self_ip", help="IP Address of this(compute) node")
        parser.add_argument(
                "--non_mgmt_ip",
                help="IP Address of non-management interface(fabric network)"
                     "on the compute  node")
        parser.add_argument("--authserver_ip", help = "IP Address of the authserver(keystone) node")
        parser.add_argument("--admin_user", help = "Authserver admin tenants user name")
        parser.add_argument("--admin_password", help = "AuthServer admin user's password")
        parser.add_argument("--admin_tenant_name", help = "AuthServer admin tenant name")
        parser.add_argument("--auth_protocol", help = "AuthServer(keystone) running protocol")
        
        parser.add_argument("--tor_name", help="Name of the TOR agent")
        parser.add_argument("--http_server_port", help="Port number for the HTTP server.")
        parser.add_argument("--tor_ip", help="TOR Switch IP")
        parser.add_argument("--tor_id", help="Unique ID for the TOR")
        parser.add_argument("--tor_ovs_port", help="OVS Port Number")
        parser.add_argument("--tsn_ip", help="Tor tunnel  Node IP")
        parser.add_argument("--tor_ovs_protocol", help="TOR OVS Protocol. Currently Only TCP and ssl  supported")
        parser.add_argument("--tor_vendor_name", help="TOR Vendor name")
        parser.add_argument("--tor_tsn_name", help="TOR Tsn name")
        parser.add_argument("--tor_agent_ovs_ka", help="TOR Agent OVS Keepalive timer value in millisecs")
        parser.add_argument("--tor_agent_name", help="TOR Agent name")
        parser.add_argument("--tor_tunnel_ip", help="TOR tunnel ip")
        parser.add_argument("--tor_product_name", help="TOR product name ")
        parser.add_argument(
                "--control-nodes",
                help="List of IP of the VNC control-nodes",
                nargs='+', type=str)
        parser.add_argument(
                "--collectors",
                help="List of IP of the VNC collectors",
                nargs='+', type=str)
        parser.add_argument("--tor_id_list", help="tor id list", nargs='+', type=str)
        parser.add_argument(
                "--xmpp_auth_enable", help="Enable xmpp auth",
                action="store_true")
        parser.add_argument(
                "--xmpp_dns_auth_enable", help="Enable DNS xmpp auth",
                action="store_true")
        parser.set_defaults(**self.global_defaults)
        self._args = parser.parse_args(args_str)

def main(args_str=None):
    tor_agent_args = TorAgentSetup(args_str)._args
    tor_agent = TorAgentBaseSetup(tor_agent_args)
    tor_agent.setup()

if __name__ == "__main__":
    main()
