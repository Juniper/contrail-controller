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


def insert_line_to_file(line, file_name, pattern=None):
    if pattern:
        local('sed -i \'/%s/d\' %s' % (pattern, file_name), warn_only=True)
    local('printf "%s\n" >> %s' % (line, file_name))


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
        if self.pdist not in ['Ubuntu']:
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
        # Workaround https://bugs.launchpad.net/juniperopenstack/+bug/1681172
        cfgfile = '/etc/contrail/contrail-vrouter-nodemgr.conf'
        if not os.path.isfile(cfgfile):
            local('sudo touch %s' % cfgfile)
        collector_list = ' '.join('%s:%s' % (server, '8086')
                                  for server in self._args.collectors)
        self.set_config(cfgfile, 'COLLECTOR', 'server_list', collector_list)

    def setup_hugepages_node(self, dpdk_args):
        """Setup hugepages on one or list of nodes
        """
        # How many times DPDK inits hugepages (rte_eal_init())
        # See function map_all_hugepages() in DPDK
        DPDK_HUGEPAGES_INIT_TIMES = 2

        # get required size of hugetlbfs
        factor = int(dpdk_args['huge_pages'])

        print dpdk_args

        if factor == 0:
            factor = 1

        # set number of huge pages
        memsize = local("sudo grep MemTotal /proc/meminfo |"
                        " tr -s ' ' | cut -d' ' -f 2 | tr -d '\n'",
                        capture=True, warn_only=True)
        pagesize = local("sudo grep Hugepagesize /proc/meminfo"
                         " | tr -s ' 'i | cut -d' ' -f 2 | tr -d '\n'",
                         capture=True, warn_only=True)
        reserved = local("sudo grep HugePages_Total /proc/meminfo"
                         " | tr -s ' 'i | cut -d' ' -f 2 | tr -d '\n'",
                         capture=True, warn_only=True)

        if (reserved == ""):
            reserved = "0"

        requested = ((int(memsize) * factor) / 100) / int(pagesize)

        if (requested > int(reserved)):
            pattern = "^vm.nr_hugepages ="
            line = "vm.nr_hugepages = %d" % requested
            insert_line_to_file(pattern=pattern, line=line,
                                file_name='/etc/sysctl.conf')

        current_max_map_count = local("sudo sysctl -n "
                                      "vm.max_map_count")
        if current_max_map_count == "":
            current_max_map_count = 0

        current_huge_pages = max(int(requested), int(reserved))

        requested_max_map_count = (DPDK_HUGEPAGES_INIT_TIMES
                                   * int(current_huge_pages))

        if int(requested_max_map_count) > int(current_max_map_count):
            pattern = "^vm.max_map_count ="
            line = "vm.max_map_count = %d" % requested_max_map_count
            insert_line_to_file(pattern=pattern, line=line,
                                file_name='/etc/sysctl.conf')

        mounted = local("sudo mount | grep hugetlbfs | cut -d' ' -f 3",
                        capture=True, warn_only=False)
        if (mounted != ""):
            print "hugepages already mounted on %s" % mounted
        else:
            local("sudo mkdir -p /hugepages", warn_only=False)
            pattern = "^hugetlbfs"
            line = "hugetlbfs    "\
                   "/hugepages    hugetlbfs defaults      0       0"
            insert_line_to_file(pattern=pattern, line=line,
                                file_name='/etc/fstab')
            local("sudo mount -t hugetlbfs hugetlbfs /hugepages",
                  warn_only=False)

    def setup_coremask_node(self, dpdk_args):
        """Setup core mask on one or list of nodes
        """
        vrouter_file = ('/etc/contrail/supervisord_vrouter_files/' +
                        'contrail-vrouter-dpdk.ini')

        try:
            coremask = dpdk_args['coremask']
        except KeyError:
            raise RuntimeError("Core mask for host %s is not defined."
                               % (dpdk_args))

        if not coremask:
            raise RuntimeError("Core mask for host %s is not defined."
                               % dpdk_args)

        # if a list of cpus is provided, -c flag must be passed to taskset
        if (',' in coremask) or ('-' in coremask):
            taskset_param = ' -c'
        else:
            taskset_param = ''

        # supported coremask format: hex: (0x3f); list: (0,3-5), (0,1,2,3,4,5)
        # try taskset on a dummy command
        if local('sudo taskset%s %s true' % (taskset_param, coremask),
                 capture=True, warn_only=False).succeeded:
            local('sudo sed -i \'s/command=/command=taskset%s %s /\' %s'
                  % (taskset_param, coremask, vrouter_file), warn_only=False)
        else:
            raise RuntimeError("Error: Core mask %s for host %s is invalid."
                               % (coremask, dpdk_args))

    def setup_vm_coremask_node(self, q_coremask, dpdk_args):
        """
        Setup CPU affinity for QEMU processes based on
        vRouter/DPDK core affinity on a given node.

        Supported core mask format:
            vRouter/DPDK:   hex (0x3f), list (0,1,2,3,4,5), range (0,3-5)
            QEMU/nova.conf: list (0,1,2,3,4,5), range (0,3-5),
                            exclusion (0-5,^4)

        QEMU needs to be pinned to different cores than vRouter. Because of
        different core mask formats, it is not possible to just set QEMU to
        <not vRouter cores>. This function takes vRouter core mask from
        testbed, changes it to list of cores and removes them from list
        of all possible cores (generated as a list from 0 to N-1, where
        N = number of cores). This is changed back to string and passed to
        openstack-config.
        """

        try:
            vr_coremask = dpdk_args['coremask']
        except KeyError:
            raise RuntimeError("vRouter core mask for "
                               "host %s is not defined." % (dpdk_args))

        if not vr_coremask:
            raise RuntimeError("vRouter core mask for host "
                               "%s is not defined." % dpdk_args)

        if not q_coremask:
            try:
                cpu_count = int(local(
                    'sudo grep -c processor /proc/cpuinfo',
                    capture=True))
            except ValueError:
                log.info("Cannot count CPUs on host %s. VM core "
                         "mask cannot be computed." % (dpdk_args))
                raise

            if not cpu_count or cpu_count == -1:
                raise ValueError("Cannot count CPUs on host %s. "
                                 "VM core mask cannot be computed."
                                 % (dpdk_args))

            all_cores = [x for x in xrange(cpu_count)]

            if 'x' in vr_coremask:  # String containing hexadecimal mask.
                vr_coremask = int(vr_coremask, 16)

                """
                Convert hexmask to a string with numbers of cores to be
                used, eg.
                0x19 -> 11001 -> 10011 -> [(0,1), (1,0), (2,0),
                (3,1), (4,1)] -> '0,3,4'
                """
                vr_coremask = [
                        x[0] for x in enumerate(reversed(bin(vr_coremask)[2:]))
                        if x[1] == '1']
            # Range or list of cores.
            elif (',' in vr_coremask) or ('-' in vr_coremask):
                # Get list of core numbers and/or core ranges.
                vr_coremask = vr_coremask.split(',')

                # Expand ranges like 0-4 to 0, 1, 2, 3, 4.
                vr_coremask_expanded = []
                for rng in vr_coremask:
                    if '-' in rng:  # If it's a range - expand it.
                        a, b = rng.split('-')
                        vr_coremask_expanded += range(int(a), int(b)+1)
                    else:  # If not, just add to the list.
                        vr_coremask_expanded.append(int(rng))

                vr_coremask = vr_coremask_expanded
            else:  # A single core.
                try:
                    single_core = int(vr_coremask)
                except ValueError:
                    log.error("vRouter core mask %s for host %s is invalid."
                              % (vr_coremask, dpdk_args))
                    raise

                vr_coremask = []
                vr_coremask.append(single_core)

            # From list of all cores remove list of vRouter cores
            # and stringify.
            diff = set(all_cores) - set(vr_coremask)
            q_coremask = ','.join(str(x) for x in diff)

            # If we have no spare cores for VMs
            if not q_coremask:
                raise RuntimeError("Setting QEMU core mask for host %s "
                                   "failed - empty string."
                                   % (dpdk_args))

        # This can fail eg. because openstack-config is not present.
        # There's no sanity check in openstack-config.
        if local("sudo crudini --set /etc/nova/nova.conf "
                 "DEFAULT vcpu_pin_set %s"
                 % q_coremask, capture=True, warn_only=False).succeeded:
            log.info("QEMU coremask on host %s set to %s."
                     % (dpdk_args, q_coremask))
        else:
            raise RuntimeError("Error: setting QEMU core mask %s for "
                               "host %s failed." % (vr_coremask, dpdk_args))

    def setup_uio_driver(self, dpdk_args):
        """Setup UIO driver to use for DPDK
        (igb_uio, uio_pci_generic or vfio-pci)
        """
        vrouter_agent_file = '/etc/contrail/contrail-vrouter-agent.conf'

        if 'uio_driver' in dpdk_args:
            uio_driver = dpdk_args['uio_driver']
        else:
            print "No UIO driver defined for host, skipping..."
            return

        if local('sudo modprobe %s'
                 % (uio_driver), capture=True, warn_only=False).succeeded:
            log.info("Setting UIO driver to %s for host..." % uio_driver)
            local('sudo sed -i.bak \'s/physical_uio_driver='
                  '.*/physical_uio_driver=%s/\' %s'
                  % (uio_driver, vrouter_agent_file))
        else:
            raise RuntimeError("Error: invalid UIO driver %s for host"
                               % (uio_driver))

    def dpdk_increase_vrouter_limit(self,
                                    vrouter_module_params_args):
        """Increase the maximum number of mpls label
        and nexthop on tsn node"""

        vrouter_file = ('/etc/contrail/supervisord_vrouter_files/' +
                        'contrail-vrouter-dpdk.ini')
        cmd = "--vr_mpls_labels %s "\
              % vrouter_module_params_args.setdefault('mpls_labels', '5120')
        cmd += "--vr_nexthops %s "\
               % vrouter_module_params_args.setdefault('nexthops', '65536')
        cmd += "--vr_vrfs %s "\
               % vrouter_module_params_args.setdefault('vrfs', '5120')
        cmd += "--vr_bridge_entries %s "\
               % vrouter_module_params_args.setdefault('macs', '262144')
        local('sudo sed -i \'s#\(^command=.*$\)#\\1 %s#\' %s'
              % (cmd, vrouter_file), warn_only=False)

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
                dpdk_args = dict(
                        u.split("=") for u in self._args.dpdk.split(","))
                log.info(dpdk_args)
                platform_mode = "dpdk"
                iface = self.dev
                if self.is_interface_vlan(self.dev):
                    iface = self.get_physical_interface_of_vlan(self.dev)
                local("ls /opt/contrail/bin/dpdk_nic_bind.py", warn_only=False)
                cmd = "sudo /opt/contrail/bin/dpdk_nic_bind.py --status | "
                cmd += "sudo grep -w %s | cut -d' ' -f 1" % iface
                pci_dev = local(cmd, capture=True, warn_only=False)
                # If there is no PCI address, the device is a bond.
                # Bond interface in DPDK has zero PCI address.
                if not pci_dev:
                    pci_dev = "0000:00:00.0"

                self.setup_hugepages_node(dpdk_args)
                self.setup_coremask_node(dpdk_args)
                self.setup_vm_coremask_node(False, dpdk_args)

                if self._args.vrouter_module_params:
                    vrouter_module_params_args = dict(
                            u.split("=") for u in
                            self._args.vrouter_module_params.split(","))
                    self.dpdk_increase_vrouter_limit(
                            vrouter_module_params_args)

                if self.pdist == 'Ubuntu':
                    # Fix /dev/vhost-net permissions. It is required for
                    # multiqueue operation
                    local('sudo echo \'KERNEL=="vhost-net", '
                          'GROUP="kvm", MODE="0660"\' > '
                          '/etc/udev/rules.d/vhost-net.rules', warn_only=True)
                    # The vhost-net module has to be loaded at startup to
                    # ensure the correct permissions while the qemu is being
                    # launched
                    local('sudo echo "vhost-net" >> /etc/modules')

                self.setup_uio_driver(dpdk_args)

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
                        'collectors': collector_servers,
                        'xmpp_auth_enable': self._args.xmpp_auth_enable},
                    'NETWORKS': {
                        'control_network_ip': compute_ip},
                    'VIRTUAL-HOST-INTERFACE': {
                        'name': 'vhost0',
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

            for section, key_vals in configs.items():
                for key, val in key_vals.items():
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
        # Workaround https://bugs.launchpad.net/juniperopenstack/+bug/1681172
        cfgfile = '/etc/contrail/contrail-lbaas-auth.conf'
        if not os.path.isfile(cfgfile):
            local('sudo touch %s' % cfgfile)
        for section, key_vals in configs.items():
            for key, val in key_vals.items():
                self.set_config(cfgfile, section, key, val)

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

                if self.pdist not in ['Ubuntu']:
                    local("sudo chkconfig network on", warn_only=True)
                    local("sudo chkconfig supervisor-vrouter on",
                          warn_only=True)
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
        if self.pdist not in ['Ubuntu']:
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
