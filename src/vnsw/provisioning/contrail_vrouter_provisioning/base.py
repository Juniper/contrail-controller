#!/usr/bin/env python
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
"""Base Contrail Provisioning module."""

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
        self.hostname = socket.gethostname()

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
        if self.pdistversion == '14.04':
            kcmd += "/etc/default/grub.d/kexec-tools.cfg"
            local(kcmd, warn_only=True)
            cmd = "[ -f /etc/default/kdump-tools ] && "
            cmd += "sudo sed -i 's/USE_KDUMP=0/USE_KDUMP=1/' "
            cmd += "/etc/default/kdump-tools"
            local(cmd, warn_only=True)
        else:
            kcmd += "/etc/grub.d/10_linux"
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
                        ' '.join(filter(lambda x: not x.startswith(
                            'crashkernel='), GRUB_CMDLINE_LINUX.split())))
                exec(el[i])
                el[i] = 'GRUB_CMDLINE_LINUX="%s kvm-intel.nested=1"' % (
                        ' '.join(filter(lambda x: not x.startswith(
                            'kvm-intel.nested='), GRUB_CMDLINE_LINUX.split())))

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

    def setup_sriov_grub(self):
        if not self._args.sriov:
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
                        ' '.join(filter(lambda x: not x.startswith(
                            'intel_iommu='),
                            GRUB_CMDLINE_LINUX_DEFAULT.split())))
                exec(el[i])
                el[i] = 'GRUB_CMDLINE_LINUX_DEFAULT="%s iommu=pt"' % (
                        ' '.join(filter(lambda x: not x.startswith(
                            'iommu='), GRUB_CMDLINE_LINUX_DEFAULT.split())))
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
        self.run_services()


class ContrailUpgrade(object):
    def __init__(self):
        self.upgrade_data = {
            'upgrade': [],
            'remove': [],
            'downgrade': [],
            'ensure': [],
            'backup': ['/etc/contrail'],
            'restore': [],
            'remove_config': [],
            'rename_config': [],
            'replace': [],
        }

    def _parse_args(self, args_str):
        '''
            Base parser.
        '''
        # Source any specified config/ini file
        # Turn off help, so we print all options in response to -h
        conf_parser = argparse.ArgumentParser(add_help=False)

        conf_parser.add_argument("-c", "--conf_file",
                                 help="Specify config file", metavar="FILE")
        conf_parser.add_argument(
                "-F", "--from_rel", type=LooseVersion,
                help="Release of contrail software installed in the node")
        conf_parser.add_argument(
                "-T", "--to_rel", type=LooseVersion,
                help="Release of contrail software to be upgraded in the node")
        conf_parser.add_argument(
                "-P", "--packages", nargs='+', type=str,
                help="List of packages to be upgraded.")
        conf_parser.add_argument(
                "-R", "--roles", nargs='+', type=str,
                help="List of contrail roles provisioned in this node.")
        args, self.remaining_argv = conf_parser.parse_known_args(
                args_str.split())

        if args.roles:
            self.global_defaults.update({'roles': args.roles})
        if args.packages:
            self.global_defaults.update({'packages': args.packages})
        if args.from_rel:
            self.global_defaults.update({'from_rel': args.from_rel})
        if args.to_rel:
            self.global_defaults.update({'to_rel': args.to_rel})

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

        parser.set_defaults(**self.global_defaults)

        return parser

    def _upgrade_package(self):
        if not self.upgrade_data['upgrade']:
            return
        pkgs = ' '.join(self.upgrade_data['upgrade'])
        if self.pdist in ['Ubuntu']:
            if (self._args.from_rel >= LooseVersion('2.20') and
                    self._args.from_rel < LooseVersion('3.00') and
                    self._args.to_rel >= LooseVersion('3.00')):
                cmd = 'DEBIAN_FRONTEND=noninteractive apt-get -y --force-yes'
                cmd += ' -o Dpkg::Options::="--force-overwrite"'
                cmd += ' -o Dpkg::Options::="--force-confmiss"'
                cmd += ' -o Dpkg::Options::="--force-confnew"'
                cmd += ' install %s' % pkgs
            else:
                cmd = 'DEBIAN_FRONTEND=noninteractive apt-get -y --force-yes'
                cmd += ' -o Dpkg::Options::="--force-overwrite"'
                cmd += ' -o Dpkg::Options::="--force-confnew"'
                cmd += ' install %s' % pkgs
        else:
            local('sudo yum clean all')
            cmd = 'sudo yum -y --disablerepo=* --enablerepo=contrail*'
            cmd += ' install %s' % pkgs
        local(cmd)

    def _backup_config(self):
        self.backup_dir = "/var/tmp/contrail-%s-%s-upgradesave" % \
                           (self._args.to_rel, self.get_build().split('~')[0])

        for backup_elem in self.upgrade_data['backup']:
            backup_config = self.backup_dir + backup_elem
            if not os.path.exists(backup_config):
                log.info("Backing up %s at: %s" % (backup_elem, backup_config))
                backup_dir = os.path.dirname(os.path.abspath(backup_config))
                if not os.path.exists(backup_dir):
                    os.makedirs(backup_dir)
                if os.path.isfile(backup_elem):
                    shutil.copy2(backup_elem, backup_config)
                elif os.path.isdir(backup_elem):
                    local('sudo cp -rfp %s %s' % (backup_elem, backup_config))
                else:
                    log.warn("[%s] is not present, no need to backup" %
                             backup_elem)
            else:
                log.info("Already the config dir %s is backed up at %s." %
                         (backup_elem, backup_config))

    def _restore_config(self):
        for restore_elem in self.upgrade_data['restore']:
            restore_config = self.backup_dir + restore_elem
            log.info("Restoring %s to: %s" % (restore_config, restore_elem))
            if os.path.isfile(restore_config):
                shutil.copy2(restore_config, restore_elem)
            elif os.path.isdir(restore_config):
                local('sudo cp -rfp %s %s' % ('%s/*' % restore_config,
                      restore_elem))
            else:
                log.warn("[%s] is not backed up, no need to restore" %
                         restore_elem)

    def _downgrade_package(self):
        if not self.upgrade_data['downgrade']:
            return
        pkgs = ' '.join(self.upgrade_data['downgrade'])
        if self.pdist in ['Ubuntu']:
            cmd = 'DEBIAN_FRONTEND=noninteractive apt-get -y --force-yes'
            cmd += ' -o Dpkg::Options::="--force-overwrite"'
            cmd += ' -o Dpkg::Options::="--force-confnew"'
            cmd += ' install %s' % pkgs
        else:
            cmd = 'sudo yum -y --nogpgcheck --disablerepo=*'
            cmd += ' --enablerepo=contrail* install %s' % pkgs
        local(cmd)

    def _remove_package(self):
        if not self.upgrade_data['remove']:
            return
        pkgs = ' '.join(self.upgrade_data['remove'])
        if self.pdist in ['Ubuntu']:
            local('sudo DEBIAN_FRONTEND=noninteractive apt-get -y remove --purge\
                   %s' % pkgs)
        else:
            local('sudo rpm -e --nodeps %s' % pkgs)

    def _replace_package(self):
        if not self.upgrade_data['replace']:
            return

        rem_pkgs = ' '.join([x for (x, y) in self.upgrade_data['replace']])
        add_pkgs = ' '.join([y for (x, y) in self.upgrade_data['replace']])
        if self.pdist in ['Ubuntu']:
            local('sudo DEBIAN_FRONTEND=noninteractive apt-get -y remove --purge\
                       %s' % rem_pkgs, warn_only=True)
            local('sudo DEBIAN_FRONTEND=noninteractive apt-get -y install --reinstall\
                   %s' % add_pkgs)
        else:
            local('sudo rpm -e --nodeps %s' % rem_pkgs, warn_only=True)
            cmd = 'sudo yum -y --nogpgcheck --disablerepo=*'
            cmd += ' --enablerepo=contrail* install %s' % add_pkgs
            local(cmd)

    def _ensure_package(self):
        if not self.upgrade_data['ensure']:
            return
        pkgs = ' '.join(self.upgrade_data['ensure'])
        log.debug(pkgs)

    def _remove_config(self):
        for remove_config in self.upgrade_data['remove_config']:
            if os.path.isfile(remove_config):
                os.remove(remove_config)
            elif os.path.isdir(remove_config):
                shutil.rmtree(remove_config)
            else:
                log.warn("[%s] is not present, no need to remove" %
                         remove_config)

    def _rename_config(self):
        for src, dst in self.upgrade_data['rename_config']:
            if os.path.isfile(src):
                shutil.copy2(src, dst)
                os.remove(src)
            elif os.path.isdir(src):
                local('sudo cp -rfp %s %s' % src, dst)
                shutil.rmtree(src)
            else:
                log.warn("[%s] is not present, no need to rename" % src)

    def get_build(self, pkg='contrail-install-packages'):
        pkg_rel = None
        if self.pdist in ['centos', 'redhat']:
            cmd = "sudo rpm -q --queryformat '%%{RELEASE}' %s" % pkg
        elif self.pdist in ['Ubuntu']:
            cmd = "sudo dpkg -s %s | grep Version: | cut -d' ' -f2 |"
            cmd += " sudo cut -d'-' -f2" % pkg
        pkg_rel = local(cmd, capture=True)
        if 'is not installed' in pkg_rel or 'is not available' in pkg_rel:
            log.info("Package %s not installed." % pkg)
            return None
        return pkg_rel

    def disable_apt_get_auto_start(self):
        if self.pdist in ['Ubuntu']:
            with open("/usr/sbin/policy-rc.d", "w+") as f:
                f.write('#!/bin/sh\n')
                f.write('exit 101\n')
                f.close()
            h = os.stat("/usr/sbin/policy-rc.d")
            os.chmod("/usr/sbin/policy-rc.d", h.st_mode | stat.S_IEXEC)

    def enable_apt_get_auto_start(self):
        if self.pdist in ['Ubuntu']:
            local('sudo rm -f /usr/sbin/policy-rc.d')

    def _upgrade(self):
        self._backup_config()
        if self.pdist in ['centos']:
            self._remove_package()
        self._ensure_package()
        self._downgrade_package()
        self._upgrade_package()
        if self.pdist in ['Ubuntu']:
            self._remove_package()
        self._replace_package()
        self._restore_config()
        self._rename_config()
        self._remove_config()
