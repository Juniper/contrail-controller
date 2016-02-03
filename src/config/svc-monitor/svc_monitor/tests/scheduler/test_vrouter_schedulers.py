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
import cfgm_common.svc_info as svc_info
from sandesh_common.vns import constants
import svc_monitor.scheduler.vrouter_scheduler as scheduler
from vnc_api.vnc_api import VirtualRouter, VirtualMachine

from svc_monitor.config_db import ServiceInstanceSM, VirtualRouterSM
import svc_monitor.tests.test_common_utils as test_utils

AGENTS_STATUS = \
{
 u'vrouter1': {u'NodeStatus': {u'process_status':
    [{u'module_id': u'contrail-vrouter-agent', u'state': u'Functional'}]}},
 u'vrouter2': {u'NodeStatus': {u'process_status':
    [{u'module_id': u'contrail-vrouter-agent', u'state': u'Functional'}]}}
}

VROUTERS_MODE = \
{
 u'vrouter1': {u'VrouterAgent': {u'mode': u'VROUTER'}},
 u'vrouter2': {u'VrouterAgent': {u'mode': u'VROUTER'}}
}


class TestRandomScheduler(unittest.TestCase):

    def setUp(self):
        super(TestRandomScheduler, self).setUp()

        self.vnc_mock = mock.MagicMock()

        self.analytics_patch = \
            mock.patch('cfgm_common.analytics_client.Client.request')
        self.analytics_mock = self.analytics_patch.start()

        self.scheduler = \
            scheduler.RandomScheduler(self.vnc_mock, mock.MagicMock(),
                mock.MagicMock(), mock.MagicMock(), 
                mock.MagicMock(netns_availability_zone=False))

    def tearDown(self):
        self.analytics_patch.stop()
        VirtualRouterSM.reset()
        ServiceInstanceSM.reset()
        super(TestRandomScheduler, self).tearDown()

    def test_get_candidates(self):
        test_utils.create_test_project('fake-domain:fake-project')
        test_utils.create_test_virtual_network('fake-domain:fake-project:vn1')
        test_utils.create_test_virtual_network('fake-domain:fake-project:vn2')
        test_utils.create_test_security_group('fake-domain:fake-project:default')
        st = test_utils.create_test_st(name='test-template',
            virt_type='network-namespace',
            intf_list=[['right', True], ['left', True]])
        si = test_utils.create_test_si(name='test-instance', count=2,
            intf_list=['vn1', 'vn2'])

        # test anti-affinity
        vr1 = test_utils.create_test_virtual_router('vr-candidate1')
        vr2 = test_utils.create_test_virtual_router('vr-candidate2')
        vm1 = test_utils.create_test_virtual_machine('vm1')
        vm2 = test_utils.create_test_virtual_machine('vm2')
        si.virtual_machines.add(vm1.uuid)
        si.virtual_machines.add(vm2.uuid)
        vm1.virtual_router = vr1.uuid
        candidates = self.scheduler._get_candidates(si, vm2)
        self.assertEqual(candidates, [vr2.uuid])

        # test same vrouter returned if already scheduled
        candidates = self.scheduler._get_candidates(si, vm1)
        self.assertEqual(len(candidates), 1) 
        self.assertEqual(candidates, [vr1.uuid])

        # test all candidates returned
        vm1.virtual_router = None
        candidates = self.scheduler._get_candidates(si, vm1)
        self.assertEqual(len(candidates), 2)

        # test non running candidates returned
        vr1.agent_state = False
        candidates = self.scheduler._get_candidates(si, vm1)
        self.assertEqual(len(candidates), 1)
        self.assertEqual(candidates, [vr2.uuid])

        # test no candidates
        vr1.agent_state = False
        vr2.agent_state = False
        candidates = self.scheduler._get_candidates(si, vm1)
        self.assertEqual(len(candidates), 0)

    def test_vrouter_running(self):
        vr1 = test_utils.create_test_virtual_router('vrouter1')
        vr2 = test_utils.create_test_virtual_router('vrouter2')
        self.scheduler.get_analytics_client = mock.MagicMock()
        def query_uve_side_effect(query_str):
            if 'NodeStatus' in query_str:
                return AGENTS_STATUS
            elif 'VrouterAgent' in query_str:
                return VROUTERS_MODE
            else:
                return {}
        self.scheduler.query_uve = query_uve_side_effect
        self.assertTrue(VirtualRouterSM.get('vrouter1').agent_state)
        self.assertTrue(VirtualRouterSM.get('vrouter2').agent_state)

    def test_random_scheduling(self):
        random_patch = mock.patch('random.choice')
        random_mock = random_patch.start()

        def side_effect(seq):
            return seq[0]
        random_mock.side_effect = side_effect

        si = test_utils.create_test_si(name='test-instance', count=2,
            intf_list=['vn1', 'vn2'])
        vm = test_utils.create_test_virtual_machine('vm')

        with mock.patch.object(scheduler.RandomScheduler, '_get_candidates',
                return_value=['vrouter1', 'vrouter2']):
            chosen_vrouter = self.scheduler.schedule(si, vm)
            self.assertEqual(random_mock.call_count, 1)
            self.assertEqual(chosen_vrouter, 'vrouter1')

        random_patch.stop()
