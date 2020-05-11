#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import PERMS_RX
from cfgm_common.exceptions import HttpError
from sandesh_common.vns.constants import TagTypeNameToId
from vnc_api.gen.resource_common import Tag
from vnc_api.gen.resource_xsd import IdPermsType

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class TagServer(ResourceMixin, Tag):
    @classmethod
    def pre_dbe_alloc(cls, obj_dict):
        type_str = obj_dict.get('tag_type_name')
        value_str = obj_dict.get('tag_value')

        if type_str is None or value_str is None:
            msg = "Tag must be created with a type and a value"
            return False, (400, msg)

        type_str = type_str.lower()
        name = '%s=%s' % (type_str, value_str)
        obj_dict['name'] = name
        obj_dict['fq_name'][-1] = name
        obj_dict['display_name'] = name
        obj_dict['tag_type_name'] = type_str
        obj_dict['tag_value'] = value_str

        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        type_str = obj_dict['tag_type_name']

        if obj_dict.get('tag_id') is not None:
            msg = "Tag ID is not setable"
            return False, (400, msg)

        if obj_dict.get('tag_type_refs') is not None:
            msg = "Tag Type reference is not setable"
            return False, (400, msg)

        ok, result = cls.server.get_resource_class('tag_type').locate(
            [type_str], id_perms=IdPermsType(user_visible=False))
        if not ok:
            return False, result
        tag_type = result

        def undo_tag_type():
            cls.server.internal_request_delete('tag-type', tag_type['uuid'])
            return True, ''
        get_context().push_undo(undo_tag_type)

        obj_dict['tag_type_refs'] = [
            {
                'uuid': tag_type['uuid'],
                'to': tag_type['fq_name'],
            },
        ]

        # Allocate ID for tag value. Use the all fq_name to distinguish same
        # tag values between global and scoped
        value_id = cls.vnc_zk_client.alloc_tag_value_id(
            type_str, ':'.join(obj_dict['fq_name']))

        def undo_value_id():
            cls.vnc_zk_client.free_tag_value_id(type_str, value_id,
                                                ':'.join(obj_dict['fq_name']))
            return True, ""
        get_context().push_undo(undo_value_id)

        # Compose Tag ID with the type ID and value ID
        obj_dict['tag_id'] = "{}{:04x}".format(tag_type['tag_type_id'],
                                               value_id)

        # set default Perms2
        obj_dict['perms2']['global_access'] = PERMS_RX
        obj_dict['perms2']['owner'] = 'cloud-admin'

        return True, ""

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        # User cannot update display_name, type, value or id once created
        if (obj_dict.get('display_name') is not None or
                obj_dict.get('tag_type_name') is not None or
                obj_dict.get('tag_value') is not None or
                obj_dict.get('tag_id') is not None):
            msg = "Tag name, type, value or ID cannot be updated"
            return (False, (400, msg))

        if obj_dict.get('tag_type_refs') is not None:
            msg = "Tag-type reference cannot be updated"
            return (False, (400, msg))

        return True, ""

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        # Deallocate ID for tag value
        value_id = int(obj_dict['tag_id'], 0) & 0x0000ffff
        cls.vnc_zk_client.free_tag_value_id(obj_dict['tag_type_name'],
                                            value_id,
                                            ':'.join(obj_dict['fq_name']))

        # Don't remove pre-defined tag types
        if obj_dict['tag_type_name'] in TagTypeNameToId:
            return True, ''

        # Try to delete referenced tag-type and ignore RefExistError which
        # means it's still in use by other Tag resource
        if obj_dict.get('tag_type_refs') is not None:
            # Tag can have only one tag-type reference
            tag_type_uuid = obj_dict['tag_type_refs'][0]['uuid']
            try:
                cls.server.internal_request_delete('tag-type', tag_type_uuid)
            except HttpError as e:
                if e.status_code != 409:
                    return False, (e.status_code, e.content)

        return True, ""

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        cls.vnc_zk_client.alloc_tag_value_id(
            obj_dict['tag_type_name'],
            ':'.join(obj_dict['fq_name']),
            int(obj_dict['tag_id'], 0) & 0x0000ffff,
        )

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        cls.vnc_zk_client.free_tag_value_id(
            obj_dict['tag_type_name'],
            int(obj_dict['tag_id'], 0) & 0x0000ffff,
            ':'.join(obj_dict['fq_name']),
            notify=True,
        )

        return True, ''
