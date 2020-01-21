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

    @staticmethod
    def set_properties(obj, prop_map):
        """Set values to object using properties map.

        For every property which allow for 'Create' operation,
        set a value from prop_map.

        :param obj: schema resource
        :param prop_map: dict
        :return: schema resource
        """
        prop_not_found = []
        for prop, info in obj.prop_field_types.items():
            if 'C' in info['operations']:
                if prop not in prop_map:
                    prop_not_found.append(prop)
                    continue

                set_method = getattr(obj, 'set_%s' % prop)
                set_method(prop_map[prop])
        if len(prop_not_found) > 0:
            raise Exception(
                'Properties nod defined in prop_map: '
                '{} for object: {}'.format(', '.join(prop_not_found),
                                           obj.__class__.__name__))
        return obj

    def assertSchemaObjCreated(self, obj):
        """Create schema object and assert that uuid has been assigned.

        :param obj: schema resource
        """
        # Create and verify that uuid exists
        uuid = self.api.job_template_create(obj)
        self.assertIsNotNone(uuid)
