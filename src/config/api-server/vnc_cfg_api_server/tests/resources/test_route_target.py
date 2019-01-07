#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common import BGP_RTGT_MIN_ID

from vnc_cfg_api_server.resources import RouteTargetServer
from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestRouteTargetBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestRouteTargetBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRouteTargetBase, cls).tearDownClass(*args, **kwargs)

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
            succeed, result = RouteTargetServer._parse_route_target_name(
                rt_name)
            if expected_succeed:
                if not succeed:
                    self.fail("Cannot parse route target '%s'" % rt_name)
                self.assertEqual(result, expected_result)
            if not expected_succeed and succeed:
                self.fail("Succeed to parse route target '%s'" % rt_name)

    def test_route_target_is_in_system_range(self):
        # RT name, global ASN, expected result
        tested_values = [
            ('target:1:1', 42, True),
            ('target:42:1', 42, True),
            ('target:42:%d' % (BGP_RTGT_MIN_ID + 1000), 42, False),
        ]

        for rt_name, global_asn, expected_result in tested_values:
            ok, result = RouteTargetServer.is_user_defined(rt_name, global_asn)
            if not ok:
                self.fail("Cannot determine if it is a user defined route "
                          "target: %s", result[1])
            self.assertEqual(result, expected_result,
                             "Route target: %s and global ASN: %d" %
                             (rt_name, global_asn))
