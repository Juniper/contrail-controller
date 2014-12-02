#!/usr/bin/python
# -*- coding: utf-8 -*-

# vim: tabstop=4 shiftwidth=4 softtabstop=4

# Copyright (c) 2014 Cloudwatt
# All Rights Reserved.
#
#    Licensed under the Apache License, Version 2.0 (the "License"); you may
#    not use this file except in compliance with the License. You may obtain
#    a copy of the License at
#
#         http://www.apache.org/licenses/LICENSE-2.0
#
#    Unless required by applicable law or agreed to in writing, software
#    distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
#    WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
#    License for the specific language governing permissions and limitations
#    under the License.
#
# @author: Numan Siddique, eNovance.

from cfgm_common import vnc_unicode_utils
import testscenarios
import testtools

load_tests = testscenarios.load_tests_apply_scenarios


class TestUnicodeUtils(testtools.TestCase):
    scenarios = [
        ('scenario_1', dict(input_str='netéù',
                            expected='net%C3%A9%C3%B9',
                            encode=True)),
        ('scenario_2', dict(input_str=u'netéù',
                            expected='net%C3%A9%C3%B9',
                            encode=True)),
        ('scenario_3', dict(input_str='net%C3%A9%C3%B9',
                            expected=u'netéù',
                            encode=False)),
        ('scenario_4', dict(input_str=u'net%C3%A9%C3%B9',
                            expected=u'netéù',
                            encode=False)),

        ('scenario_5', dict(input_str='ùüéêœô',
                            expected='%C3%B9%C3%BC%C3%A9%C3%AA%C5%93%C3%B4',
                            encode=True)),
        ('scenario_6', dict(input_str=u'ùüéêœô',
                            expected='%C3%B9%C3%BC%C3%A9%C3%AA%C5%93%C3%B4',
                            encode=True)),
        ('scenario_7', dict(input_str='%C3%B9%C3%BC%C3%A9%C3%AA%C5%93%C3%B4',
                            expected=u'ùüéêœô',
                            encode=False)),
        ('scenario_8', dict(input_str=u'%C3%B9%C3%BC%C3%A9%C3%AA%C5%93%C3%B4',
                            expected=u'ùüéêœô',
                            encode=False)),

        ('scenario_9', dict(input_str=['demo', 'ùüéêœô', 'project'],
                            expected=['demo',
                                      '%C3%B9%C3%BC%C3%A9%C3%AA%C5%93%C3%B4',
                                      'project'],
                            encode=True)),
        ('scenario_10', dict(input_str=[(
            'demo', u'%C3%B9%C3%BC%C3%A9%C3%AA%C5%93%C3%B4', 'project')],
            expected=['demo', u'ùüéêœô', 'project'],
            encode=False)),

        ('scenario_11', dict(input_str=['demo', 'ùüéêœô', 'project'],
                             expected=['demo', 'ùüéêœô', 'project'],
                             encode=False)),

        # scenario to test that encode_str_list should not encode a
        # string if already encoded
        ('scenario_12', dict(input_str=['demo', 'net%25C3%25A9%25C3%25B9',
                                        'project'],
                             expected=['demo', 'net%25C3%25A9%25C3%25B9',
                                       'project'],
                             encode=True)),
    ]

    def _test_encode_string(self):
        return vnc_unicode_utils.encode_string(self.input_str)

    def _test_decode_string(self):
        return vnc_unicode_utils.decode_string(self.input_str)

    def _test_encode_str_list(self):
        vnc_unicode_utils.encode_str_list(self.input_str)

    def _test_decode_str_list(self):
        vnc_unicode_utils.decode_str_list(self.input_str)

    def test_encode_utils(self):
        if type(self.input_str) is list:
            if self.encode:
                self._test_encode_str_list()
            else:
                self._test_decode_str_list()
            self.assertEqual(self.input_str, self.expected)
        else:
            if self.encode:
                observed = self._test_encode_string()
            else:
                observed = self._test_decode_string()
            self.assertEqual(self.expected, observed)
        