#!/usr/bin/python
#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#

import os
import sys
import argparse
import ConfigParser
from setup import TorAgentSetup
from contrail_provisioning.common.base import ContrailSetup

class TorAgentBaseCleanup(ContrailSetup):
    def __init__(self, tor_agent_args, args_str=None):
        super(TorAgentBaseCleanup, self).__init__()
        self._args = tor_agent_args

    def remove_ssl_certs(self):
        self.ssl_cert = '/etc/contrail/ssl/certs/tor.' + self.args_tor_id + '.cert.pem'
        self.ssl_privkey = '/etc/contrail/ssl/private/tor.' + self.args_tor_id + '.privkey.pem'
        # Remove ssl certs once
        if os.path.exists(self.ssl_cert):
            os.remove(self.ssl_cert)
        if os.path.exists(self.ssl_privkey):
            os.remove(self.ssl_privkey)

    def remove_tor_agent_conf(self):
        self.tor_file_name = 'contrail-tor-agent-' + self.args_tor_id + '.conf'
        os.remove("/etc/contrail/%s" % self.tor_file_name)

    def remove_tor_ini(self):
        self.tor_process_name = 'contrail-tor-agent-' + self.args_tor_id
        local("sudo service %s stop" % self.tor_process_name)
        self.tor_ini_file_name = self.tor_process_name + '.ini'
        os.remove("/etc/contrail/supervisord_vrouter_files/%s" % self.tor_ini_file_name)

    def remove_init_file(self):
        os.remove("/etc/init.d/%s" % self.tor_process_name)

    def del_vnc_config(self):
        cmd = "sudo python /opt/contrail/utils/provision_vrouter.py"
        cmd += " --host_name %s" % self.agent_name
        cmd += " --host_ip %s" % self._args.self_ip
        cmd += " --api_server_ip %s" % self._args.cfgm_ip
        cmd += " --admin_user %s" % self._args.admin_user
        cmd += " --admin_password %s"  % self._args.admin_password
        cmd += " --admin_tenant_name %s" % self._args.admin_tenant
        cmd += " --openstack_ip %s" % self._args.authserver_ip
        cmd += " --oper del "
        local(cmd)

    def del_physical_device(self):
        cmd = "sudo python /opt/contrail/utils/provision_physical_device.py"
        cmd += " --device_name %s" % self.args_tor_name
        cmd += " --vendor_name %s" % self.args_tor_vendor_name
        cmd += " --api_server_ip %s" % self._args.cfgm_ip
        cmd += " --admin_user %s"  % self._args.admin_user
        cmd += " --admin_password %s" % self._args.admin_password
        cmd += " --admin_tenant_name %s" % self._args.admin_tenant
        cmd += " --openstack_ip %s" % self._args.authserver_ip
        cmd += " --oper del"
        local(cmd)

    def stop_services(self):
        if self._args.restart:
            local("sudo supervisorctl -c /etc/contrail/supervisord_vrouter.conf update")

    def cleanup(self):
            self.agent_name = self.args_tor_agent_name
            self.remove_ssl_certs()
            self.remove_tor_agent_conf()
            self.remove_tor_ini()
            self.remove_init_file()
            self.del_vnc_config()
            self.del_physical_device()
            self.stop_services()


def main(args_str = None):
    tor_agent_args = TorAgentSetup(args_str)._args
    tor_agent = TorAgentBaseCleanup(tor_agent_args)
    tor_agent.cleanup()

if __name__ == "__main__":
    main()
