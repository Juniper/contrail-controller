#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.utils import _DEFAULT_ZK_COUNTER_PATH_PREFIX
from vnc_api.gen.resource_common import SecurityGroup

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._policy_base import check_policy_rules
from vnc_cfg_api_server.resources._resource_base import ResourceMixin
from vnc_cfg_api_server.vnc_quota import QUOTA_OVER_ERROR_CODE
from vnc_cfg_api_server.vnc_quota import QuotaHelper


class SecurityGroupServer(ResourceMixin, SecurityGroup):
    get_nested_key_as_list = classmethod(lambda cls, x, y, z: (
        x.get(y).get(z)
        if (type(x) is dict and x.get(y) and x.get(y).get(z)) else []))

    @classmethod
    def _set_configured_security_group_id(cls, obj_dict):
        fq_name_str = ':'.join(obj_dict['fq_name'])
        configured_sg_id = obj_dict.get('configured_security_group_id') or 0
        sg_id = obj_dict.get('security_group_id')
        if sg_id is not None:
            sg_id = int(sg_id)

        if configured_sg_id > 0:
            if sg_id is not None:
                cls.vnc_zk_client.free_sg_id(sg_id, fq_name_str)

                def undo_dealloacte_sg_id():
                    # In case of error try to re-allocate the same ID as it was
                    # not yet freed on other node
                    new_sg_id = cls.vnc_zk_client.alloc_sg_id(fq_name_str,
                                                              sg_id)
                    if new_sg_id != sg_id:
                        cls.vnc_zk_client.alloc_sg_id(fq_name_str)
                        cls.server.internal_request_update(
                            cls.resource_type,
                            obj_dict['uuid'],
                            {'security_group_id': new_sg_id},
                        )
                    return True, ""
                get_context().push_undo(undo_dealloacte_sg_id)
            obj_dict['security_group_id'] = configured_sg_id
        else:
            if (sg_id is not None and
                    fq_name_str == cls.vnc_zk_client.get_sg_from_id(sg_id)):
                obj_dict['security_group_id'] = sg_id
            else:
                sg_id_allocated = cls.vnc_zk_client.alloc_sg_id(fq_name_str)

                def undo_allocate_sg_id():
                    cls.vnc_zk_client.free_sg_id(sg_id_allocated, fq_name_str)
                    return True, ""
                get_context().push_undo(undo_allocate_sg_id)
                obj_dict['security_group_id'] = sg_id_allocated

        return True, ''

    @classmethod
    def check_security_group_rule_quota(
            cls, proj_dict, db_conn, rule_count):
        quota_counter = cls.server.quota_counter
        obj_type = 'security_group_rule'
        quota_limit = QuotaHelper.get_quota_limit(proj_dict, obj_type)

        if (rule_count and quota_limit >= 0):
            path_prefix = _DEFAULT_ZK_COUNTER_PATH_PREFIX + proj_dict['uuid']
            path = path_prefix + "/" + obj_type
            if not quota_counter.get(path):
                # Init quota counter for security group rule
                QuotaHelper._zk_quota_counter_init(
                    path_prefix,
                    {obj_type: quota_limit},
                    proj_dict['uuid'],
                    db_conn,
                    quota_counter)
            ok, result = QuotaHelper.verify_quota(
                obj_type, quota_limit, quota_counter[path],
                count=rule_count)
            if not ok:
                msg = "security_group_entries: %d" % quota_limit
                return False, (QUOTA_OVER_ERROR_CODE, msg)

            def undo():
                # Revert back quota count
                quota_counter[path] -= rule_count
            get_context().push_undo(undo)

        return True, ""

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):

        ok, response = check_policy_rules(
            obj_dict.get('security_group_entries'))
        if not ok:
            return ok, response

        # Does not authorize to set the security group ID as it's allocated
        # by the vnc server
        if obj_dict.get('security_group_id') is not None:
            return (False, (403, "Cannot set the security group ID"))

        if obj_dict['id_perms'].get('user_visible', True):
            ok, result = QuotaHelper.get_project_dict_for_quota(
                obj_dict['parent_uuid'], db_conn)
            if not ok:
                return False, result
            proj_dict = result

            rule_count = len(cls.get_nested_key_as_list(obj_dict,
                             'security_group_entries', 'policy_rule'))
            ok, result = cls.check_security_group_rule_quota(
                proj_dict, db_conn, rule_count)
            if not ok:
                return ok, result

        # Allocate security group ID if necessary
        return cls._set_configured_security_group_id(obj_dict)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        deallocated_security_group_id = None
        ok, result = cls.dbe_read(db_conn, 'security_group', id)
        if not ok:
            return ok, result
        sg_dict = result

        # Does not authorize to update the security group ID as it's allocated
        # by the vnc server
        new_sg_id = obj_dict.get('security_group_id')
        if (new_sg_id is not None and
                int(new_sg_id) != sg_dict['security_group_id']):
            return (False, (403, "Cannot update the security group ID"))

        # Update the configured security group ID
        if 'configured_security_group_id' in obj_dict:
            actual_sg_id = sg_dict['security_group_id']
            sg_dict['configured_security_group_id'] =\
                obj_dict['configured_security_group_id']
            ok, result = cls._set_configured_security_group_id(sg_dict)
            if not ok:
                return ok, result
            if actual_sg_id != sg_dict['security_group_id']:
                deallocated_security_group_id = actual_sg_id
            obj_dict['security_group_id'] = sg_dict['security_group_id']

        ok, result = check_policy_rules(
            obj_dict.get('security_group_entries'))
        if not ok:
            return ok, result

        if sg_dict['id_perms'].get('user_visible', True):
            ok, result = QuotaHelper.get_project_dict_for_quota(
                sg_dict['parent_uuid'], db_conn)
            if not ok:
                return False, result
            proj_dict = result
            new_rule_count = len(cls.get_nested_key_as_list(
                obj_dict, 'security_group_entries', 'policy_rule'))
            existing_rule_count = len(cls.get_nested_key_as_list(
                sg_dict, 'security_group_entries', 'policy_rule'))
            rule_count = (new_rule_count - existing_rule_count)
            ok, result = cls.check_security_group_rule_quota(
                proj_dict, db_conn, rule_count)
            if not ok:
                return ok, result

        return True, {
            'deallocated_security_group_id': deallocated_security_group_id,
        }

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        ok, result = cls.dbe_read(db_conn, 'security_group', id)
        if not ok:
            return ok, result, None
        sg_dict = result

        if sg_dict['id_perms'].get('user_visible', True) is not False:
            ok, result = QuotaHelper.get_project_dict_for_quota(
                sg_dict['parent_uuid'], db_conn)
            if not ok:
                return False, result, None
            proj_dict = result
            obj_type = 'security_group_rule'
            quota_limit = QuotaHelper.get_quota_limit(proj_dict, obj_type)

            if 'security_group_entries' in obj_dict and quota_limit >= 0:
                rule_count = len(
                    obj_dict['security_group_entries']['policy_rule'])
                path_prefix = (_DEFAULT_ZK_COUNTER_PATH_PREFIX +
                               proj_dict['uuid'])
                path = path_prefix + "/" + obj_type
                quota_counter = cls.server.quota_counter
                # If the SG has been created before R3, there is no
                # path in ZK. It is created on next update and we
                # can ignore it for now
                if quota_counter.get(path):
                    quota_counter[path] -= rule_count

                    def undo():
                        # Revert back quota count
                        quota_counter[path] += rule_count
                    get_context().push_undo(undo)

        return True, "", None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        # Deallocate the security group ID
        cls.vnc_zk_client.free_sg_id(
            obj_dict.get('security_group_id'), ':'.join(obj_dict['fq_name']))
        return True, ""

    @classmethod
    def _notify_sg_id_modified(cls, obj_dict,
                               deallocated_security_group_id=None):
        fq_name_str = ':'.join(obj_dict['fq_name'])
        sg_id = obj_dict.get('security_group_id')

        if deallocated_security_group_id is not None:
            cls.vnc_zk_client.free_sg_id(deallocated_security_group_id,
                                         fq_name_str, notify=True)
        if sg_id is not None:
            cls.vnc_zk_client.alloc_sg_id(fq_name_str, sg_id)

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        cls._notify_sg_id_modified(obj_dict)

        return True, ''

    @classmethod
    def dbe_update_notification(cls, obj_id, extra_dict=None):
        ok, result = cls.dbe_read(cls.db_conn, cls.object_type, obj_id,
                                  obj_fields=['fq_name', 'security_group_id'])
        if not ok:
            return False, result
        obj_dict = result

        if extra_dict is not None:
            cls._notify_sg_id_modified(
                obj_dict, extra_dict.get('deallocated_security_group_id'))
        else:
            cls._notify_sg_id_modified(obj_dict)

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        cls.vnc_zk_client.free_sg_id(
            obj_dict.get('security_group_id'),
            ':'.join(obj_dict['fq_name']),
            notify=True,
        )

        return True, ''
