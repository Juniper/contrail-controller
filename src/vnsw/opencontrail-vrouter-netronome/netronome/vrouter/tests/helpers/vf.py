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

from netronome.vrouter import fallback
from netronome.vrouter.tests.unit import *

TWO_VFS = make_vfset('7685:43:21.0 1e1e:1f:21.3')

VFSET1 = make_vfset('''
    0001:06:08.0 0001:06:08.1 0001:06:08.2 0001:06:08.3 0001:06:08.4
    0001:06:08.5 0001:06:08.6 0001:06:08.7
''')

VFSET_REAL = make_vfset('''
    0000:04:08.0 0000:04:08.1 0000:04:08.2 0000:04:08.3 0000:04:08.4
    0000:04:08.5 0000:04:08.6 0000:04:08.7 0000:04:09.0 0000:04:09.1
    0000:04:09.2 0000:04:09.3 0000:04:09.4 0000:04:09.5 0000:04:09.6
    0000:04:09.7 0000:04:0a.0 0000:04:0a.1 0000:04:0a.2 0000:04:0a.3
    0000:04:0a.4 0000:04:0a.5 0000:04:0a.6 0000:04:0a.7 0000:04:0b.0
    0000:04:0b.1 0000:04:0b.2 0000:04:0b.3 0000:04:0b.4 0000:04:0b.5
    0000:04:0b.6 0000:04:0b.7 0000:04:0c.0 0000:04:0c.1 0000:04:0c.2
    0000:04:0c.3 0000:04:0c.4 0000:04:0c.5 0000:04:0c.6 0000:04:0c.7
    0000:04:0d.0 0000:04:0d.1 0000:04:0d.2 0000:04:0d.3 0000:04:0d.4
    0000:04:0d.5 0000:04:0d.6 0000:04:0d.7 0000:04:0e.0 0000:04:0e.1
    0000:04:0e.2 0000:04:0e.3 0000:04:0e.4 0000:04:0e.5 0000:04:0e.6
    0000:04:0e.7 0000:04:0f.0 0000:04:0f.1 0000:04:0f.2 0000:04:0f.3
    0000:04:0f.4 0000:04:0f.5
''')


def fake_FallbackMap(vfset):
    """Create a fake FallbackMap for VF pool tests."""
    ans = fallback.FallbackMap()
    ans.vfset = copy.copy(vfset)
    return ans
