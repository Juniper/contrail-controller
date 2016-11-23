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

import unittest

# test import
from netronome.vrouter import fallback
from netronome.vrouter.fallback import FallbackInterface as FI
from netronome.vrouter.pci import PciAddress as A
from netronome.vrouter.pci import parse_pci_address
from netronome.vrouter.tests.unit import *

_FALLBACK_LOGGER = make_getLogger('netronome.vrouter.fallback')

TEST_DATA_HYDROGEN = r'''
wrong_number_of_columns
QQQQ:RR:SS.T 0000:05:08.0 nfp_v0.0
0000:06:08.0 0000:05:08.0 nfp_v1.0
0000:05:00.0 0000:05:08.1 nfp_v0.1  2 $
0000:05:00.0 0000:05:08.2 NFP_V0.2 3
0000:05:00.0 0000:05:08.3 nfp_v0.3  5
0000:05:00.0 0000:05:08.4 nfp_v0.4 11
0000:05:00.0 0000:05:08.5 nfp_v0.5 10
0000:07:00.0 0000:07:08.9 xnfp_v0.6x 0
'''.translate(None, '$').lstrip()

TEST_DATA_HYDROGEN_PFMAP_ANS = {
    A(0,5,0,0): {
        A(0,5,8,1): FI(A(0,5,0,0), A(0,5,8,1), "nfp_v0.1", 2),
        A(0,5,8,2): FI(A(0,5,0,0), A(0,5,8,2), "NFP_V0.2", 3),
        A(0,5,8,3): FI(A(0,5,0,0), A(0,5,8,3), "nfp_v0.3", 5),
        A(0,5,8,4): FI(A(0,5,0,0), A(0,5,8,4), "nfp_v0.4", 11),
        A(0,5,8,5): FI(A(0,5,0,0), A(0,5,8,5), "nfp_v0.5", 10),
    },
    A(0,7,0,0): {
        A(0,7,8,9): FI(A(0,7,0,0), A(0,7,8,9), "xnfp_v0.6x", 0),
    },
}

TEST_DATA_HYDROGEN_VFMAP_ANS = {
    A(0,5,8,1): FI(A(0,5,0,0), A(0,5,8,1), "nfp_v0.1", 2),
    A(0,5,8,2): FI(A(0,5,0,0), A(0,5,8,2), "NFP_V0.2", 3),
    A(0,5,8,3): FI(A(0,5,0,0), A(0,5,8,3), "nfp_v0.3", 5),
    A(0,5,8,4): FI(A(0,5,0,0), A(0,5,8,4), "nfp_v0.4", 11),
    A(0,5,8,5): FI(A(0,5,0,0), A(0,5,8,5), "nfp_v0.5", 10),
    A(0,7,8,9): FI(A(0,7,0,0), A(0,7,8,9), "xnfp_v0.6x", 0),
}

TEST_DATA_HYDROGEN_VFSET_ANS = set([
    A(0,5,8,1),
    A(0,5,8,2),
    A(0,5,8,3),
    A(0,5,8,4),
    A(0,5,8,5),
    A(0,7,8,9),
])

TEST_DATA_NONE = r'''
wrong_number_of_columns
QQQQ:RR:SS.T 0000:04:08.0 nfp_v0.0
'''.lstrip()

TEST_DATA_NONE_PFMAP_ANS = {}
TEST_DATA_NONE_VFMAP_ANS = {}
TEST_DATA_NONE_VFSET_ANS = set()

TEST_DATA_MULTIPLE = r'''
1234:06:50.0 0000:04:08.1 nfp_v0.1  14
0000:05:1E.2 0000:04:08.2 nfp_v1.2 16   $
'''.translate(None, '$').lstrip()

TEST_DATA_MULTIPLE_PFMAP_ANS = {
    A(0x1234,6,0x50,0): {
        A(0,4,8,1): FI(A(0x1234,6,0x50,0), A(0,4,8,1), "nfp_v0.1", 14),
    },
    A(0,5,30,2): {
        A(0,4,8,2): FI(A(0,5,30,2), A(0,4,8,2), "nfp_v1.2", 16),
    },
}

TEST_DATA_MULTIPLE_VFMAP_ANS = {
    A(0,4,8,1): FI(A(0x1234,6,0x50,0), A(0,4,8,1), "nfp_v0.1", 14),
    A(0,4,8,2): FI(A(0,5,30,2), A(0,4,8,2), "nfp_v1.2", 16),
}

TEST_DATA_MULTIPLE_VFSET_ANS = set([
    A(0,4,8,1),
    A(0,4,8,2),
])


class TestSysFS(unittest.TestCase):
    def test_read_sysfs_fallback_map(self):
        self.maxDiff = None

        pairs = (
            (TEST_DATA_HYDROGEN, (
             TEST_DATA_HYDROGEN_PFMAP_ANS,
             TEST_DATA_HYDROGEN_VFMAP_ANS,
             TEST_DATA_HYDROGEN_VFSET_ANS,
             )),
            (TEST_DATA_NONE, (
             TEST_DATA_NONE_PFMAP_ANS,
             TEST_DATA_NONE_VFMAP_ANS,
             TEST_DATA_NONE_VFSET_ANS,
             )),
            (TEST_DATA_MULTIPLE, (
             TEST_DATA_MULTIPLE_PFMAP_ANS,
             TEST_DATA_MULTIPLE_VFMAP_ANS,
             TEST_DATA_MULTIPLE_VFSET_ANS
             )),
        )
        for p in pairs:
            addrs = fallback.read_sysfs_fallback_map(_in=p[0])
            self.assertIsInstance(addrs, fallback.FallbackMap)
            self.assertEqual(addrs.pfmap, p[1][0])
            self.assertEqual(addrs.vfmap, p[1][1])
            self.assertEqual(addrs.vfset, p[1][2])


class TestFallbackInterface(unittest.TestCase):
    def test_eq(self):
        a1 = A(1,2,3,4)
        a5 = A(5,6,7,8)

        self.assertEqual   (FI(a1, a5, 'nfp_x', 3), FI(a1, a5, 'nfp_x', '3'))
        self.assertNotEqual(FI(a1, a5, 'nfp_x', 3), FI(a5, a5, 'nfp_x', '3'))
        self.assertNotEqual(FI(a1, a5, 'nfp_x', 3), FI(a1, a1, 'nfp_x', '3'))
        self.assertNotEqual(FI(a1, a5, 'nfp_x', 3), FI(a1, a5, 'nfp_y', '3'))
        self.assertNotEqual(FI(a1, a5, 'nfp_x', 3), FI(a1, a5, 'nfp_x', '4'))

        self.assertEqual(FI(a1, a5, 'NFP_X', 3).__eq__(3), NotImplemented)
        self.assertEqual(FI(a1, a5, 'NFP_X', 3).__ne__(3), NotImplemented)


TEST_DATA_RESERVED = r'''
0000:04:00.0 0000:04:08.0 nfp_v0.0 0
0000:04:00.0 0000:04:08.1 nfp_v0.1 1
0000:04:00.0 0000:04:08.2 nfp_v0.58 58
0000:04:00.0 0000:04:08.3 nfp_v0.59 59
0000:04:00.0 0000:04:08.4 nfp_v0.60 60
0000:04:00.0 0000:04:08.5 nfp_v0.61 61
'''.translate(None, '$').lstrip()

TEST_DATA_RESERVED_VFSET_ANS = set([
    A(0,4,8,0),
    A(0,4,8,1),
    A(0,4,8,2),
    A(0,4,8,3),
])


class TestFallbackReserved(unittest.TestCase):
    def test_reserved(self):
        with attachLogHandler(_FALLBACK_LOGGER(), LogMessageCounter()) as lmc:
            addrs = fallback.read_sysfs_fallback_map(_in=TEST_DATA_RESERVED)
            self.assertEqual(addrs.vfset, TEST_DATA_RESERVED_VFSET_ANS)
        self.assertEqual(lmc.count, {_FALLBACK_LOGGER.name: {'DEBUG': 1}})


if __name__ == '__main__':
    unittest.main()
