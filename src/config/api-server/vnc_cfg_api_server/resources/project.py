#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from cfgm_common.utils import _DEFAULT_ZK_COUNTER_PATH_PREFIX
from sandesh_common.vns.constants import\
    POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT
from vnc_api.gen.resource_common import ApplicationPolicySet
from vnc_api.gen.resource_common import PolicyManagement
from vnc_api.gen.resource_common import Project

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin
from vnc_cfg_api_server.resources._security_base import SecurityResourceBase
from vnc_cfg_api_server.vnc_quota import QuotaHelper


class ProjectServer(ResourceMixin, Project):
    @classmethod
    def _ensure_default_application_policy_set(cls, project_uuid,
                                               project_fq_name):
        default_name = 'default-%s' % ApplicationPolicySet.resource_type
        attrs = {
            'parent_type': cls.object_type,
            'parent_uuid': project_uuid,
            'name': default_name,
            'display_name': default_name,
            'all_applications': True,
        }
        ok, result = cls.server.get_resource_class(
            ApplicationPolicySet.object_type).locate(
                project_fq_name + [default_name], **attrs)
        if not ok:
            return False, result
        default_aps = result

        return cls.server.internal_request_ref_update(
            cls.resource_type,
            project_uuid,
            'ADD',
            ApplicationPolicySet.resource_type,
            default_aps['uuid'],
        )

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls._ensure_default_application_policy_set(
            obj_dict['uuid'], obj_dict['fq_name'])
        if not ok:
            return False, result

        SecurityResourceBase.server = cls.server
        return SecurityResourceBase.set_policy_management_for_security_draft(
            cls.resource_type, obj_dict)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        if ('vxlan_routing' not in obj_dict and
                'enable_security_policy_draft' not in obj_dict):
            return True, ''

        fields = ['vxlan_routing', 'logical_routers',
                  'enable_security_policy_draft']
        ok, result = cls.dbe_read(db_conn, cls.object_type, id,
                                  obj_fields=fields)
        if not ok:
            return ok, result
        db_obj_dict = result

        if 'enable_security_policy_draft' in obj_dict:
            obj_dict['fq_name'] = db_obj_dict['fq_name']
            obj_dict['uuid'] = db_obj_dict['uuid']
            SecurityResourceBase.server = cls.server
            ok, result = SecurityResourceBase.\
                set_policy_management_for_security_draft(
                    cls.resource_type,
                    obj_dict,
                    draft_mode_enabled=db_obj_dict.get(
                        'enable_security_policy_draft', False),
                )
            if not ok:
                return False, result

        return True, ""

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                        prop_collection_updates=None, ref_update=None):
        if fq_name == Project().fq_name:
            cls.server.default_project = None
            cls.server.default_project
        return True, ''

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        draft_pm_uuid = None
        draft_pm_name = POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT
        draft_pm_fq_name = obj_dict['fq_name'] + [draft_pm_name]
        try:
            draft_pm_uuid = db_conn.fq_name_to_uuid(
                PolicyManagement.object_type, draft_pm_fq_name)
        except NoIdError:
            pass
        if draft_pm_uuid is not None:
            try:
                # If pending security modifications, it fails to delete the
                # draft PM
                cls.server.internal_request_delete(
                    PolicyManagement.resource_type, draft_pm_uuid)
            except HttpError as e:
                if e.status_code != 404:
                    return False, (e.status_code, e.content), None

        default_aps_uuid = None
        defaut_aps_fq_name = obj_dict['fq_name'] +\
            ['default-%s' % ApplicationPolicySet.resource_type]
        try:
            default_aps_uuid = db_conn.fq_name_to_uuid(
                ApplicationPolicySet.object_type, defaut_aps_fq_name)
        except NoIdError:
            pass
        if default_aps_uuid is not None:
            try:
                cls.server.internal_request_ref_update(
                    cls.resource_type,
                    id,
                    'DELETE',
                    ApplicationPolicySet.resource_type,
                    default_aps_uuid,
                )
                cls.server.internal_request_delete(
                    ApplicationPolicySet.resource_type, default_aps_uuid)
            except HttpError as e:
                if e.status_code != 404:
                    return False, (e.status_code, e.content), None

            def undo():
                return cls._ensure_default_application_policy_set(
                    default_aps_uuid, obj_dict['fq_name'])
            get_context().push_undo(undo)

        return True, '', None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn, **kwargs):
        # Delete the zookeeper counter nodes
        path = _DEFAULT_ZK_COUNTER_PATH_PREFIX + id
        if db_conn._zk_db.quota_counter_exists(path):
            db_conn._zk_db.delete_quota_counter(path)
        return True, ""

    @classmethod
    def pre_dbe_read(cls, id, fq_name, db_conn):
        return cls._ensure_default_application_policy_set(id, fq_name)

    @classmethod
    def dbe_update_notification(cls, obj_id, extra_dict=None):
        quota_counter = cls.server.quota_counter
        db_conn = cls.server._db_conn
        ok, result = QuotaHelper.get_project_dict_for_quota(obj_id, db_conn)
        if not ok:
            return False, result
        proj_dict = result

        quota_limits = QuotaHelper.get_quota_limits(proj_dict)
        for obj_type, quota_limit in quota_limits.items():
            path_prefix = _DEFAULT_ZK_COUNTER_PATH_PREFIX + obj_id
            path = path_prefix + "/" + obj_type
            if (quota_counter.get(path) and (quota_limit == -1 or
                                             quota_limit is None)):
                # free the counter from cache for resources updated
                # with unlimted quota
                del quota_counter[path]

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        quota_counter = cls.server.quota_counter
        for obj_type in obj_dict.get('quota', {}).keys():
            path_prefix = _DEFAULT_ZK_COUNTER_PATH_PREFIX + obj_id
            path = path_prefix + "/" + obj_type
            if quota_counter.get(path):
                # free the counter from cache
                del quota_counter[path]

        return True, ''
