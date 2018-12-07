#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from sandesh_common.vns.constants import TagTypeNameToId
from vnc_api.gen.resource_common import TagType

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class TagTypeServer(ResourceMixin, TagType):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        type_str = obj_dict['fq_name'][-1]
        obj_dict['name'] = type_str
        obj_dict['display_name'] = type_str

        if obj_dict.get('tag_type_id') is not None:
            msg = "Tag Type ID is not setable"
            return False, (400, msg)

        # Allocate ID for tag-type
        type_id = cls.vnc_zk_client.alloc_tag_type_id(type_str)

        def undo_type_id():
            cls.vnc_zk_client.free_tag_type_id(type_id, obj_dict['fq_name'])
            return True, ""
        get_context().push_undo(undo_type_id)

        obj_dict['tag_type_id'] = "0x{:04x}".format(type_id)

        return True, ""

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        # User can't update type or value once created
        if obj_dict.get('display_name') or obj_dict.get('tag_type_id'):
            msg = "Tag Type value or ID cannot be updated"
            return False, (400, msg)

        return True, ""

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        # Deallocate tag-type ID
        cls.vnc_zk_client.free_tag_type_id(int(obj_dict['tag_type_id'], 0),
                                           obj_dict['fq_name'][-1])
        return True, ''

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        cls.vnc_zk_client.alloc_tag_type_id(
            ':'.join(obj_dict['fq_name']), int(obj_dict['tag_type_id'], 0))

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        # Deallocate in memory tag-type ID
        cls.vnc_zk_client.free_tag_type_id(int(obj_dict['tag_type_id'], 0),
                                           obj_dict['fq_name'][-1],
                                           notify=True)

        return True, ''

    @classmethod
    def get_tag_type_id(cls, type_str):
        if type_str in TagTypeNameToId:
            return True, TagTypeNameToId[type_str]

        ok, result = cls.locate(fq_name=[type_str], create_it=False,
                                fields=['tag_type_id'])
        if not ok:
            return False, result
        tag_type = result

        return True, int(tag_type['tag_type_id'], 0)
