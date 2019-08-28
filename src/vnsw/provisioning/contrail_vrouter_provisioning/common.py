#!/usr/bin/env python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function
from __future__ import division
from builtins import str
from builtins import range
from past.utils import old_div
import os
import socket
import netaddr
import logging
import netifaces
import tempfile

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

        self.dev = None  # Will be physical device
        if self._args.physical_interface:
            # During re-provision/upgrade vhost0 will be present
            # so vhost0 should be treated as dev,
            # which is used to get netmask/gateway
            if 'vhost0' in netifaces.interfaces():
                self.dev = 'vhost0'
            # During intial provision actual interface should be treated as dev
            # which is used to get netmask/gateway
            elif self._args.physical_interface in netifaces.interfaces():
                self.dev = self._args.physical_interface
            else:
                raise KeyError('Interface %s in present' %
                               self._args.physical_interface)
        else:
            # Get the physical device and provision status
            # if reprov is False, it means fresh install
            #              True,  it means reprovision
            (self.dev, self.reprov) = self.get_device_info(self.vhost_ip)

    def fixup_config_files(self):
        self.add_dev_tun_in_cgroup_device_acl()
        self.fixup_contrail_vrouter_agent()
        self.add_qos_config()
        self.fixup_contrail_vrouter_nodemgr()
        self.fixup_contrail_lbaas()

    def setup_lbaas_prereq(self):
        if self.pdist in ['centos', 'redhat']:
            local('sudo groupadd -f nogroup')
            cmd = "sudo sed -i s/'Defaults    requiretty'/'#Defaults    "
            cmd += "requiretty'/g /etc/sudoers"
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
        self.set_config(cfgfile, 'SANDESH', 'sandesh_ssl_enable',
                        self._args.sandesh_ssl_enable)
        self.set_config(cfgfile, 'SANDESH', 'introspect_ssl_enable',
                        self._args.introspect_ssl_enable)

    def setup_hugepages_node(self, dpdk_args):
        """Setup hugepages on one or list of nodes
        """
        # How many times DPDK inits hugepages (rte_eal_init())
        # See function map_all_hugepages() in DPDK
        DPDK_HUGEPAGES_INIT_TIMES = 2

        # get required size of hugetlbfs
        factor = int(dpdk_args['huge_pages'])

        print(dpdk_args)

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

        requested = old_div((old_div((int(memsize) * factor), 100)), int(pagesize))

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
        local('sudo sysctl -p', warn_only=True)

        mounted = local("sudo mount | grep hugetlbfs | cut -d' ' -f 3",
                        capture=True, warn_only=False)
        if (mounted != ""):
            print("hugepages already mounted on %s" % mounted)
        else:
            local("sudo mkdir -p /hugepages", warn_only=False)
            pattern = "^hugetlbfs"
            line = "hugetlbfs    "\
                   "/hugepages    hugetlbfs defaults      0       0"
            insert_line_to_file(pattern=pattern, line=line,
                                file_name='/etc/fstab')
            local("sudo mount -t hugetlbfs hugetlbfs /hugepages",
                  warn_only=False)

    def search_and_replace(self, lvalue, rvalue, position, vrouter_file):
        """Search and replace strings in the format <key>=<filepath> <args>
           - 'position' determines where the <rvalue> needs to be inserted
           - If it is "Begin", the string becomes:
               <key>=<rvalue> <filepath> <args>
           - If it is "End", the string becomes:
               <key>=<filepath> <args> <rvalue>
           - If <rvalue> already exists in <args>, it deletes it first
           - If <rvalue> already preceeds <filepath> it deletes it first
           Input:
           - lvalue = <key>
           - rvalue = <arg> to be searched and replaced
           - position = Begin/End
           - vrouter_file = path of vrouter file
        """
        if position == "Begin":
            regexp_del = r"'s/\(^ *%s *=\)\(.*\)\( \/.*\)/\1\3/'" % (lvalue)
            regexp_add = r"'s/\(^%s=\)\(.*\)/\1%s \2/'" % (lvalue, rvalue)
            regexp = "sed -i.bak -e %s -e %s %s" \
                     % (regexp_del, regexp_add, vrouter_file)
            local(regexp, warn_only=False)
        elif position == "End":
            regexp_del = r"'s/\(^ *%s *=.*\) \(%s [^ ]*\)\(.*\) *$/\1\3/'" \
                         % (lvalue, rvalue.split(' ')[0])
            regexp_add = r"'s/\(^ *%s *=.*\)/\1 %s/'" % (lvalue, rvalue)
            regexp = "sed -i.bak -e %s -e %s %s" \
                     % (regexp_del, regexp_add, vrouter_file)
            local(regexp, warn_only=False)

    def setup_coremask_node(self, dpdk_args):
        """Setup core mask on one or list of nodes
        """
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
            self.search_and_replace(self.command_key, '\/usr\/bin\/taskset ' + coremask,
                                    "Begin", self.vrouter_file)
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

            all_cores = [x for x in range(cpu_count)]

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
                        vr_coremask_expanded += list(range(int(a), int(b) + 1))
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
            if uio_driver == "vfio-pci":
                self.setup_sriov_grub(uio_driver)
        else:
            print("No UIO driver defined for host, skipping...")
            return

        if local('sudo modprobe %s'
                 % (uio_driver), capture=True, warn_only=False).succeeded:
            log.info("Setting UIO driver to %s for host..." % uio_driver)
            local('sudo contrail-config --set %s DEFAULT '\
                  'physical_uio_driver %s' % (vrouter_agent_file, uio_driver))
        else:
            raise RuntimeError("Error: invalid UIO driver %s for host"
                               % (uio_driver))

    def dpdk_increase_vrouter_limit(self,
                                    vrouter_module_params_args):
        """Increase the maximum number of mpls label
        and nexthop on tsn node"""

        vr_params = {
            'flow_entries': '524288',
            'oflow_entries': '3000',
            'mpls_labels': '5120',
            'nexthops': '65536',
            'vrfs': '5120',
            'macs': {'bridge_entries': '262144'},
        }
        for param in vr_params:
            if isinstance(vr_params[param], dict):
                for p in vr_params[param]:
                    param_name = p
                    param_val = vrouter_module_params_args.setdefault(
                        param, vr_params[param][p])
            else:
                param_name = param
                param_val = vrouter_module_params_args.setdefault(
                    param, vr_params[param])

            param = "--vr_" + param_name + " " + param_val
            self.search_and_replace(self.command_key, param,
                                    "End", self.vrouter_file)

    def fixup_contrail_vrouter_agent(self):
        compute_ip = self._args.self_ip
        non_mgmt_gw = self._args.non_mgmt_gw
        vgw_public_subnet = self._args.vgw_public_subnet
        vgw_public_vn_name = self._args.vgw_public_vn_name
        vgw_intf_list = self._args.vgw_intf_list
        vgw_gateway_routes = self._args.vgw_gateway_routes
        compute_as_gateway = self._args.compute_as_gateway
        flow_thread_count = self._args.flow_thread_count

        self.mac = None
        # Fresh install
        if self.dev and not self.reprov:
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
            self.cidr = netaddr.IPNetwork('%s/%s' % (self.vhost_ip,
                                                     self.netmask))
        elif self.dev:
            # Reprovision
            cfg_file = "/etc/contrail/contrail-vrouter-agent.conf"
            section = "DEFAULT"
            key = "physical_interface_mac"
            self.mac = self.get_config(cfg_file, section, key).strip()
            section = "VIRTUAL-HOST-INTERFACE"
            key = "ip"
            self.cidr = netaddr.IPNetwork(self.get_config(cfg_file, section, key).strip())
            section = "VIRTUAL-HOST-INTERFACE"
            key = "gateway"
            self.gateway = self.get_config(cfg_file, section, key).strip()
            self.netmask = "255.255.255.0"

        if self.dev:
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

            vmware_dev = None
            gateway_mode = None
            if (self._args.mode == 'vcenter' or
                    self._args.hypervisor == 'vmware'):
                vmware_dev = self.get_secondary_device(self.dev)
            if compute_as_gateway == 'server':
                gateway_mode = "server"

            # Set template options for DPDK mode
            pci_dev = ""
            platform_mode = "default"
            if self._args.dpdk:
                dpdk_args = dict(
                    u.split("=") for u in self._args.dpdk.split(","))
                log.info(dpdk_args)
                platform_mode = "dpdk"

                supervisor_vrouter_file = ('/etc/contrail/' +
                                           'supervisord_vrouter_files/' +
                                           'contrail-vrouter-dpdk.ini')
                systemd_vrouter_file = ('/lib/systemd/system/' +
                                        'contrail-vrouter-dpdk.service')

                if os.path.isfile(supervisor_vrouter_file):
                    self.vrouter_file = supervisor_vrouter_file
                    self.command_key = "command"
                elif os.path.isfile(systemd_vrouter_file):
                    self.vrouter_file = systemd_vrouter_file
                    self.command_key = "ExecStart"
                else:
                    raise RuntimeError("Vrouter Supervisor/Systemd not found.")

                self.setup_hugepages_node(dpdk_args)
                self.setup_coremask_node(dpdk_args)
                self.setup_vm_coremask_node(False, dpdk_args)
                self.setup_uio_driver(dpdk_args)

            if self._args.dpdk and not self.reprov:
                iface = self.dev
                if self.is_interface_vlan(self.dev):
                    iface = self.get_physical_interface_of_vlan(self.dev)
                local("ls /opt/contrail/bin/dpdk_nic_bind.py", warn_only=False)
                cmd = "sudo /opt/contrail/bin/dpdk_nic_bind.py --status | "
                cmd += "sudo grep -w %s | cut -d' ' -f 1" % iface.strip()
                pci_dev = local(cmd, capture=True, warn_only=False)
                # If there is no PCI address, the device is a bond.
                # Bond interface in DPDK has zero PCI address.
                if not pci_dev:
                    pci_dev = "0000:00:00.0"
            elif self._args.dpdk and self.reprov:
                cfg_file = "/etc/contrail/contrail-vrouter-agent.conf"
                section = "DEFAULT"
                key = "physical_interface_address"
                pci_dev = self.get_config(cfg_file, section, key).strip()

            if self.pdist == 'Ubuntu':
                # Fix /dev/vhost-net permissions. It is required for
                # multiqueue operation
                local('sudo echo \'KERNEL=="vhost-net", '
                      'GROUP="kvm", MODE="0660"\' > '
                      '/etc/udev/rules.d/vhost-net.rules', warn_only=True)
                # The vhost-net module has to be loaded at startup to
                # ensure the correct permissions while the qemu is being
                # launched
                pattern = "vhost-net"
                line = "vhost-net"
                insert_line_to_file(pattern=pattern, line=line,
                                    file_name='/etc/modules')

            if not self._args.dpdk:
                self.setup_vrouter_kmod_hugepages()

            vrouter_kmod_1G_page = ''
            vrouter_kmod_2M_page = ''
            if self._args.vrouter_1G_hugepages != '0':
                if (os.path.isfile('/mnt/hugepage_1G/vrouter_1G_mem_0')):
                    vrouter_kmod_1G_page = '/mnt/hugepage_1G/vrouter_1G_mem_0'
                if (os.path.isfile('/mnt/hugepage_1G/vrouter_1G_mem_1')):
                    vrouter_kmod_1G_page = vrouter_kmod_1G_page + ' /mnt/hugepage_1G/vrouter_1G_mem_1'
            if self._args.vrouter_2M_hugepages != '0':
                if (os.path.isfile('/mnt/hugepage_2M/vrouter_2M_mem_0')):
                    vrouter_kmod_2M_page = '/mnt/hugepage_2M/vrouter_2M_mem_0'
                if (os.path.isfile('/mnt/hugepage_2M/vrouter_2M_mem_1')):
                    vrouter_kmod_2M_page = vrouter_kmod_2M_page + ' /mnt/hugepage_2M/vrouter_2M_mem_1'

            control_servers = ' '.join('%s:%s' % (server, '5269')
                                       for server in self._args.control_nodes)
            dns_servers = ' '.join('%s:%s' % (server, '53')
                                   for server in self._args.control_nodes)
            collector_servers = ' '.join('%s:%s' % (server, '8086')
                                         for server in self._args.collectors)
            if self._args.tsn_evpn_mode and self._args.tsn_servers:
                tsn_servers = ' '.join(self._args.tsn_servers)
            else:
                tsn_servers = ''
            configs = {
                'DEFAULT': {
                    'platform': platform_mode,
                    'gateway_mode': gateway_mode or '',
                    'physical_interface_address': pci_dev,
                    'physical_interface_mac': self.mac,
                    'collectors': collector_servers,
                    'xmpp_auth_enable': self._args.xmpp_auth_enable,
                    'xmpp_dns_auth_enable': self._args.xmpp_dns_auth_enable,
                    'tsn_servers': tsn_servers,
                    'agent_mode': ''},
                'NETWORKS': {
                    'control_network_ip': compute_ip},
                'VIRTUAL-HOST-INTERFACE': {
                    'name': 'vhost0',
                    'ip': str(self.cidr),
                    'gateway': self.gateway,
                    'physical_interface': self.dev},
                'HYPERVISOR': {
                    'type': ('kvm' if self._args.hypervisor == 'libvirt'
                             else self._args.hypervisor),
                    'vmware_mode': self._args.mode or '',
                    'vmware_physical_interface': vmware_dev or ''},
                'CONTROL-NODE': {
                    'servers': control_servers},
                'DNS': {
                    'servers': dns_servers},
                'SANDESH': {
                    'sandesh_ssl_enable': self._args.sandesh_ssl_enable,
                    'introspect_ssl_enable':
                        self._args.introspect_ssl_enable},
                'FLOWS': {
                    'thread_count': flow_thread_count},
                'METADATA': {
                    'metadata_proxy_secret': self._args.metadata_secret,
                    'metadata_use_ssl': self._args.metadata_use_ssl,
                    'metadata_client_cert': ('/etc/contrail/ssl/certs/server.pem'
                                             if self._args.metadata_use_ssl else ''),
                    'metdata_client_cert_type': ('PEM' if self._args.metadata_use_ssl
                                                 else ''),
                    'metadata_client_key': ('/etc/contrail/ssl/private/server-privkey.pem'
                                            if self._args.metadata_use_ssl else '')},
                'RESTART': {
                    'huge_page_2M': vrouter_kmod_2M_page,
                    'huge_page_1G': vrouter_kmod_1G_page,
                    'backup_enable': (True
                                    if self._args.resource_backup_restore else False),
                    'backup_dir': ('/var/lib/contrail/backup'),
                    'backup_file_count': (self._args.backup_file_count),
                    'backup_idle_timeout': (self._args.backup_idle_timeout),
                    'restore_enable': (True
                                    if self._args.resource_backup_restore else False),
                    'restore_audit_timeout': (self._args.restore_audit_timeout)},
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
                                                 'routes': routes,
                                                 'routing_instance': vgw_public_vn_name[i]}

            for section, key_vals in list(configs.items()):
                for key, val in list(key_vals.items()):
                    self.set_config(
                        '/etc/contrail/contrail-vrouter-agent.conf',
                        section, key, val)

            if self.running_in_container:
                self.config_vhost0_interface_in_container()
            else:
                self.fixup_vhost0_interface_configs()

    def config_vhost0_interface_in_container(self):
        if self.reprov:
            log.info("vhost0 configuration already present")
            return
        # Insert vrouter and setup vrouter vifs
        insert_cmd = "source /opt/contrail/bin/vrouter-functions.sh && "
        insert_cmd += "insert_vrouter"
        local(insert_cmd, executable='/bin/bash')
        # Move ip address from vrouter physical device to vhost
        config_vhost0_cmd = "ip address delete %s/%s dev %s && " % (
            self.vhost_ip, self.cidr.prefixlen, self.dev)
        config_vhost0_cmd += "ip address add %s/%s dev vhost0 && " % (
            self.vhost_ip, self.cidr.prefixlen)
        config_vhost0_cmd += "ip link set dev vhost0 up"
        local(config_vhost0_cmd)
        # Add default gateway to new device as link local if /32 IP Address
        if self.cidr.prefixlen == 32:
            local("ip route add unicast %s dev vhost0 scope link" %
                  self.gateway)
        if not self.multi_net:
            # Add default gateway to vhost
            local("ip route add default via %s dev vhost0" % self.gateway)

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
        for section, key_vals in list(configs.items()):
            for key, val in list(key_vals.items()):
                self.set_config(cfgfile, section, key, val)

    def fixup_vhost0_interface_configs(self):
        if self.reprov:
            log.info("fixup_vhost0_interface_configs() not applicable")
            return

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
                        f.write('DNS%d=%s\n' % (i + 1, dns))
                    domain_list = self.get_domain_search_list()
                    if domain_list:
                        f.write('DOMAIN="%s"\n' % domain_list)

                prsv_cfg = []
                mtu = self.get_if_mtu(self.dev)
                if mtu:
                    dcfg = 'MTU=%s' % str(mtu)
                    f.write(dcfg + '\n')
                    prsv_cfg.append(dcfg)
                f.flush()
            if self.dev != 'vhost0':
                src = "%s/ifcfg-vhost0" % self._temp_dir_name
                dst = "/etc/sysconfig/network-scripts/ifcfg-vhost0"
                local("sudo mv %s %s" % (src, dst), warn_only=True)
                local("sudo sync", warn_only=True)
                # make ifcfg-$dev
                ifcfg = "/etc/sysconfig/network-scripts/ifcfg-%s" % self.dev
                ifcfg_bkp = "/etc/sysconfig/network-scripts/orig.ifcfg-%s.rpmsave"\
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
        if self.running_in_container:
            for svc in ['contrail-vrouter-agent', 'contrail-vrouter-nodemgr']:
                local('sudo service %s restart' % svc)

    def add_vnc_config(self):
        compute_ip = self._args.self_ip
        compute_hostname = socket.gethostname()
        use_ssl = False
        if self._args.apiserver_auth_protocol == 'https':
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

    def add_qos_config(self):
        qos_logical_queue = self._args.qos_logical_queue
        qos_queue_id_list = self._args.qos_queue_id
        default_hw_queue_qos = self._args.default_hw_queue_qos
        qos_priority_tagging = self._args.qos_priority_tagging
        priority_id_list = self._args.priority_id
        priority_scheduling = self._args.priority_scheduling
        priority_bandwidth = self._args.priority_bandwidth
        agent_conf = "/etc/contrail/contrail-vrouter-agent.conf"
        conf_file = "contrail-vrouter-agent.conf"
        configs = {}

        # Clean existing qos config
        ltemp_dir = tempfile.mkdtemp()
        local("sudo cp %s %s/" % (agent_conf, ltemp_dir))
        local(
            "sudo sed -i -e '/^\[QOS\]/d' -e '/^\[QUEUE-/d' -e '/^logical_queue/d' -e '/^default_hw_queue/d' -e '/^priority_tagging/d'  %s/%s" %
            (ltemp_dir, conf_file))
        local(
            "sudo sed -i -e '/^\[QOS-NIANTIC\]/d' -e '/^\[PG-/d' -e '/^scheduling/d' -e '/^bandwidth/d' %s/%s" %
            (ltemp_dir, conf_file))
        local("sudo cp %s/%s %s" % (ltemp_dir, conf_file, agent_conf))
        local('sudo rm -rf %s' % (ltemp_dir))
        # Set qos_enabled in agent_param to false
        self.set_config(
            '/etc/contrail/agent_param',
            sec="''",
            var='qos_enabled',
            val='false')

        # QOS configs
        if qos_queue_id_list is not None:

            self.set_config(
                agent_conf,
                'QOS',
                'priority_tagging',
                qos_priority_tagging)
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

            for section, key_vals in list(configs.items()):
                for key, val in list(key_vals.items()):
                    self.set_config(
                        agent_conf,
                        section, key, val)

        if priority_id_list is not None:

            local(
                'sudo contrail-config --set /etc/contrail/contrail-vrouter-agent.conf  QOS-NIANTIC')
            for i in range(len(priority_id_list)):
                configs['PG-%s' % priority_id_list[i]] = {
                    'scheduling': priority_scheduling[i],
                    'bandwidth': priority_bandwidth[i]}

            for section, key_vals in list(configs.items()):
                for key, val in list(key_vals.items()):
                    self.set_config(
                        agent_conf,
                        section, key, val)

        if (qos_queue_id_list or priority_id_list):
            # Set qos_enabled in agent_param
            self.set_config(
                '/etc/contrail/agent_param',
                sec="''",
                var='qos_enabled',
                val='true')

            # Run qosmap script on physical interface (on all members for bond
            # interface)
            physical_interface = local(
                "sudo openstack-config --get /etc/contrail/contrail-vrouter-agent.conf VIRTUAL-HOST-INTERFACE physical_interface")
            if os.path.isdir('/sys/class/net/%s/bonding' % physical_interface):
                physical_interfaces_str = local(
                    "sudo cat /sys/class/net/%s/bonding/slaves | tr ' ' '\n' | sort | tr '\n' ' '" %
                    physical_interface)
            else:
                physical_interfaces_str = physical_interface
            local(
                "cd /opt/contrail/utils; python qosmap.py --interface_list %s " %
                physical_interfaces_str)

    def disable_nova_compute(self):
        # Check if nova-compute is present in nova service list
        # Disable nova-compute on TSN node
        if local("nova service-list | grep nova-compute", warn_only=True).succeeded:
            # Stop the service
            local("sudo service nova-compute stop", warn_only=True)
            if self.pdist in ['Ubuntu']:
                local('sudo echo "manual" >> /etc/init/nova-compute.override')
            else:
                local('sudo chkconfig nova-compute off')

    def add_tsn_vnc_config(self):
        tsn_ip = self._args.self_ip
        self.tsn_hostname = socket.gethostname()
        prov_args = "--host_name %s --host_ip %s --api_server_ip %s --oper add "\
                    "--admin_user %s --admin_password %s --admin_tenant_name %s "\
                    "--openstack_ip %s --router_type tor-service-node --disable_vhost_vmi "\
                    % (self.tsn_hostname, tsn_ip, self._args.cfgm_ip,
                       self._args.keystone_admin_user,
                       self._args.keystone_admin_password,
                       self._args.keystone_admin_tenant_name, self._args.keystone_ip)
        if self._args.apiserver_auth_protocol == 'https':
            prov_args += " --api_server_use_ssl True"
        local(
            "python /opt/contrail/utils/provision_vrouter.py %s" %
            (prov_args))

    def start_tsn_service(self):
        nova_conf_file = '/etc/contrail/contrail-vrouter-agent.conf'
        mode = 'tsn'
        if self._args.tsn_evpn_mode:
            mode = 'tsn-no-forwarding'
        local(
            "openstack-config --set %s DEFAULT agent_mode %s" %
            (nova_conf_file, mode))

    def setup_tsn_node(self):
        self.disable_nova_compute()
        self.add_tsn_vnc_config()
        self.start_tsn_service()

    def increase_vrouter_limit(self):
        """Increase the maximum number of mpls label
        and nexthop on tsn node"""

        if self._args.vrouter_module_params:
            vrouter_module_params = self._args.vrouter_module_params.rstrip(
                ',')
            vrouter_module_params_args = dict(
                u.split("=") for u in
                vrouter_module_params.split(","))
            if self._args.dpdk:
                self.dpdk_increase_vrouter_limit(
                    vrouter_module_params_args)
            else:
                cmd = "options vrouter"
                if 'mpls_labels' in list(vrouter_module_params_args.keys()):
                    cmd += " vr_mpls_labels=%s" % vrouter_module_params_args['mpls_labels']
                if 'nexthops' in list(vrouter_module_params_args.keys()):
                    cmd += " vr_nexthops=%s" % vrouter_module_params_args['nexthops']
                if 'vrfs' in list(vrouter_module_params_args.keys()):
                    cmd += " vr_vrfs=%s" % vrouter_module_params_args['vrfs']
                if 'macs' in list(vrouter_module_params_args.keys()):
                    cmd += " vr_bridge_entries=%s" % vrouter_module_params_args['macs']
                if 'flow_entries' in list(vrouter_module_params_args.keys()):
                    cmd += " vr_flow_entries=%s" % vrouter_module_params_args['flow_entries']
                if 'oflow_entries' in list(vrouter_module_params_args.keys()):
                    cmd += " vr_oflow_entries=%s" % vrouter_module_params_args['oflow_entries']
                if 'mac_oentries' in list(vrouter_module_params_args.keys()):
                    cmd += " vr_bridge_oentries=%s" % vrouter_module_params_args['mac_oentries']
                if 'flow_hold_limit' in list(vrouter_module_params_args.keys()):
                    cmd += " vr_flow_hold_limit=%s" % vrouter_module_params_args['flow_hold_limit']
                if 'max_interface_entries' in list(vrouter_module_params_args.keys()):
                    cmd += " vr_interfaces=%s" % vrouter_module_params_args['max_interface_entries']
                if 'vrouter_dbg' in list(vrouter_module_params_args.keys()):
                    cmd += " vrouter_dbg=%s" % vrouter_module_params_args['vrouter_dbg']
                if 'vr_memory_alloc_checks' in list(vrouter_module_params_args.keys()):
                    cmd += " vr_memory_alloc_checks=%s" % vrouter_module_params_args['vr_memory_alloc_checks']
                local(
                    "echo %s > %s" %
                    (cmd, '/etc/modprobe.d/vrouter.conf'), warn_only=True)

    def setup_vrouter_kmod_hugepages(self):
        """Setup 1G and 2M hugepages for vrouter"""

        no_of_pages = 2
        # Update vrouter kernel mode hugepage config
        self.setup_vrouter_kmod_hugepage_grub()

        # Delete vrouter kernel mode 1G hugepage config
        if os.path.isfile('/etc/fstab'):
           pattern = "hugepage_1G"
           line = ""
           insert_line_to_file(pattern=pattern, line=line,
                            file_name='/etc/fstab')
        pattern = "vrouter_kmod_1G_hugepages"
        line = "vrouter_kmod_1G_hugepages=0"
        insert_line_to_file(pattern=pattern, line=line,
                            file_name='/etc/contrail/agent_param')

        # Delete vrouter kernel mode 2M hugepage config
        if os.path.isfile('/etc/fstab'):
            pattern = "hugepage_2M"
            line = ""
            insert_line_to_file(pattern=pattern, line=line,
                                file_name='/etc/fstab')

        pattern = "vrouter_kmod_2M_hugepages"
        line = "vrouter_kmod_2M_hugepages=0"
        insert_line_to_file(pattern=pattern, line=line,
                            file_name='/etc/contrail/agent_param')

        # Configure vrouter kernel mode 1G hugepages
        if self._args.vrouter_1G_hugepages != '0':
            if int(self._args.vrouter_1G_hugepages) > 0 and int(self._args.vrouter_1G_hugepages) <= 2:
                no_of_pages = int(self._args.vrouter_1G_hugepages)
            mounted = local("sudo mount | grep hugepage_1G | cut -d' ' -f 3",
                            capture=True, warn_only=False)
            if (mounted != ""):
                print("hugepages already mounted on %s" % mounted)
            else:
                local("sudo mkdir -p /mnt/hugepage_1G", warn_only=False)
                local("sudo mount -t hugetlbfs -o pagesize=1G none /mnt/hugepage_1G", warn_only=False)
                if os.path.isdir('/mnt/hugepage_1G'):
                    for i in range(no_of_pages):
                        local("sudo touch /mnt/hugepage_1G/vrouter_1G_mem_%s " % i, warn_only=False)
            pattern = "hugepage_1G"
            line = "hugetlbfs    "\
                   "/mnt/hugepage_1G    hugetlbfs defaults,pagesize=1G      0       0"
            insert_line_to_file(pattern=pattern, line=line,
                                file_name='/etc/fstab')
            pattern = "vrouter_kmod_1G_hugepages"
            line = "vrouter_kmod_1G_hugepages=%s" % no_of_pages
            insert_line_to_file(pattern=pattern, line=line,
                                file_name='/etc/contrail/agent_param')

        # Configure vrouter kernel mode 2M hugepages
        if self._args.vrouter_2M_hugepages != '0' and self._args.vrouter_1G_hugepages != '0':
            if int(self._args.vrouter_2M_hugepages) >= 0 and int(self._args.vrouter_2M_hugepages) <= 2:
                no_of_pages = int(self._args.vrouter_2M_hugepages)
            mounted = local("sudo mount | grep hugepage_2M | cut -d' ' -f 3",
                            capture=True, warn_only=False)
            if (mounted != ""):
                print("hugepages already mounted on %s" % mounted)
            else:
                local("sudo mkdir -p /mnt/hugepage_2M", warn_only=False)
                local("sudo mount -t hugetlbfs -o pagesize=2M none /mnt/hugepage_2M", warn_only=False)
                if os.path.isdir('/mnt/hugepage_2M'):
                    for i in range(no_of_pages):
                        local("sudo touch /mnt/hugepage_2M/vrouter_2M_mem_%s " % i, warn_only=False)
            pattern = "hugepage_2M"
            line = "hugetlbfs    "\
                   "/mnt/hugepage_2M    hugetlbfs defaults,pagesize=2M      0       0"
            insert_line_to_file(pattern=pattern, line=line,
                                file_name='/etc/fstab')
            pattern = "vrouter_kmod_2M_hugepages"
            line = "vrouter_kmod_2M_hugepages=%s" % no_of_pages
            insert_line_to_file(pattern=pattern, line=line,
                                file_name='/etc/contrail/agent_param')

    def setup(self):
        self.disable_selinux()
        self.disable_iptables()
        self.setup_coredump()
        self.fixup_config_files()
        self.increase_vrouter_limit()
        self.setup_sriov_grub()
        if self._args.tsn_mode or self._args.tsn_evpn_mode:
            self.setup_tsn_node()
            self.run_services()
        else:
            self.run_services()
            if self._args.register and not self.reprov:
                self.add_vnc_config()
