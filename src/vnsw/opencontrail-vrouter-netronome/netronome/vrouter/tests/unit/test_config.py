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

import copy
import logging
import os
import random
import sys
import tempfile
import unittest
import uuid

from netronome.vrouter import (
    config, config_editor, config_opts, database, fallback, flavor, glance,
    pci, plug_modes as PM, port, vf
)
from netronome.vrouter.config import AccelerationModeConflict
from netronome.vrouter.sa.helpers import one_or_none
from netronome.vrouter.tests.helpers.config import (
    _random_pci_address, _select_plug_mode_for_port, FakeSysfs, _TMP_PREFIX
)
from netronome.vrouter.tests.helpers.vf import (fake_FallbackMap)
from netronome.vrouter.tests.unit import *

from ConfigParser import ConfigParser
from lxml import etree
from nova.virt.libvirt.config import LibvirtConfigGuestInterface
from sqlalchemy.orm.session import sessionmaker

_VROUTER_LOGGER = make_getLogger('netronome.vrouter')
_CONFIG_LOGGER = make_getLogger('netronome.vrouter.config')
_VF_LOGGER = make_getLogger('netronome.vrouter.vf')


def _CONFIG_LMC(cls=LogMessageCounter):
    return attachLogHandler(_CONFIG_LOGGER(), cls())


def _VROUTER_LMC(cls=LogMessageCounter):
    return attachLogHandler(_VROUTER_LOGGER(), cls())


def _ROOT_LMC(cls=LogMessageCounter):
    return attachLogHandler(logging.root, cls())


def _test_data(tweak=None):
    u = str(uuid.uuid1())
    tap_name = ('nic' + u)[:14]
    ans = {
        'uuid': u,
        'instance_uuid': 'dda746d1-d4bf-4b04-b57b-2999403b5b01',
        'vn_uuid': '2d624ac9-6156-421f-9550-07d46d823f1c',
        'vm_project_uuid': '55cb77ce-5eb1-49ca-b4fd-5bff809aba35',
        'ip_address': '0.0.0.0',
        'ipv6_address': None,
        'vm_name': 'Test VM #1',
        'mac': '11:22:33:44:55:66',
        'tap_name': tap_name,
        'port_type': 'NovaVMPort',
        'vif_type': 'Vrouter',
        'rx_vlan_id': -1,
        'tx_vlan_id': -1,
    }
    if tweak:
        tweak(ans)
    return ans


class TestMixedModeSelectionAPIs(unittest.TestCase):
    def test_calculate_acceleration_modes_for_port(self):
        gmodes = glance.allowed_hw_acceleration_modes
        fmodes = flavor.allowed_hw_acceleration_modes

        test_data = (
            # SR-IOV not permitted by Glance metadata.
            {
                'ans': (PM.VirtIO, PM.unaccelerated),
            },

            # SR-IOV permitted by Glance metadata.
            {
                'glance_allowed_modes': gmodes({
                    glance.HW_ACCELERATION_FEATURES_PROPERTY:
                        glance.SRIOV_FEATURE_TOKEN
                }),
                'ans': (PM.SRIOV, PM.VirtIO, PM.unaccelerated),
            },

            # SR-IOV not permitted by Glance metadata, flavor restricted to
            # accelerated modes.
            {
                'flavor_allowed_modes': fmodes({
                    flavor.HW_ACCELERATION_PROPERTY: 'on'
                }),
                'ans': (PM.VirtIO,),
            },

            # SR-IOV permitted by Glance metadata, flavor restricted to
            # accelerated modes.
            {
                'glance_allowed_modes': gmodes({
                    glance.HW_ACCELERATION_FEATURES_PROPERTY:
                        glance.SRIOV_FEATURE_TOKEN
                }),
                'flavor_allowed_modes': fmodes({
                    flavor.HW_ACCELERATION_PROPERTY: 'on'
                }),
                'ans': (PM.SRIOV, PM.VirtIO,),
            },

            # SR-IOV not permitted by Glance metadata, flavor restricted to
            # non-accelerated modes.
            {
                'flavor_allowed_modes': fmodes({
                    flavor.HW_ACCELERATION_PROPERTY: 'off'
                }),
                'ans': (PM.unaccelerated,),
            },

            # SR-IOV permitted by Glance metadata, flavor restricted to
            # non-accelerated modes.
            {
                'glance_allowed_modes': gmodes({
                    glance.HW_ACCELERATION_FEATURES_PROPERTY:
                        glance.SRIOV_FEATURE_TOKEN
                }),
                'flavor_allowed_modes': fmodes({
                    flavor.HW_ACCELERATION_PROPERTY: 'off'
                }),
                'ans': (PM.unaccelerated,),
            },

            # LXC/Docker instance.
            {
                'glance_allowed_modes': None,
                'flavor_allowed_modes': None,
                'virt_type': 'lxc',
                'ans': (PM.unaccelerated,),
                'logs': {_CONFIG_LOGGER.name: {'INFO': 1}},
            },

            # DPDK vRouter port.
            {
                'glance_allowed_modes': None,
                'flavor_allowed_modes': None,
                'vif_type': 'vhostuser',
                'ans': (PM.unaccelerated,),
                'logs': {_CONFIG_LOGGER.name: {'INFO': 1}},
            },

            # LXC/Docker instance, flavor requires acceleration.
            {
                'flavor_allowed_modes': fmodes({
                    flavor.HW_ACCELERATION_PROPERTY: 'on'
                }),
                'virt_type': 'lxc',
                'logs': {_CONFIG_LOGGER.name: {'INFO': 3, 'ERROR': 1}},
                'raises': self.assertRaises(AccelerationModeConflict),
            },

            # DPDK vRouter port, flavor requires acceleration.
            {
                'flavor_allowed_modes': fmodes({
                    flavor.HW_ACCELERATION_PROPERTY: 'on'
                }),
                'vif_type': 'vhostuser',
                'logs': {_CONFIG_LOGGER.name: {'INFO': 3, 'ERROR': 1}},
                'raises': self.assertRaises(AccelerationModeConflict),
            },

            # LXC/Docker instance, --hw-acceleration-mode=SR-IOV.
            {
                'hw_acceleration_mode': PM.SRIOV,
                'virt_type': 'lxc',
                'logs': {_CONFIG_LOGGER.name: {'INFO': 3, 'ERROR': 1}},
                'raises': self.assertRaises(AccelerationModeConflict),
            },

            # DPDK vRouter port, --hw-acceleration-mode=SR-IOV.
            {
                'hw_acceleration_mode': PM.SRIOV,
                'vif_type': 'vhostuser',
                'logs': {_CONFIG_LOGGER.name: {'INFO': 3, 'ERROR': 1}},
                'raises': self.assertRaises(AccelerationModeConflict),
            },

            # Image metadata causes a conflict.
            {
                'glance_allowed_modes': (PM.VirtIO,),
                'vif_type': 'vhostuser',
                'logs': {_CONFIG_LOGGER.name: {'INFO': 3, 'ERROR': 1}},
                'raises': self.assertRaises(AccelerationModeConflict),
            },
        )

        for td in test_data:
            neutron_port = uuid.uuid1()
            d = {
                'neutron_port': neutron_port,
                'vif_type': None,
                'virt_type': None,
                'glance_allowed_modes': gmodes({}),
                'flavor_allowed_modes': fmodes({}),
            }
            d.update(td)
            expected_port_modes = d.pop('ans', ())
            expected_logs = d.pop('logs', {})
            expected_exception = d.pop('raises', None)

            with _CONFIG_LMC() as lmc:
                if expected_exception is None:
                    port_modes = config.calculate_acceleration_modes_for_port(
                        **d
                    )
                    self.assertEqual(port_modes, expected_port_modes)
                else:
                    with expected_exception:
                        config.calculate_acceleration_modes_for_port(**d)

                    e = expected_exception.exception
                    if isinstance(e, AccelerationModeConflict):
                        self.assertEqual(e.neutron_port, neutron_port)
                        self.assertIsInstance(e.old_modes, tuple)
                        self.assertIsInstance(e.allowed_modes, tuple)

                self.assertEqual(lmc.count, expected_logs)

    def test_apply_compute_node_restrictions(self):
        sysfs = FakeSysfs()

        SVU = (PM.SRIOV, PM.VirtIO, PM.unaccelerated)
        SV  = (PM.SRIOV, PM.VirtIO)
        U   = (PM.unaccelerated,)
        S   = (PM.SRIOV,)
        V   = (PM.VirtIO,)

        test_data = (
            # Everything working OK.
            {
                'ans': SVU,
            },

            # Acceleration not loaded.
            {
                'ans': U,
                'physical_vif_count': 0,
                'logs': {_CONFIG_LOGGER.name: {'INFO': 1}},
            },

            # Acceleration system reporting an error.
            {
                'ans': U,
                'nfp_status': 0,
                'logs': {_CONFIG_LOGGER.name: {'CRITICAL': 1}},
            },

            # Node configured to require acceleration, but acceleration not
            # loaded.
            {
                'hw_acceleration_modes': SV,
                'ans': U,
                'physical_vif_count': 0,
                'logs': {_CONFIG_LOGGER.name: {'ERROR': 1, 'INFO': 3}},
                'raises': self.assertRaises(AccelerationModeConflict),
            },

            # Node configured to require acceleration, but acceleration system
            # reporting an error.
            {
                'hw_acceleration_modes': SV,
                'ans': U,
                'nfp_status': 0,
                'logs': {_CONFIG_LOGGER.name: {
                    'CRITICAL': 1, 'ERROR': 1, 'INFO': 2
                }},
                'raises': self.assertRaises(AccelerationModeConflict),
            },

            # Everything working OK, but instance requirements conflict with
            # compute node configuration conflict.
            {
                'port_modes': V,
                'hw_acceleration_modes': S,
                'raises': self.assertRaises(AccelerationModeConflict),
                'logs': {_CONFIG_LOGGER.name: {'ERROR': 1, 'INFO': 2}},
            },

            # Compute node configured to allow accelerated or unaccelerated
            # operation, acceleration is broken so compute node is restricted
            # to unaccelerated, but then the instance requires VirtIO or
            # SR-IOV.
            {
                'port_modes': SV,
                'nfp_status': 0,
                'raises': self.assertRaises(AccelerationModeConflict),
                'logs': {_CONFIG_LOGGER.name: {
                    'CRITICAL': 1, 'ERROR': 1, 'INFO': 2
                }},
            },
        )

        for td in test_data:
            neutron_port = uuid.uuid1()
            d = {
                'neutron_port': neutron_port,
                'port_modes': SVU,
                'hw_acceleration_modes': SVU,
                '_root_dir': sysfs.root_dir,
            }
            d.update(td)
            expected_port_modes = d.pop('ans', ())
            expected_logs = d.pop('logs', {})
            expected_exception = d.pop('raises', None)
            sysfs.set_physical_vif_count(d.pop('physical_vif_count', 1))
            sysfs.set_nfp_status(d.pop('nfp_status', 1))

            with _CONFIG_LMC() as lmc:
                if expected_exception is None:
                    port_modes = config.apply_compute_node_restrictions(**d)
                    self.assertEqual(port_modes, expected_port_modes)
                else:
                    with expected_exception:
                        config.apply_compute_node_restrictions(**d)

                    e = expected_exception.exception
                    if isinstance(e, AccelerationModeConflict):
                        self.assertEqual(e.neutron_port, neutron_port)
                        self.assertIsInstance(e.old_modes, tuple)
                        self.assertIsInstance(e.allowed_modes, tuple)

                self.assertEqual(lmc.count, expected_logs)

    def test_calculate_acceleration_modes_for_port_bad_args(self):
        with self.assertRaises(ValueError):
            config.calculate_acceleration_modes_for_port(
                neutron_port='invalid neutron_port!!!',
                vif_type=None,
                virt_type=None,
                glance_allowed_modes=None,
                flavor_allowed_modes=None
            )


class TestPlugMode(unittest.TestCase):
    def test_plug_nonexistent_port(self):
        engine = database.create_engine('tmp')[0]
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        # setup vf pool
        vf_pool = vf.Pool(
            fake_FallbackMap([pci.parse_pci_address('0123:45:67.6')])
        )

        # setup sysfs acceleration file
        sysfs = FakeSysfs()

        # setup config
        hw_acceleration_modes = (PM.SRIOV, PM.unaccelerated,)

        # 1. create a new port and attempt to plug it in SR-IOV mode
        #    a. verify that it gets a plug,
        #       i. that says "unaccelerated", since acceleration is not
        #          enabled,
        #    b. verify that it does NOT get a VF,
        #    c. verify that we get a warning (that acceleration is not enabled)
        td = _test_data()
        u = uuid.UUID(td['uuid'])

        s = Session()
        with _VROUTER_LMC() as lmc:
            _select_plug_mode_for_port(
                s, u, vf_pool, hw_acceleration_modes, vif_type=None,
                virt_type=None, root_dir=sysfs.root_dir
            )

        self.assertEqual(lmc.count, {_CONFIG_LOGGER.name: {'INFO': 1}})

        # NOW retrieve the port (that we created AFTER the plug)
        po = port.Port(**td)
        s.add(po)
        s.commit()
        po = s.query(port.Port).get(u)
        self.assertIsNotNone(po)
        self.assertIsNotNone(po.plug)
        self.assertEqual(po.plug.mode, PM.unaccelerated)

        v = one_or_none(s.query(vf.VF).filter(vf.VF.neutron_port == u))
        self.assertIsNone(v)
        s.commit()

    def test_select_plug_mode_for_port(self):
        engine = database.create_engine('tmp')[0]
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        # setup port
        td = _test_data()
        u = td['uuid']
        po = port.Port(**td)
        s.add(po)
        s.commit()
        del s, po

        # setup vf pool
        vf_pool = vf.Pool(
            fake_FallbackMap([pci.parse_pci_address('0123:45:67.6')])
        )

        # setup sysfs acceleration file
        sysfs = FakeSysfs(nfp_status=1)

        # setup config
        hw_acceleration_modes = (PM.unaccelerated,)

        # 1. port with no prior plug, plug in unaccelerated mode
        #    a. verify that it gets a plug,
        #       i. that says PM.unaccelerated,
        #    b. verify that it does NOT get a vf.
        s = Session()
        po = s.query(port.Port).get(u)
        self.assertIsNotNone(po)

        with _VROUTER_LMC() as lmc:
            _select_plug_mode_for_port(
                s, po.uuid, vf_pool, hw_acceleration_modes, vif_type=None,
                virt_type=None, root_dir=sysfs.root_dir
            )

        self.assertEqual(lmc.count, {_CONFIG_LOGGER.name: {'INFO': 1}})

        self.assertIsNotNone(po.plug)
        self.assertEqual(po.plug.mode, PM.unaccelerated)
        s.commit()

        s = Session()
        pm = s.query(port.PlugMode).get(u)
        self.assertIsNotNone(pm)
        self.assertEqual(pm.mode, PM.unaccelerated)
        po = s.query(port.Port).get(u)
        self.assertEqual(po.plug.mode, PM.unaccelerated)
        v = one_or_none(s.query(vf.VF).filter(vf.VF.neutron_port == u))
        self.assertIsNone(v)
        s.commit()
        del s, po, pm, v

        # 2. create a new port and attempt to plug it in SR-IOV mode
        #    a. verify that it gets a plug,
        #       i. that says "unaccelerated", since acceleration is not
        #          enabled,
        #    b. verify that it does NOT get a VF,
        #    c. verify that we get a warning (that acceleration is not enabled)
        td = _test_data()
        u = td['uuid']
        po = port.Port(**td)
        s = Session()
        s.add(po)
        s.commit()

        hw_acceleration_modes = (PM.SRIOV, PM.unaccelerated,)
        po = s.query(port.Port).get(u)
        self.assertIsNotNone(po)
        self.assertIsNone(po.plug)

        with _VROUTER_LMC() as lmc:
            _select_plug_mode_for_port(
                s, po.uuid, vf_pool, hw_acceleration_modes, vif_type=None,
                virt_type=None, root_dir=sysfs.root_dir
            )
            s.refresh(po)

        self.assertEqual(lmc.count, {_CONFIG_LOGGER.name: {'INFO': 1}})

        self.assertIsNotNone(po.plug)
        self.assertEqual(po.plug.mode, PM.unaccelerated)
        v = one_or_none(s.query(vf.VF).filter(vf.VF.neutron_port == u))
        self.assertIsNone(v)
        s.commit()

        # 3. create a new port and attempt to plug it with
        #    --hw-acceleration-mode=VirtIO
        #    a. verify that we get an AccelerationModeConflict
        #    b. verify that it DOES NOT get a plug,
        #    c. verify that it DOES NOT get a VF,
        td = _test_data()
        u = td['uuid']
        po = port.Port(**td)
        s = Session()
        s.add(po)
        s.commit()

        hw_acceleration_modes = (PM.VirtIO,)
        po = s.query(port.Port).get(u)
        self.assertIsNotNone(po)
        self.assertIsNone(po.plug)

        with _VROUTER_LMC() as lmc:
            with self.assertRaises(config.AccelerationModeConflict) as cm:
                _select_plug_mode_for_port(
                    s, po.uuid, vf_pool, hw_acceleration_modes, vif_type=None,
                    virt_type=None, root_dir=sysfs.root_dir,
                    hw_acceleration_mode=PM.VirtIO,
                )
            self.assertEqual(cm.exception.neutron_port, uuid.UUID(u))

        self.assertEqual(lmc.count, {
            _CONFIG_LOGGER.name: {'INFO': 3, 'ERROR': 1}
        })

        self.assertIsNone(po.plug)
        v = one_or_none(s.query(vf.VF).filter(vf.VF.neutron_port == u))
        self.assertIsNone(v)
        s.commit()

        # 4. now enable acceleration and attempt to plug the port in SR-IOV
        #    mode
        #    a. verify that it gets a plug,
        #       i. that says "SR-IOV",
        #    b. verify that it gets a *temporary* VF allocation,
        #    c. verify that we DO NOT get a warning
        sysfs.set_physical_vif_count(1)

        hw_acceleration_modes = (PM.SRIOV,)
        s = Session()
        po = s.query(port.Port).get(u)
        self.assertIsNotNone(po)
        self.assertIsNone(po.plug)

        with _VROUTER_LMC() as lmc:
            _select_plug_mode_for_port(
                s, po.uuid, vf_pool, hw_acceleration_modes, vif_type=None,
                virt_type=None, root_dir=sysfs.root_dir,
                hw_acceleration_mode=PM.SRIOV,
                reservation_timeout=config_opts.parse_timedelta('30m'),
            )

        s.refresh(po)
        self.assertEqual(lmc.count, {_VF_LOGGER.name: {'INFO': 1}})

        self.assertIsNotNone(po.plug)
        self.assertEqual(po.plug.mode, PM.SRIOV)
        v = s.query(vf.VF).filter(vf.VF.neutron_port == u).one()
        old_addr = v.addr
        self.assertEqual(v, po.vf)
        self.assertIsNotNone(v.expires)
        s.commit()

        # 5. replug the same port in VirtIO mode
        #    a. verify that we get a plug,
        #       i.  that says "VirtIO"
        #       ii. that reuses the same addr as last time
        #    b. verify that we have a VF (possibly redundant with ii?)
        #    c. verify that we get a warning (that the plug mode changed)
        hw_acceleration_modes = (PM.VirtIO,)
        s = Session()
        po = s.query(port.Port).get(u)
        self.assertIsNotNone(po)
        self.assertIsNotNone(po.plug)
        self.assertEqual(po.plug.mode, PM.SRIOV)

        with _VROUTER_LMC() as lmc:
            _select_plug_mode_for_port(
                s, po.uuid, vf_pool, hw_acceleration_modes, vif_type=None,
                virt_type=None, root_dir=sysfs.root_dir,
                hw_acceleration_mode=PM.VirtIO,
                reservation_timeout=config_opts.parse_timedelta('30m'),
            )

        self.assertEqual(lmc.count, {
            _CONFIG_LOGGER.name: {'WARNING': 1},
            _VF_LOGGER.name: {'INFO': 1},
        })

        self.assertIsNotNone(po.plug)
        self.assertEqual(po.plug.mode, PM.VirtIO)
        v = s.query(vf.VF).filter(vf.VF.neutron_port == u).one()
        self.assertEqual(v, po.vf)
        self.assertEqual(v.addr, old_addr)
        self.assertIsNotNone(v.expires)
        s.commit()

        # plug new port in VirtIO, verify that we run out of vfs and get an
        # exception.
        td = _test_data()
        u = td['uuid']
        po = port.Port(**td)
        s = Session()
        s.add(po)
        s.commit()

        s = Session()
        po = s.query(port.Port).get(u)
        self.assertIsNotNone(po)
        self.assertIsNone(po.plug)

        with _VROUTER_LMC() as lmc:
            with self.assertRaises(vf.AllocationError) as cm:
                _select_plug_mode_for_port(
                    s, po.uuid, vf_pool, hw_acceleration_modes, vif_type=None,
                    virt_type=None, root_dir=sysfs.root_dir,
                    hw_acceleration_mode=PM.VirtIO
                )
            self.assertTrue(u in str(cm.exception))

        self.assertEqual(lmc.count, {_VF_LOGGER.name: {'ERROR': 1}})

        self.assertIsNone(po.plug)
        v = one_or_none(s.query(vf.VF).filter(vf.VF.neutron_port == u))
        self.assertIsNone(v)
        s.commit()

        # turn off mandatory, do the same thing, verify that we STILL get an
        # allocation error since the code selected SR-IOV mode and will not
        # try to fall back to unaccelerated mode just because we are out of
        # VFs (behavior change for VRT-536).
        hw_acceleration_modes = (PM.SRIOV, PM.unaccelerated)

        td = _test_data()
        u = td['uuid']
        po = port.Port(**td)
        s = Session()
        s.add(po)
        s.commit()

        s = Session()
        po = s.query(port.Port).get(u)
        self.assertIsNotNone(po)
        self.assertIsNone(po.plug)

        with _VROUTER_LMC() as lmc:
            with self.assertRaises(vf.AllocationError) as cm:
                _select_plug_mode_for_port(
                    s, po.uuid, vf_pool, hw_acceleration_modes, vif_type=None,
                    virt_type=None, root_dir=sysfs.root_dir,
                )
            self.assertTrue(u in str(cm.exception))

        s.refresh(po)
        self.assertEqual(lmc.count, {_VF_LOGGER.name: {'ERROR': 1}})

        self.assertIsNone(po.plug)
        v = one_or_none(s.query(vf.VF).filter(vf.VF.neutron_port == u))
        self.assertIsNone(v)
        s.commit()

        # create a new one, manually create a plug for it that says VirtIO,
        # plug it in the same condition, verify that we STILL get an allocation
        # error since the code no longer falls back to unaccelerated mode when
        # we are out of VFs.
        td = _test_data()
        u = td['uuid']
        po = port.Port(**td)
        s = Session()
        s.add(po)
        pm = port.PlugMode(neutron_port=po.uuid, mode=PM.VirtIO)
        s.add(pm)
        s.commit()

        s = Session()
        po = s.query(port.Port).get(u)
        self.assertIsNotNone(po)
        self.assertIsNotNone(po.plug)
        self.assertEqual(po.plug.mode, PM.VirtIO)

        with _VROUTER_LMC() as lmc:
            with self.assertRaises(vf.AllocationError) as cm:
                _select_plug_mode_for_port(
                    s, po.uuid, vf_pool, hw_acceleration_modes, vif_type=None,
                    virt_type=None, root_dir=sysfs.root_dir
                )
            self.assertTrue(u in str(cm.exception))

        self.assertEqual(lmc.count, {_VF_LOGGER.name: {'ERROR': 1}})

        self.assertIsNotNone(po.plug)
        self.assertEqual(po.plug.mode, PM.VirtIO)
        v = one_or_none(s.query(vf.VF).filter(vf.VF.neutron_port == u))
        self.assertIsNone(v)
        s.commit()

    def test_select_plug_mode_for_port_with_nfp_error_status(self):
        engine = database.create_engine('tmp')[0]
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        # setup port
        td = _test_data()
        u = td['uuid']
        po = port.Port(**td)
        s.add(po)
        s.commit()
        del s, po

        # setup vf pool
        vf_pool = vf.Pool(
            fake_FallbackMap([pci.parse_pci_address('0123:45:67.6')])
        )

        # setup sysfs acceleration file
        sysfs = FakeSysfs(physical_vif_count=1, nfp_status=0)

        # setup config
        hw_acceleration_modes = (PM.SRIOV, PM.unaccelerated,)

        # attempt to plug the port in SR-IOV mode
        #    a. verify that it gets a plug,
        #       i. that says "unaccelerated" since the NFP is broken
        #    b. verify that it DOES NOT get a VF,

        s = Session()
        po = s.query(port.Port).get(u)
        self.assertIsNotNone(po)
        self.assertIsNone(po.plug)

        with _VROUTER_LMC() as lmc:
            _select_plug_mode_for_port(
                s, po.uuid, vf_pool, hw_acceleration_modes, vif_type=None,
                virt_type=None, root_dir=sysfs.root_dir
            )

        s.refresh(po)
        self.assertEqual(lmc.count, {_CONFIG_LOGGER.name: {'CRITICAL': 1}})

        self.assertIsNotNone(po.plug)
        self.assertEqual(po.plug.mode, PM.unaccelerated)
        v = one_or_none(s.query(vf.VF).filter(vf.VF.neutron_port == u))
        self.assertIsNone(v)
        s.commit()

    def _test_vhostuser_and_lxc_ports(
        self, plug_mode, plug_mode_mandatory, should_raise, expected_logs,
        select_plug_mode_for_port_kwds
    ):
        """
        Backend for tests requiring certain types of ports to end up
        unaccelerated.
        """

        engine = database.create_engine('tmp')[0]
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        # setup port
        #
        # NOTE! We don't check the values of vif_type or virt_type in the port
        # at all! The values passed in the function signature
        # ("vrouter-port-control config" command line, in real life) take
        # precedence. Actually the values in our Port object should either be a
        # record of these, or should simply not exist.
        td = _test_data()
        u = td['uuid']
        po = port.Port(**td)
        s.add(po)
        s.commit()

        # setup vf pool
        vf_pool = vf.Pool(
            fake_FallbackMap([pci.parse_pci_address('0123:45:67.6')])
        )

        # setup sysfs acceleration file, with acceleration enabled
        sysfs = FakeSysfs(physical_vif_count=1, nfp_status=1)

        # setup config (emulate semantics of pre-VRT-536 config, for ease of
        # converting unit test code)
        hw_acceleration_modes = (plug_mode, PM.unaccelerated)
        hw_acceleration_mode = plug_mode if plug_mode_mandatory else None

        # No matter what we do to the port, it is supposed to either:
        #   1. End up unaccelerated (if should_raise is False), or
        #   2. Raise (otherwise).
        s = Session()
        po = s.query(port.Port).get(u)
        self.assertIsNotNone(po)

        with _VROUTER_LMC() as lmc:
            kwds = copy.deepcopy(select_plug_mode_for_port_kwds)
            kwds.setdefault('vif_type', None)
            kwds.setdefault('virt_type', None)

            def _fn():
                _select_plug_mode_for_port(
                    s, po.uuid, vf_pool, hw_acceleration_modes,
                    hw_acceleration_mode=hw_acceleration_mode,
                    root_dir=sysfs.root_dir, **kwds
                )

            if not should_raise:
                _fn()
                self.assertIsNotNone(po.plug)
                self.assertEqual(po.plug.mode, PM.unaccelerated)
                self.assertEqual(lmc.count, expected_logs)

            else:
                with self.assertRaises(config.AccelerationModeConflict) as cm:
                    _fn()
                self.assertTrue(u in str(cm.exception))

        s.commit()

        # load the port from DB again and double-check, just to be sure
        if not should_raise:
            s = Session()
            pm = s.query(port.PlugMode).get(u)
            self.assertIsNotNone(pm)
            self.assertEqual(pm.mode, PM.unaccelerated)
            po = s.query(port.Port).get(u)
            self.assertEqual(po.plug.mode, PM.unaccelerated)
            v = one_or_none(s.query(vf.VF).filter(vf.VF.neutron_port == u))
            self.assertIsNone(v)
        else:
            s = Session()
            pm = one_or_none(
                s.query(port.PlugMode).filter(port.PlugMode.neutron_port == u)
            )
            self.assertIsNone(pm)
            po = s.query(port.Port).get(u)
            self.assertIsNone(po.plug)
            v = one_or_none(s.query(vf.VF).filter(vf.VF.neutron_port == u))
            self.assertIsNone(v)

        s.commit()

    @staticmethod
    def _generate_vif_or_virt_type_test(plug_mode, plug_mode_mandatory, kwds):
        def fn(self):
            should_raise = plug_mode_mandatory and (
                plug_mode in PM.accelerated_plug_modes
            )
            try:
                return self._test_vhostuser_and_lxc_ports(
                    plug_mode=plug_mode,
                    plug_mode_mandatory=plug_mode_mandatory,
                    should_raise=should_raise,
                    expected_logs={_CONFIG_LOGGER.name: {'INFO': 1}},
                    select_plug_mode_for_port_kwds=kwds,
                )

            except AssertionError as e:
                e.args = explain_assertion(
                    e.args,
                    'Case failed: '
                    'plug_mode={!r}, plug_mode_mandatory={!r}'
                    .format(plug_mode, plug_mode_mandatory)
                )
                raise

        return fn

    @classmethod
    def _generate_vif_and_virt_type_tests(cls):
        # Generate numerous test methods that can run in parallel.
        params = (
            ('vif_type', ('vhostuser', 'VhostUser')),
            ('virt_type', ('lxc', 'NameSpacePort')),
        )
        for plug_mode in PM.all_plug_modes:
            for plug_mode_mandatory in (False, True):
                for p in params:
                    for param_value in p[1]:
                        kwds = {p[0]: param_value}
                        name = (
                            'test_select_plug_mode_{}_{}_{}'.format(
                                plug_mode.replace('-', ''),
                                'MUST' if plug_mode_mandatory else 'MAY',
                                param_value
                            )
                        )
                        setattr(cls, name, cls._generate_vif_or_virt_type_test(
                            plug_mode, plug_mode_mandatory, kwds
                        ))

TestPlugMode._generate_vif_and_virt_type_tests()


# A typical result of get_base_config().
_base_config = {
    'type': 'LibvirtConfigGuestInterface',
    'vars': {
        'source_dev': None,
        'vporttype': None,
        'vportparams': [],
        'vif_outbound_burst': None,
        'target_dev': None,
        'filterparams': [],
        'ns_prefix': None,
        'script': None,
        'vhostuser_mode': None,
        'vif_inbound_burst': None,
        'ns_uri': None,
        'vhostuser_path': None,
        'vhostuser_type': None,
        'vif_inbound_peak': None,
        'filtername': None,
        'vif_outbound_average': None,
        'vlan': None,
        'vif_inbound_average': None,
        'root_name': 'interface',
        'source_mode': 'private',
        'driver_name': None,
        'net_type': None,
        'mac_addr': '02:22:3c:d2:22:7b',
        'vif_outbound_peak': None,
        'model': 'virtio'
    },
}

# A typical result of unmodified get_config_vrouter().
_ethernet_config_uuid = uuid.UUID('83227397-ac2e-4329-8646-7ab683d1bd76')
_ethernet_config = copy.deepcopy(_base_config)
_ethernet_config['vars'].update({
    'script': '',
    'target_dev': 'tap83227397-ac',
    'net_type': 'ethernet',
})


def _validate_dom(
    test, dom, interface_type, required_children, forbidden_children
):
    xml = etree.tostring(dom, pretty_print=True)

    try:
        test.assertEqual(dom.tag, 'interface')
        test.assertEqual(dom.attrib['type'], interface_type)
        for xpath in required_children:
            ans = dom.findall(xpath)
            test.assertEqual(
                len(ans), 1,
                '{} required in {} mode; found {} not 1 of it'.format(
                    xpath, interface_type, len(ans)
                )
            )
        for xpath in forbidden_children:
            ans = dom.findall(xpath)
            test.assertEqual(
                len(ans), 0, '{} forbidden in {} mode'.format(
                    xpath, interface_type
                )
            )
    except AssertionError:
        print >>sys.stderr, xml
        raise


def _validate_ethernet_dom(test, dom):
    _validate_dom(
        test,
        dom,
        interface_type='ethernet',
        required_children=(
            './mac',
            './mac[@address]',
            './model',
            './model[@type="virtio"]',
            './target',
            './target[@dev]',
            './script',
            './script[@path]',
        ),
        forbidden_children=()
    )


def _validate_hostdev_dom(test, dom):
    _validate_dom(
        test,
        dom,
        interface_type='hostdev',
        required_children=(
            './mac',
            './mac[@address]',
            './source',
            './source/address[@type="pci"]',
            './driver',
            './driver[@name]',
        ),
        forbidden_children=(
            './model',
            './script',
            './target',
        )
    )


def _validate_vhostuser_dom(test, dom):
    _validate_dom(
        test,
        dom,
        interface_type='vhostuser',
        required_children=(
            './mac',
            './mac[@address]',
            './source',
            './source[@type="unix"]',
            './source[@path]',
            './source[@mode]',
            './target',
            './target[@dev]',
            './model',
            './model[@type="virtio"]',
        ),
        forbidden_children=(
            './driver',
            './source/address',
        )
    )


class TestSetConfig(unittest.TestCase):
    def test_set_config_unaccelerated(self):
        for c in (
            (_base_config['vars'], uuid.uuid1()),
            (_ethernet_config['vars'], _ethernet_config_uuid)
        ):
            # 1. construct initial config that would be sent over from Nova
            gi = LibvirtConfigGuestInterface()
            config_editor._apply_changes(gi, c[0])

            # 2. edit it to be "ethernet" (tap)
            config.set_config_unaccelerated(gi, c[1])

            # 3. check that it is serializable
            dom = gi.format_dom()

            # 4. check that it has the right contents
            _validate_ethernet_dom(self, dom)

    def test_set_config_sriov(self):
        for c in (
            (_base_config['vars'], uuid.uuid1()),
            (_ethernet_config['vars'], _ethernet_config_uuid)
        ):
            # 1. construct initial config that would be sent over from Nova
            gi = LibvirtConfigGuestInterface()
            config_editor._apply_changes(gi, c[0])

            # 2. edit it to be SR-IOV
            vf_addr = _random_pci_address()
            config.set_config_sriov(gi, c[1], vf_addr)

            # 3. check that it is serializable
            dom = gi.format_dom()

            # 4. check that it has the right contents
            _validate_hostdev_dom(self, dom)

    def test_set_config_virtio(self):
        for c in (
            (_base_config['vars'], uuid.uuid1()),
            (_ethernet_config['vars'], _ethernet_config_uuid)
        ):
            # 1. construct initial config that would be sent over from Nova
            gi = LibvirtConfigGuestInterface()
            config_editor._apply_changes(gi, c[0])

            # 2. edit it to be SR-IOV
            vf_number = random.randint(0, 59)
            config.set_config_virtio(gi, c[1], vf_number)

            # 3. check that it is serializable
            dom = gi.format_dom()

            # 4. check that it has the right contents
            _validate_vhostuser_dom(self, dom)

_FALLBACK_MAP = r'''
0000:02:04.6 0001:03:05.7 nfp_v0.0 0
0000:04:06.8 0003:05:07.9 nfp_v0.1 1
'''.strip()


def _arbitrary_fallback_map(n):
    """Generate an arbitrary fallback map of size `n`."""
    ans = []

    used_addrs = set()
    vf_numbers = set(xrange(60))

    for i in xrange(n):
        assert vf_numbers, \
            'ran out of VF numbers on iteration {}'.format(i + 1)

        pf_addr = _random_pci_address()
        while pf_addr in used_addrs:
            pf_addr = _random_pci_address()
        used_addrs.add(pf_addr)

        vf_addr = _random_pci_address()
        while vf_addr in used_addrs:
            vf_addr = _random_pci_address()
        used_addrs.add(vf_addr)

        vf_number = random.choice(tuple(vf_numbers))
        vf_numbers.remove(vf_number)

        nbi = random.randint(0, 3)  # maybe an NBI, not sure
        ans.append('{} {} nfp_v{}.{} {}'.format(
            pf_addr, vf_addr, nbi, vf_number, vf_number
        ))

    ans.append('')
    return '\n'.join(ans)


class TestSetConfigForPort(unittest.TestCase):
    def test_set_config_for_port(self):
        with _ROOT_LMC() as lmc:
            # 1. create a fallback map
            fallback_map = fallback.read_sysfs_fallback_map(_in=_FALLBACK_MAP)
            self.assertEqual(len(fallback_map.vfset), 2)
            vf_pool = vf.Pool(fallback_map)

            # 2. create some ports
            ports = [None, None, None]

            # setup sysfs acceleration file and enable acceleration
            sysfs = FakeSysfs(physical_vif_count=1, nfp_status=1)

            engine = database.create_engine('tmp')[0]
            port.create_metadata(engine)
            vf.create_metadata(engine)
            Session = sessionmaker(bind=engine)

            s = Session()

            for i in xrange(len(ports)):
                ports[i] = port.Port(**_test_data())
                s.add(ports[i])

            # configure the plug mode for the ports
            hw_acceleration_modes = (PM.SRIOV,)
            _select_plug_mode_for_port(
                s, ports[0].uuid, vf_pool, hw_acceleration_modes,
                vif_type=None, virt_type=None, root_dir=sysfs.root_dir,
                hw_acceleration_mode=hw_acceleration_modes[0],
            )
            hw_acceleration_modes = (PM.VirtIO,)
            _select_plug_mode_for_port(
                s, ports[1].uuid, vf_pool, hw_acceleration_modes,
                vif_type=None, virt_type=None, root_dir=sysfs.root_dir,
                hw_acceleration_mode=hw_acceleration_modes[0],
            )
            hw_acceleration_modes = (PM.unaccelerated,)
            _select_plug_mode_for_port(
                s, ports[2].uuid, vf_pool, hw_acceleration_modes,
                vif_type=None, virt_type=None, root_dir=sysfs.root_dir,
            )
            for p in ports:
                s.refresh(p)

            # 3. "set config" for each of them and make sure that we get the
            # XML we expect
            expected = (
                (ports[0], _validate_hostdev_dom),
                (ports[1], _validate_vhostuser_dom),
                (ports[2], _validate_ethernet_dom)
            )

            for e in expected:
                for c in (_base_config['vars'], _ethernet_config['vars']):
                    # 1. construct initial config that would be sent over from
                    # Nova
                    gi = LibvirtConfigGuestInterface()
                    config_editor._apply_changes(gi, c)

                    # 2. edit it to match the port
                    config.set_config_for_port(s, gi, e[0].uuid, fallback_map)

                    # 3. check that it is serializable
                    dom = gi.format_dom()

                    # 4. check that it has the right contents
                    e[1](self, dom)

        self.assertEqual(lmc.count, {_VF_LOGGER.name: {'INFO': 2}})

        s.commit()

    def test_set_config_for_port_plug_before_create(self):
        with _ROOT_LMC() as lmc:
            # 1. create a fallback map
            fallback_map = fallback.read_sysfs_fallback_map(_in=_FALLBACK_MAP)
            self.assertEqual(len(fallback_map.vfset), 2)
            vf_pool = vf.Pool(fallback_map)

            # 2. create some port uuids
            ports = [uuid.uuid1() for i in xrange(3)]

            # setup sysfs acceleration file and enable acceleration
            sysfs = FakeSysfs(physical_vif_count=1, nfp_status=1)

            engine = database.create_engine('tmp')[0]
            port.create_metadata(engine)
            vf.create_metadata(engine)
            Session = sessionmaker(bind=engine)

            s = Session()

            # configure the plug mode for the ports
            hw_acceleration_modes = (PM.SRIOV,)
            _select_plug_mode_for_port(
                s, ports[0], vf_pool, hw_acceleration_modes, vif_type=None,
                virt_type=None, root_dir=sysfs.root_dir,
                hw_acceleration_mode=hw_acceleration_modes[0],
            )
            hw_acceleration_modes = (PM.VirtIO,)
            _select_plug_mode_for_port(
                s, ports[1], vf_pool, hw_acceleration_modes, vif_type=None,
                virt_type=None, root_dir=sysfs.root_dir,
                hw_acceleration_mode=hw_acceleration_modes[0],
            )
            hw_acceleration_modes = (PM.unaccelerated,)
            _select_plug_mode_for_port(
                s, ports[2], vf_pool, hw_acceleration_modes, vif_type=None,
                virt_type=None, root_dir=sysfs.root_dir,
                hw_acceleration_mode=hw_acceleration_modes[0],
            )

            # NOW create the ports
            for i in xrange(len(ports)):
                u = ports[i]
                ports[i] = port.Port(**_test_data())
                ports[i].uuid = u
                ports[i].plug = s.query(port.PlugMode).get(u)
                s.add(ports[i])

            # 3. "set config" for each of them and make sure that we get the
            # XML we expect
            expected = (
                (ports[0], _validate_hostdev_dom),
                (ports[1], _validate_vhostuser_dom),
                (ports[2], _validate_ethernet_dom)
            )

            for e in expected:
                for c in (_base_config['vars'], _ethernet_config['vars']):
                    # 1. construct initial config that would be sent over from
                    # Nova
                    gi = LibvirtConfigGuestInterface()
                    config_editor._apply_changes(gi, c)

                    # 2. edit it to match the port
                    config.set_config_for_port(s, gi, e[0].uuid, fallback_map)

                    # 3. check that it is serializable
                    dom = gi.format_dom()

                    # 4. check that it has the right contents
                    e[1](self, dom)

        self.assertEqual(lmc.count, {_VF_LOGGER.name: {'INFO': 2}})

        s.commit()

    # I think this is what we will actually be doing when called by Nova.
    def test_set_config_for_port_do_not_bother_creating_ports_just_plug_them(
        self
    ):
        N = 60

        # 1. create a fallback map with N entries
        fallback_map = fallback.read_sysfs_fallback_map(
            _in=_arbitrary_fallback_map(N)
        )
        self.assertEqual(len(fallback_map.vfset), N)
        vf_pool = vf.Pool(fallback_map)

        # 2. create some port uuids, that we will never create ports for
        neutron_ports = [uuid.uuid1() for i in xrange(N)]

        # setup sysfs acceleration file and enable acceleration
        sysfs = FakeSysfs(physical_vif_count=1, nfp_status=1)

        engine = database.create_engine('tmp')[0]
        port.create_metadata(engine)
        vf.create_metadata(engine)
        Session = sessionmaker(bind=engine)

        s = Session()

        # configure the plug mode for the phantom ports
        mode_choices = (
            (PM.SRIOV, _validate_hostdev_dom),
            (PM.VirtIO, _validate_vhostuser_dom),
            (PM.unaccelerated, _validate_ethernet_dom),
        )
        expected = []
        for i in xrange(N):
            m = random.choice(mode_choices)
            hw_acceleration_modes = (m[0],)
            _select_plug_mode_for_port(
                s, neutron_ports[i], vf_pool, hw_acceleration_modes,
                vif_type=None, virt_type=None, root_dir=sysfs.root_dir,
                hw_acceleration_mode=hw_acceleration_modes[0],
            )
            expected.append((neutron_ports[i], m[1]))

            # don't create the ports yet. or actually ever

            # 3. "set config" for each of them and make sure that we get
            # the XML we expect
            for e in expected:
                for c in (_base_config['vars'], _ethernet_config['vars']):
                    # 1. construct initial config that would be sent over from
                    # Nova
                    gi = LibvirtConfigGuestInterface()
                    config_editor._apply_changes(gi, c)

                    # 2. edit it to match the port
                    config.set_config_for_port(s, gi, e[0], fallback_map)

                    # 3. check that it is serializable
                    dom = gi.format_dom()

                    # 4. check that it has the right contents
                    e[1](self, dom)

        s.commit()

if __name__ == '__main__':
    unittest.main()
