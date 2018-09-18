#
# Copyright (c) 2013,2014 Juniper Networks, Inc. All rights reserved.
#
import gevent
import gevent.monkey
gevent.monkey.patch_all()
import sys
import uuid
import logging
import coverage
import requests

import testtools
from testtools.matchers import Equals, MismatchError, Not, Contains
from testtools import content, content_type, ExpectedException
import unittest
import json

from vnc_api.vnc_api import *
import vnc_api.gen.vnc_api_test_gen
from vnc_api.gen.resource_test import *

import cfgm_common
from cfgm_common import vnc_cgitb
vnc_cgitb.enable(format='text')

sys.path.append('../common/tests')
from test_utils import *
import test_common
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

from sandesh_common.vns import constants

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
            fq_name=["default-global-system-config", "test_device_qfx"],
            physical_router_management_ip="1.1.1.1",
            physical_router_vendor_name="juniper",
            physical_router_product_name="qfx5k",
            physical_router_user_credentials={"username": "username",
                                              "password": "password"},
            physical_router_device_family='junos-qfx')
        pr_uuid = self._vnc_lib.physical_router_create(phy_router_obj)

        phy_int_obj = PhysicalInterface(name="phy_intf_qfx",
                                parent_obj=phy_router_obj)
        phy_intf_uuid = self._vnc_lib.physical_interface_create(phy_int_obj)

        log_int_obj = LogicalInterface(name="log_intf_qfx",
                                       parent_obj=phy_int_obj,
                                       logical_interface_vlan_tag=2,
                                       logical_interface_type='l2')
        try:
            log_intf_uuid = self._vnc_lib.logical_interface_create(log_int_obj)
        except PermissionDenied as ex:
            self.assertEqual(ex.message, "Vlan ids " + str(constants.RESERVED_QFX_L2_VLAN_TAGS) +
                                " are not allowed on QFX"
                                " logical interface type: l2")

    def test_logical_interface_create_other_l2(self):
        # create test device object
        phy_router_obj = PhysicalRouter(
            parent_type='global-system-config',
            fq_name=["default-global-system-config", "test_device_mx"],
            physical_router_management_ip="1.1.1.1",
            physical_router_vendor_name="juniper",
            physical_router_product_name="mx240",
            physical_router_user_credentials={"username": "username",
                                              "password": "password"},
            physical_router_device_family='juniper-mx')
        pr_uuid = self._vnc_lib.physical_router_create(phy_router_obj)

        phy_int_obj = PhysicalInterface(name="phy_intf_mx",
                                parent_obj=phy_router_obj)
        phy_intf_uuid = self._vnc_lib.physical_interface_create(phy_int_obj)

        log_int_obj = LogicalInterface(name="log_intf_mx",
                                       parent_obj=phy_int_obj,
                                       logical_interface_vlan_tag=2,
                                       logical_interface_type='l2')

        log_intf_uuid = self._vnc_lib.logical_interface_create(log_int_obj)


    def test_logical_interface_create_qfx_l3(self):
        # create test device object
        phy_router_obj = PhysicalRouter(
            parent_type='global-system-config',
            fq_name=["default-global-system-config", "test_device_qfx_2"],
            physical_router_management_ip="1.1.1.1",
            physical_router_vendor_name="juniper",
            physical_router_product_name="qfx5k",
            physical_router_user_credentials={"username": "username",
                                              "password": "password"},
            physical_router_device_family='junos-qfx')
        pr_uuid = self._vnc_lib.physical_router_create(phy_router_obj)

        phy_int_obj = PhysicalInterface(name="phy_intf_qfx_2",
                                parent_obj=phy_router_obj)
        phy_intf_uuid = self._vnc_lib.physical_interface_create(phy_int_obj)

        log_int_obj = LogicalInterface(name="log_intf_qfx_2",
                                       parent_obj=phy_int_obj,
                                       logical_interface_vlan_tag=2,
                                       logical_interface_type='l3')

        log_intf_uuid = self._vnc_lib.logical_interface_create(log_int_obj)

    def test_logical_interface_update_qfx_l2(self):
        log_intf_obj = self._vnc_lib.logical_interface_read(fq_name=[
                                                                 "default-global-system-config",
                                                                 "test_device_qfx_2",
                                                                 "phy_intf_qfx_2",
                                                                 "log_intf_qfx_2"
                                                           ])
        log_intf_obj.set_logical_interface_type('l2')
        try:
            log_intf_uuid = self._vnc_lib.logical_interface_update(log_intf_obj)
        except PermissionDenied as ex:
            self.assertEqual(ex.message, "Vlan ids " + str(constants.RESERVED_QFX_L2_VLAN_TAGS) +
                                " are not allowed on QFX"
                                " logical interface type: l2")

#end class TestLogicalInterface

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    unittest.main()
