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

import logging
import unittest

from netronome.vrouter import flavor as flav, plug_modes as PM
from netronome.vrouter.tests.unit import *

_FLAVOR_LOGGER = make_getLogger('netronome.vrouter.flavor')


def _FLAVOR_LMC(cls=LogMessageCounter):
    return attachLogHandler(_FLAVOR_LOGGER(), cls())


class TestAllowedHwAccelerationModes(unittest.TestCase):
    def test_allowed_hw_acceleration_modes(self):
        SV  = (PM.SRIOV, PM.VirtIO)
        U   = (PM.unaccelerated,)
        SVU = (PM.SRIOV, PM.VirtIO, PM.unaccelerated)

        f = flav.allowed_hw_acceleration_modes

        # Property not set should produce the default set of modes with no
        # warnings.
        self.assertEqual(f({}), SVU)

        test_data = (
            ('', SVU, {}, None),
            ('on', SV, {}, None),
            ('off', U, {}, None),
            (' on ', SV, {}, None),
            ('on  ', SV, {}, None),
            ('  on', SV, {}, 'm1.tiny'),
            (' off', U, {}, None),
            ('off ', U, {}, None),
            ('SR-IOV', (PM.SRIOV,), {}, 's1.small'),
            ('SR-IOV', (PM.SRIOV,), {}, None),
            ('VirtIO', (PM.VirtIO,), {}, 'v1.small'),
            ('VirtIO', (PM.VirtIO,), {}, None),
            ('beeper', SVU, {_FLAVOR_LOGGER.name: {'WARNING': 1}}, None),
            ('beeper', SVU, {_FLAVOR_LOGGER.name: {'WARNING': 1}}, ''),
            ('beeper', SVU, {_FLAVOR_LOGGER.name: {'WARNING': 1}}, 'm1.tiny'),
        )

        for td in test_data:
            val = {flav.HW_ACCELERATION_PROPERTY: td[0]}

            # Self-check.
            self.assertIsInstance(td[1], tuple)

            try:
                with _FLAVOR_LMC() as lmc:
                    self.assertEqual(f(val, name=td[3]), td[1])

            except AssertionError as e:
                e.args = explain_assertion(
                    e.args, 'Wrong answer for input: {!r}'.format(td[0])
                )
                raise

            try:
                # Check that we got the expected number of warnings.
                self.assertEqual(lmc.count, td[2])

            except AssertionError as e:
                e.args = explain_assertion(
                    e.args, 'Wrong log messages for input: {!r}'.format(td[0])
                )
                raise

    def test_allowed_hw_acceleration_modes_bad_args(self):
        with self.assertRaises(ValueError):
            flav.allowed_hw_acceleration_modes(extra_specs=None)

        with self.assertRaises(ValueError):
            flav.allowed_hw_acceleration_modes(extra_specs='hello, world')


class TestAssertFlavorSupportsVirtIO(unittest.TestCase):
    def test_assert_flavor_supports_virtio(self):
        # ALL of these should raise an exception.
        test_data_bad = (
            {},
            {'agilio:hw_acceleration': 'off'},
            {'agilio:hw_acceleration': 'on'},
            {'some_extra_prop': 1, 'agilio:hw_acceleration': 'on'},
            {'some_extra_prop': 1},
        )

        for td in test_data_bad:
            try:
                with _FLAVOR_LMC() as lmc:
                    with self.assertRaises(flav.FlavorVirtIOConfigError):
                        flav.assert_flavor_supports_virtio(td)

                self.assertEqual(
                    lmc.count, {_FLAVOR_LOGGER.name: {'CRITICAL': 1}}
                )

            except AssertionError as e:
                e.args = explain_assertion(e.args, 'Input: {!r}'.format(td))
                raise

        # NONE of these should raise an exception.
        test_data_good = (
            {'hw:mem_page_size': ''},
            {'hw:mem_page_size': 'all'},
            {'hw:mem_page_size': 'any'},
            {'hw:mem_page_size': 'large'},
            {'hw:mem_page_size': '2M'},
            {'hw:mem_page_size': '1G'},
            {'hw:mem_page_size': 'vblah'},
            {'hw:mem_page_size': 'large', 'agilio:hw_acceleration': 'on'},
        )

        for td in test_data_good:
            try:
                with _FLAVOR_LMC() as lmc:
                    flav.assert_flavor_supports_virtio(td)

                self.assertEqual(lmc.count, {})

            except AssertionError as e:
                e.args = explain_assertion(e.args, 'Input: {!r}'.format(td))
                raise

if __name__ == '__main__':
    unittest.main()
