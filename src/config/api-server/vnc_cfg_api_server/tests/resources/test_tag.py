#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import json
import logging

from cfgm_common.exceptions import BadRequest
from cfgm_common.exceptions import NoIdError
from cfgm_common.exceptions import RefsExistError
import gevent
import mock
from sandesh_common.vns import constants
from vnc_api.utils import OP_POST
from vnc_api.vnc_api import AddressGroup
from vnc_api.vnc_api import PermType2
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import Tag
from vnc_api.vnc_api import TagType
from vnc_api.vnc_api import VirtualMachine
from vnc_api.vnc_api import VirtualNetwork

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


class TestTag(TestTagBase):
    STALE_LOCK_SECS = '0.2'

    @classmethod
    def setUpClass(cls):
        super(TestTag, cls).setUpClass(
            extra_config_knobs=[
                ('DEFAULTS', 'stale_lock_seconds', cls.STALE_LOCK_SECS),
            ]
        )

    def test_tag_name_is_composed_with_type_and_value(self):
        name = 'fake-name-%s' % self.id()
        tag_type = 'fake_type-%s' % self.id()
        tag_type = tag_type.lower()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(name=name, display_name=name, tag_type_name=tag_type,
                  tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        self.assertEqual(tag.name, '%s=%s' % (tag_type, tag_value))
        self.assertEqual(tag.fq_name[-1], '%s=%s' % (tag_type, tag_value))
        self.assertEqual(tag.display_name, '%s=%s' % (tag_type, tag_value))
        self.assertEqual(tag.tag_type_name, tag_type)
        self.assertEqual(tag.tag_value, tag_value)

    def test_tag_is_unique(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag1 = Tag(tag_type_name=tag_type, tag_value=tag_value)
        self.api.tag_create(tag1)
        scoped_tag1 = Tag(tag_type_name=tag_type, tag_value=tag_value,
                          parent_obj=project)
        self.api.tag_create(scoped_tag1)
        gevent.sleep(float(self.STALE_LOCK_SECS))

        tag2 = Tag(tag_type_name=tag_type, tag_value=tag_value)
        self.assertRaises(RefsExistError, self.api.tag_create, tag2)
        scoped_tag2 = Tag(tag_type_name=tag_type, tag_value=tag_value,
                          parent_obj=project)
        self.assertRaises(RefsExistError, self.api.tag_create, scoped_tag2)

    def test_tag_type_is_mandatory(self):
        value = 'fake_value-%s' % self.id()
        tag = Tag(tag_value=value)

        self.assertRaises(BadRequest, self.api.tag_create, tag)

    def test_tag_type_cannot_be_updated(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        tag.tag_type_name = 'new_fake_type-%s' % self.id()
        self.assertRaises(BadRequest, self.api.tag_update, tag)

    def test_tag_value_is_mandatory(self):
        tag_type = 'fake_type-%s' % self.id()
        tag = Tag(tag_type_name=tag_type)

        self.assertRaises(BadRequest, self.api.tag_create, tag)

    def test_tag_value_cannot_be_updated(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        tag.tag_value = 'new_fake_type-%s' % self.id()
        self.assertRaises(BadRequest, self.api.tag_update, tag)

    def test_tag_id_cannot_be_set(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value,
                  tag_id='0xDEADBEEF')
        self.assertRaises(BadRequest, self.api.tag_create, tag)

    def test_tag_id_cannot_be_updated(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        tag.tag_id = '0xdeadbeef'
        self.assertRaises(BadRequest, self.api.tag_update, tag)

    def test_tag_type_reference_cannot_be_set(self):
        tag_value = 'fake_value-%s' % self.id()
        tag_type = TagType(name='tag-type-%s' % self.id())
        tag = Tag(tag_type_name=tag_type.name, tag_value=tag_value)
        tag.set_tag_type(tag_type)

        self.assertRaises(BadRequest, self.api.tag_create, tag)

    def test_tag_type_reference_cannot_be_updated(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        tag_type = TagType(name='tag-type-%s' % self.id())
        tag_type_uuid = self.api.tag_type_create(tag_type)
        tag_type = self.api.tag_type_read(id=tag_type_uuid)
        tag.set_tag_type(tag_type)
        self.assertRaises(BadRequest, self.api.tag_update, tag)

    def test_tag_type_reference_is_unique(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        tag_type = TagType(name='tag-type-%s' % self.id())
        tag_type_uuid = self.api.tag_type_create(tag_type)
        tag_type = self.api.tag_type_read(id=tag_type_uuid)
        tag.add_tag_type(tag_type)
        self.assertRaises(BadRequest, self.api.tag_update, tag)

    def test_tag_type_reference_cannot_be_removed(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        tag_type_uuid = tag.get_tag_type_refs()[0]['uuid']
        with mock.patch.object(self._api_server, 'is_admin_request',
                               return_value=True):
            tag_type = self.api.tag_type_read(id=tag_type_uuid)
        tag.del_tag_type(tag_type)
        self.assertRaises(BadRequest, self.api.tag_update, tag)

    def test_associated_tag_type_is_hidden_to_user(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        tag_type_uuid = tag.get_tag_type_refs()[0]['uuid']
        with mock.patch.object(self._api_server, 'is_admin_request',
                               return_value=True):
            tag_type = self.api.tag_type_read(id=tag_type_uuid)
        self.assertFalse(tag_type.get_id_perms().get_user_visible())

    def test_associated_tag_type_is_deleted_if_not_used(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        tag_type_uuid = tag.get_tag_type_refs()[0]['uuid']
        self.api.tag_delete(id=tag_uuid)
        with mock.patch.object(self._api_server, 'is_admin_request',
                               return_value=True):
            self.assertRaises(NoIdError, self.api.tag_type_read,
                              id=tag_type_uuid)

    def test_associated_tag_type_is_not_deleted_if_in_use(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value1 = 'fake_value1-%s' % self.id()
        tag1 = Tag(tag_type_name=tag_type, tag_value=tag_value1)
        tag1_id = self.api.tag_create(tag1)
        tag1 = self.api.tag_read(id=tag1_id)

        tag_value2 = 'fake_value2-%s' % self.id()
        tag2 = Tag(tag_type_name=tag_type, tag_value=tag_value2)
        self.api.tag_create(tag2)

        tag_type_uuid = tag1.get_tag_type_refs()[0]['uuid']
        self.api.tag_delete(id=tag1_id)
        with mock.patch.object(self._api_server, 'is_admin_request',
                               return_value=True):
            tag_type = self.api.tag_type_read(id=tag_type_uuid)
        self.assertEqual(tag_type_uuid, tag_type.uuid)

    def test_pre_defined_tag_type_is_not_deleted_even_if_not_use(self):
        mock_zk = self._api_server._db_conn._zk_db
        tag_value = 'fake_value1-%s' % self.id()
        for tag_type_name in list(constants.TagTypeNameToId.keys()):
            tag = Tag(tag_type_name=tag_type_name, tag_value=tag_value)
            tag_uuid = self.api.tag_create(tag)
            tag = self.api.tag_read(id=tag_uuid)

            tag_type_uuid = tag.get_tag_type_refs()[0]['uuid']
            zk_id = int(tag.tag_id, 0) & 0x0000ffff
            self.assertEqual(
                mock_zk.get_tag_value_from_id(tag.tag_type_name, zk_id),
                tag.get_fq_name_str(),
            )
            self.api.tag_delete(id=tag_uuid)
            with mock.patch.object(self._api_server, 'is_admin_request',
                                   return_value=True):
                tag_type = self.api.tag_type_read(id=tag_type_uuid)
            self.assertEqual(tag_type_uuid, tag_type.uuid)
            self.assertNotEqual(
                mock_zk.get_tag_value_from_id(tag.tag_type_name, zk_id),
                tag.get_fq_name_str(),
            )

    def test_tag_type_is_allocated(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        self.assertEqual(len(tag.get_tag_type_refs()), 1)

    def test_allocate_tag_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        zk_id = int(tag.tag_id, 0) & 0x0000ffff
        self.assertEqual(
            tag.get_fq_name_str(),
            mock_zk.get_tag_value_from_id(tag.tag_type_name, zk_id),
        )

    def test_deallocate_tag_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        zk_id = int(tag.tag_id, 0) & 0x0000ffff
        self.api.tag_delete(id=tag_uuid)
        self.assertNotEqual(
            mock_zk.get_tag_value_from_id(tag.tag_type_name, zk_id),
            tag.get_fq_name_str(),
        )

    def test_not_deallocate_tag_id_if_value_does_not_correspond(self):
        mock_zk = self._api_server._db_conn._zk_db
        tag_type = 'fake_type-%s' % self.id()
        tag_value1 = 'fake_value1-%s' % self.id()
        tag_value2 = 'fake_value2-%s' % self.id()
        tag1 = Tag(tag_type_name=tag_type, tag_value=tag_value1)
        tag_uuid = self.api.tag_create(tag1)
        tag1 = Tag(tag_type_name=tag_type, tag_value=tag_value2)
        tag_uuid = self.api.tag_create(tag1)
        tag1 = self.api.tag_read(id=tag_uuid)

        zk_id = int(tag1.tag_id, 0) & 0x0000ffff
        fake_fq_name = "fake fq_name"
        mock_zk._tag_value_id_allocator[tag1.tag_type_name].delete(zk_id)
        mock_zk._tag_value_id_allocator[tag1.tag_type_name].reserve(
            zk_id, fake_fq_name)
        self.api.tag_delete(id=tag_uuid)
        self.assertIsNotNone(
            mock_zk.get_tag_value_from_id(tag1.tag_type_name, zk_id))
        self.assertEqual(
            fake_fq_name,
            mock_zk.get_tag_value_from_id(tag1.tag_type_name, zk_id),
        )

    def test_create_project_scoped_tag(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)

        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(parent_obj=project, tag_type_name=tag_type,
                  tag_value=tag_value)
        tag_uuid = self.api.tag_create(tag)
        tag = self.api.tag_read(id=tag_uuid)

        self.assertEqual(
            tag.get_fq_name_str(),
            '%s:%s=%s' % (project.get_fq_name_str(), tag_type.lower(),
                          tag_value),
        )

    def test_tag_duplicable_between_global_and_project(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()

        global_tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        global_tag_uuid = self.api.tag_create(global_tag)
        global_tag = self.api.tag_read(id=global_tag_uuid)

        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        scoped_tag = Tag(parent_obj=project, tag_type_name=tag_type,
                         tag_value=tag_value)
        scoped_tag_uuid = self.api.tag_create(scoped_tag)
        scoped_tag = self.api.tag_read(id=scoped_tag_uuid)

        self.assertNotEquals(global_tag.uuid, scoped_tag.uuid)
        self.assertNotEquals(global_tag.fq_name, scoped_tag.fq_name)
        self.assertNotEquals(global_tag.tag_id, scoped_tag.tag_id)

    def test_tag_duplicable_between_project(self):
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()

        project1 = Project('project1-%s' % self.id())
        self.api.project_create(project1)
        project1_tag = Tag(parent_obj=project1, tag_type_name=tag_type,
                           tag_value=tag_value)
        project1_tag_uuid = self.api.tag_create(project1_tag)
        project1_tag = self.api.tag_read(id=project1_tag_uuid)

        project2 = Project('project2-%s' % self.id())
        self.api.project_create(project2)
        project2_tag = Tag(parent_obj=project2, tag_type_name=tag_type,
                           tag_value=tag_value)
        project2_tag_uuid = self.api.tag_create(project2_tag)
        project2_tag = self.api.tag_read(id=project2_tag_uuid)

        self.assertNotEquals(project1_tag.uuid, project2_tag.uuid)
        self.assertNotEquals(project1_tag.fq_name, project2_tag.fq_name)
        self.assertNotEquals(project1_tag.tag_id, project2_tag.tag_id)

    def test_tag_created_before_associated_to_a_resource(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)

        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()

        # global
        tags_dict = {
            tag_type: {
                'is_global': True,
                'value': tag_value,
            }
        }
        self.assertRaises(NoIdError, self.api.set_tags, vn, tags_dict)

        # scoped
        tags_dict = {
            tag_type: {
                'value': tag_value,
            }
        }
        self.assertRaises(NoIdError, self.api.set_tags, vn, tags_dict)

    def test_associate_global_tag_to_a_resource(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        vn_uuid = self.api.virtual_network_create(vn)

        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        global_tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        self.api.tag_create(global_tag)

        tags_dict = {
            tag_type: {
                'is_global': True,
                'value': tag_value,
            },
        }
        self.api.set_tags(vn, tags_dict)

        vn = self._vnc_lib.virtual_network_read(id=vn_uuid)
        self.assertEqual(len(vn.get_tag_refs()), 1)
        self.assertEqual(vn.get_tag_refs()[0]['uuid'], global_tag.uuid)

    def test_fail_to_associate_global_tag_to_a_resource_if_not_precise(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)

        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        global_tag = Tag(tag_type_name=tag_type, tag_value=tag_value)
        self.api.tag_create(global_tag)

        tags_dict = {
            tag_type: {
                # 'is_global': True, Don't precise the tag is global
                # => assoc fail
                'value': tag_value,
            },
        }
        self.assertRaises(NoIdError, self.api.set_tags, vn, tags_dict)

    def test_associate_scoped_tag_to_a_resource(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        vn_uuid = self.api.virtual_network_create(vn)

        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        scoped_tag = Tag(tag_type_name=tag_type, tag_value=tag_value,
                         parent_obj=project)
        self.api.tag_create(scoped_tag)

        tags_dict = {
            tag_type: {
                'value': tag_value,
            },
        }
        self.api.set_tags(vn, tags_dict)

        vn = self._vnc_lib.virtual_network_read(id=vn_uuid)
        self.assertEqual(len(vn.get_tag_refs()), 1)
        self.assertEqual(vn.get_tag_refs()[0]['uuid'], scoped_tag.uuid)

    def test_fail_to_associate_scoped_tag_to_a_resource_if_not_precise(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)

        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        scoped_tag = Tag(tag_type_name=tag_type, tag_value=tag_value,
                         parent_obj=project)
        self.api.tag_create(scoped_tag)

        tags_dict = {
            tag_type: {
                'is_global': True,  # Precise the tag is global => assoc fail
                'value': tag_value,
            }
        }
        self.assertRaises(NoIdError, self.api.set_tags, vn, tags_dict)

    def test_only_one_value_for_a_type_can_be_associate_to_a_resource(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        vn_uuid = self.api.virtual_network_create(vn)

        tag_type = 'fake_type-%s' % self.id()
        global_tag_value = 'global_fake_value-%s' % self.id()
        global_tag = Tag(tag_type_name=tag_type, tag_value=global_tag_value)
        self.api.tag_create(global_tag)

        tags_dict = {
            tag_type: {
                'is_global': True,
                'value': global_tag_value,
            },
        }
        self.api.set_tags(vn, tags_dict)

        vn = self._vnc_lib.virtual_network_read(id=vn_uuid)
        self.assertEqual(len(vn.get_tag_refs()), 1)
        self.assertEqual(vn.get_tag_refs()[0]['uuid'], global_tag.uuid)

        scoped_tag_value = 'scoped_fake_value-%s' % self.id()
        scoped_tag = Tag(tag_type_name=tag_type, tag_value=scoped_tag_value,
                         parent_obj=project)
        self.api.tag_create(scoped_tag)

        tags_dict = {
            tag_type: {
                'value': scoped_tag_value,
            },
        }
        self.api.set_tags(vn, tags_dict)

        # Scoped tag which is the same type as the global tag but with a
        # different value, replaced the global tag ref of the VN. One at a time
        vn = self._vnc_lib.virtual_network_read(id=vn_uuid)
        self.assertEqual(len(vn.get_tag_refs()), 1)
        self.assertEqual(vn.get_tag_refs()[0]['uuid'], scoped_tag.uuid)

    def test_only_one_value_for_a_type_can_be_associate_to_a_resource2(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        tag_type = 'fake_type-%s' % self.id()
        global_tag = Tag(tag_type_name=tag_type,
                         tag_value='global_fake_value-%s' % self.id())
        self.api.tag_create(global_tag)
        scoped_tag = Tag(tag_type_name=tag_type,
                         tag_value='scoped_fake_value-%s' % self.id(),
                         parent_obj=project)
        self.api.tag_create(scoped_tag)

        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        vn.add_tag(global_tag)
        vn.add_tag(scoped_tag)
        self.assertRaises(BadRequest, self.api.virtual_network_create, vn)

        vn.set_tag(global_tag)
        self.api.virtual_network_create(vn)
        vn = self._vnc_lib.virtual_network_read(id=vn.uuid)
        self.assertEqual(len(vn.get_tag_refs()), 1)
        self.assertEqual(vn.get_tag_refs()[0]['uuid'], global_tag.uuid)

        vn.add_tag(scoped_tag)
        self.assertRaises(BadRequest, self.api.virtual_network_update, vn)

    def test_address_group_can_only_have_label_tag_type_ref(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value,
                  parent_obj=project)
        self.api.tag_create(tag)

        # Cannot create AG with ref to a non label tag
        ag = AddressGroup('ag-%s' % self.id(), parent_obj=project)
        ag.add_tag(tag)
        self.assertRaises(BadRequest, self.api.address_group_create, ag)

        ag.set_tag_list([])
        self.api.address_group_create(ag)

        # Cannot set non lable tag to an AG with /set-tag API
        self.assertRaises(BadRequest, self.api.set_tag, ag, tag_type,
                          tag_value)

        # Cannot add ref to a non label tag to AG
        ag.add_tag(tag)
        self.assertRaises(BadRequest, self.api.address_group_update, ag)

    def test_unset_tag_from_a_resource(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        vn_uuid = self.api.virtual_network_create(vn)

        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value,
                  parent_obj=project)
        self.api.tag_create(tag)

        self.api.set_tag(vn, tag_type, tag_value)
        for system_tag_type in constants.TagTypeNameToId:
            self.api.tag_create(
                Tag(tag_type_name=system_tag_type, tag_value=tag_value))
            self.api.set_tag(vn, system_tag_type, tag_value, is_global=True)

        vn = self._vnc_lib.virtual_network_read(id=vn_uuid)
        self.assertEqual(len(vn.get_tag_refs()),
                         len(constants.TagTypeNameToId) + 1)
        self.assertTrue(tag.uuid in {ref['uuid'] for ref in vn.get_tag_refs()})

        self.api.unset_tag(vn, tag_type)

        vn = self._vnc_lib.virtual_network_read(id=vn_uuid)
        self.assertEqual(len(vn.get_tag_refs()),
                         len(constants.TagTypeNameToId))
        self.assertFalse(tag.uuid in
                         {ref['uuid'] for ref in vn.get_tag_refs()})

    def test_resource_exists_before_disassociate_tag(self):
        vn = VirtualNetwork('vn-%s' % self.id())

        self.assertRaises(BadRequest, self.api.unset_tag, vn,
                          'fake_type-%s' % self.id())

    def test_set_unset_multi_value_of_authorized_type_on_one_resource(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        vn_uuid = self.api.virtual_network_create(vn)

        # Label tag type is the only one type authorized to be set multiple
        # time on a same resource
        tag_type = 'label'
        tag_value1 = '%s-label1' % self.id()
        label_tag1 = Tag(tag_type_name=tag_type, tag_value=tag_value1,
                         parent_obj=project)
        self.api.tag_create(label_tag1)
        tag_value2 = '%s-label2' % self.id()
        label_tag2 = Tag(tag_type_name=tag_type, tag_value=tag_value2,
                         parent_obj=project)
        self.api.tag_create(label_tag2)
        tag_value3 = '%s-label3' % self.id()
        label_tag3 = Tag(tag_type_name=tag_type, tag_value=tag_value3,
                         parent_obj=project)
        self.api.tag_create(label_tag3)

        tags_dict = {
            tag_type: {
                'value': tag_value1,
                'add_values': [tag_value2],
            },
        }
        self.api.set_tags(vn, tags_dict)

        vn = self._vnc_lib.virtual_network_read(id=vn_uuid)
        self.assertEqual(len(vn.get_tag_refs()), 2)
        self.assertEqual({ref['uuid'] for ref in vn.get_tag_refs()},
                         set([label_tag1.uuid, label_tag2.uuid]))

        tags_dict = {
            tag_type: {
                'add_values': [tag_value3],
                'delete_values': [tag_value1],
            },
        }
        self.api.set_tags(vn, tags_dict)

        vn = self._vnc_lib.virtual_network_read(id=vn_uuid)
        self.assertEqual(len(vn.get_tag_refs()), 2)
        self.assertEqual({ref['uuid'] for ref in vn.get_tag_refs()},
                         set([label_tag2.uuid, label_tag3.uuid]))

        self.api.unset_tag(vn, tag_type)

        vn = self._vnc_lib.virtual_network_read(id=vn_uuid)
        self.assertIsNone(vn.get_tag_refs())

    def test_add_remove_multi_value_of_authorized_type_on_same_resource(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        # Label tag type is the only one type authorized to be set multiple
        # time on a same resource
        tag_type = 'label'
        tag_value1 = '%s-label1' % self.id()
        label_tag1 = Tag(tag_type_name=tag_type, tag_value=tag_value1,
                         parent_obj=project)
        self.api.tag_create(label_tag1)
        tag_value2 = '%s-label2' % self.id()
        label_tag2 = Tag(tag_type_name=tag_type, tag_value=tag_value2,
                         parent_obj=project)
        self.api.tag_create(label_tag2)
        tag_value3 = '%s-label3' % self.id()
        label_tag3 = Tag(tag_type_name=tag_type, tag_value=tag_value3,
                         parent_obj=project)
        self.api.tag_create(label_tag3)

        vn.add_tag(label_tag1)
        vn.add_tag(label_tag2)
        self.api.virtual_network_create(vn)
        vn = self._vnc_lib.virtual_network_read(id=vn.uuid)
        self.assertEqual(len(vn.get_tag_refs()), 2)
        self.assertEqual({ref['uuid'] for ref in vn.get_tag_refs()},
                         set([label_tag1.uuid, label_tag2.uuid]))

        vn.add_tag(label_tag3)
        self.api.virtual_network_update(vn)
        vn = self._vnc_lib.virtual_network_read(id=vn.uuid)
        self.assertEqual(len(vn.get_tag_refs()), 3)
        self.assertEqual({ref['uuid'] for ref in vn.get_tag_refs()},
                         set([label_tag1.uuid, label_tag2.uuid,
                              label_tag3.uuid]))

        vn.del_tag(label_tag2)
        self.api.virtual_network_update(vn)
        vn = self._vnc_lib.virtual_network_read(id=vn.uuid)
        self.assertEqual(len(vn.get_tag_refs()), 2)
        self.assertEqual({ref['uuid'] for ref in vn.get_tag_refs()},
                         set([label_tag1.uuid, label_tag3.uuid]))

    def test_associate_scoped_tag_to_project(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value,
                  parent_obj=project)
        self.api.tag_create(tag)

        self.api.set_tag(project, tag_type, tag_value)

    def test_associate_scoped_tag_to_virtual_machine(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vm = VirtualMachine('vm-%s' % self.id())
        vm_uuid = self.api.virtual_machine_create(vm)
        vm = self.api.virtual_machine_read(id=vm_uuid)
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value,
                  parent_obj=project)
        self.api.tag_create(tag)

        self.assertRaises(NoIdError, self.api.set_tag, vm, tag_type, tag_value)

        perms2 = PermType2()
        perms2.owner = project.uuid.replace('-', '')
        vm.set_perms2(perms2)
        self.api.virtual_machine_update(vm)
        self.api.set_tag(vm, tag_type, tag_value)

    def test_resource_not_updated_if_no_tag_references_modified(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value,
                  parent_obj=project)
        self.api.tag_create(tag)
        original_resource_update = self._api_server._db_conn.dbe_update

        def update_resource(*args, **kwargs):
            return original_resource_update(*args, **kwargs)

        with mock.patch.object(self._api_server._db_conn, 'dbe_update',
                               side_effect=update_resource) as mock_db_update:
            self.api.unset_tag(vn, tag_type)
            mock_db_update.assert_not_called()

            mock_db_update.reset_mock()
            self.api.set_tag(vn, tag_type, tag_value)
            mock_db_update.assert_called()

            mock_db_update.reset_mock()
            self.api.set_tag(vn, tag_type, tag_value)
            mock_db_update.assert_not_called()

            mock_db_update.reset_mock()
            self.api.unset_tag(vn, tag_type)
            mock_db_update.assert_called()

            tag_type = 'label'
            tag_value1 = '%s-label1' % self.id()
            label_tag1 = Tag(tag_type_name=tag_type, tag_value=tag_value1,
                             parent_obj=project)
            self.api.tag_create(label_tag1)
            tag_value2 = '%s-label2' % self.id()
            label_tag2 = Tag(tag_type_name=tag_type, tag_value=tag_value2,
                             parent_obj=project)
            self.api.tag_create(label_tag2)

            tags_dict = {
                tag_type: {
                    'delete_values': [tag_value1, tag_value2],
                },
            }
            mock_db_update.reset_mock()
            self.api.set_tags(vn, tags_dict)
            mock_db_update.assert_not_called()

            tags_dict = {
                tag_type: {
                    'add_values': [tag_value1, tag_value2],
                },
            }
            mock_db_update.reset_mock()
            self.api.set_tags(vn, tags_dict)
            mock_db_update.assert_called()

            mock_db_update.reset_mock()
            self.api.set_tag(vn, tag_type, tag_value1)
            self.api.set_tags(vn, tags_dict)
            mock_db_update.assert_not_called()

            tags_dict = {
                tag_type: {
                    'delete_values': [tag_value1, tag_value2],
                },
            }
            mock_db_update.reset_mock()
            self.api.set_tags(vn, tags_dict)
            mock_db_update.assert_called()

    def test_set_tag_api_sanity(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        tag_type = 'fake_type-%s' % self.id()
        tag_value = 'fake_value-%s' % self.id()
        tag = Tag(tag_type_name=tag_type, tag_value=tag_value,
                  parent_obj=project)
        self.api.tag_create(tag)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)
        url = self.api._action_uri['set-tag']

        test_suite = [
            ({'obj_uuid': vn.uuid}, BadRequest),
            ({'obj_type': 'virtual_network'}, BadRequest),
            ({'obj_uuid': 'fake_uuid', 'obj_type': 'virtual_network'},
             NoIdError),
            ({'obj_uuid': vn.uuid, 'obj_type': 'wrong_type'}, BadRequest),
            ({
                'obj_uuid': vn.uuid, 'obj_type': 'virtual_network',
                tag_type: {'value': tag_value}
            }, None),
            ({
                'obj_uuid': vn.uuid, 'obj_type': 'virtual-network',
                tag_type: {'value': tag_value}
            }, None),
        ]

        for tags_dict, result in test_suite:
            if result and issubclass(result, Exception):
                self.assertRaises(result, self.api._request_server,
                                  OP_POST, url, json.dumps(tags_dict))
            else:
                self.api._request_server(OP_POST, url, json.dumps(tags_dict))
