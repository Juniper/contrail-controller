#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.exceptions import ResourceExistsError
from cfgm_common.exceptions import ResourceOutOfRangeError
from vnc_api.gen.resource_common import SubCluster

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
                   "bytes" % obj_dict.get('sub_cluster_id'))
            return False, (400, msg)
        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                       prop_collection_updates=None, ref_update=None):
        if 'sub_cluster_asn' in obj_dict:
            return False, (400, 'Sub cluster ASN can not be modified')
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
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        cls.vnc_zk_client.free_sub_cluster_id(
            cls.server.global_autonomous_system,
            obj_dict.get('sub_cluster_id'),
            ':'.join(obj_dict['fq_name']))
        return True, ""

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        cls.vnc_zk_client.free_sub_cluster_id(
            cls.server.global_autonomous_system,
            obj_dict.get('sub_cluster_id'),
            ':'.join(obj_dict['fq_name']),
            notify=True)
        return True, ''

    @classmethod
    def validate_decrease_id_to_two_bytes(cls):
        allocator = cls.vnc_zk_client.get_sub_cluster_allocator(
            cls.server.global_autonomous_system)
        last_id = allocator.get_last_allocated_id()
        if last_id and last_id >= 1 << 16:
            msg = ("Cannot reduce Sub-Cluster ID to 2 bytes, some IDs are "
                   "bigger than 2 bytes")
            return False, (400, msg)
        return True, ''
