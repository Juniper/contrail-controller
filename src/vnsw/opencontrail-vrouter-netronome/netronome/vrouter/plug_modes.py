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

__all__ = ('all_plug_modes', 'accelerated_plug_modes')

SRIOV = 'SR-IOV'
VirtIO = 'VirtIO'
unaccelerated = 'unaccelerated'

all_plug_modes = (SRIOV, VirtIO, unaccelerated)
accelerated_plug_modes = (SRIOV, VirtIO)
