#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import logging

from vnc_api.exceptions import BadRequest
from vnc_api.gen.resource_client import StormControlProfile
from vnc_api.gen.resource_xsd import StormControlParameters
from vnc_api.gen.resource_client import PortProfile

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestPortProfile(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestPortProfile, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestPortProfile, cls).tearDownClass(*args, **kwargs)

    def test_port_profile_create_one_sc_profile(self):

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

        pp_name = "pp_one_sc"
        port_profile = PortProfile(name=pp_name)
        port_profile.set_storm_control_profile(sc_obj)

        self.assertIsNotNone(
            self._vnc_lib.port_profile_create(
                port_profile))

    def test_port_profile_update_two_sc_profiles(self):
        # create test sc profile object

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

        sc_name = 'strm_ctrl_2'
        actions = ['interface-shutdown']
        bw_percent = 30

        sc_params_list = StormControlParameters(
            storm_control_actions=actions,
            bandwidth_percent=bw_percent)

        sc_obj_2 = StormControlProfile(
            name=sc_name,
            storm_control_parameters=sc_params_list
        )

        self.assertIsNotNone(
            self._vnc_lib.storm_control_profile_create(
                sc_obj_2))

        pp_name = "pp_two_scs"
        port_profile = PortProfile(name=pp_name)
        port_profile.set_storm_control_profile(sc_obj)

        self.assertIsNotNone(
            self._vnc_lib.port_profile_create(
                port_profile))

        # now update another reference to pp

        port_profile.set_storm_control_profile_list([
            {'to': sc_obj.get_fq_name()},
            {'to': sc_obj_2.get_fq_name()}
        ])

        err_msg = "Port profile None has more than one storm " \
            "profile refs "

        self.assertRaisesRegexp(BadRequest, err_msg,
                                self._vnc_lib.port_profile_update,
                                port_profile)
