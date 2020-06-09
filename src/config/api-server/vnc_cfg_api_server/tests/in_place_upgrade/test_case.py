#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
import inspect
import logging
import os

from cfgm_common.tests.test_common import TestCase
from vnc_api import vnc_api
from vnc_api.exceptions import RefsExistError


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
    def tearDownClass(cls, *args, **kwargs):
        super(InPlaceUpgradeTestCase, cls).tearDownClass(*args, **kwargs)

        golden_json_filename = cls._get_golden_json()
        with open(golden_json_filename, 'r') as f:
            golden_json = f.read()

        missing_objs = set()
        for obj_type in cls._get_all_vnc_obj_types():
            if obj_type not in golden_json:
                missing_objs.add(obj_type)

        if missing_objs:
            raise Exception(
                'In-place-upgrade assertion error. '
                '{} schema object types have not been tested: "{}" '
                'These objects are not visible '
                'in golden json db-dump: {}'.format(len(missing_objs),
                                                    missing_objs,
                                                    golden_json_filename))

    @classmethod
    def _get_all_vnc_obj_types(cls):
        obj_types = set()
        for name, obj in inspect.getmembers(vnc_api):
            if inspect.isclass(obj) and hasattr(obj, 'object_type'):
                obj_types.add(obj.object_type)
        return obj_types

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

        if len(prop_not_found) > 0:
            raise Exception(
                'Properties nod defined in prop_map: '
                '{} for object: {}'.format(', '.join(prop_not_found),
                                           obj.resource_type))
        return obj(**prop_map)

    def assertSchemaObjCreateOrUpdate(self, obj):
        """Create schema object and assert that uuid has been assigned.

        :param obj: schema resource
        """
        # Create and verify that uuid exists
        create = getattr(self.api, '{}_create'.format(obj.object_type))
        update = getattr(self.api, '{}_update'.format(obj.object_type))

        try:
            uuid = create(obj)
            obj.set_uuid(uuid)
        except RefsExistError:
            update(obj)

        self.assertIsNotNone(obj.uuid)
