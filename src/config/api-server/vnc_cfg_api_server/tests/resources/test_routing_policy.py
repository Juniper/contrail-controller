#
# Copyright (c) 2013,2014 Juniper Networks, Inc. All rights reserved.
#


import logging

import cfgm_common
from testtools import ExpectedException
from vnc_api.vnc_api import PolicyStatementType, PolicyTermType
from vnc_api.vnc_api import RouteFilterProperties, RouteFilterType
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

    def create_rp(self, rp_name, action="accept", term_t='network-device'):
        rp = RoutingPolicy(name=rp_name, term_type=term_t)
        rp.set_routing_policy_entries(
            PolicyStatementType(
                term=[
                    PolicyTermType(
                        term_match_condition=TermMatchConditionType(),
                        term_action_list=TermActionListType(action=action))
                ])
        )
        return rp
    # end create_rp

    def _validate_rp_error(self, rp):
        func_call = self._vnc_lib.routing_policy_create
        rp_term = rp.routing_policy_entries.term[0]

        # test invalid rp protocol
        for not_allow_protocol in ['xmpp', 'service-chain', 'interface',
                                   'interface-static', 'service-interface',
                                   'bgpaas']:
            rp_term.term_match_condition.protocol = [not_allow_protocol]
            with ExpectedException(
                    cfgm_common.exceptions.BadRequest,
                    "%s protocol value is not valid value for "
                    "network-device term" % not_allow_protocol):
                func_call(rp)
        rp_term.term_match_condition.protocol = ['bgp']

        # test invalid value in rp nlri_route_type
        for nlri_type in [0, 11]:
            rp_term.term_match_condition.nlri_route_type = [nlri_type]
            with ExpectedException(
                    cfgm_common.exceptions.BadRequest,
                    '%s nlri-route-type is not valid value, value should be '
                    'in range of 1 to 10' % nlri_type):
                func_call(rp)
        rp_term.term_match_condition.nlri_route_type = [1]

        # test all set of invalid value in rp route-filter
        route_filter = RouteFilterType(route_filter_properties=[
            RouteFilterProperties(route="10.1.2.3", route_type='exact',
                                  route_type_value='')]
        )
        rp_term.term_match_condition.route_filter = route_filter
        rp_route_filter = route_filter.route_filter_properties[0]
        rp_route_filter.route = None
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                'route_filter must have non-empty route value for '
                'network-device term'):
            func_call(rp)
        rp_route_filter.route = '10.1.2.3'

        rp_route_filter.route_type = None
        with ExpectedException(
                cfgm_common.exceptions.BadRequest,
                'route_filter must have valid route_type for network-device '
                'term'):
            func_call(rp)

        rp_route_filter.route_type = 'prefix-length-range'
        for rtv in [None, "31-32", "/33-/1", "/30-/33"]:
            rp_route_filter.route_type_value = rtv
            with ExpectedException(
                    cfgm_common.exceptions.BadRequest,
                    "route-filter value %s invalid. prefix-length-range "
                    "should be in format '/minlength-/maxlength'" % rtv):
                func_call(rp)

        rp_route_filter.route_type = 'upto'
        for rtv in [None, "0", "/33", '/90']:
            rp_route_filter.route_type_value = rtv
            with ExpectedException(
                    cfgm_common.exceptions.BadRequest,
                    "route-filter type value '%s' is invalid. '%s' should be "
                    "in format '/number'" % (rtv, 'upto')):
                func_call(rp)

        rp_route_filter.route_type = 'through'
        for rtv in [None, ""]:
            rp_route_filter.route_type_value = rtv
            with ExpectedException(
                    cfgm_common.exceptions.BadRequest,
                    "route-filter type value for '%s' should be non-empty "
                    "string." % 'through'):
                func_call(rp)

        # set valid values for router filter so later create successed
        rp_term.term_match_condition.route_filter = None
        rp_route_filter.route_type = 'prefix-length-range'
        rp_route_filter.route_type_value = '/29-/31'

        # test negative cases for term_action_list of RP
        for i in range(2):
            aspath_type = 'as_path_expand'
            if i == 1:
                aspath_type = 'as_path_prepend'
            for aspath in ["ffss 223", "222 ffff "]:
                if aspath_type == 'as_path_expand':
                    rp_term.term_action_list.as_path_expand = aspath
                else:
                    rp_term.term_action_list.as_path_prepend = aspath
                with ExpectedException(
                        cfgm_common.exceptions.BadRequest):
                    func_call(rp)

        rp_term.term_action_list.as_path_expand = "65134 65123"
        rp_term.term_action_list.as_path_prepend = "64123"
    # end _validate_rp_error

    def test_routing_policy_for_network_device(self):
        # create routing policy object of type network_device
        rp = self.create_rp('rp_1')

        # validate all negative error cases of RP at create
        self._validate_rp_error(rp)

        # create successfully routing policy
        rp_uuid = self._vnc_lib.routing_policy_create(rp)

        # validate changing type value not allowed
        rp.set_term_type('vrouter')
        with ExpectedException(
                cfgm_common.exceptions.PermissionDenied,
                "Cannot change term_type of routing policy! Please specify "
                "'%s' as a term_type in routing_policy_entries." %
                'network-device'):
            self._vnc_lib.routing_policy_update(rp)
        self._vnc_lib.routing_policy_read(id=rp.get_uuid())

        # cleanup
        self._vnc_lib.routing_policy_delete(id=rp_uuid)
    # end test_routing_policy_for_network_device

# end TestRoutingPolicy
