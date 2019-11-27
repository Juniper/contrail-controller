#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common import get_bgp_rtgt_min_id
from cfgm_common.exceptions import BadRequest
import mock
from vnc_api.vnc_api import GlobalSystemConfig, RouteTargetList, VirtualNetwork

from vnc_cfg_api_server.resources import GlobalSystemConfigServer
from vnc_cfg_api_server.tests import test_case

logger = logging.getLogger(__name__)


class TestGlobalSystemConfig(test_case.ApiServerTestCase):
    DEFAULT_ASN = 64512
    NEW_ASN = 42
    ASN_4_BYTES = 6553692
    FAKE_VN_LIST = [
        {
            'fq_name': ['fake-name1'],
            'uuid': 'fake_uuid1',
            'route_target_list': {
                'route_target': [
                    'target:%d:%d' % (NEW_ASN,
                                      get_bgp_rtgt_min_id(NEW_ASN) + 1000),
                ]
            },
            'import_route_target_list': {
                'route_target': [
                    'target:%d:%d' % (NEW_ASN,
                                      get_bgp_rtgt_min_id(NEW_ASN) + 1001),
                ]
            },
            'export_route_target_list': {
                'route_target': [
                    'target:%d:%d' % (NEW_ASN,
                                      get_bgp_rtgt_min_id(NEW_ASN) + 1002),
                ]
            }
        },
        {
            'fq_name': ['fake-name2'],
            'uuid': 'fake_uuid2',
            'route_target_list': {
                'route_target': [
                    'target:%s:%s' % (NEW_ASN, get_bgp_rtgt_min_id(NEW_ASN)),
                ]
            },
        },
        {
            'fq_name': ['fake-name3'],
            'uuid': 'fake_uuid3',
            'import_route_target_list': {
                'route_target': [
                    'target:%s:%s' % (NEW_ASN, get_bgp_rtgt_min_id(NEW_ASN)),
                ]
            },
        },
        {
            'fq_name': ['fake-name4'],
            'uuid': 'fake_uuid4',
            'export_route_target_list': {
                'route_target': [
                    'target:%s:%s' % (NEW_ASN, get_bgp_rtgt_min_id(NEW_ASN)),
                ]
            },
        },
    ]

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestGlobalSystemConfig, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestGlobalSystemConfig, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib

    def test_only_one_global_system_config_can_exists(self):
        gsc = GlobalSystemConfig('gsc-%s' % self.id())
        self.assertRaises(BadRequest, self.api.global_system_config_create,
                          gsc)

    @mock.patch.object(GlobalSystemConfigServer, 'locate',
                       return_value=(True, {'autonomous_system': DEFAULT_ASN}))
    def test_global_asn_populated(self, locate_mock):
        self.assertEqual(self.DEFAULT_ASN,
                         self._api_server.global_autonomous_system)
        locate_mock.assert_called_once_with(
            uuid=self._api_server._gsc_uuid,
            create_it=False, fields=['autonomous_system'],
        )

        locate_mock.reset_mock()
        self.assertEqual(self.DEFAULT_ASN,
                         self._api_server.global_autonomous_system)
        locate_mock.assert_not_called()

    def test_update_global_asn(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)

        self.assertEqual(self.DEFAULT_ASN, gsc.autonomous_system)
        gsc.autonomous_system = self.NEW_ASN
        gsc = self.api.global_system_config_update(gsc)
        self.assertEqual(self.NEW_ASN,
                         self._api_server.global_autonomous_system)

    def test_update_global_asn_with_valid_4_byte(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)

        # First, enable 4byte AS flag.
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        # Read back the GSC
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        self.assertEqual(True, gsc.enable_4byte_as)

        # Update global ASN to a 4 byte value
        gsc.autonomous_system = 700000
        self.api.global_system_config_update(gsc)

        # Read back the GSC
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        self.assertEqual(700000, gsc.autonomous_system)

        # Set the DEFAULT ASN and enable_4byte_as flag back to default as in
        # CI, the order of test cases can change
        gsc.autonomous_system = self.DEFAULT_ASN
        self.api.global_system_config_update(gsc)

        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

    def test_update_both_global_asn_and_asn_flag(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)

        # First, enable 4byte AS flag.
        gsc.enable_4byte_as = True
        gsc.autonomous_system = 61450
        self.api.global_system_config_update(gsc)

        # Set the enable_4_byte back to false
        gsc.enable_4byte_as = False
        gsc.autonomous_system = self.DEFAULT_ASN
        self.api.global_system_config_update(gsc)

    def test_update_2_byte_asn_range_check(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        gsc.autonomous_system = 70000
        self.assertRaises(BadRequest, self.api.global_system_config_update,
                          gsc)

    def test_update_4_byte_asn_range_check(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)

        # First, enable 4byte AS flag.
        gsc.enable_4byte_as = True
        self.api.global_system_config_update(gsc)

        # Update ASN to greater than 0xFFffFFff
        gsc.autonomous_system = 0x1FFFFFFFF
        self.assertRaises(BadRequest, self.api.global_system_config_update,
                          gsc)

        # Set enable_4byte_as flag back to default
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)

    def test_cannot_update_global_asn_if_used_by_user(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)

        gsc.autonomous_system = self.NEW_ASN
        with mock.patch.object(self._api_server._db_conn, 'dbe_list',
                               return_value=(True, self.FAKE_VN_LIST, None)):
            try:
                self.api.global_system_config_update(gsc)
                self.assertFail()
            except Exception as ext:
                self.assertIsInstance(ext, BadRequest)
                self.assertEqual(ext.status_code, 400)
                existing_rt = ext.content.split('\t')[1:]
                rt_count_in_fake_vn_list = 6
                self.assertEqual(len(existing_rt), rt_count_in_fake_vn_list)

    def test_can_update_global_asn_if_not_used_by_user(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        gsc.autonomous_system = self.NEW_ASN - 1
        gsc.enable_4byte_as = False

        with mock.patch.object(self._api_server._db_conn, 'dbe_list',
                               return_value=(True, self.FAKE_VN_LIST, None)):
            self.api.global_system_config_update(gsc)

        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        self.assertEqual(gsc.autonomous_system, self.NEW_ASN - 1)

        # clean up
        gsc.autonomous_system = self.DEFAULT_ASN
        self.api.global_system_config_update(gsc)
        self._api_server._global_asn = None

    def test_update_asn_if_any_rt_uses_4_byte(self):
        """
        Test scenario.

        1. Set enable_4byte_as to true
        2. Create RT with 4 bytes ASN
        3. Set enable_4byte_as to false
        4. Change global ASN to different 2 bytes numbers
        """
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)

        # Set enable_4byte_as to True
        gsc.enable_4byte_as = True
        gsc.autonomous_system = self.ASN_4_BYTES
        self.api.global_system_config_update(gsc)

        # reread gsc
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        self.assertEqual(gsc.enable_4byte_as, True)

        # create VN and RT with 4bytes ASN
        vn = VirtualNetwork('%s-vn' % self.id())
        rt_name = 'target:%d:%d' % (self.ASN_4_BYTES, 1000)
        vn.set_route_target_list(RouteTargetList([rt_name]))
        self.api.virtual_network_create(vn)

        # Set enable_4byte_as to false
        gsc.enable_4byte_as = False
        self.api.global_system_config_update(gsc)
        # Change global ASN to 2 bytes numbers (must be in separate step)
        gsc.autonomous_system = self.NEW_ASN
        self.api.global_system_config_update(gsc)

        # reread gsc to confirm change
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        self.assertEqual(gsc.autonomous_system, self.NEW_ASN)

        # cleanup
        self.api.virtual_network_delete(id=vn.uuid)
        gsc.autonomous_system = self.DEFAULT_ASN
        self.api.global_system_config_update(gsc)
