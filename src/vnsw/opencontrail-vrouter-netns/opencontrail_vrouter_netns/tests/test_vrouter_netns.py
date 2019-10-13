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

from builtins import str
import mock
import netaddr
import requests
import unittest
import uuid

from opencontrail_vrouter_netns.vrouter_netns import NetnsManager


NIC1_UUID = str(uuid.uuid4())
NIC1 = {'uuid': NIC1_UUID,
        'mac': netaddr.EUI('00:11:22:33:44:55'),
        'ip': netaddr.IPNetwork('172.16.0.12/24')}

NIC2_UUID = str(uuid.uuid4())
NIC2 = {'uuid': NIC2_UUID,
        'mac': netaddr.EUI('66:77:88:99:aa:bb'),
        'ip': netaddr.IPNetwork('80.0.0.123/29')}


class NetnsManagerTest(unittest.TestCase):
    def setUp(self):
        self.ip_cls_p = mock.patch('opencontrail_vrouter_netns.linux.ip_lib.'
                                   'IPWrapper')
        self.ip_cls_p.start()

        self.post_p = mock.patch('requests.post')
        self.mock_post = self.post_p.start()

        self.delete_p = mock.patch('requests.delete')
        self.mock_delete = self.delete_p.start()

    def tearDown(self):
        self.ip_cls_p.stop()
        self.post_p.stop()
        self.delete_p.stop()

    def _add_port_to_agent(self, status_code=200):
        self.netns_mgr = NetnsManager('fake_vm_uuid', NIC1, NIC2)
        self.netns_mgr.vrouter_client = mock.Mock()
        self.netns_mgr._get_tap_name = mock.Mock()
        self.netns_mgr._get_tap_name.return_value = 'tap1234'

        resp = requests.Response()
        resp.status_code = status_code
        self.mock_post.return_value = resp

        self.netns_mgr._add_port_to_agent(NIC1)

    def test_add_port_to_agent(self):
        self._add_port_to_agent()

        self.mock_post.assert_called_with(
            'http://localhost:9091/port',
            headers={'content-type': 'application/json'},
            data=('{"tx-vlan-id": -1, '
                  '"ip-address": "172.16.0.12", '
                  '"display-name": null, '
                  '"id": "%s", '
                  '"instance-id": "fake_vm_uuid", '
                  '"ip6-address": "", '
                  '"rx-vlan-id": -1, '
                  '"vn-id": "", '
                  '"vm-project-id": "", '
                  '"type": 1, '
                  '"mac-address": "00-11-22-33-44-55", '
                  '"system-name": "tap1234"}') % NIC1_UUID)

    def test_add_port_to_agent_fails(self):
        self.assertRaises(ValueError,
                          self._add_port_to_agent,
                          500)

    def _delete_port_from_agent(self, status_code=200):
        self.netns_mgr = NetnsManager('fake_vm_uuid', NIC1, NIC2)
        self.netns_mgr.vrouter_client = mock.Mock()

        resp = requests.Response()
        resp.status_code = status_code
        self.mock_delete.return_value = resp

        self.netns_mgr._delete_port_to_agent(NIC1)

    def test_delete_port_from_agent(self):
        self._delete_port_from_agent()

        self.mock_delete.assert_called_with(
            'http://localhost:9091/port/%s' % NIC1_UUID,
            headers={'content-type': 'application/json'},
            data=None)

    def test_delete_port_to_agent_fails(self):
        self.assertRaises(ValueError,
                          self._delete_port_from_agent,
                          500)
