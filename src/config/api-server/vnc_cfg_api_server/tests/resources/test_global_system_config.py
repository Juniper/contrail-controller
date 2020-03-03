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
            'fq_name': ['fake-vn-name1'],
            'uuid': 'fake_vn_uuid1',
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
            'fq_name': ['fake-vn-name2'],
            'uuid': 'fake_vn_uuid2',
            'route_target_list': {
                'route_target': [
                    'target:%s:%s' % (NEW_ASN, get_bgp_rtgt_min_id(NEW_ASN)),
                    'target:%d:%d' % (NEW_ASN + 1,
                                      get_bgp_rtgt_min_id(NEW_ASN)),
                    'target:%d:%d' % (NEW_ASN + 2,
                                      get_bgp_rtgt_min_id(NEW_ASN)),
                ]
            },
        },
        {
            'fq_name': ['fake-vn-name3'],
            'uuid': 'fake_vn_uuid3',
            'import_route_target_list': {
                'route_target': [
                    'target:%s:%s' % (NEW_ASN, get_bgp_rtgt_min_id(NEW_ASN)),
                ]
            },
        },
        {
            'fq_name': ['fake-vn-name4'],
            'uuid': 'fake_vn_uuid4',
            'export_route_target_list': {
                'route_target': [
                    'target:%s:%s' % (NEW_ASN, get_bgp_rtgt_min_id(NEW_ASN)),
                ]
            },
        },
    ]
    FAKE_LR_LIST = [
        {
            'fq_name': ['fake-lr-name1'],
            'uuid': 'fake_lr_uuid1',
            'configured_route_target_list': {
                'route_target': [
                    'target:%s:%s' % (NEW_ASN, get_bgp_rtgt_min_id(NEW_ASN)),
                    'target:%s:%s' % (NEW_ASN,
                                      get_bgp_rtgt_min_id(NEW_ASN)),
                    'target:%s:%s' % (NEW_ASN + 1,
                                      get_bgp_rtgt_min_id(NEW_ASN)),
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

    def tearDown(self, *args, **kwargs):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        gsc.autonomous_system = self.DEFAULT_ASN
        gsc.enable_4byte_as = False

        self.api.global_system_config_update(gsc)
        self._api_server._global_asn = None

        test_case.ApiServerTestCase.tearDown(self)

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

    def test_update_both_global_asn_and_asn_flag(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)

        # First, enable 4byte AS flag.
        gsc.enable_4byte_as = True
        gsc.autonomous_system = 61450
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

    def test_cannot_update_global_asn_if_used_by_user(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)

        tests = [
            {'autonomous_system': self.NEW_ASN,
             'mock_return_value': (True, self.FAKE_VN_LIST, None),
             'expected_rt_count': 6},
            {'autonomous_system': self.NEW_ASN + 1,
             'mock_return_value': (True, self.FAKE_VN_LIST, None),
             'expected_rt_count': 1},
            {'autonomous_system': self.NEW_ASN + 2,
             'mock_return_value': (True, self.FAKE_VN_LIST, None),
             'expected_rt_count': 1},
            {'autonomous_system': self.NEW_ASN,
             'mock_return_value': (True, self.FAKE_LR_LIST, None),
             'expected_rt_count': 2},
            {'autonomous_system': self.NEW_ASN + 1,
             'mock_return_value': (True, self.FAKE_LR_LIST, None),
             'expected_rt_count': 1},
        ]
        for t in tests:
            gsc.autonomous_system = t['autonomous_system']
            with mock.patch.object(self._api_server._db_conn, 'dbe_list',
                                   return_value=t['mock_return_value']):
                try:
                    self.api.global_system_config_update(gsc)
                except Exception as exc:
                    self.assertIsInstance(exc, BadRequest)
                    self.assertEqual(exc.status_code, 400)
                    # The first line of exception is a generic message.
                    # The remaining lines are list of Route Targets
                    # that are invalid.
                    # We want to count all invalid Route Targets,
                    # so we start with the second element of the list.
                    existing_rt = exc.content.split('\t')[1:]
                    self.assertEqual(len(existing_rt), t['expected_rt_count'])

    def test_can_update_global_asn_if_not_used_by_user(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)
        gsc.enable_4byte_as = False

        tests = [
            {'autonomous_system': self.NEW_ASN + 10,
             'mock_return_value': (True, self.FAKE_VN_LIST, None)},
            {'autonomous_system': self.NEW_ASN + 2,
             'mock_return_value': (True, self.FAKE_LR_LIST, None)},
        ]
        for t in tests:
            with mock.patch.object(self._api_server._db_conn, 'dbe_list',
                                   return_value=t['mock_return_value']):
                gsc.autonomous_system = t['autonomous_system']
                self.api.global_system_config_update(gsc)

            gsc = self.api.global_system_config_read(
                GlobalSystemConfig().fq_name)
            self.assertEqual(gsc.autonomous_system, t['autonomous_system'])

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
