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

import errno
import logging
import os
import re

from netronome.vrouter import pci

logger = logging.getLogger(__name__)

# Number of non-reserved fallback devices as specified in the ARD. (This
# corresponds to the constant MAX_RELAYS in virtiorelayd.)
ARD_MINIMUM_RESERVED_FALLBACK = 60


class FallbackInterface(object):
    """Represents an entry in an nfp_vrouter.ko sysfs fallback_map."""

    def __init__(self, pf_addr, vf_addr, netdev, vf_number, **kwds):
        super(FallbackInterface, self).__init__(**kwds)

        if not isinstance(pf_addr, pci.PciAddress):
            raise ValueError('pf_addr must be a PciAddress')
        if not isinstance(vf_addr, pci.PciAddress):
            raise ValueError('vf_addr must be a PciAddress')
        if not isinstance(netdev, basestring):
            raise ValueError('netdev must be a string')

        self.pf_addr = pf_addr
        self.vf_addr = vf_addr
        self.netdev = netdev
        self.vf_number = int(vf_number)

    def __str__(self):
        return self.netdev

    def __repr__(self):
        return "<{}(pf_addr={}, vf_addr={}, netdev={}, vf_number={})>".format(
            type(self).__name__, self.pf_addr, self.vf_addr, self.netdev,
            self.vf_number
        )

    def __eq__(self, other):
        if not isinstance(other, FallbackInterface):
            return NotImplemented

        return self.pf_addr    == other.pf_addr \
            and self.vf_addr   == other.vf_addr \
            and self.netdev    == other.netdev  \
            and self.vf_number == other.vf_number

    def __ne__(self, other):
        eq = self.__eq__(other)
        return eq if eq is NotImplemented else not eq


class FallbackMap(object):
    """Represents an nfp_vrouter.ko sysfs fallback_map."""

    def __init__(self, **kwds):
        super(FallbackMap, self).__init__(**kwds)
        self.pfmap = {}
        self.vfmap = {}
        self.vfset = set()

    def add_interface(self, interface):
        if interface.pf_addr not in self.pfmap:
            self.pfmap[interface.pf_addr] = {}
        self.pfmap[interface.pf_addr][interface.vf_addr] = interface
        self.vfmap[interface.vf_addr] = interface
        self.vfset.add(interface.vf_addr)


def read_sysfs_fallback_map(_in=None):
    """Read the nfp_vrouter.ko sysfs fallback_map."""

    out = FallbackMap()

    if _in is None:
        fname = '/sys/module/nfp_vrouter/control/fallback_map'
        try:
            with open(fname, 'r') as fh:
                _in = fh.read()
        except IOError as e:
            if e.errno != errno.ENOENT:
                raise
            _in = ''

    ignored = {}
    for line in _in.split('\n'):
        cols = re.split(r'\s+', line.rstrip())
        if len(cols) != 4:
            continue

        netdev = cols[2]
        try:
            pf_addr = pci.parse_pci_address(cols[0])
            vf_addr = pci.parse_pci_address(cols[1])
            vf_number = int(cols[3])
        except ValueError:
            continue

        # Ignore reserved fallback devices (VRT-200).
        if vf_number >= ARD_MINIMUM_RESERVED_FALLBACK:
            ignored[vf_addr] = vf_number
            continue

        out.add_interface(
            FallbackInterface(pf_addr, vf_addr, netdev, vf_number)
        )

    if ignored:
        ignored_str = ', '.join(
            '{}={}'.format(k, ignored[k]) for k in sorted(ignored.iterkeys())
        )
        logger.debug(
            '%s',
            'ignoring VFs in the reserved range (>= {}): {}'.format(
                ARD_MINIMUM_RESERVED_FALLBACK, ignored_str
            )
        )

    return out
