#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import logging

from vnc_api.exceptions import BadRequest
from vnc_api.gen.resource_client import LogicalInterface
from vnc_api.gen.resource_client import PhysicalInterface
from vnc_api.gen.resource_client import PhysicalRouter

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestLogicalInterface(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestLogicalInterface, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestLogicalInterface, cls).tearDownClass(*args, **kwargs)

    def test_logical_interface_create_qfx_l2(self):
        # create test device object
        phy_router_obj = PhysicalRouter(
            parent_type='global-system-config',
            fq_name=["default-global-system-config",
                     "test_device_qfx_%s" % self.id()],
            physical_router_management_ip="1.1.1.1",
            physical_router_vendor_name="juniper",
            physical_router_product_name="qfx5k",
            physical_router_user_credentials={"username": "username",
                                              "password": "password"},
            physical_router_device_family='junos-qfx')
        self._vnc_lib.physical_router_create(phy_router_obj)

        phy_int_obj = PhysicalInterface(name="phy_intf_qfx_%s" % self.id(),
                                        parent_obj=phy_router_obj)
        self._vnc_lib.physical_interface_create(phy_int_obj)

        log_int_obj = LogicalInterface(name="log_intf_qfx_%s" % self.id(),
                                       parent_obj=phy_int_obj,
                                       logical_interface_vlan_tag=2,
                                       logical_interface_type='l2')

        regex_msg = (r"Vlan ids 1, 2, 4094 are not allowed on "
                     "QFX logical interface type: l2")
        self.assertRaisesRegexp(BadRequest, regex_msg,
                                self._vnc_lib.logical_interface_create,
                                log_int_obj)

    def test_logical_interface_create_other_l2(self):
        # create test device object
        phy_router_obj = PhysicalRouter(
            parent_type='global-system-config',
            fq_name=["default-global-system-config",
                     "test_device_mx_%s" % self.id()],
            physical_router_management_ip="1.1.1.1",
            physical_router_vendor_name="juniper",
            physical_router_product_name="mx240",
            physical_router_user_credentials={"username": "username",
                                              "password": "password"},
            physical_router_device_family='juniper-mx')
        self._vnc_lib.physical_router_create(phy_router_obj)

        phy_int_obj = PhysicalInterface(name="phy_intf_mx_%s" % self.id(),
                                        parent_obj=phy_router_obj)
        self._vnc_lib.physical_interface_create(phy_int_obj)

        log_int_obj = LogicalInterface(name="log_intf_mx_%s" % self.id(),
                                       parent_obj=phy_int_obj,
                                       logical_interface_vlan_tag=2,
                                       logical_interface_type='l2')

        self.assertIsNotNone(self._vnc_lib.logical_interface_create(
            log_int_obj))

    def test_logical_interface_create_qfx_l3(self):
        # create test device object
        phy_router_obj = PhysicalRouter(
            parent_type='global-system-config',
            fq_name=["default-global-system-config",
                     "test_device_qfx_%s" % self.id()],
            physical_router_management_ip="1.1.1.1",
            physical_router_vendor_name="juniper",
            physical_router_product_name="qfx5k",
            physical_router_user_credentials={"username": "username",
                                              "password": "password"},
            physical_router_device_family='junos-qfx')
        self._vnc_lib.physical_router_create(phy_router_obj)

        phy_int_obj = PhysicalInterface(name="phy_intf_qfx_%s" % self.id(),
                                        parent_obj=phy_router_obj)
        self._vnc_lib.physical_interface_create(phy_int_obj)

        log_int_obj = LogicalInterface(name="log_intf_qfx_%s" % self.id(),
                                       parent_obj=phy_int_obj,
                                       logical_interface_vlan_tag=2,
                                       logical_interface_type='l3')

        self.assertIsNotNone(self._vnc_lib.logical_interface_create(
            log_int_obj))

    def test_logical_interface_update_qfx_l2(self):

        # create test device object
        phy_router_obj = PhysicalRouter(
            parent_type='global-system-config',
            fq_name=["default-global-system-config",
                     "test_device_qfx_%s" % self.id()],
            physical_router_management_ip="1.1.1.1",
            physical_router_vendor_name="juniper",
            physical_router_product_name="qfx5k",
            physical_router_user_credentials={"username": "username",
                                              "password": "password"},
            physical_router_device_family='junos-qfx')

        self._vnc_lib.physical_router_create(phy_router_obj)

        phy_int_obj = PhysicalInterface(name="phy_intf_qfx_%s" % self.id(),
                                        parent_obj=phy_router_obj)
        self._vnc_lib.physical_interface_create(phy_int_obj)

        log_int_obj = LogicalInterface(name="log_intf_qfx_%s" % self.id(),
                                       parent_obj=phy_int_obj,
                                       logical_interface_vlan_tag=1,
                                       logical_interface_type='l3')

        self.assertIsNotNone(self._vnc_lib.logical_interface_create(
            log_int_obj))

        log_intf_obj = self._vnc_lib.logical_interface_read(fq_name=[
            "default-global-system-config",
            "test_device_qfx_%s" % self.id(),
            "phy_intf_qfx_%s" % self.id(),
            "log_intf_qfx_%s" % self.id()])
        log_intf_obj.set_logical_interface_type('l2')

        regex_msg = (r"Vlan ids 1, 2, 4094 are not allowed on "
                     "QFX logical interface type: l2")
        self.assertRaisesRegexp(BadRequest, regex_msg,
                                self._vnc_lib.logical_interface_update,
                                log_intf_obj)

    def test_logical_interface_update_qfx_l2_4094(self):
        # create test device object
        phy_router_obj = PhysicalRouter(
            parent_type='global-system-config',
            fq_name=["default-global-system-config",
                     "test_device_qfx_%s" % self.id()],
            physical_router_management_ip="1.1.1.1",
            physical_router_vendor_name="juniper",
            physical_router_product_name="qfx5k",
            physical_router_user_credentials={"username": "username",
                                              "password": "password"},
            physical_router_device_family='junos-qfx')
        self._vnc_lib.physical_router_create(phy_router_obj)

        phy_int_obj = PhysicalInterface(name="phy_intf_qfx_%s" % self.id(),
                                        parent_obj=phy_router_obj)
        self._vnc_lib.physical_interface_create(phy_int_obj)

        log_int_obj = LogicalInterface(name="log_intf_qfx_%s" % self.id(),
                                       parent_obj=phy_int_obj)

        self.assertIsNotNone(self._vnc_lib.logical_interface_create(
            log_int_obj))

        log_intf_obj = self._vnc_lib.logical_interface_read(fq_name=[
            "default-global-system-config",
            "test_device_qfx_%s" % self.id(),
            "phy_intf_qfx_%s" % self.id(),
            "log_intf_qfx_%s" % self.id()])

        log_intf_obj.set_logical_interface_vlan_tag(4094)
        log_intf_obj.set_logical_interface_type('l2')

        regex_msg = (r"Vlan ids 1, 2, 4094 are not allowed on "
                     "QFX logical interface type: l2")
        self.assertRaisesRegexp(BadRequest, regex_msg,
                                self._vnc_lib.logical_interface_update,
                                log_intf_obj)
