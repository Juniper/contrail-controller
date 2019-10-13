#!/usr/bin/python
#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

from __future__ import absolute_import
from future import standard_library
standard_library.install_aliases()
import os
import sys
import argparse
from six.moves import configparser
import platform
import re
import subprocess
import signal
import logging

from contrail_vrouter_provisioning import local
from .setup import TorAgentSetup
from contrail_vrouter_provisioning.base import ContrailSetup
from distutils.version import LooseVersion

(PLATFORM, VERSION, EXTRA) = platform.linux_distribution()

log = logging.getLogger('contrail_vrouter_provisioning.common')

class TorAgentBaseCleanup(ContrailSetup):
    def __init__(self, tor_agent_args, args_str=None):
        super(TorAgentBaseCleanup, self).__init__()
        self._args = tor_agent_args
        self.tor_name = ""
        self.tor_vendor_name = ""
        self.tor_agent_name = ""
        self.tor_id_list_old = []
        # if the non_mgmt_ip is set use this as control ip
        if self._args.non_mgmt_ip:
            self._args.self_ip = self._args.non_mgmt_ip
        if (('ubuntu' in PLATFORM.lower()) and
            (LooseVersion(VERSION) >= LooseVersion('16.04'))):
            self.systemd_setup = True
            self.dirname = '/lib/systemd/system/'
            self.suffix = '.service'
        else:
            self.systemd_setup = False
            self.dirname = '/etc/contrail/supervisord_vrouter_files/'
            self.suffix = '.ini'

    def remove_tor_agent_conf_files(self, tor_id):
        ssl_cert = '/etc/contrail/ssl/certs/tor.' + tor_id + '.cert.pem'
        ssl_privkey = '/etc/contrail/ssl/private/tor.' + tor_id + '.privkey.pem'
        # Remove ssl certs once
        if os.path.exists(ssl_cert):
            os.remove(ssl_cert)
        if os.path.exists(ssl_privkey):
            os.remove(ssl_privkey)
        self.tor_file_name = '/etc/contrail/contrail-tor-agent-' + tor_id + '.conf'
        if os.path.exists(self.tor_file_name):
            os.remove(self.tor_file_name)
        self.tor_process_name = 'contrail-tor-agent-' + tor_id
        local("sudo service %s stop" % self.tor_process_name)
        if self.systemd_setup:
            local("sudo systemctl disable %s" % self.tor_process_name)

    def remove_tor_agent_ini_files(self, tor_id):
        self.tor_ini_file_name = self.dirname + self.tor_process_name + self.suffix
        if os.path.exists(self.tor_ini_file_name):
            os.remove(self.tor_ini_file_name)
        initd_file = '/etc/init.d/' + self.tor_process_name
        if os.path.exists(initd_file):
            os.remove(initd_file)

    def del_vnc_config(self):
        cmd = "sudo python /opt/contrail/utils/provision_vrouter.py"
        cmd += " --host_name %s" % self.tor_agent_name
        cmd += " --host_ip %s" % self._args.self_ip
        cmd += " --api_server_ip %s" % self._args.cfgm_ip
        cmd += " --admin_user %s" % self._args.admin_user
        cmd += " --admin_password %s"  % self._args.admin_password
        cmd += " --admin_tenant_name %s" % self._args.admin_tenant_name
        cmd += " --openstack_ip %s" % self._args.authserver_ip
        cmd += " --oper del "
        local(cmd)

    def del_physical_device(self):
        cmd = "sudo python /opt/contrail/utils/provision_physical_device.py"
        cmd += " --device_name %s" % self.tor_name
        cmd += " --vendor_name %s" % self.tor_vendor_name
        cmd += " --api_server_ip %s" % self._args.cfgm_ip
        cmd += " --admin_user %s"  % self._args.admin_user
        cmd += " --admin_password %s" % self._args.admin_password
        cmd += " --admin_tenant_name %s" % self._args.admin_tenant_name
        cmd += " --openstack_ip %s" % self._args.authserver_ip
        cmd += " --oper del"
        local(cmd)

    def stop_services(self):
        if self._args.restart:
            if not self.systemd_setup:
                local("sudo supervisorctl -c /etc/contrail/supervisord_vrouter.conf update")
            try:
                vrouter_nodemgr_pid = [line.split()[1] for line in \
                                        subprocess.check_output("ps -eaf".split()).split("\n") \
                                        if '--nodetype=contrail-vrouter' in line][0]
                os.kill(int(vrouter_nodemgr_pid), signal.SIGHUP)
            except:
                log.warning("contrail-vrouter-nodemgr is not running")

    def cleanup(self, tor_id):
        self.remove_tor_agent_conf_files(tor_id)
        self.remove_tor_agent_ini_files(tor_id)
        self.del_physical_device()
        self.del_vnc_config()

    def get_tor_id_list_old(self):
        for file in os.listdir(self.dirname):
            if file.startswith('contrail-tor-agent'):
                tor_id_old = re.findall(r'\d+', file)
                self.tor_id_list_old.append(tor_id_old[0])

    def delete_torid_config(self, tor_id):
        tor_conf_file = '/etc/contrail/contrail-tor-agent-' + tor_id + '.conf'
        config = configparser.SafeConfigParser()
        config.read(tor_conf_file)
        self.tor_name = config.get('TOR', 'tor_name')
        self.tor_vendor_name = config.get('TOR', 'tor_vendor_name')
        self.tor_agent_name = config.get('DEFAULT', 'agent_name')
        self.cleanup(tor_id)

    def audit(self):
        flag = False
        self.get_tor_id_list_old()
        for tor_id_old in self.tor_id_list_old:
            if self._args.tor_id_list is None or \
               not (tor_id_old in self._args.tor_id_list):
                self.delete_torid_config(tor_id_old)
                flag = True
        if flag:
            self.stop_services()

def main(args_str = None):
    tor_agent_args = TorAgentSetup(args_str)._args
    tor_agent = TorAgentBaseCleanup(tor_agent_args)
    tor_agent.audit()

if __name__ == "__main__":
    main()
