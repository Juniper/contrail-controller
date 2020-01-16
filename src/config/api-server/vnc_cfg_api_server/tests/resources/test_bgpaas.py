#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from builtins import range
from builtins import str
import logging

from cfgm_common.exceptions import BadRequest
from vnc_api.vnc_api import BGPaaSControlNodeZoneAttributes
from vnc_api.vnc_api import BgpAsAService
from vnc_api.vnc_api import BgpSessionAttributes
from vnc_api.vnc_api import ControlNodeZone
from vnc_api.vnc_api import GlobalSystemConfig

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestBgpaas(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestBgpaas, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestBgpaas, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def create_bgpaas(self):
        proj = self.api.project_read(
            fq_name=["default-domain", "default-project"])
        bgpaas_obj = BgpAsAService(name='bgpaas_update',
                                   parent_obj=proj)
        # Set a valid ASN and create bgpaas object
        bgpaas_obj.autonomous_system = 64512
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        return bgpaas_uuid, bgpaas_obj

    def create_bgpaas_with_local_asn(self):
        proj = self.api.project_read(
            fq_name=["default-domain", "default-project"])
        bgp_session_attr = BgpSessionAttributes(local_autonomous_system=64500)

        bgpaas_obj = BgpAsAService(name='bgpaas',
                                   parent_obj=proj,
                                   bgpaas_session_attributes=bgp_session_attr)

        bgpaas_obj.autonomous_system = 64512
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        return bgpaas_uuid, bgpaas_obj

    def test_bgpaas_create_with_valid_2_byte_asn(self):
        proj = self.api.project_read(
            fq_name=["default-domain", "default-project"])
        bgp_session_attr = BgpSessionAttributes(local_autonomous_system=64500)
        bgpaas_obj = BgpAsAService(name='bgpaas',
                                   parent_obj=proj,
                                   bgpaas_session_attributes=bgp_session_attr)
        # Set a valid ASN and create bgpaas object
        bgpaas_obj.autonomous_system = 64512
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)

        # Now delete the bgpaas object
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

    def test_bgpaas_create_with_valid_4_byte_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        proj = self.api.project_read(
            fq_name=["default-domain", "default-project"])
        bgp_session_attr = BgpSessionAttributes(
            local_autonomous_system=700001)

        bgpaas_obj = BgpAsAService(name='bgpaas',
                                   parent_obj=proj,
                                   bgpaas_session_attributes=bgp_session_attr)
        # Set a valid ASN and create bgpaas object
        bgpaas_obj.autonomous_system = 700000
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)

        # Now delete the bgpaas object and disable 4 byte ASN flag
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

    def test_bgpaas_create_with_invalid_2_byte_asn(self):
        proj = self.api.project_read(
            fq_name=["default-domain", "default-project"])
        bgp_session_attr = BgpSessionAttributes(local_autonomous_system=64500)

        bgpaas_obj = BgpAsAService(name='bgpaas',
                                   parent_obj=proj,
                                   bgpaas_session_attributes=bgp_session_attr)
        # Set a invalid ASN and create bgpaas object
        bgpaas_obj.autonomous_system = 700000
        self.assertRaises(BadRequest, self.api.bgp_as_a_service_create,
                          bgpaas_obj)

    def test_bgpaas_create_with_invalid_2_byte_local_asn(self):
        proj = self.api.project_read(
            fq_name=["default-domain", "default-project"])

        # Set a invalid Local ASN and create bgpaas object
        bgp_session_attr = BgpSessionAttributes(
            local_autonomous_system=700001)

        bgpaas_obj = BgpAsAService(name='bgpaas',
                                   parent_obj=proj,
                                   bgpaas_session_attributes=bgp_session_attr)
        bgpaas_obj.autonomous_system = 64512
        self.assertRaises(BadRequest, self.api.bgp_as_a_service_create,
                          bgpaas_obj)

    def test_bgpaas_create_with_invalid_4_byte_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        proj = self.api.project_read(
            fq_name=["default-domain", "default-project"])
        bgpaas_obj = BgpAsAService(name='bgpaas',
                                   parent_obj=proj)
        # Set a invalid ASN and create bgpaas object
        bgpaas_obj.autonomous_system = 0x1FFFFFFFF
        self.assertRaises(BadRequest, self.api.bgp_as_a_service_create,
                          bgpaas_obj)

        # Finally, disable 4 byte ASN flag
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

    def test_bgpaas_create_with_invalid_4_byte_local_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        # Set a invalid Local ASN and create bgpaas object
        proj = self.api.project_read(
            fq_name=["default-domain", "default-project"])
        bgp_session_attr = BgpSessionAttributes(
            local_autonomous_system=0x1FFFFFFFF)

        bgpaas_obj = BgpAsAService(name='bgpaas',
                                   parent_obj=proj,
                                   bgpaas_session_attributes=bgp_session_attr)
        bgpaas_obj.autonomous_system = 0x7000001
        self.assertRaises(BadRequest, self.api.bgp_as_a_service_create,
                          bgpaas_obj)

        # Finally, disable 4 byte ASN flag
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

    def test_bgpaas_update_with_valid_2_byte_asn(self):
        # Create bgpaas object with ASN 64512
        bgpaas_uuid, bgpaas_obj = self.create_bgpaas()

        # Update ASN with a valid value
        bgpaas_obj.autonomous_system = 64500
        self.api.bgp_as_a_service_update(bgpaas_obj)

        # Finally, delete the bgpaas object
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

    def test_bgpaas_update_with_valid_2_byte_local_asn(self):
        # Create bgpaas object with ASN 64512 and Local ASN 64500
        bgpaas_uuid, bgpaas_obj = self.create_bgpaas_with_local_asn()

        # Update Local ASN with a valid value
        bgp_session_attr = BgpSessionAttributes(local_autonomous_system=64000)
        bgpaas_obj.set_bgpaas_session_attributes(bgp_session_attr)
        self.api.bgp_as_a_service_update(bgpaas_obj)

        # Finally, delete the bgpaas object
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

    def test_bgpaas_update_with_valid_4_byte_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        # Create bgpaas object with ASN 64512
        bgpaas_uuid, bgpaas_obj = self.create_bgpaas()

        # Update ASN with a valid 4 byte value
        bgpaas_obj.autonomous_system = 700000
        self.api.bgp_as_a_service_update(bgpaas_obj)

        # Disable 4 byte ASN flag
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

        # Finally, delete the bgpaas object
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

    def test_bgpaas_update_with_valid_4_byte_local_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        # Create bgpaas object with ASN 64512 and Local ASN 64500
        bgpaas_uuid, bgpaas_obj = self.create_bgpaas_with_local_asn()

        # Update Local ASN with a valid value
        bgp_session_attr = BgpSessionAttributes(local_autonomous_system=700002)
        bgpaas_obj.set_bgpaas_session_attributes(bgp_session_attr)
        self.api.bgp_as_a_service_update(bgpaas_obj)

        # Disable 4 byte ASN flag
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

        # Finally, delete the bgpaas object
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

    def test_bgpaas_update_with_invalid_2_byte_asn(self):
        # Create bgpaas object with ASN 64512
        bgpaas_uuid, bgpaas_obj = self.create_bgpaas()

        # Update ASN with an invalid 2 byte value
        bgpaas_obj.autonomous_system = 700000
        self.assertRaises(BadRequest, self.api.bgp_as_a_service_update,
                          bgpaas_obj)

        # Finally, delete the bgpaas object
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

    def test_bgpaas_update_with_invalid_2_byte_local_asn(self):
        # Create bgpaas object with ASN 64512 and Local ASN 64500
        bgpaas_uuid, bgpaas_obj = self.create_bgpaas_with_local_asn()

        # Update Local ASN with an invalid value
        bgp_session_attr = BgpSessionAttributes(local_autonomous_system=700001)
        bgpaas_obj.set_bgpaas_session_attributes(bgp_session_attr)

        self.assertRaises(BadRequest, self.api.bgp_as_a_service_update,
                          bgpaas_obj)

        # Finally, delete the bgpaas object
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

    def test_bgpaas_update_with_invalid_4_byte_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        # Create bgpaas object with ASN 64512
        bgpaas_uuid, bgpaas_obj = self.create_bgpaas()

        # Update ASN with an invalid 4 byte value
        bgpaas_obj.autonomous_system = 0x1FFFFFFFF
        self.assertRaises(BadRequest, self.api.bgp_as_a_service_update,
                          bgpaas_obj)

        # Disable 4 byte ASN flag
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

        # Finally, delete the bgpaas object
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

    def test_bgpaas_update_with_invalid_4_byte_local_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        # Enable 4 byte ASN flag in GSC
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        # Create bgpaas object with ASN 64512 and Local ASN 64500
        bgpaas_uuid, bgpaas_obj = self.create_bgpaas_with_local_asn()

        # Update Local ASN with an invalid 4 byte value
        bgp_session_attr = BgpSessionAttributes(
            local_autonomous_system=0x1FFFFFFFF)
        bgpaas_obj.set_bgpaas_session_attributes(bgp_session_attr)

        self.assertRaises(BadRequest, self.api.bgp_as_a_service_update,
                          bgpaas_obj)

        # Disable 4 byte ASN flag
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

        # Finally, delete the bgpaas object
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

    def test_control_node_zone(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        cnz = []
        for i in range(2):
            cnz_name = "cnz-" + str(i)
            cnz.append(ControlNodeZone(name=cnz_name, parent_obj=gsc))
            self.api.control_node_zone_create(cnz[i])

        proj = self.api.project_read(
            fq_name=["default-domain", "default-project"])
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        primary_attr = BGPaaSControlNodeZoneAttributes("primary")
        secondary_attr = BGPaaSControlNodeZoneAttributes("secondary")

        # create bgpaas with two control-node-zones as "primary"
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        bgpaas_obj.add_control_node_zone(cnz[1], primary_attr)
        try:
            self.api.bgp_as_a_service_create(bgpaas_obj)
        except BadRequest:
            pass

        # create bgpaas with two control-node-zones as "secondary"
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        bgpaas_obj.add_control_node_zone(cnz[1], secondary_attr)
        try:
            self.api.bgp_as_a_service_create(bgpaas_obj)
        except BadRequest:
            pass

        # update bgpaas with two control-node-zones as "primary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        bgpaas_obj.add_control_node_zone(cnz[1], primary_attr)
        try:
            self.api.bgp_as_a_service_update(bgpaas_obj)
        except BadRequest:
            self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with two control-node-zones as "secondary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        bgpaas_obj.add_control_node_zone(cnz[1], secondary_attr)
        try:
            self.api.bgp_as_a_service_update(bgpaas_obj)
        except BadRequest:
            self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with same control-node-zone as "primary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        self.api.bgp_as_a_service_update(bgpaas_obj)
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with same control-node-zone as "secondary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        self.api.bgp_as_a_service_update(bgpaas_obj)
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with new control-node-zone as "primary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[1], primary_attr)
        try:
            self.api.bgp_as_a_service_update(bgpaas_obj)
        except BadRequest:
            self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with same control-node-zone as "secondary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        self.api.bgp_as_a_service_update(bgpaas_obj)
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with same control-node-zone as "primary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[0], primary_attr)
        self.api.bgp_as_a_service_update(bgpaas_obj)
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        # update bgpaas with new control-node-zone as "secondary"
        bgpaas_obj = BgpAsAService(name='bgpaas', parent_obj=proj)
        bgpaas_obj.add_control_node_zone(cnz[0], secondary_attr)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
        bgpaas_obj = self.api.bgp_as_a_service_read(id=bgpaas_uuid)
        bgpaas_obj.add_control_node_zone(cnz[1], secondary_attr)
        try:
            self.api.bgp_as_a_service_update(bgpaas_obj)
        except BadRequest:
            self.api.bgp_as_a_service_delete(id=bgpaas_uuid)

        for i in range(2):
            self.api.control_node_zone_delete(fq_name=cnz[i].fq_name)

    hold_time_test_cases = [{"hold_time": -1, "should_pass": False},
                            {"hold_time": 0, "should_pass": True},
                            {"hold_time": 1, "should_pass": False},
                            {"hold_time": 2, "should_pass": False},
                            {"hold_time": 3, "should_pass": True},
                            {"hold_time": 10, "should_pass": True},
                            {"hold_time": 10234, "should_pass": True},
                            {"hold_time": 65535, "should_pass": True},
                            {"hold_time": 65536, "should_pass": False}]

    def test_bgpaas_create_with_hold_time(self):
        proj = self.api.project_read(
            fq_name=["default-domain", "default-project"])
        for test in self.hold_time_test_cases:
            bgp_session_attr = BgpSessionAttributes(
                hold_time=test['hold_time'])
            bgpaas_obj = BgpAsAService(
                name='bgpaas',
                parent_obj=proj,
                bgpaas_session_attributes=bgp_session_attr)
            if test['should_pass']:
                # Create bgpaas object with proper hold time
                bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)
                # Now delete the bgpaas object
                self.api.bgp_as_a_service_delete(id=bgpaas_uuid)
            else:
                # Try to create bgpaas object with improper hold time
                # and expect error
                self.assertRaises(BadRequest, self.api.bgp_as_a_service_create,
                                  bgpaas_obj)

    def test_bgpaas_update_with_hold_time(self):
        # Create bgpaas object
        proj = self.api.project_read(
            fq_name=['default-domain', 'default-project'])
        bgpaas_obj = BgpAsAService(name='bgpaas_update',
                                   parent_obj=proj)
        bgpaas_uuid = self.api.bgp_as_a_service_create(bgpaas_obj)

        for test in self.hold_time_test_cases:
            # Update hold time with valid and invalid values
            bgp_session_attr = BgpSessionAttributes(
                hold_time=test['hold_time'])
            bgpaas_obj.set_bgpaas_session_attributes(bgp_session_attr)
            if test['should_pass']:
                self.api.bgp_as_a_service_update(bgpaas_obj)
            else:
                self.assertRaises(BadRequest, self.api.bgp_as_a_service_update,
                                  bgpaas_obj)
        # Finally, delete the bgpaas object
        self.api.bgp_as_a_service_delete(id=bgpaas_uuid)
