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

"""
Tests few of the vnc_api/gen/resource_common.py
and the vnc_api/gen/resource_client.py for the unicode conversion
when utf-8 characters are used
"""

import testtools
from vnc_api.gen import resource_client


class FakeParentObject(object):
    def __init__(self, fq_name):
        self.fq_name = fq_name
        self._type = 'fake_type'


class TestResourceClassesForUnicode(testtools.TestCase):
    def setUp(self):
        super(TestResourceClassesForUnicode, self).setUp()
        self.fake_parent_obj = FakeParentObject([u'default-domain',
                                                 u'default-project'])

    def test_if_fq_name_encoded(self):
        net_name = 'eçéùqpàèaù'
        encoded_net_name = 'e%C3%A7%C3%A9%C3%B9qp%C3%A0%C3%A8a%C3%B9'

        net_obj = resource_client.VirtualNetwork(net_name,
                                                 self.fake_parent_obj)

        expected_net_fq_name = list(self.fake_parent_obj.fq_name)
        expected_net_fq_name.append(encoded_net_name)
        self.assertEqual(expected_net_fq_name, net_obj.fq_name)

    def test_if_fq_name_decoded(self):
        encoded_net_name = u'e%C3%A7%C3%A9%C3%B9qp%C3%A0%C3%A8a%C3%B9'
        decoded_net_name = u'eçéùqpàèaù'

        encoded_fq_name = list(self.fake_parent_obj.fq_name)
        encoded_fq_name.append(encoded_net_name)
        kwargs = {'fq_name': encoded_fq_name, 'uuid': 'fake_uuid'}

        expected_fq_name = list(self.fake_parent_obj.fq_name)
        expected_fq_name.append(decoded_net_name)
        net_obj = resource_client.VirtualNetwork.from_dict(**kwargs)
        self.assertEqual(expected_fq_name, net_obj.fq_name)
