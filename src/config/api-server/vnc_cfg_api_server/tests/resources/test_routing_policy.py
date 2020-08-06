#
# Copyright (c) 2013,2014 Juniper Networks, Inc. All rights reserved.
#


import logging

from vnc_api.vnc_api import ActionCommunityType, ActionUpdateType
from vnc_api.vnc_api import CommunityListType
from vnc_api.vnc_api import PolicyStatementType, PolicyTermType
from vnc_api.vnc_api import RoutingPolicy
from vnc_api.vnc_api import TermActionListType, TermMatchConditionType

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestRoutingPolicy(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestRoutingPolicy, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRoutingPolicy, cls).tearDownClass(*args, **kwargs)

    def test_routing_policy_community_target(self):
        rp = RoutingPolicy(name=self.id(), term_type='network-device')
        rp.set_routing_policy_entries(PolicyStatementType(term=[
            PolicyTermType(
                term_match_condition=TermMatchConditionType(),
                term_action_list=TermActionListType(
                    action='accept',
                    update=ActionUpdateType(
                        community=ActionCommunityType(
                            add=CommunityListType(community=['color:30:12345',
                                                             '30:1234556890',
                                                             '0x030b:1:1'])
                        )))),
        ]))
        self._vnc_lib.routing_policy_create(rp)
        self.assertIsNotNone(rp.uuid)
    # end test_routing_policy_community_target
