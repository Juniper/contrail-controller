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

import struct


class RandMac(object):
    def __init__(self, **kwds):
        super(RandMac, self).__init__(**kwds)
        self.fh = open('/dev/urandom', 'rb')

    def generate(self):
        bytes = list(struct.unpack("6B", self.fh.read(6)))
        bytes[0] = bytes[0] & 0xfc | 2
        return ':'.join(map(lambda byte: '{:02x}'.format(byte), bytes))
