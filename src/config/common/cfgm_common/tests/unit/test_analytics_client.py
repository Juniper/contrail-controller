from __future__ import unicode_literals
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

from builtins import range
from builtins import object
import os
import mock
import unittest
from requests.exceptions import ConnectionError
from testtools import content, content_type, ExpectedException

from cfgm_common.analytics_client import Client
from cfgm_common.utils import find_buildroot

builddir = find_buildroot(os.getcwd())

class TestOpenContrailClient(unittest.TestCase):

    def setUp(self):
        super(TestOpenContrailClient, self).setUp()
        self.client = Client('127.0.0.1', 8081, {'arg1': 'aaa'})

        self.get_resp = mock.MagicMock()
        self.get = mock.patch('requests.get',
                              return_value=self.get_resp).start()
        self.get_resp.raw_version = 1.1
        self.get_resp.status_code = 200

    def test_round_robin_client(self):
        client_ips = "127.0.1.1 127.0.1.2 127.0.1.3"
        expected_ip_list = ['127.0.1.1', '127.0.1.2', '127.0.1.3']
        new_client = Client(client_ips, 8081, {'arg1': 'aaaa'})

        self.get_resp_multiple = mock.MagicMock()
        self.get_multiple = mock.patch('requests.get',
                              return_value=self.get_resp_multiple).start()
        self.get_resp_multiple.raw_version = 1.1
        self.get_resp_multiple.status_code = 200

        index = -1
        for i in range(6):
            index += 1
            if index >= len(expected_ip_list):
                index = 0

            new_client.request('/fake/path/', 'fake_uuid')

            call_args = self.get_multiple.call_args_list[i][0]
            call_kwargs = self.get_multiple.call_args_list[i][1]

            expected_url = 'http://%s:8081' % expected_ip_list[index] + '/fake/path/fake_uuid'
            self.assertEqual(expected_url, call_args[0])

            data = call_kwargs.get('data')
            expected_data = {'arg1': 'aaaa'}
            self.assertEqual(expected_data, data)

    def mocked_response(*args, **kwargs):
        class MockResponse(object):
            def __init__(self, json_data, status_code):
                self.status_code = status_code
                self.json_data = json_data
                self.raw_version = 1.1
            def json(self):
                return self.json_data

        if args[1] == 'http://127.0.1.1:8081/fake/path/fake_uuid':
            raise ConnectionError

        else:
            return MockResponse({"response": "success"}, 200)

    def test_round_robin_client_with_failure(self):
        client_ips = "127.0.1.1 127.0.1.2 127.0.1.3"
        expected_ip_list = ['127.0.1.1', '127.0.1.2', '127.0.1.3']
        new_client = Client(client_ips, 8081, {'arg1': 'aaaa'})

        self.get_multiple = mock.patch('requests.get',
                              side_effect=self.mocked_response).start()

        index = -1
        failure_count = 0

        for i in range(6):
            index += 1
            if index >= len(expected_ip_list):
                index = 0

            #Since we are making the MOCK fail for 127.0.1.1 (index 0), every time we hit this
            #increase the failure count to check the call_args_list
            #We will get response from only 127.0.1.2 and 127.0.1.3

            if index == 0:
                failure_count += 1

            new_client.request('/fake/path/', 'fake_uuid')

            call_args = self.get_multiple.call_args_list[i+failure_count][0]
            call_kwargs = self.get_multiple.call_args_list[i+failure_count][1]

            expected_url = 'http://%s:8081' % expected_ip_list[(index+failure_count) %3] + '/fake/path/fake_uuid'
            self.assertEqual(expected_url, call_args[0])

            data = call_kwargs.get('data')
            expected_data = {'arg1': 'aaaa'}
            self.assertEqual(expected_data, data)

    def test_round_robin_client_with_all_failure(self):
        client_ips = "127.0.1.1 127.0.1.2 127.0.1.3"
        expected_ip_list = ['127.0.1.1', '127.0.1.2', '127.0.1.3']
        new_client = Client(client_ips, 8081, {'arg1': 'aaaa'})

        self.get_multiple = mock.patch('requests.get',
                              side_effect = ConnectionError()).start()

        index = -1
        for i in range(6):
            index += 1
            if index >= len(expected_ip_list):
                index = 0

            with ExpectedException(ConnectionError) as e:
                new_client.request('/fake/path/', 'fake_uuid')

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
                            data={'key1': 'value1',
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
                            data={'key3': 'value3',
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

    def test_analytics_request_with_certificates(self):
        api_params = {'ssl_enable': True,
                      'insecure_enable': False,
                      'ca_cert': builddir +'/opserver/test/data/ssl/ca-cert.pem',
                      'keyfile': builddir + '/opserver/test/data/ssl/server-privkey.pem',
                      'certfile': builddir + '/opserver/test/data/ssl/server.pem'}

        self.client.set_analytics_api_ssl_params(analytics_api_ssl_params = \
                                                 api_params)
        self.client.request('fake/path/', 'fake_uuid')

        call_args = self.get.call_args_list[0][0]
        call_kwargs = self.get.call_args_list[0][1]

        expected_url = ('https://127.0.0.1:8081/fake/path/fake_uuid')
        self.assertEqual(expected_url, call_args[0])

        data = call_kwargs.get('data')

        expected_data = {'arg1': 'aaa'}
        self.assertEqual(expected_data, data)
