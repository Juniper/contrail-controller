#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import logging

from vnc_api.exceptions import BadRequest
from vnc_api.gen.resource_client import StormControlProfile
from vnc_api.gen.resource_xsd import StormControlParameters

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestStormControlProfile(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestStormControlProfile, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestStormControlProfile, cls).tearDownClass(*args, **kwargs)

    def test_storm_control_profile_create(self):
        # create test sc profile object

        sc_name = 'strm_ctrl_' + self.id()
        bw_percent = 20
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']

        sc_params_list = StormControlParameters(
            storm_control_actions=actions,
            bandwidth_percent=bw_percent)

        if 'no-broadcast' in traffic_type:
            sc_params_list.set_no_broadcast(True)
        if 'no-multicast' in traffic_type:
            sc_params_list.set_no_multicast(True)
        if 'no-registered-multicast' in traffic_type:
            sc_params_list.set_no_registered_multicast(True)
        if 'no-unknown-unicast' in traffic_type:
            sc_params_list.set_no_unknown_unicast(True)
        if 'no-unregistered-multicast' in traffic_type:
            sc_params_list.set_no_unregistered_multicast(True)

        sc_obj = StormControlProfile(
            name=sc_name,
            storm_control_parameters=sc_params_list
        )
        self.assertIsNotNone(
            self._vnc_lib.storm_control_profile_create(
            sc_obj))

    def test_storm_control_profile_create_negative_percent(self):
        # create test sc profile object

        sc_name = 'strm_ctrl_' + self.id()
        bw_percent = -20
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']

        sc_params_list = StormControlParameters(
            storm_control_actions=actions,
            bandwidth_percent=bw_percent)

        if 'no-broadcast' in traffic_type:
            sc_params_list.set_no_broadcast(True)
        if 'no-multicast' in traffic_type:
            sc_params_list.set_no_multicast(True)
        if 'no-registered-multicast' in traffic_type:
            sc_params_list.set_no_registered_multicast(True)
        if 'no-unknown-unicast' in traffic_type:
            sc_params_list.set_no_unknown_unicast(True)
        if 'no-unregistered-multicast' in traffic_type:
            sc_params_list.set_no_unregistered_multicast(True)

        sc_obj = StormControlProfile(
            name=sc_name,
            storm_control_parameters=sc_params_list
        )
        regex_msg = (r"Invalid bandwidth percentage %d"
                     % bw_percent)
        self.assertRaisesRegexp(
            BadRequest, regex_msg,
            self._vnc_lib.storm_control_profile_create,
            sc_obj)

    def test_storm_control_profile_update_invalid_rec_timeout(self):
        sc_name = 'strm_ctrl_' + self.id()
        bw_percent = 20
        traffic_type = ['no-broadcast', 'no-multicast']
        actions = ['interface-shutdown']
        rec_timeout = 1000

        sc_params_list = StormControlParameters(
            storm_control_actions=actions,
            recovery_timeout=rec_timeout,
            bandwidth_percent=bw_percent)

        if 'no-broadcast' in traffic_type:
            sc_params_list.set_no_broadcast(True)
        if 'no-multicast' in traffic_type:
            sc_params_list.set_no_multicast(True)
        if 'no-registered-multicast' in traffic_type:
            sc_params_list.set_no_registered_multicast(True)
        if 'no-unknown-unicast' in traffic_type:
            sc_params_list.set_no_unknown_unicast(True)
        if 'no-unregistered-multicast' in traffic_type:
            sc_params_list.set_no_unregistered_multicast(True)

        sc_obj = StormControlProfile(
            name=sc_name,
            storm_control_parameters=sc_params_list
        )
        self.assertIsNotNone(
            self._vnc_lib.storm_control_profile_create(
            sc_obj))

        # set invalid recovery timeout
        rec_timeout = 3800
        sc_params_list.set_recovery_timeout(rec_timeout)

        sc_obj = StormControlProfile(
            name=sc_name,
            storm_control_parameters=sc_params_list
        )
        regex_msg = (r"Invalid recovery timeout %d"
                     % rec_timeout)
        self.assertRaisesRegexp(
            BadRequest, regex_msg,
            self._vnc_lib.storm_control_profile_update,
            sc_obj)
