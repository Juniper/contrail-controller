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
import unittest

import cfgm_common.analytics_client as analytics
import svc_monitor.scheduler.vrouter_scheduler as scheduler
from vnc_api.vnc_api import VirtualRouter, VirtualMachine


RUNNING_VROUTER_UVES_STATUS = {
    "NodeStatus": {
        "process_status": [
            {
                "instance_id": "0",
                "module_id": "VRouterAgent",
                "state": "Functional",
                "connection_infos": [
                    {
                        "server_addrs": [
                            "10.0.1.2:0"
                        ],
                        "status": "Up",
                        "type": "XMPP",
                        "name": "control-node:10.0.1.2",
                        "description": "OpenSent"
                    },
                    {
                        "server_addrs": [
                            "127.0.0.1:8086"
                        ],
                        "status": "Up",
                        "type": "Collector",
                        "name": "null",
                        "description": "Established"
                    }
                ]
            }
        ],
        "description": "null"
    }
}

NON_RUNNING_VROUTER_UVES_STATUS_1 = {
    "NodeStatus": {
        "process_status": [
            {
                "instance_id": "2",
                "module_id": "VRouterAgent",
                "state": "Functional",
                "connection_infos": [
                    {
                        "server_addrs": [
                            "10.0.1.2:0"
                        ],
                        "status": "Up",
                        "type": "XMPP",
                        "name": "control-node:10.0.1.2",
                        "description": "OpenSent"
                    },
                    {
                        "server_addrs": [
                            "127.0.0.1:8086"
                        ],
                        "status": "Up",
                        "type": "Collector",
                        "name": "null",
                        "description": "Established"
                    }
                ]
            }
        ],
        "description": "null"
    }
}

NON_RUNNING_VROUTER_UVES_STATUS_2 = {
    "NodeStatus": {
        "process_status": [
            {
                "instance_id": "0",
                "module_id": "FakeModuleID",
                "state": "Functional",
                "connection_infos": [
                    {
                        "server_addrs": [
                            "10.0.1.2:0"
                        ],
                        "status": "Up",
                        "type": "XMPP",
                        "name": "control-node:10.0.1.2",
                        "description": "OpenSent"
                    },
                    {
                        "server_addrs": [
                            "127.0.0.1:8086"
                        ],
                        "status": "Up",
                        "type": "Collector",
                        "name": "null",
                        "description": "Established"
                    }
                ]
            }
        ],
        "description": "null"
    }
}

NON_RUNNING_VROUTER_UVES_STATUS_3 = {
    "NodeStatus": {
        "process_status": [
            {
                "instance_id": "0",
                "module_id": "VRouterAgent",
                "state": "Non-functional",
                "connection_infos": [
                    {
                        "server_addrs": [
                            "10.0.1.2:0"
                        ],
                        "status": "Up",
                        "type": "XMPP",
                        "name": "control-node:10.0.1.2",
                        "description": "OpenSent"
                    },
                    {
                        "server_addrs": [
                            "127.0.0.1:8086"
                        ],
                        "status": "Up",
                        "type": "Collector",
                        "name": "null",
                        "description": "Established"
                    }
                ]
            }
        ],
        "description": "null"
    }
}

NON_RUNNING_VROUTER_UVES_STATUS_4 = {}

class TestRandomScheduler(unittest.TestCase):

    def setUp(self):
        super(TestRandomScheduler, self).setUp()

        self.vnc_patch = mock.patch('vnc_api.vnc_api.VncApi', autospec=True)
        self.vnc_mock = self.vnc_patch.start()

        self.analytics_patch = \
            mock.patch('cfgm_common.analytics_client.Client.request')
        self.analytics_mock = self.analytics_patch.start()

        self.scheduler = \
            scheduler.RandomScheduler(self.vnc_mock, mock.MagicMock())

    def tearDown(self):
        self.analytics_patch.stop()
        self.vnc_patch.stop()
        super(TestRandomScheduler, self).tearDown()

    def test_get_candidates(self):
        VROUTER_LIST = {
                "virtual-routers": [
                    {
                        "href": "http://127.0.0.1:8082/virtual-router/uuid1",
                        "fq_name": [
                            "default-global-system-config",
                            "vrouter1"
                        ],
                        "uuid": "uuid1"
                    },
                    {
                        "href": "http://127.0.0.1:8082/virtual-router/uuid2",
                        "fq_name": [
                            "default-global-system-config",
                            "vrouter2"
                        ],
                        "uuid": "uuid2"
                    },
                    {
                        "href": "http://127.0.0.1:8082/virtual-router/uuid3",
                        "fq_name": [
                            "default-global-system-config",
                            "vrouter3"
                        ],
                        "uuid": "uuid3"
                    }
                ]
            }

        self.vnc_mock.virtual_routers_list.return_value = VROUTER_LIST

        self.analytics_mock.side_effect = [RUNNING_VROUTER_UVES_STATUS,
                                           NON_RUNNING_VROUTER_UVES_STATUS_3,
                                           RUNNING_VROUTER_UVES_STATUS]

        vr_obj = VirtualRouter()
        vr_obj.get_virtual_machine_refs = mock.MagicMock(
            return_value=[{'uuid': 'fake_uuid'}])
        self.vnc_mock.virtual_router_read.return_value = vr_obj

        vm_obj_1 = VirtualMachine()
        vm_obj_1.get_service_instance_refs = mock.MagicMock(
            return_value=[{'uuid': 'fake_uuid'}])
        vm_obj_2 = VirtualMachine()
        vm_obj_2.get_service_instance_refs = mock.MagicMock(
            return_value=[{'uuid': 'fake_si_uuid'}])
        self.vnc_mock.virtual_machine_read.side_effect = [vm_obj_1, vm_obj_2]

        expected_result = [["default-global-system-config", "vrouter1"]]
        self.assertEqual(self.scheduler._get_candidates('fake_si_uuid',
                                                        'fake_uuid'),
                         expected_result)

    def test_vrouter_running(self):
        self.analytics_mock.side_effect = [analytics.OpenContrailAPIFailed,
                                           NON_RUNNING_VROUTER_UVES_STATUS_1,
                                           NON_RUNNING_VROUTER_UVES_STATUS_2,
                                           NON_RUNNING_VROUTER_UVES_STATUS_3,
                                           NON_RUNNING_VROUTER_UVES_STATUS_4,
                                           RUNNING_VROUTER_UVES_STATUS]
        self.assertFalse(self.scheduler.vrouter_running('fake_vrouter_name'))
        self.assertFalse(self.scheduler.vrouter_running('fake_vrouter_name'))
        self.assertFalse(self.scheduler.vrouter_running('fake_vrouter_name'))
        self.assertFalse(self.scheduler.vrouter_running('fake_vrouter_name'))
        self.assertFalse(self.scheduler.vrouter_running('fake_vrouter_name'))
        self.assertTrue(self.scheduler.vrouter_running('fake_vrouter_name'))

    def test_random_scheduling(self):
        random_patch = mock.patch('random.choice')
        random_mock = random_patch.start()

        def side_effect(seq):
            return seq[0]
        random_mock.side_effect = side_effect

        with mock.patch.object(scheduler.RandomScheduler, '_get_candidates',
            return_value=[["default-global-system-config", "vrouter1"],
                          ["default-global-system-config", "vrouter2"]]):
            self.assertEqual(self.scheduler.schedule('fake_uuid', 'fake_uuid'),
                             ["default-global-system-config", "vrouter1"])
            self.assertEqual(random_mock.call_count, 1)

        random_patch.stop()
