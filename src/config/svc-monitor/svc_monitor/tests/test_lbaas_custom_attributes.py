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
import svc_monitor.services.loadbalancer.drivers.ha_proxy.custom_attributes.haproxy_validator as validator
from sys import version_info
if version_info.major == 2:
    import __builtin__ as builtins
else:
    import builtins

custom_attributes_dict = {
    'global': {
        'ssl_ciphers': {
            'type': 'str',
            'limits': [1, 100],
            'cmd': 'ssl-default-bind-ciphers %s'
        },
    },
    'default': {
        'server_timeout': {
            'type': 'int',
            'limits': [1, 5000000],
             'cmd': 'timeout server %d'
        },
        'client_timeout': {
            'type': 'int',
            'limits': [1, 5000000],
            'cmd': 'timeout client %d'
        },
    },
    'frontend': {
        'tls_container': {
            'type': 'CustomAttrTlsContainer',
            'limits': None,
            'cmd': None
        }
    },
    'backend': {},
}

class CustomAttributeTest(unittest.TestCase):
    def test_false_custom_attributes(self):
        fake_config = {
            'key1': 'value1', 'key2': 'value2'
        }
        resp_dict = validator.validate_custom_attributes(custom_attributes_dict,
                                                         'global', fake_config)
        self.assertFalse('key1' in resp_dict.keys() or \
                         'key2' in resp_dict.keys())

    def test_mixed_custom_attributes(self):
        fake_config = {
            'key': 'value', 'server_timeout': '50000'
        }
        resp_dict = validator.validate_custom_attributes(custom_attributes_dict,
                                                         'default', fake_config)
        self.assertTrue('key' not in resp_dict.keys() and \
                        'server_timeout' in resp_dict.keys())
