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

from netronome.vrouter.pci import PciAddress, parse_pci_address


def _test_addr_args():
    for domain in range(0,4):
        for bus in range(0,4):
            for slot in range(0,4):
                for function in range(0,4):
                    yield {
                        'domain': domain,
                        'bus': bus,
                        'slot': slot,
                        'function': function
                    }


class TestPciAddress(unittest.TestCase):
    """Tests PciAddress object."""

    def test_PciAddress_ctor(self):
        addr = PciAddress(domain=0, bus=2, slot=3, function=4)

    def test_PciAddress_str(self):
        addr = PciAddress(domain=1, bus=3, slot=13, function=0)
        self.assertEqual(str(addr), '0001:03:0d.0')

    def test_PciAddress_eq(self):
        for a in _test_addr_args():
            addr1 = PciAddress(**a)
            addr2 = PciAddress(**a)
            self.assertEqual(addr1, addr2)

    def test_PciAddress_eq_None(self):
        addr = PciAddress(domain=1, bus=3, slot=13, function=0)
        # Some 3rd party code does "== None" instead of "is None", therefore
        # "== None" must not crash.
        addr == None  # noqa

    def test_PciAddress_ne(self):
        for a in _test_addr_args():
            addr1 = PciAddress(**a)
            for k in a.iterkeys():
                for add in (-2,-1,1,2):
                    aa = a.copy()
                    aa[k] += add
                    addr2 = PciAddress(**aa)
                    self.assertNotEqual(addr1, addr2)

    def test_PciAddress_cmp(self):
        data = (
            ((1,2,3,4), (0,2,3,4),  1),
            ((1,2,3,4), (1,2,3,4),  0),
            ((0,3,3,4), (0,4,3,4), -1),
            ((0,3,3,4), (0,3,3,4),  0),
            ((0,3,3,4), (0,2,3,4),  1),
            ((0,0,5,4), (0,0,6,4), -1),
            ((0,0,5,4), (0,0,4,4),  1),
            ((0,0,0,4), (0,0,0,5), -1),
            ((0,0,0,6), (0,0,0,5),  1),
            ((0,0,0,5), (0,0,0,5),  0),
        )

        for d in data:
            a0 = PciAddress(*d[0])
            a1 = PciAddress(*d[1])
            if d[2] > 0:
                self.assertGreater(a0, a1)
            elif d[2] < 0:
                self.assertLess(a0, a1)
            else:
                self.assertEqual(a0, a1)

    def test_PciAddress_NotImplemented(self):
        self.assertEqual(PciAddress(0, 0, 0, 0).__cmp__(3), NotImplemented)
        self.assertEqual(PciAddress(0, 0, 0, 0).__eq__ (3), NotImplemented)
        self.assertEqual(PciAddress(0, 0, 0, 0).__ne__ (3), NotImplemented)

    def test_PciAddress_copy_to(self):
        class A(object):
            pass

        a = A()
        a.form = ['sloar']  # arbitrary variables should be left alone

        addr = parse_pci_address('abcd:ef:78.9')
        addr.copy_to(a)

        self.assertEqual(vars(a), {
            'domain'  : 0xabcd,
            'bus'     : 0xef,
            'slot'    : 0x78,
            'function': 9,
            'form'    : ['sloar'],
        })

    def test_parse_pci_address(self):
        good = (
            ('1234:56:78.f', PciAddress(0x1234, 0x56, 0x78, 15)),
            ('0000:00:00.0', PciAddress(0, 0, 0, 0)),
        )
        for g in good:
            addr = parse_pci_address(g[0])
            self.assertIsInstance(addr, PciAddress)
            self.assertEqual(addr, g[1])

    def test_parse_pci_address_bad_arg(self):
        bad = (
            '123:45:67.e', '', None, 3, '1234:5:67.f', '1234:56:78:f',
            '0000:00:00.0\n'
        )
        for b in bad:
            with self.assertRaises((TypeError, ValueError)):
                addr = parse_pci_address(b)

if __name__ == '__main__':
    unittest.main()
