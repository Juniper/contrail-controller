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

import re


class PciAddress(object):
    def __init__(self, domain, bus, slot, function, **kwds):
        super(PciAddress, self).__init__(**kwds)
        self.domain   = int(domain)
        self.bus      = int(bus)
        self.slot     = int(slot)
        self.function = int(function)

    def __str__(self):
        return '{:04x}:{:02x}:{:02x}.{:x}'.format(
            self.domain, self.bus, self.slot, self.function
        )

    def __repr__(self):
        return "<{}={}>".format(type(self).__name__, str(self))

    def __hash__(self):
        return self.domain + self.bus + self.slot + self.function

    def __eq__(self, other):
        ans = self.__cmp__(other)
        return ans if ans is NotImplemented else ans == 0

    def __ne__(self, other):
        ans = self.__cmp__(other)
        return ans if ans is NotImplemented else ans != 0

    def __cmp__(self, other):
        if not isinstance(other, PciAddress):
            return NotImplemented

        d = self.domain   - other.domain
        b = self.bus      - other.bus
        s = self.slot     - other.slot
        f = self.function - other.function

        return d or b or s or f

    def copy_to(self, other):
        other.domain   = self.domain
        other.bus      = self.bus
        other.slot     = self.slot
        other.function = self.function

PCI_ADDRESS_RE = re.compile(
    r'''^
        (?P<domain>   [0-9A-F]{4}):
        (?P<bus>      [0-9A-F]{2}):
        (?P<slot>     [0-9A-F]{2})\.
        (?P<function> [0-9A-F]) \Z''', re.I | re.X)


def parse_pci_address(addr_str):
    """Converts a string to a PciAddress object."""
    m = PCI_ADDRESS_RE.match(addr_str)
    if m is None:
        raise ValueError('invalid PCI address: "{}"'.format(addr_str))

    return PciAddress(
        domain=int(m.group('domain'), 16),
        bus=int(m.group('bus'), 16),
        slot=int(m.group('slot'), 16),
        function=int(m.group('function'), 16),
    )
