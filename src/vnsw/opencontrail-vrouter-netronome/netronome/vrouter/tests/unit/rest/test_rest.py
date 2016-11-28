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
import werkzeug.datastructures

from netronome.vrouter.rest import *

__all__ = ['AssertContentType', 'AssertResponseCode']


# Library code for other tests

class AssertContentType(object):
    def assertContentType(self, headers, expected_content_type):
        content_type = parse_content_type(headers)
        self.assertIsNotNone(content_type)
        self.assertEqual(content_type[0], expected_content_type)


class AssertResponseCode(object):
    def assertIsSuccess(self, status):
        tc = HTTPSuccessTc()
        assert tc.check(status), tc.get_message(status)

    def assertIsRedirect(self, status):
        tc = HTTPRedirectTc()
        assert tc.check(status), tc.get_message(status)


# Some actual tests of netronome.vrouter.rest

class TestParseContentType(unittest.TestCase):
    def _assertContentType(self, headers, expected_content_type):
        content_type = parse_content_type(
            werkzeug.datastructures.Headers(defaults=headers)
        )
        self.assertIsInstance(content_type, tuple)
        self.assertIsNotNone(content_type[0])
        self.assertEqual(content_type[0], expected_content_type)

    def test_parse_content_type(self):
        self._assertContentType(
            {'cOnTeNt-TyPe': 'application/json'}, 'application/json'
        )
        self._assertContentType(
            {'cOnTeNt-TyPe': 'text/plain;charset=UTF-9'}, 'text/plain'
        )
        self._assertContentType({}, '')
        self._assertContentType(None, '')


class TestAssertResponseCode(unittest.TestCase):
    def _test_tc(self, tc, good, bad):
        for status in good:
            self.assertTrue(tc.check(status), tc.get_message(status))
        for status in bad:
            self.assertFalse(tc.check(status), tc.get_message(status))

    def test_SuccessTc(self):
        self._test_tc(
            HTTPSuccessTc(),
            (200, 201, 204,),
            (101,
             300, 301, 302, 307,
             400, 404, 418, 423,)
        )

    def test_RedirectTc(self):
        self._test_tc(
            HTTPRedirectTc(),
            (300, 301, 302, 307,),
            (101,
             200, 201, 204,
             400, 404, 418, 423,)
        )


if __name__ == '__main__':
    unittest.main()
