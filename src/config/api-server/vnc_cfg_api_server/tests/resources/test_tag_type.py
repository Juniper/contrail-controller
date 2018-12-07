#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import logging

from cfgm_common.exceptions import BadRequest
from cfgm_common.exceptions import RefsExistError
import mock
from sandesh_common.vns import constants
from vnc_api.vnc_api import TagType

from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestTagBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestTagBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestTagBase, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib


class TestTagType(TestTagBase):
    def test_pre_definied_tag_type_initialized(self):
        with mock.patch.object(self._api_server, 'is_admin_request',
                               return_value=True):
            tag_types = {tag_type.name for tag_type
                         in self.api.tag_types_list(detail=True)}
        self.assertTrue(
            set(constants.TagTypeNameToId.keys()).issubset(tag_types))

    def test_tag_type_is_unique(self):
        name = 'tag-type-%s' % self.id()
        tag_type = TagType(name=name)
        self.api.tag_type_create(tag_type)
        new_tag_type = TagType(name=name)
        self.assertRaises(RefsExistError, self.api.tag_type_create,
                          new_tag_type)

    def test_tag_type_id_cannot_be_set(self):
        tag_type = TagType(name='tag-type-%s' % self.id(),
                           tag_type_id='0xBEEF')
        self.assertRaises(BadRequest, self.api.tag_type_create, tag_type)

    def test_tag_type_id_cannot_be_updated(self):
        tag_type = TagType(name='tag-type-%s' % self.id())
        tag_type_uuid = self.api.tag_type_create(tag_type)
        tag_type = self.api.tag_type_read(id=tag_type_uuid)
        tag_type.tag_type_id = '0xbeef'
        self.assertRaises(BadRequest, self.api.tag_type_update, tag_type)

    def test_tag_type_display_name_cannot_be_set(self):
        name = 'tag-type-%s' % self.id()
        tag_type = TagType(name=name, display_name='fake_name')
        tag_type_uuid = self.api.tag_type_create(tag_type)
        tag_type = self.api.tag_type_read(id=tag_type_uuid)
        self.assertEqual(tag_type.display_name, name)

    def test_tag_type_display_name_cannot_be_updated(self):
        tag_type = TagType(name='tag-type-%s' % self.id())
        tag_type_uuid = self.api.tag_type_create(tag_type)
        tag_type = self.api.tag_type_read(id=tag_type_uuid)
        tag_type.display_name = 'new_name'
        self.assertRaises(BadRequest, self.api.tag_type_update, tag_type)

    def test_allocate_tag_type_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        type_str = 'tag-type-%s' % self.id()
        tag_type = TagType(name=type_str)
        tag_type_uuid = self.api.tag_type_create(tag_type)

        tag_type = self.api.tag_type_read(id=tag_type_uuid)
        zk_id = int(tag_type.tag_type_id, 0)
        self.assertEqual(type_str, mock_zk.get_tag_type_from_id(zk_id))

    def test_deallocate_tag_type_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        type_str = 'tag-type-%s' % self.id()
        tag_type = TagType(name=type_str)
        tag_type_uuid = self.api.tag_type_create(tag_type)

        tag_type = self.api.tag_type_read(id=tag_type_uuid)
        zk_id = int(tag_type.tag_type_id, 0)
        self.api.tag_type_delete(id=tag_type_uuid)
        self.assertNotEqual(mock_zk.get_tag_type_from_id(zk_id),
                            tag_type.fq_name[-1])

    def test_not_deallocate_tag_type_id_if_value_does_not_correspond(self):
        mock_zk = self._api_server._db_conn._zk_db
        type_str = 'tag-type-%s' % self.id()
        tag_type = TagType(name=type_str)
        tag_type_uuid = self.api.tag_type_create(tag_type)

        tag_type = self.api.tag_type_read(id=tag_type_uuid)
        zk_id = int(tag_type.tag_type_id, 0)
        fake_tag_type = "fake tag type"
        mock_zk._tag_type_id_allocator.delete(zk_id)
        mock_zk._tag_type_id_allocator.reserve(zk_id, fake_tag_type)
        self.api.tag_type_delete(id=tag_type_uuid)
        self.assertIsNotNone(mock_zk.get_tag_type_from_id(zk_id))
        self.assertEqual(fake_tag_type, mock_zk.get_tag_type_from_id(zk_id))
