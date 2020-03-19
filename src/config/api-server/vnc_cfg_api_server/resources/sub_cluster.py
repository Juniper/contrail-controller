#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.exceptions import ResourceExistsError
from cfgm_common.exceptions import ResourceOutOfRangeError
from vnc_api.gen.resource_common import SubCluster

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class SubClusterServer(ResourceMixin, SubCluster):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        try:
            obj_dict['sub_cluster_id'] = \
                cls.vnc_zk_client.alloc_sub_cluster_id(
                    cls.server.global_autonomous_system,
                    ':'.join(obj_dict['fq_name']),
                    obj_dict.get('sub_cluster_id'))
        except ResourceExistsError:
            msg = ("Sub-cluster ID '%d' is already used, choose another one" %
                   obj_dict.get('sub_cluster_id'))
            return False, (400, msg)
        except ResourceOutOfRangeError:
            msg = ("Requested ID %d is out of the range. Two bytes if global "
                   "ASN uses four bytes, four bytes if global ASN uses two "
                   "bytes. IDs start from 1" % obj_dict.get('sub_cluster_id'))
            return False, (400, msg)

        def undo_allocate_sub_cluster_id():
            cls.vnc_zk_client.free_sub_cluster_id(
                obj_dict.get('sub_cluster_id'), ':'.join(obj_dict['fq_name']))
        get_context().push_undo(undo_allocate_sub_cluster_id)

        return True, ''

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        if obj_dict.get('sub_cluster_id'):
            cls.vnc_zk_client.alloc_sub_cluster_id(
                cls.server.global_autonomous_system,
                ':'.join(obj_dict['fq_name']),
                obj_dict.get('sub_cluster_id'))
        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                       prop_collection_updates=None, ref_update=None):
        if 'sub_cluster_asn' in obj_dict:
            return False, (400, 'Sub cluster ASN can not be modified')

        if not obj_dict.get('sub_cluster_id'):
            return True, ''

        deallocated_id = None
        ok, result = cls.locate(
            uuid=id, create_it=False, fields=['sub_cluster_id'])
        if not ok:
            return False, result
        actual_id = result['sub_cluster_id']
        new_id = obj_dict.get('sub_cluster_id')

        if new_id != actual_id:
            # try to allocate desired ID
            try:
                cls.vnc_zk_client.alloc_sub_cluster_id(
                    cls.server.global_autonomous_system,
                    ':'.join(fq_name),
                    new_id)
            except ResourceExistsError:
                msg = ("Sub-cluster ID '%d' is already used, choose another "
                       "one" % new_id)
                return False, (400, msg)

            def undo_allocate_sub_cluster_id():
                cls.vnc_zk_client.free_sub_cluster_id(
                    new_id, ':'.join(fq_name))
            get_context().push_undo(undo_allocate_sub_cluster_id)

            # if available, deallocate already allocate ID
            cls.vnc_zk_client.free_sub_cluster_id(actual_id, ':'.join(fq_name))

            def undo_deallocate_sub_cluster_id():
                # In case of error try to re-allocate the same ID as it was
                # not yet freed on other node
                try:
                    cls.vnc_zk_client.alloc_sub_cluster_id(
                        cls.server.global_autonomous_system,
                        ':'.join(fq_name),
                        actual_id)
                except ResourceExistsError:
                    undo_new_id = cls.vnc_zk_client.alloc_sub_cluster_id(
                        cls.server.global_autonomous_system, ':'.join(fq_name))
                    cls.server.internal_request_update(
                        cls.resource_type, id, {'sub_cluster_id': undo_new_id})
                return True, ""
            get_context().push_undo(undo_deallocate_sub_cluster_id)
            deallocated_id = actual_id

        return True, {
            'fq_name': fq_name,
            'sub_cluster_id': new_id,
            'deallocated_sub_cluster_id': deallocated_id,
        }

    @classmethod
    def dbe_update_notification(cls, obj_id, extra_dict=None):
        if extra_dict:
            fq_name = extra_dict['fq_name']
            if extra_dict.get('sub_cluster_id'):
                try:
                    cls.vnc_zk_client.alloc_sub_cluster_id(
                        cls.server.global_autonomous_system,
                        fq_name,
                        extra_dict.get('sub_cluster_id'))
                except ResourceExistsError:
                    pass
            if extra_dict and extra_dict.get('deallocated_sub_cluster_id'):
                cls.vnc_zk_client.free_sub_cluster_id(
                    extra_dict.get('deallocated_sub_cluster_id'),
                    fq_name,
                    notify=True)
        return True, ''

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        cls.vnc_zk_client.free_sub_cluster_id(
            obj_dict.get('sub_cluster_id'),
            ':'.join(obj_dict['fq_name']))
        return True, ""

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        cls.vnc_zk_client.free_sub_cluster_id(
            obj_dict.get('sub_cluster_id'),
            ':'.join(obj_dict['fq_name']),
            notify=True)
        return True, ''

    @classmethod
    def validate_decrease_id_to_two_bytes(cls):
        last_id = cls.vnc_zk_client.get_last_sub_cluster_allocated_id()
        if last_id and last_id >= 1 << 16:
            msg = ("Cannot reduce Sub-Cluster ID to 2 bytes, some IDs are "
                   "bigger than 2 bytes")
            return False, (400, msg)
        return True, ''
