#!/usr/bin/python
#
# Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
#
"""Upgrade's Contrail Compute components."""

from distutils.version import LooseVersion

from contrail_vrouter_provisioning import local
from contrail_vrouter_provisioning.setup import ComputeSetup
from contrail_vrouter_provisioning.base import ContrailUpgrade
from contrail_vrouter_provisioning.common import ComputeBaseSetup


class ComputeUpgrade(ContrailUpgrade, ComputeSetup):
    def __init__(self, args_str=None):
        ContrailUpgrade.__init__(self)
        ComputeSetup.__init__(self)

        self.compute_setup = ComputeBaseSetup(self._args)

        self.update_upgrade_data()

    def update_upgrade_data(self):
        self.upgrade_data['upgrade'] = self._args.packages

        self.upgrade_data['restore'] += [
                '/etc/contrail/agent_param',
                '/etc/contrail/contrail-vrouter-agent.conf',
                '/etc/contrail/vrouter_nodemgr_param',
                                        ]

        if (self._args.from_rel >= LooseVersion('2.20')):
            self.upgrade_data['restore'].append(
                    '/etc/contrail/contrail-vrouter-nodemgr.conf')

    def upgrade(self):
        self.disable_apt_get_auto_start()
        self._upgrade()
        if ((self.pdist not in ['Ubuntu']) and
            ('running' in local('sudo service supervisor-vrouter status',
                                capture=True))):
            local("sudo service supervisor-vrouter stop")
        # Seperate contrail-<role>-nodemgr.conf is introduced from release 2.20
        if (self._args.from_rel < LooseVersion('2.20') and
                self._args.to_rel >= LooseVersion('2.20')):
            self.compute_setup.fixup_contrail_vrouter_nodemgr()
        self.enable_apt_get_auto_start()


def main():
    compute = ComputeUpgrade()
    compute.upgrade()

if __name__ == "__main__":
    main()
