#!/usr/bin/env python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import os
import socket
import netaddr
import logging
import netifaces

from contrail_vrouter_provisioning import local

from contrail_vrouter_provisioning.base import ContrailSetup
from contrail_vrouter_provisioning.network import ComputeNetworkSetup


log = logging.getLogger('contrail_vrouter_provisioning.common')


class CommonComputeSetup(ContrailSetup, ComputeNetworkSetup):
    def __init__(self, args):
        super(CommonComputeSetup, self).__init__()
        self._args = args

        # Using keystone admin password for nova/neutron if not supplied
        if not self._args.neutron_password:
            self._args.neutron_password = self._args.keystone_admin_password

        self.multi_net = False
        if self._args.non_mgmt_ip:
            self.multi_net = True
            self.vhost_ip = self._args.non_mgmt_ip
        else:
            self.vhost_ip = self._args.self_ip

        self.dev = None
        if self._args.physical_interface:
            if self._args.physical_interface in netifaces.interfaces():
                self.dev = self._args.physical_interface
            else:
                raise KeyError('Interface %s in present' %
                               self._args.physical_interface)
        else:
            # Deduce the phy interface from ip, if configured
            self.dev = self.get_device_by_ip(self.vhost_ip)

    def enable_kernel_core(self):
        self.enable_kernel_core()
        for svc in ['abrt-vmcore', 'abrtd', 'kdump']:
            local('sudo chkconfig %s on' % svc)

    def fixup_config_files(self):
        self.add_dev_tun_in_cgroup_device_acl()
        self.fixup_contrail_vrouter_agent()
        self.fixup_contrail_vrouter_nodemgr()
        self.fixup_contrail_lbaas()

    def setup_lbaas_prereq(self):
        if self.pdist in ['centos', 'redhat']:
            local('sudo groupadd -f nogroup')
            cmd = "sudo sed -i s/'Defaults    requiretty'/'#Defaults    "
            cmd += "requiretty'/g /etc/ers"
            local(cmd)

    def add_dev_tun_in_cgroup_device_acl(self):
        # add /dev/net/tun in cgroup_device_acl needed
        # for type=ethernet interfaces
        fl = "/etc/libvirt/qemu.conf"
        ret = local("sudo grep -q '^cgroup_device_acl' %s" % fl,
                    warn_only=True)
        if ret.failed:
            if self.pdist in ['centos', 'redhat']:
                local('sudo echo "clear_emulator_capabilities = 1" >> %s' % fl,
                      warn_only=True)
                local('sudo echo \'user = "root"\' >> %s' % fl, warn_only=True)
                local('sudo echo \'group = "root"\' >> %s' % fl,
                      warn_only=True)
            cmds = ['echo \'cgroup_device_acl = [\' >> %s' % fl,
                    'echo \'    "/dev/null", "/dev/full", "/dev/zero",\''
                    + ' >> %s' % fl,
                    'echo \'    "/dev/random", "/dev/urandom",\''
                    + ' >> %s' % fl,
                    'echo \'    "/dev/ptmx", "/dev/kvm", "/dev/kqemu",\''
                    + ' >> %s' % fl,
                    'echo \'    "/dev/rtc", "/dev/hpet", "/dev/net/tun",\''
                    + ' >> %s' % fl,
                    'echo \']\' >> %s' % fl]
            for cmd in cmds:
                local('sudo ' + cmd, warn_only=True)
            self._fixed_qemu_conf = True
        # add "alias bridge off" in /etc/modprobe.conf for Centos
        if self.pdist in ['centos', 'redhat']:
            local('sudo echo "alias bridge off" > /etc/modprobe.conf',
                  warn_only=True)

    def fixup_contrail_vrouter_nodemgr(self):
        collector_list = ' '.join('%s:%s' % (server, '8086')
                                  for server in self._args.collectors)
        self.set_config('/etc/contrail/contrail-vrouter-nodemgr.conf',
                        'COLLECTOR', 'server_list', collector_list)

    def fixup_contrail_vrouter_agent(self):
        compute_ip = self._args.self_ip
        non_mgmt_gw = self._args.non_mgmt_gw
        vgw_public_subnet = self._args.vgw_public_subnet
        vgw_public_vn_name = self._args.vgw_public_vn_name
        vgw_intf_list = self._args.vgw_intf_list
        vgw_gateway_routes = self._args.vgw_gateway_routes
        gateway_server_list = self._args.gateway_server_list
        qos_logical_queue = self._args.qos_logical_queue
        qos_queue_id_list = self._args.qos_queue_id
        default_hw_queue_qos = self._args.default_hw_queue_qos
        priority_id_list = self._args.priority_id
        priority_scheduling = self._args.priority_scheduling
        priority_bandwidth = self._args.priority_bandwidth

        self.mac = None
        if self.dev and self.dev != 'vhost0':
            self.mac = netifaces.ifaddresses(self.dev)[netifaces.AF_LINK][0][
                           'addr']
            if not self.mac:
                raise KeyError('Interface %s Mac %s' % (str(self.dev),
                                                        str(self.mac)))
            self.netmask = netifaces.ifaddresses(self.dev)[
                               netifaces.AF_INET][0]['netmask']
            if self.multi_net:
                self.gateway = non_mgmt_gw
            else:
                self.gateway = self.find_gateway(self.dev)
            cidr = str(netaddr.IPNetwork('%s/%s' % (self.vhost_ip,
                                                    self.netmask)))

            if vgw_public_subnet:
                os.chdir(self._temp_dir_name)
                # Manipulating the string to use in agent_param
                vgw_public_subnet_str = []
                for i in vgw_public_subnet[1:-1].split(";"):
                    j = i[1:-1].split(",")
                    j = ";".join(j)
                    vgw_public_subnet_str.append(j)
                vgw_public_subnet_str = str(tuple(
                    vgw_public_subnet_str)).replace("'", "")
                vgw_public_subnet_str = vgw_public_subnet_str.replace(" ", "")
                vgw_intf_list_str = str(tuple(
                    vgw_intf_list[1:-1].split(";"))).replace(" ", "")

                cmds = ["sudo sed 's@dev=.*@dev=%s@g;" % self.dev,
                        "s@vgw_subnet_ip=.*@vgw_subnet_ip=%s@g;" %
                        vgw_public_subnet_str,
                        "s@vgw_intf=.*@vgw_intf=%s@g'" % vgw_intf_list_str,
                        " /etc/contrail/agent_param.tmpl > agent_param.new"]
                local(' '.join(cmds))
                local("sudo mv agent_param.new /etc/contrail/agent_param")
            else:
                os.chdir(self._temp_dir_name)
                cmds = ["sudo sed 's/dev=.*/dev=%s/g' " % self.dev,
                        "/etc/contrail/agent_param.tmpl > agent_param.new"]
                local(''.join(cmds))
                local("sudo mv agent_param.new /etc/contrail/agent_param")
            vmware_dev = ""
            hypervisor_type = "kvm"
            mode = ""
            gateway_mode = ""
            if self._args.mode == 'vcenter':
                mode = "vcenter"
                vmware_dev = self.get_secondary_device(self.dev)
                hypervisor_type = "vmware"
            if self._args.vmware:
                vmware_dev = self.get_secondary_device(self.dev)
                hypervisor_type = "vmware"
            if self._args.hypervisor == 'docker':
                hypervisor_type = "docker"
            if compute_ip in gateway_server_list:
                gateway_mode = "server"

            # Set template options for DPDK mode
            pci_dev = ""
            platform_mode = "default"
            if self._args.dpdk:
                platform_mode = "dpdk"
                iface = self.dev
                if self.is_interface_vlan(self.dev):
                    iface = self.get_physical_interface_of_vlan(self.dev)
                cmd = "sudo /opt/contrail/bin/dpdk_nic_bind.py --status | "
                cmd += "sudo grep -w %s | cut -d' ' -f 1" % iface
                pci_dev = local(cmd, capture=True)
                # If there is no PCI address, the device is a bond.
                # Bond interface in DPDK has zero PCI address.
                if not pci_dev:
                    pci_dev = "0000:00:00.0"

            control_servers = ' '.join('%s:%s' % (server, '5269')
                                       for server in self._args.control_nodes)
            dns_servers = ' '.join('%s:%s' % (server, '53')
                                   for server in self._args.control_nodes)
            collector_servers = ' '.join('%s:%s' % (server, '8086')
                                         for server in self._args.collectors)
            configs = {
                    'DEFAULT': {
                        'platform': platform_mode,
                        'gateway_mode': gateway_mode,
                        'physical_interface_address': pci_dev,
                        'physical_interface_mac': self.mac,
                        'collectors': collector_servers},
                    'NETWORKS': {
                        'control_network_ip': compute_ip},
                    'VIRTUAL-HOST-INTERFACE': {
                        'ip': cidr,
                        'gateway': self.gateway,
                        'physical_interface': self.dev},
                    'HYPERVISOR': {
                        'type': hypervisor_type,
                        'vmware_mode': mode,
                        'vmware_physical_interface': vmware_dev},
                    'CONTROL-NODE': {
                        'servers': control_servers},
                    'DNS': {
                        'servers': dns_servers},
                    }

            # VGW configs
            if vgw_public_vn_name and vgw_public_subnet:
                vgw_public_vn_name = vgw_public_vn_name[1:-1].split(';')
                vgw_public_subnet = vgw_public_subnet[1:-1].split(';')
                vgw_intf_list = vgw_intf_list[1:-1].split(';')
                if vgw_gateway_routes is not None:
                    vgw_gateway_routes = vgw_gateway_routes[1:-1].split(';')
                for i in range(len(vgw_public_vn_name)):
                    ip_blocks = ''
                    if vgw_public_subnet[i].find("[") != -1:
                        for ele in vgw_public_subnet[i][1:-1].split(","):
                            ip_blocks += ele[1:-1] + " "
                    else:
                        ip_blocks += vgw_public_subnet[i]
                    routes = ''
                    if (vgw_gateway_routes is not None and
                            i < len(vgw_gateway_routes)):
                        if vgw_gateway_routes[i] != '[]':
                            if vgw_gateway_routes[i].find("[") != -1:
                                for ele in vgw_gateway_routes[i][1:-1].split(
                                        ","):
                                    routes += ele[1:-1] + " "
                            else:
                                routes += vgw_gateway_routes[i]

                    configs['GATEWAY-%s' % i] = {'interface': vgw_intf_list[i],
                                                 'ip_blocks': ip_blocks,
                                                 'routes': routes}

            # QOS configs
            if qos_queue_id_list is not None:
                qos_str = ""
                qos_str += "[QOS]\n"
                num_sections = len(qos_logical_queue)
                if(len(qos_logical_queue) == len(qos_queue_id_list) and
                        default_hw_queue_qos):
                    num_sections = num_sections - 1
                for i in range(num_sections):
                    configs['QUEUE-%s' % qos_queue_id_list[i]] = {
                        'logical_queue':
                            '[%s]' % qos_logical_queue[i].replace(",", ", ")}

                if (default_hw_queue_qos):
                    if(len(qos_logical_queue) == len(qos_queue_id_list)):
                        logical_queue = '[%s]' %\
                                qos_logical_queue[-1].replace(",", ", ")
                    else:
                        logical_queue = '[ ]'

                    configs['QUEUE-%s' % qos_queue_id_list[-1]] = {
                        'default_hw_queue': 'true',
                        'logical_queue': logical_queue}

            if priority_id_list is not None:
                for i in range(len(priority_id_list)):
                    configs['PG-%s' % priority_id_list[i]] = {
                        'scheduling': priority_scheduling[i],
                        'bandwidth': priority_bandwidth[i]}

            if self._args.metadata_secret:
                configs['METADATA'] = {
                    'metadata_proxy_secret': self._args.metadata_secret}

            for section, key_vals in configs:
                for key, val in key_vals:
                    self.set_config(
                            '/etc/contrail/contrail-vrouter-agent.conf',
                            section, key, val)

            self.fixup_vhost0_interface_configs()

    def fixup_contrail_lbaas(self):
        auth_url = self._args.keystone_auth_protocol + '://'
        auth_url += self._args.keystone_ip
        auth_url += ':' + self._args.keystone_auth_port
        auth_url += '/' + 'v2.0'
        configs = {
                'BARBICAN': {
                    'admin_tenant_name': 'service',
                    'admin_user': 'neutron',
                    'admin_password': self._args.neutron_password,
                    'auth_url': auth_url,
                    'region': 'RegionOne'}
                  }
        for section, key_vals in configs.items():
            for key, val in key_vals.items():
                self.set_config(
                        '/etc/contrail/contrail-lbaas-auth.conf',
                        section, key, val)

    def fixup_vhost0_interface_configs(self):
        if self.pdist in ['centos', 'fedora', 'redhat']:
            # make ifcfg-vhost0
            with open('%s/ifcfg-vhost0' % self._temp_dir_name, 'w') as f:
                f.write('''#Contrail vhost0
DEVICE=vhost0
ONBOOT=yes
BOOTPROTO=none
IPV6INIT=no
USERCTL=yes
IPADDR=%s
NETMASK=%s
NM_CONTROLLED=no
#NETWORK MANAGER BUG WORKAROUND
SUBCHANNELS=1,2,3
''' % (self.vhost_ip, self.netmask))
                # Don't set gateway and DNS on vhost0 if on non-mgmt network
                if not self.multi_net:
                    if self.gateway:
                        f.write('GATEWAY=%s\n' % self.gateway)
                    dns_list = self.get_dns_servers(self.dev)
                    for i, dns in enumerate(dns_list):
                        f.write('DNS%d=%s\n' % (i+1, dns))
                    domain_list = self.get_domain_search_list()
                    if domain_list:
                        f.write('DOMAIN="%s"\n' % domain_list)

                prsv_cfg = []
                mtu = self.get_if_mtu(self.dev)
                if mtu:
                    dcfg = 'MTU=%s' % str(mtu)
                    f.write(dcfg+'\n')
                    prsv_cfg.append(dcfg)
                f.flush()
            if self.dev != 'vhost0':
                src = "%s/ifcfg-vhost0" % self._temp_dir_name
                dst = "/etc/sysconfig/network-scripts/ifcfg-vhost0"
                local("sudo mv %s %s" % (src, dst), warn_only=True)
                local("sudo sync", warn_only=True)
                # make ifcfg-$dev
                ifcfg = "/etc/sysconfig/network-scripts/ifcfg-%s %s" % self.dev
                ifcfg_bkp = "/etc/sysconfig/network-scripts/ifcfg-%s.rpmsave"\
                            % self.dev
                if not os.path.isfile(ifcfg_bkp):
                    local("sudo cp %s %s" % (ifcfg, ifcfg_bkp), warn_only=True)
                ifcfg_tmp = '%s/ifcfg-%s' % (self._temp_dir_name, self.dev)
                self._rewrite_ifcfg_file(ifcfg_tmp, self.dev, prsv_cfg)

                if self.multi_net:
                    self.migrate_routes(self.dev)

                local("sudo mv %s /etc/contrail/" % ifcfg_tmp, warn_only=True)

                local("sudo chkconfig network on", warn_only=True)
                local("sudo chkconfig supervisor-vrouter on", warn_only=True)
        # end self.pdist == centos | fedora | redhat
        # setup lbaas prereqs
        self.setup_lbaas_prereq()

        if self.pdist in ['Ubuntu']:
            self._rewrite_net_interfaces_file(
                    self.dev, self.mac, self.vhost_ip, self.netmask,
                    self.gateway, self._args.vmware,
                    self._args.vmware_vmpg_vswitch_mtu,
                    self._args.vmware_datanic_mtu)
        # end self.pdist == ubuntu

    def run_services(self):
        for svc in ['supervisor-vrouter']:
            local('sudo chkconfig %s on' % svc)

    def add_vnc_config(self):
        compute_ip = self._args.self_ip
        compute_hostname = socket.gethostname()
        use_ssl = False
        if self._args.keystone_auth_protocol == 'https':
            use_ssl = True
        prov_args = "--host_name %s --host_ip %s --api_server_ip %s "\
                    "--oper add --admin_user %s --admin_password %s "\
                    "--admin_tenant_name %s --openstack_ip %s "\
                    "--api_server_use_ssl %s" \
                    % (compute_hostname, compute_ip, self._args.cfgm_ip,
                       self._args.keystone_admin_user,
                       self._args.keystone_admin_password,
                       self._args.keystone_admin_tenant_name,
                       self._args.keystone_ip,
                       use_ssl)
        if self._args.dpdk:
            prov_args += " --dpdk_enabled"
        cmd = "sudo python /opt/contrail/utils/provision_vrouter.py "
        local(cmd + prov_args)

    def setup(self):
        self.disable_selinux()
        self.disable_iptables()
        self.setup_coredump()
        self.fixup_config_files()
        self.run_services()
        self.add_vnc_config()
