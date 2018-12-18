#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common import BGP_RTGT_MIN_ID
from cfgm_common import VNID_MIN_ALLOC
from cfgm_common.exceptions import BadRequest
from cfgm_common.exceptions import PermissionDenied
from testtools import ExpectedException
from vnc_api.vnc_api import GlobalSystemConfig
from vnc_api.vnc_api import RouteTargetList
from vnc_api.vnc_api import VirtualNetwork
from vnc_api.vnc_api import VirtualNetworkType

from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestVirtualNetwork(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestVirtualNetwork, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestVirtualNetwork, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def test_allocate_vn_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        vn_obj = VirtualNetwork('%s-vn' % self.id())

        self.api.virtual_network_create(vn_obj)

        vn_obj = self.api.virtual_network_read(id=vn_obj.uuid)
        vn_id = vn_obj.virtual_network_network_id
        self.assertEqual(vn_obj.get_fq_name_str(),
                         mock_zk.get_vn_from_id(vn_id))
        self.assertGreaterEqual(vn_id, VNID_MIN_ALLOC)

    def test_deallocate_vn_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        vn_obj = VirtualNetwork('%s-vn' % self.id())
        self.api.virtual_network_create(vn_obj)
        vn_obj = self.api.virtual_network_read(id=vn_obj.uuid)
        vn_id = vn_obj.virtual_network_network_id

        self.api.virtual_network_delete(id=vn_obj.uuid)

        self.assertNotEqual(mock_zk.get_vn_from_id(vn_id),
                            vn_obj.get_fq_name_str())

    def test_not_deallocate_vn_id_if_fq_name_does_not_correspond(self):
        mock_zk = self._api_server._db_conn._zk_db
        vn_obj = VirtualNetwork('%s-vn' % self.id())
        self.api.virtual_network_create(vn_obj)
        vn_obj = self.api.virtual_network_read(id=vn_obj.uuid)
        vn_id = vn_obj.virtual_network_network_id

        fake_fq_name = "fake fq_name"
        mock_zk._vn_id_allocator.delete(vn_id - VNID_MIN_ALLOC)
        mock_zk._vn_id_allocator.reserve(vn_id - VNID_MIN_ALLOC, fake_fq_name)
        self.api.virtual_network_delete(id=vn_obj.uuid)

        self.assertIsNotNone(mock_zk.get_vn_from_id(vn_id))
        self.assertEqual(fake_fq_name, mock_zk.get_vn_from_id(vn_id))

    def test_cannot_set_vn_id(self):
        vn_obj = VirtualNetwork('%s-vn' % self.id())
        vn_obj.set_virtual_network_network_id(42)

        with ExpectedException(PermissionDenied):
            self.api.virtual_network_create(vn_obj)

    def test_cannot_update_vn_id(self):
        vn_obj = VirtualNetwork('%s-vn' % self.id())
        self.api.virtual_network_create(vn_obj)
        vn_obj = self.api.virtual_network_read(id=vn_obj.uuid)

        vn_obj.set_virtual_network_network_id(42)
        with ExpectedException(PermissionDenied):
            self.api.virtual_network_update(vn_obj)

        # test can update with same value, needed internally
        # TODO(ethuleau): not sure why it's needed
        vn_obj = self.api.virtual_network_read(id=vn_obj.uuid)
        vn_obj.set_virtual_network_network_id(
            vn_obj.virtual_network_network_id)
        self.api.virtual_network_update(vn_obj)

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

    def test_allocate_vxlan_id(self):
        # enable vxlan routing on project
        proj = self._vnc_lib.project_read(
            fq_name=["default-domain", "default-project"])
        proj.set_vxlan_routing(True)
        self._vnc_lib.project_update(proj)

        mock_zk = self._api_server._db_conn._zk_db
        vn_obj = VirtualNetwork('%s-vn' % self.id())

        vn_obj_properties = VirtualNetworkType(forwarding_mode='l3')
        vn_obj_properties.set_vxlan_network_identifier(6000)
        vn_obj.set_virtual_network_properties(vn_obj_properties)

        self.api.virtual_network_create(vn_obj)

        # VN created, now read back the VN data to check if vxlan_id is set
        vn_obj = self.api.virtual_network_read(id=vn_obj.uuid)
        vn_obj_properties = vn_obj.get_virtual_network_properties()
        if not vn_obj_properties:
            self.fail("VN properties are not set")
        vxlan_id = vn_obj_properties.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id, 6000)
        self.assertEqual(vn_obj.get_fq_name_str() + "_vxlan",
                         mock_zk.get_vn_from_id(vxlan_id))
        self.assertGreaterEqual(vxlan_id, VNID_MIN_ALLOC)

        self.api.virtual_network_delete(id=vn_obj.uuid)
        logger.debug('PASS - test_allocate_vxlan_id')

    def test_cannot_allocate_vxlan_id(self):
        # enable vxlan routing on project
        proj = self._vnc_lib.project_read(
            fq_name=["default-domain", "default-project"])
        proj.set_vxlan_routing(True)
        self._vnc_lib.project_update(proj)

        mock_zk = self._api_server._db_conn._zk_db
        vn1_obj = VirtualNetwork('%s-vn' % self.id())

        vn1_obj_properties = VirtualNetworkType(forwarding_mode='l3')
        vn1_obj_properties.set_vxlan_network_identifier(6001)
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        self.api.virtual_network_create(vn1_obj)

        # VN created, now read back the VN data to check if vxlan_id is set
        vn1_obj = self.api.virtual_network_read(id=vn1_obj.uuid)
        vn1_obj_properties = vn1_obj.get_virtual_network_properties()
        if not vn1_obj_properties:
            self.fail("VN properties are not set")
        vxlan_id = vn1_obj_properties.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id, 6001)

        # Verified vxlan_id for VN1, now create VN2 with same vxlan_id
        vn2_obj = VirtualNetwork('%s-vn2' % self.id())
        vn2_obj_properties = VirtualNetworkType(forwarding_mode='l3')
        vn2_obj_properties.set_vxlan_network_identifier(6001)
        vn2_obj_properties.set_forwarding_mode('l2_l3')
        vn2_obj.set_virtual_network_properties(vn2_obj_properties)

        with ExpectedException(BadRequest):
            self.api.virtual_network_create(vn2_obj)

        self.assertEqual(vn1_obj.get_fq_name_str() + "_vxlan",
                         mock_zk.get_vn_from_id(vxlan_id))
        self.assertGreaterEqual(vxlan_id, VNID_MIN_ALLOC)
        self.api.virtual_network_delete(id=vn1_obj.uuid)
        logger.debug('PASS - test_cannot_allocate_vxlan_id')

    def test_deallocate_vxlan_id(self):
        # enable vxlan routing on project
        proj = self._vnc_lib.project_read(
            fq_name=["default-domain", "default-project"])
        proj.set_vxlan_routing(True)
        self._vnc_lib.project_update(proj)

        mock_zk = self._api_server._db_conn._zk_db
        vn_obj = VirtualNetwork('%s-vn' % self.id())

        vn_obj_properties = VirtualNetworkType(forwarding_mode='l3')
        vn_obj_properties.set_vxlan_network_identifier(6002)
        vn_obj.set_virtual_network_properties(vn_obj_properties)

        self.api.virtual_network_create(vn_obj)

        # VN created, now read back the VN data to check if vxlan_id is set
        vn_obj = self.api.virtual_network_read(id=vn_obj.uuid)
        vn_obj_properties = vn_obj.get_virtual_network_properties()
        if not vn_obj_properties:
            self.fail("VN properties are not set")
        vxlan_id = vn_obj_properties.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id, 6002)

        self.api.virtual_network_delete(id=vn_obj.uuid)
        self.assertNotEqual(vn_obj.get_fq_name_str() + "_vxlan",
                            mock_zk.get_vn_from_id(vxlan_id))
        logger.debug('PASS - test_deallocate_vxlan_id')

    def test_update_vxlan_id(self):
        # enable vxlan routing on project
        proj = self._vnc_lib.project_read(
            fq_name=["default-domain", "default-project"])
        proj.set_vxlan_routing(True)
        self._vnc_lib.project_update(proj)

        vn_obj = VirtualNetwork('%s-vn' % self.id())

        vn_obj_properties = VirtualNetworkType(forwarding_mode='l3')
        vn_obj_properties.set_vxlan_network_identifier(6003)
        vn_obj_properties.set_forwarding_mode('l2_l3')
        vn_obj.set_virtual_network_properties(vn_obj_properties)

        self.api.virtual_network_create(vn_obj)

        # VN created, now read back the VN data to check if vxlan_id is set
        vn_obj_read = self.api.virtual_network_read(id=vn_obj.uuid)
        vn_obj_properties_read = vn_obj_read.get_virtual_network_properties()
        if not vn_obj_properties_read:
            self.fail("VN properties are not set")
        vxlan_id = vn_obj_properties_read.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id, 6003)

        # Created VN. Now Update it with a different vxlan_id
        vn_obj_properties.set_vxlan_network_identifier(6004)
        vn_obj.set_virtual_network_properties(vn_obj_properties)
        self.api.virtual_network_update(vn_obj)

        vn_obj_read = self.api.virtual_network_read(id=vn_obj.uuid)
        vn_obj_properties_read = vn_obj_read.get_virtual_network_properties()
        if not vn_obj_properties_read:
            self.fail("VN properties are not set")
        vxlan_id = vn_obj_properties_read.get_vxlan_network_identifier()

        self.assertEqual(vxlan_id, 6004)
        self.api.virtual_network_delete(id=vn_obj.uuid)
        logger.debug('PASS - test_update_vxlan_id')

    def test_cannot_update_vxlan_id(self):
        # enable vxlan routing on project
        proj = self._vnc_lib.project_read(
            fq_name=["default-domain", "default-project"])
        proj.set_vxlan_routing(True)
        self._vnc_lib.project_update(proj)

        vn1_obj = VirtualNetwork('%s-vn1' % self.id())

        vn1_obj_properties = VirtualNetworkType(forwarding_mode='l3')
        vn1_obj_properties.set_vxlan_network_identifier(6005)
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        self.api.virtual_network_create(vn1_obj)

        # VN created, create second VN with different vxlan_id
        vn2_obj = VirtualNetwork('%s-vn2' % self.id())

        vn2_obj_properties = VirtualNetworkType(forwarding_mode='l3')
        vn2_obj_properties.set_vxlan_network_identifier(6006)
        vn2_obj_properties.set_forwarding_mode('l2_l3')
        vn2_obj.set_virtual_network_properties(vn2_obj_properties)

        self.api.virtual_network_create(vn2_obj)

        # Created Two VNs. Now Update it second VN with 1st VNs VXLAN_ID
        vn2_obj_properties.set_vxlan_network_identifier(6005)
        vn2_obj.set_virtual_network_properties(vn2_obj_properties)

        with ExpectedException(BadRequest):
            self.api.virtual_network_update(vn2_obj)

        vn_obj_read = self.api.virtual_network_read(id=vn2_obj.uuid)
        vn_obj_properties_read = vn_obj_read.get_virtual_network_properties()
        if not vn_obj_properties_read:
            self.fail("VN properties are not set")
        vxlan_id = vn_obj_properties_read.get_vxlan_network_identifier()
        self.assertEqual(vxlan_id, 6006)

        self.api.virtual_network_delete(id=vn2_obj.uuid)
        self.api.virtual_network_delete(id=vn1_obj.uuid)
        logger.debug('PASS - test_cannot_update_vxlan_id')
