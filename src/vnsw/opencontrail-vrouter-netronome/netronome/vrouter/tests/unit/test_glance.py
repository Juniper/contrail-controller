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

from netronome.vrouter import glance as G, plug_modes as PM
from netronome.vrouter.tests.unit import *

_GLANCE_LOGGER = make_getLogger('netronome.vrouter.glance')


def _GLANCE_LMC(cls=LogMessageCounter):
    return attachLogHandler(_GLANCE_LOGGER(), cls())


class TestAllowedHwAccelerationModes(unittest.TestCase):
    def test_allowed_hw_acceleration_modes(self):
        VU = (PM.VirtIO, PM.unaccelerated)
        SVU = (PM.SRIOV, PM.VirtIO, PM.unaccelerated)

        f = G.allowed_hw_acceleration_modes

        # Property not set should produce the default set of modes with no
        # warnings.
        self.assertEqual(f({}), VU)

        test_data = (
            ('', VU, {}, {}),
            (' ', VU, {}, {}),
            (' ,', VU, {}, {}),
            (',', VU, {}, {}),
            (',,', VU, {}, {}),
            (',, ', VU, {}, {}),
            (',, ,', VU, {}, {}),

            ('SR-IOV', SVU, {}, {}),
            (' SR-IOV  ', SVU, {}, {}),
            (' SR-IOV', SVU, {}, {}),
            ('SR-IOV ', SVU, {}, {}),
            ('SR-IOV,SR-IOV', SVU, {}, {}),
            ('SR-IOV, SR-IOV', SVU, {}, {}),
            ('SR-IOV, SR-IOV,', SVU, {}, {}),
            ('SR-IOV, SR-IOV,,', SVU, {}, {}),
            ('SR-IOV, SR-IOV,,', SVU, {}, {}),
            (',,,SR-IOV', SVU, {}, {}),
            (',,,SR-IOV', SVU, {}, {'name': 'cirros_20160531T1546_netvf'}),
            (',,,SR-IOV', SVU, {},
             {'id': '6b1da771-a199-4393-8ef9-c3212312a626'}),
            (',,,SR-IOV', SVU, {},
             {'name': 'cirros_20190909T0909_netvf',
              'id': '6b1da771-a199-4393-8ef9-c3212312a626'}),

            # Test warnings.
            ('blue', VU, {_GLANCE_LOGGER.name: {'WARNING': 1}}, {}),
            ('blue,green', VU, {_GLANCE_LOGGER.name: {'WARNING': 1}}, {}),
            ('blue,green,SR-IOV', SVU, {_GLANCE_LOGGER.name: {'WARNING': 1}},
             {}),
            ('blue,green,SR-IOV', SVU, {_GLANCE_LOGGER.name: {'WARNING': 1}},
             {'name': 'cirros_20220717T1414_netvf'}),
            ('blue,green,SR-IOV', SVU, {_GLANCE_LOGGER.name: {'WARNING': 1}},
             {'id': '50599c54-c1fe-487f-a349-38d650e65179'}),
            ('blue,green,SR-IOV', SVU, {_GLANCE_LOGGER.name: {'WARNING': 1}},
             {'name': 'cirros_20220717T1414_netvf',
              'id': '50599ce4-c1fe-487f-a349-38d650e65179'}),
        )

        for td in test_data:
            val = {G.HW_ACCELERATION_FEATURES_PROPERTY: td[0]}

            try:
                with _GLANCE_LMC() as lmc:
                    self.assertEqual(f(val, **td[3]), td[1])

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
            G.allowed_hw_acceleration_modes(image_properties=None)

        with self.assertRaises(ValueError):
            G.allowed_hw_acceleration_modes(image_properties='hello, world')

if __name__ == '__main__':
    unittest.main()
