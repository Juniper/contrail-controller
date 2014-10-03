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
# @author: Edouard Thuleau, Cloudwatt.

import mock
import netaddr
import unittest
import uuid

from opencontrail_vrouter_netns.vrouter_netns import NetnsManager


NIC1 = {'uuid': str(uuid.uuid4()),
        'mac': netaddr.EUI('00:11:22:33:44:55'),
        'ip': netaddr.IPNetwork('172.16.0.12/24')}

NIC2 = {'uuid': str(uuid.uuid4()),
        'mac': netaddr.EUI('66:77:88:99:aa:bb'),
        'ip': netaddr.IPNetwork('80.0.0.123/29')}


class NetnsManagerTest(unittest.TestCase):
    def setUp(self):
        self.ip_cls_p = mock.patch('opencontrail_vrouter_netns.linux.ip_lib.'
                                   'IPWrapper')
        ip_cls = self.ip_cls_p.start()
        self.mock_ip = mock.MagicMock()
        ip_cls.return_value = self.mock_ip

    def test_add_port_to_agent(self):
        self.netns_mgr = NetnsManager('fake_vm_uuid', NIC1, NIC2)
        self.netns_mgr.vrouter_client = mock.Mock()
        self.netns_mgr._get_tap_name = mock.Mock()
        self.netns_mgr._get_tap_name.return_value = 'tap1234'

        self.netns_mgr._add_port_to_agent(NIC1)
        kwargs = {}
        kwargs['ip_address'] = str(NIC1['ip'].ip)
        self.netns_mgr.vrouter_client.add_port.assert_called_with(
            'fake_vm_uuid', NIC1['uuid'], 'tap1234', str(NIC1['mac']),
            **kwargs)

    def test_delete_port_to_agent(self):
        self.netns_mgr = NetnsManager('fake_vm_uuid', NIC1, NIC2)
        self.netns_mgr.vrouter_client = mock.Mock()

        self.netns_mgr._delete_port_to_agent(NIC1)
        self.netns_mgr.vrouter_client.delete_port.assert_called_with(
            NIC1['uuid'])
