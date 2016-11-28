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

# Similar interface to Moose::Meta::TypeConstraint, for command line
# scripts/REST API parameter sanity checking.

import re


class Tc(object):
    def assert_valid(self, value):
        err = self.validate(value)
        if err is not None:
            raise ValueError(err)
        else:
            return True

    def validate(self, value):
        if not self.check(value):
            return self.get_message(value)


class PciAddressParam(Tc):
    def __init__(self, **kwds):
        super(Tc, self).__init__(**kwds)

        # This is quite permissive since we want to allow values like "default"
        # in addition to real PCI addresses.
        self._re = re.compile(r'^[A-Z0-9:.]+\Z', re.I)

    def check(self, value):
        return isinstance(value, basestring) and self._re.match(value)

    def get_message(self, value):
        return 'invalid PCI address: "{}"'.format(value)


class NetworkNameParam(Tc):
    def __init__(self, **kwds):
        super(Tc, self).__init__(**kwds)

        self._re = re.compile(r'^[A-Z_][A-Z0-9_]*\Z', re.I)

    def check(self, value):
        return isinstance(value, basestring) and self._re.match(value)

    def get_message(self, value):
        return 'invalid network name: "{}"'.format(value)


class TargetDevParam(Tc):
    def __init__(self, **kwds):
        super(Tc, self).__init__(**kwds)

    def check(self, value):
        # a target device name must contain only non-space ASCII characters,
        # after stripping leading and trailing space
        if not isinstance(value, basestring):
            return False

        value = value.strip()
        return value != '' and not re.search(r'[^\x21-\x7E]', value)

    def get_message(self, value):
        return 'invalid target device name: "{}"'.format(value)
