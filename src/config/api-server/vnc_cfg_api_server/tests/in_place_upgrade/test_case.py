#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
import logging
import os

from cfgm_common.tests.test_common import TestCase


logger = logging.getLogger(__name__)


class InPlaceUpgradeTestCase(TestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        kwargs['in_place_upgrade_path'] = cls._get_golden_json()
        super(InPlaceUpgradeTestCase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def _get_golden_json(cls):
        test_root = os.path.normpath(os.getcwd())
        dirpath = os.path.join(
            test_root, 'vnc_cfg_api_server', 'tests', 'in_place_upgrade')
        dirpath = os.path.abspath(dirpath)
        if not os.path.exists(dirpath):
            os.makedirs(dirpath)
        return os.path.join(dirpath, 'db-dump.json')

    @property
    def api(self):
        return self._vnc_lib
