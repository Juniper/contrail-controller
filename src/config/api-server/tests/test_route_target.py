#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import gevent.monkey
gevent.monkey.patch_all()  # noqa
import logging

from cfgm_common import BGP_RTGT_MIN_ID
from cfgm_common.exceptions import BadRequest
from vnc_api.vnc_api import GlobalSystemConfig
from vnc_api.vnc_api import RouteTargetList
from vnc_api.vnc_api import VirtualNetwork

from tests import test_case
from vnc_cfg_api_server.vnc_cfg_types import RouteTargetServer


logger = logging.getLogger(__name__)


class TestRouteTarget(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestRouteTarget, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRouteTarget, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def test_route_target_format_name(self):
        # list of tested route target names with succeed or not and parsed
        # result
        tested_values = [
            (':::', False, None),
            ('not-enough-part-in-rt', False, None),
            ('to:much:part:in:rt', False, None),
            ('bad-prefix:1:1', False, None),
            ('target:non-digit-asn:1', False, None),
            ('target:1:non-digit-target', False, None),
            ('target:1:1', True, (1, 1)),
            ('target:1.1.1.1:1', True, ('1.1.1.1', 1)),
            (['target', '1.1.1.1', '1'], True, ('1.1.1.1', 1)),
            (['target', '1', 1], True, (1, 1)),
        ]
        for rt_name, expected_succeed, expected_result in tested_values:
            succeed, result = RouteTargetServer.parse_route_target_name(
                rt_name)
            if expected_succeed:
                if not succeed:
                    self.fail("Cannot parse route target '%s'" % rt_name)
                self.assertEqual(result, expected_result)
            if not expected_succeed and succeed:
                self.fail("Succeed to parse route target '%s'" % rt_name)

    def test_create_vn_with_configured_rt_in_system_range(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        vn = VirtualNetwork('%s-vn' % self.id())
        rt_name = 'target:%d:%d' % (gsc.autonomous_system,
                                    BGP_RTGT_MIN_ID + 1000)
        vn.set_route_target_list(RouteTargetList([rt_name]))

        self.assertRaises(BadRequest, self.api.virtual_network_create, vn)

    def test_update_vn_with_configured_rt_in_system_range(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        vn = VirtualNetwork('%s-vn' % self.id())
        self.api.virtual_network_create(vn)

        rt_name = 'target:%d:%d' % (gsc.autonomous_system,
                                    BGP_RTGT_MIN_ID + 1000)
        vn.set_route_target_list(RouteTargetList([rt_name]))
        self.assertRaises(BadRequest, self.api.virtual_network_update, vn)
