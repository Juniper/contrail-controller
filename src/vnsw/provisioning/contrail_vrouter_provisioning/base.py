#!/usr/bin/env python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
"""Base Contrail Provisioning module."""

from builtins import str
from builtins import object
import os
import re
import stat
import shutil
import socket
import tempfile
import platform
import logging
import argparse
from distutils.version import LooseVersion


from contrail_vrouter_provisioning import local, ExtList


log = logging.getLogger('contrail_vrouter_provisioning.base')


class ContrailSetup(object):
    def __init__(self):
        (self.pdist, self.pdistversion, self.pdistrelease) = platform.dist()
        self.hostname = socket.getfqdn()
        self.running_in_container = False
        if os.path.exists('/.dockerenv'):
            self.running_in_container = True

        self._temp_dir_name = tempfile.mkdtemp()
        self.contrail_bin_dir = '/opt/contrail/bin'
        self._fixed_qemu_conf = False

    def update_vips_in_ctrl_details(self, ctrl_infos):
        if self._args.internal_vip:
            ctrl_infos.append('INTERNAL_VIP=%s' % self._args.internal_vip)
        if self._args.contrail_internal_vip:
            ctrl_infos.append('CONTRAIL_INTERNAL_VIP=%s' %
                              self._args.contrail_internal_vip)
        if self._args.external_vip:
            ctrl_infos.append('EXTERNAL_VIP=%s' % self._args.external_vip)

    def _template_substitute(self, template, vals):
        data = template.safe_substitute(vals)
        return data

    def _template_substitute_write(self, template, vals, filename):
        data = self._template_substitute(template, vals)
        outfile = open(filename, 'w')
        outfile.write(data)
        outfile.close()

    def _replaces_in_file(self, file, replacement_list):
        rs = [(re.compile(regexp), repl)
              for (regexp, repl) in replacement_list]
        file_tmp = file + ".tmp"
        with open(file, 'r') as f:
            with open(file_tmp, 'w') as f_tmp:
                for line in f:
                    for r, replace in rs:
                        match = r.search(line)
                        if match:
                            line = replace + "\n"
                    f_tmp.write(line)
        shutil.move(file_tmp, file)

    def replace_in_file(self, file, regexp, replace):
        self._replaces_in_file(file, [(regexp, replace)])

    def setup_crashkernel_params(self):
        kcmd = r"sudo sed -i 's/crashkernel=.*\([ | \"]\)"
        kcmd += r"/crashkernel=384M-2G:64M,2G-16G:128M,16G-:256M\1/g' "
        kcmd += "/etc/default/grub.d/kexec-tools.cfg"
        if self.pdistversion == '14.04':
            local(kcmd, warn_only=True)
            cmd = "[ -f /etc/default/kdump-tools ] && "
            cmd += "sudo sed -i 's/USE_KDUMP=0/USE_KDUMP=1/' "
            cmd += "/etc/default/kdump-tools"
            local(cmd, warn_only=True)
        else:
            local(kcmd)
        local("sudo update-grub")

    def enable_kernel_core(self):
        """
        enable_kernel_core:
        update grub file
        install grub2
        enable services
        """
        gcnf = ''
        with open('/etc/default/grub', 'r') as f:
            gcnf = f.read()
            p = re.compile('\s*GRUB_CMDLINE_LINUX')
            el = ExtList(gcnf.split('\n'))
            try:
                i = el.findex(p.match)
                exec(el[i])
                el[i] = 'GRUB_CMDLINE_LINUX="%s crashkernel=128M"' % (
                        ' '.join([x for x in GRUB_CMDLINE_LINUX.split() if not x.startswith(
                            'crashkernel=')]))
                exec(el[i])
                el[i] = 'GRUB_CMDLINE_LINUX="%s kvm-intel.nested=1"' % (
                        ' '.join([x for x in GRUB_CMDLINE_LINUX.split() if not x.startswith(
                            'kvm-intel.nested=')]))

                with open('%s/grub' % self._temp_dir_name, 'w') as f:
                    f.write('\n'.join(el))
                    f.flush()
                local('sudo mv %s/grub /etc/default/grub' %
                      self._temp_dir_name)
                local('sudo /usr/sbin/grub2-mkconfig -o /boot/grub2/grub.cfg')
            except LookupError:
                log.warning('Improper grub file, kernel crash not enabled')

    def disable_selinux(self):
        # Disable selinux
        os.chdir(self._temp_dir_name)
        cmd = "sudo sed -i 's/SELINUX=.*/SELINUX=disabled/g'"
        cmd += " /etc/selinux/config"
        local(cmd, warn_only=True)
        local("sudo setenforce 0", warn_only=True)
        # cleanup in case move had error
        local("sudo rm config.new", warn_only=True)

    def setup_sriov_grub(self, uio_driver="None"):
        if not self._args.sriov:
            if uio_driver != "vfio-pci":
                return

        if self.pdist != 'Ubuntu':
            log.info("Not configuring SRIOV Grub changes for %s distribution",
                     self.pdist)
            return

        with open('/etc/default/grub', 'r') as f:
            gcnf = f.read()
            p = re.compile('\s*GRUB_CMDLINE_LINUX_DEFAULT')
            el = gcnf.split('\n')
            for i, x in enumerate(el):
                if not p.match(x):
                    continue
                exec(el[i])
                el[i] = 'GRUB_CMDLINE_LINUX_DEFAULT="%s intel_iommu=on"' % (
                        ' '.join([x for x in GRUB_CMDLINE_LINUX_DEFAULT.split() if not x.startswith(
                            'intel_iommu=')]))
                exec(el[i])
                el[i] = 'GRUB_CMDLINE_LINUX_DEFAULT="%s iommu=pt"' % (
                        ' '.join([x for x in GRUB_CMDLINE_LINUX_DEFAULT.split() if not x.startswith(
                            'iommu=')]))
                exec(el[i])
                with open('%s/grub' % self._temp_dir_name, 'w') as f:
                    f.write('\n'.join(el))
                    f.flush()
                local('sudo mv %s/grub /etc/default/grub' %
                      self._temp_dir_name)
                local('sudo /usr/sbin/update-grub')
                break

    def setup_sriov_vfs(self):
        # Set the required number of Virtual Functions for given interfaces
        if self.pdist != 'Ubuntu':
            log.info("Not configuring VF's for %s distribution", self.pdist)
            return

        sriov_string = self._args.sriov
        if sriov_string:
            intf_list = sriov_string.split(",")
            for intf_details in intf_list:
                info = intf_details.split(":")
                str = 'echo %s > ' % info[1]
                str += '/sys/class/net/%s/device/sriov_numvfs;' % info[0]
                str += 'sleep 2; ifup -a'
                # Do nothing if the entry already present in /etc/rc.local
                if local('sudo grep -w \'%s\' /etc/rc.local' % str,
                         warn_only=True).succeeded:
                        continue

                sed = 'sudo sed -i \'/^\s*exit/i ' + str + '\' /etc/rc.local'
                local(sed, warn_only=True)

    def setup_vrouter_kmod_hugepage_grub(self):
        if self.pdist != 'Ubuntu':
            log.info("Not configuring vrouter kmod hugepages Grub changes for %s distribution",
                     self.pdist)
            return

        with open('/etc/default/grub', 'r') as f:
            gcnf = f.read()
            p = re.compile('\s*GRUB_CMDLINE_LINUX_DEFAULT')
            el = gcnf.split('\n')
            for i, x in enumerate(el):
                if not p.match(x):
                    continue
                exec(el[i])
                el[i] = 'GRUB_CMDLINE_LINUX_DEFAULT="%s "' % (
                        ' '.join([x for x in GRUB_CMDLINE_LINUX_DEFAULT.split() if not (x.startswith(
                              'default_hugepagesz') or x.startswith('hugepagesz')
                              or x.startswith('hugepages'))]))
                exec(el[i])
                if self._args.vrouter_1G_hugepages != '0':
                    el[i] = 'GRUB_CMDLINE_LINUX_DEFAULT="%s default_hugepagesz=1G"' % (
                            ' '.join([x for x in GRUB_CMDLINE_LINUX_DEFAULT.split() if not x.startswith(
                                'default_hugepagesz=')]))
                    exec(el[i])
                    el[i] = 'GRUB_CMDLINE_LINUX_DEFAULT="%s hugepagesz=1G"' % (
                            ' '.join([x for x in GRUB_CMDLINE_LINUX_DEFAULT.split() if not x.startswith(
                                'hugepagesz=1')]))
                    exec(el[i])
                    el[i] = 'GRUB_CMDLINE_LINUX_DEFAULT="%s hugepages=%s"' % (
                            ' '.join([x for x in GRUB_CMDLINE_LINUX_DEFAULT.split() if not x.startswith(
                                'hugepages=')]), self._args.vrouter_1G_hugepages)
                    exec(el[i])
                if self._args.vrouter_2M_hugepages != '0':
                    el[i] = 'GRUB_CMDLINE_LINUX_DEFAULT="%s hugepagesz=2M"' % (
                            ' '.join([x for x in GRUB_CMDLINE_LINUX_DEFAULT.split() if not x.startswith(
                                'hugepagesz=2')]))
                    exec(el[i])
                    el[i] = 'GRUB_CMDLINE_LINUX_DEFAULT="%s hugepages_2M=%s"' % (
                            ' '.join([x for x in GRUB_CMDLINE_LINUX_DEFAULT.split() if not x.startswith(
                                'hugepages_2M')]), self._args.vrouter_2M_hugepages)
                    el[i] = el[i].replace('hugepages_2M', 'hugepages')
                    exec(el[i])

                with open('%s/grub' % self._temp_dir_name, 'w') as f:
                    f.write('\n'.join(el))
                    f.flush()
                local('sudo mv %s/grub /etc/default/grub' %
                      self._temp_dir_name)
                local('sudo /usr/sbin/update-grub', warn_only=True)
                break

    def disable_iptables(self):
        # Disable iptables
        if self.pdist not in ['Ubuntu']:
            local("sudo chkconfig iptables off", warn_only=True)
        local("sudo iptables --flush", warn_only=True)
        if self.pdist == 'redhat' or \
           self.pdist == 'centos' and self.pdistversion.startswith('7'):
            local("sudo service iptables stop", warn_only=True)
            local("sudo service ip6tables stop", warn_only=True)
            local("sudo systemctl stop firewalld", warn_only=True)
            local("sudo systemctl status firewalld", warn_only=True)
            local("sudo chkconfig firewalld off", warn_only=True)
            local("sudo /usr/libexec/iptables/iptables.init stop",
                  warn_only=True)
            local("sudo /usr/libexec/iptables/ip6tables.init stop",
                  warn_only=True)
            local("sudo service iptables save", warn_only=True)
            local("sudo service ip6tables save", warn_only=True)
            local("sudo iptables -L", warn_only=True)

    def enable_kdump(self):
        '''Enable kdump for centos based systems'''
        status = local("sudo chkconfig --list | grep kdump", warn_only=True)
        if status.failed:
            log.warning("Seems kexec-tools is not installed.")
            log.warning("Skipping enable kdump")
            return False
        local("sudo chkconfig kdump on")
        local("sudo service kdump start")
        local("sudo service kdump status")
        local("sudo cat /sys/kernel/kexec_crash_loaded")
        local("sudo cat /proc/iomem | grep Crash")

    def setup_coredump(self):
        # usable core dump
        initf = '/etc/sysconfig/init'
        local("sudo sed '/DAEMON_COREFILE_LIMIT=.*/d' %s > %s.new" %
              (initf, initf), warn_only=True)
        local("sudo mv %s.new %s" % (initf, initf), warn_only=True)

        if self.pdist in ['centos', 'fedora', 'redhat']:
            core_unlim = "echo DAEMON_COREFILE_LIMIT=\"'unlimited'\""
            local("sudo %s >> %s" % (core_unlim, initf))

        # Core pattern
        pattern = 'kernel.core_pattern = /var/crashes/core.%e.%p.%h.%t'
        ip_fwd_setting = 'net.ipv4.ip_forward = 1'
        sysctl_file = '/etc/sysctl.conf'
        log.info(pattern)
        cmd = "sudo grep -q '%s' /etc/sysctl.conf || " % pattern
        cmd += "sudo echo '%s' >> /etc/sysctl.conf" % pattern
        local(cmd, warn_only=True)
        local("sudo sed 's/net.ipv4.ip_forward.*/%s/g' %s > /tmp/sysctl.new" %
              (ip_fwd_setting, sysctl_file), warn_only=True)
        local("sudo mv /tmp/sysctl.new %s" % (sysctl_file), warn_only=True)
        local("sudo rm /tmp/sysctl.new", warn_only=True)
        local('sudo sysctl -p', warn_only=True)
        local('sudo mkdir -p /var/crashes', warn_only=True)
        local('sudo chmod 777 /var/crashes', warn_only=True)

        try:
            if self.pdist in ['fedora', 'centos', 'redhat']:
                self.enable_kernel_core()
            if self.pdist == 'Ubuntu':
                self.setup_crashkernel_params()
        except Exception as e:
            log.warning("Ignoring failure kernel core dump")

        try:
            if self.pdist in ['fedora', 'centos', 'redhat']:
                self.enable_kdump()
        except Exception as e:
            log.warning("Ignoring failure when enabling kdump")
            log.warning("Exception: %s", str(e))

    def set_config(self, fl, sec, var, val=''):
        local("sudo contrail-config --set %s %s %s '%s'" % (fl, sec, var, val),
              warn_only=True)

    def del_config(self, fl, sec, var=''):
        local("sudo contrail-config --del %s %s %s" % (fl, sec, var),
              warn_only=True)

    def get_config(self, fl, sec, var=''):
        output = None
        output = local("sudo contrail-config --get %s %s %s" % (fl, sec, var),
                       capture=True, warn_only=True)
        return output

    def has_config(self, fl, sec, var=''):
        output = local("sudo contrail-config --get %s %s %s" % (fl, sec, var),
                       capture=True, warn_only=True)
        return output.succeeded

    def setup(self):
        self.disable_selinux()
        self.disable_iptables()
        self.setup_coredump()
        self.fixup_config_files()
        self.setup_sriov_grub()
        self.setup_sriov_vfs()
        self.run_services()
