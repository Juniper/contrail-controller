# vim: set expandtab shiftwidth=4 fileencoding=UTF-8:

# Copyright 2016 Netronome Systems, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from __future__ import absolute_import

import os
import random
import tempfile

from netronome.vrouter import (config, pci)

if 'NS_VROUTER_SHORT_TMP_PREFIXES' in os.environ:
    _TMP_PREFIX = 'config.'
else:
    _TMP_PREFIX = 'netronome.vrouter.tests.unit.test_config.'


def _random_pci_address():
    return pci.PciAddress(
        random.randint(0, 65535),
        random.randint(0, 255),
        random.randint(0, 255),
        random.randint(0, 15),
    )


# Wrapper to emulate the old select_plug_mode_for_port() API for the sake of
# reusing its unit test code. This won't exercise the image metadata/instance
# type (flavor) metadata parsing code (by default), but fortunately that has
# pretty good unit test coverage of its own.
def _select_plug_mode_for_port(
    session, neutron_port, vf_pool, hw_acceleration_modes, vif_type, virt_type,
    root_dir, reservation_timeout=None, hw_acceleration_mode=None,
    glance_allowed_modes=None, flavor_allowed_modes=None
):
    port_modes = config.calculate_acceleration_modes_for_port(
        neutron_port, vif_type, virt_type, glance_allowed_modes,
        flavor_allowed_modes, hw_acceleration_mode
    )
    port_modes = config.apply_compute_node_restrictions(
        neutron_port, port_modes, hw_acceleration_modes, _root_dir=root_dir
    )
    return config.set_acceleration_mode_for_port(
        session, neutron_port, port_modes[0], vf_pool, reservation_timeout
    )


class FakeSysfs(object):
    """
    A fake sysfs to control acceleration parameters. (Early versions of the
    tests didn't have this and hence were inadvertently dependent on the host
    environment).
    """

    def __init__(
        self, physical_vif_count=None, nfp_status=None, _root_dir=None
    ):
        if _root_dir is None:
            self.root_dir = os.path.join(
                tempfile.mkdtemp(prefix=_TMP_PREFIX), 'FakeSysfs'
            )
        else:
            self.root_dir = _root_dir

        control = os.path.join(self.root_dir, 'sys/module/nfp_vrouter/control')
        os.makedirs(control)

        self.physical_vif_count_fname = os.path.join(
            control, 'physical_vif_count'
        )
        self.nfp_status_fname = os.path.join(control, 'nfp_status')

        if physical_vif_count is not None:
            self.set_physical_vif_count(physical_vif_count)
        if nfp_status is not None:
            self.set_nfp_status(nfp_status)

    def set_physical_vif_count(self, physical_vif_count):
        assert isinstance(physical_vif_count, int)
        with open(self.physical_vif_count_fname, 'w') as fh:
            print >>fh, physical_vif_count

    def set_nfp_status(self, nfp_status):
        assert isinstance(nfp_status, int)
        with open(self.nfp_status_fname, 'w') as fh:
            print >>fh, nfp_status
