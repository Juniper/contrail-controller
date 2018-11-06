#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.utils import _DEFAULT_ZK_COUNTER_PATH_PREFIX
from vnc_api.gen.resource_common import ApplicationPolicySet
from vnc_api.gen.resource_common import FirewallPolicy
from vnc_api.gen.resource_common import Project

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.context import is_internal_request
from vnc_cfg_api_server.resources._security_base import SecurityResourceBase
from vnc_cfg_api_server.vnc_quota import QuotaHelper


class ApplicationPolicySetServer(SecurityResourceBase, ApplicationPolicySet):
    @staticmethod
    def _check_all_applications_flag(obj_dict):
        # all_applications flag is read-only for user
        if not is_internal_request() and 'all_applications' in obj_dict:
            msg = "Application policy set 'all-applications' flag is read-only"
            return (False, (400, msg))
        return True, ""

    @classmethod
    def check_openstack_firewall_group_quota(cls, obj_dict, deleted=False):
        obj_type = 'firewall_group'
        if (not obj_dict['id_perms'].get('user_visible', True) or
                obj_dict.get('parent_type') != Project.object_type):
            return True, ''

        ok, result = QuotaHelper.get_project_dict_for_quota(
            obj_dict['parent_uuid'], cls.db_conn)
        if not ok:
            return False, result
        project = result
        quota_limit = QuotaHelper.get_quota_limit(project, obj_type)
        if quota_limit < 0:
            return True, ''

        quota_count = 1
        if deleted:
            quota_count = -1

        path_prefix = _DEFAULT_ZK_COUNTER_PATH_PREFIX + project['uuid']
        path = path_prefix + "/" + obj_type
        if not cls.server.quota_counter.get(path):
            QuotaHelper._zk_quota_counter_init(
                path_prefix,
                {obj_type: quota_limit},
                project['uuid'],
                cls.db_conn,
                cls.server.quota_counter)
        return QuotaHelper.verify_quota(
            obj_type, quota_limit, cls.server.quota_counter[path], quota_count)

        def undo():
            # revert back counter in case of any failure during creation
            if not deleted:
                cls.server.quota_counter[path] -= 1
            else:
                cls.server.quota_counter[path] += 1
        get_context().push_undo(undo)

        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        ok, result = cls._check_all_applications_flag(obj_dict)
        if not ok:
            return False, result

        ok, result = cls.check_openstack_firewall_group_quota(obj_dict)
        if not ok:
            return False, result

        return cls.check_associated_firewall_resource_in_same_scope(
            obj_dict['uuid'],
            obj_dict['fq_name'],
            obj_dict,
            FirewallPolicy,
        )

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        ok, result = cls._check_all_applications_flag(obj_dict)
        if not ok:
            return False, result

        ok, result = cls.check_associated_firewall_resource_in_same_scope(
            id, fq_name, obj_dict, FirewallPolicy)
        if not ok:
            return False, result

        if 'user_visible' in obj_dict.get('id_perms', {}):
            new_user_visible = obj_dict['id_perms'].get('user_visible', True)
            ok, result = cls.dbe_read(db_conn, 'application_policy_set', id)
            if not ok:
                return ok, result
            aps = result
            old_user_visible = aps['id_perms'].get('user_visible', True)
            if new_user_visible and not old_user_visible:
                return cls.check_openstack_firewall_group_quota(obj_dict)
            elif not new_user_visible and old_user_visible:
                return cls.check_openstack_firewall_group_quota(obj_dict, True)

        return True, ''

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        ok, result = cls.check_openstack_firewall_group_quota(obj_dict, True)
        if not ok:
            return False, result, None

        ok, result = cls.dbe_read(db_conn, 'application_policy_set', id)
        if not ok:
            return ok, result, None
        aps = result

        if aps.get('all_applications', False) and not is_internal_request():
            msg = ("Application Policy Set defined on all applications cannot "
                   "be deleted")
            return (False, (400, msg), None)

        return True, '', None
