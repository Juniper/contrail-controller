# Copyright (c) 2016 Symantec
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
# @author: Varun Lodaya, Symantec.

import unittest
import mock
from opencontrail_vrouter_netns.haproxy_cert import GenericCertManager, BarbicanCertManager
import opencontrail_vrouter_netns.keystone_auth as keystone_auth
import opencontrail_vrouter_netns.haproxy_validator as haproxy_validator
from sys import version_info
if version_info.major == 2:
    import __builtin__ as builtins
else:
    import builtins


def mocked_requests_get(*args, **kwargs):
    class MockResponse:
        def __init__(self, data, data_type, status_code):
           self.text = data
           self.status_code = status_code
           self.data_type = data_type

        def text(self):
            return self.text

    if args[0] == 'http://barbican/v1/containers/uuid':
        return MockResponse('{"secret_refs": [{"secret_ref": "http://barbican/v1/secrets/uuid"}]}', "json", 200)
    else:
        return MockResponse("secret", "text", 200)

    return MockResponse("", "text", 404)

class MockedIdentity:
    def __init__(self, args_dict):
        self.barbican_ep = 'http://barbican/v1'
        self.auth_token = '1234'

class CustomAttributeTest(unittest.TestCase):
    def test_false_custom_attributes(self):
        fake_config = {
            'custom-attributes': {'key1': 'value1', 'key2': 'value2'}
        }
        resp_dict = haproxy_validator.validate_custom_attributes(fake_config, 'global', None)
        self.assertFalse('key1' in resp_dict.keys() or 'key2' in resp_dict.keys())

    def test_mixed_custom_attributes(self):
        fake_config = {
            'custom-attributes': {'key': 'value', 'server_timeout': '50000'}
        }
        resp_dict = haproxy_validator.validate_custom_attributes(fake_config, 'default', None)
        self.assertTrue('key' not in resp_dict.keys() and 'server_timeout' in resp_dict.keys())

    def test_missing_custom_attr_conf_file(self):
        fake_config = {
            'custom-attributes': {'tls_container': 'http://barbican/v1'}
        }
        self.assertRaises(haproxy_validator.validate_custom_attributes(fake_config, 'vip', None))

    def test_generic_cert_manager_read(self):
        with mock.patch.object(builtins, 'open', mock.mock_open(read_data='secret')):
            cert_manager = GenericCertManager()
            self.assertEqual(cert_manager._populate_tls_pem('foo'), 'secret')

    @mock.patch('keystone_auth.Identity', side_effect=MockedIdentity)
    @mock.patch('haproxy_cert.requests.get', side_effect=mocked_requests_get)
    def test_barbican_cert_manager_read(self, mockClass, mock_get):
        fake_args_dict = {}
        identity = keystone_auth.Identity(fake_args_dict)
        cert_manager = BarbicanCertManager(identity)
        data = cert_manager._populate_tls_pem('http://barbican/v1/containers/uuid')
        self.assertEqual(data, "secret\n")

