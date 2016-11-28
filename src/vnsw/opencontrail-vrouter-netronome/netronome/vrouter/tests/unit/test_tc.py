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

import unittest

from netronome.vrouter.tc import (
    PciAddressParam, NetworkNameParam, TargetDevParam
)


class TestTc(unittest.TestCase):
    def test_PciAddressParam(self):
        tc = PciAddressParam()
        self.assertTrue(tc.check('default'))  # important special case!
        self.assertTrue(tc.check('0000:0::.'))
        self.assertFalse(tc.check(''))
        self.assertFalse(tc.check('0000:0::.\n'))
        self.assertIsNone(tc.validate('0000:0::.'))
        self.assertIsNotNone(tc.validate(''))

    def test_NetworkNameParam(self):
        tc = NetworkNameParam()
        self.assertTrue(tc.check('hello'))
        self.assertFalse(tc.check(''))
        self.assertFalse(tc.check('this is invalid'))
        self.assertFalse(tc.check('hello\n'))
        self.assertIsNone(tc.validate('hello'))
        self.assertIsNotNone(tc.validate(''))

    def test_TargetDevParam(self):
        tc = TargetDevParam()
        self.assertTrue(tc.check('a'))
        self.assertTrue(tc.check('a\n'))
        self.assertTrue(tc.check('a '))
        self.assertTrue(tc.check(' a'))
        self.assertTrue(tc.check(' a '))
        self.assertFalse(tc.check(' '))
        self.assertFalse(tc.check(''))
        self.assertFalse(tc.check('a\ny'))
        self.assertFalse(tc.check('añy'))
        self.assertFalse(tc.check('á'))
        self.assertIsNone(tc.validate('a'))
        self.assertIsNotNone(tc.validate('á'))

if __name__ == '__main__':
    unittest.main()
