#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common import BGP_RTGT_MIN_ID
from cfgm_common.exceptions import BadRequest
import mock
from vnc_api.vnc_api import GlobalSystemConfig

from vnc_cfg_api_server.resources import GlobalSystemConfigServer
from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestGlobalSystemConfig(test_case.ApiServerTestCase):
    DEFAULT_ASN = 64512
    NEW_ASN = 42
    FAKE_VN_LIST = [
        {
            'fq_name': ['fake-name1'],
            'uuid': 'fake_uuid1',
            'route_target_list': {
                'route_target': [
                    'target:%d:%d' % (NEW_ASN, BGP_RTGT_MIN_ID + 1000),
                ]
            }
        }
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

    def test_cannot_update_global_asn_if_used_by_user(self):
        gsc = self.api.global_system_config_read(GlobalSystemConfig().fq_name)

        gsc.autonomous_system = self.NEW_ASN
        with mock.patch.object(self._api_server._db_conn, 'dbe_list',
                               return_value=(True, self.FAKE_VN_LIST, None)):
            self.assertRaises(BadRequest, self.api.global_system_config_update,
                              gsc)
