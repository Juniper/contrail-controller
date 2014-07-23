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
# @author: Sylvain Afchain, eNovance.

import mock
import unittest

from cfgm_common.analytics_client import Client


class TestOpenContrailClient(unittest.TestCase):

    def setUp(self):
        super(TestOpenContrailClient, self).setUp()
        self.client = Client('http://127.0.0.1:8081', {'arg1': 'aaa'})

        self.get_resp = mock.MagicMock()
        self.get = mock.patch('requests.get',
                              return_value=self.get_resp).start()
        self.get_resp.raw_version = 1.1
        self.get_resp.status_code = 200

    def test_analytics_request_without_data(self):
        self.client.request('/fake/path/', 'fake_uuid')

        call_args = self.get.call_args_list[0][0]
        call_kwargs = self.get.call_args_list[0][1]

        expected_url = ('http://127.0.0.1:8081/fake/path/fake_uuid')
        self.assertEqual(expected_url, call_args[0])

        data = call_kwargs.get('data')

        expected_data = {'arg1': 'aaa'}
        self.assertEqual(expected_data, data)

    def test_analytics_request_with_data(self):
        self.client.request('fake/path/', 'fake_uuid',
                            {'key1': 'value1',
                             'key2': 'value2'})

        call_args = self.get.call_args_list[0][0]
        call_kwargs = self.get.call_args_list[0][1]

        expected_url = ('http://127.0.0.1:8081/fake/path/fake_uuid')
        self.assertEqual(expected_url, call_args[0])

        data = call_kwargs.get('data')

        expected_data = {'arg1': 'aaa',
                         'key1': 'value1',
                         'key2': 'value2'}
        self.assertEqual(expected_data, data)

        self.client.request('fake/path/', 'fake_uuid',
                            {'key3': 'value3',
                             'key4': 'value4'})

        call_args = self.get.call_args_list[1][0]
        call_kwargs = self.get.call_args_list[1][1]

        expected_url = ('http://127.0.0.1:8081/fake/path/fake_uuid')
        self.assertEqual(expected_url, call_args[0])

        data = call_kwargs.get('data')

        expected_data = {'arg1': 'aaa',
                         'key3': 'value3',
                         'key4': 'value4'}
        self.assertEqual(expected_data, data)