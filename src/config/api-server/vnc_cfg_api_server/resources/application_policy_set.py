#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import ApplicationPolicySet
from vnc_api.gen.resource_common import FirewallPolicy

from vnc_cfg_api_server.context import is_internal_request
from vnc_cfg_api_server.resources._security_base import SecurityResourceBase


class ApplicationPolicySetServer(SecurityResourceBase, ApplicationPolicySet):
    @staticmethod
    def _check_all_applications_flag(obj_dict):
        # all_applications flag is read-only for user
        if not is_internal_request() and 'all_applications' in obj_dict:
            msg = "Application policy set 'all-applications' flag is read-only"
            return (False, (400, msg))
        return True, ""

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        ok, result = cls._check_all_applications_flag(obj_dict)
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

        return cls.check_associated_firewall_resource_in_same_scope(
            id, fq_name, obj_dict, FirewallPolicy)

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        ok, result = cls.dbe_read(db_conn, 'application_policy_set', id)
        if not ok:
            return ok, result, None
        aps = result

        if aps.get('all_applications', False) and not is_internal_request():
            msg = ("Application Policy Set defined on all applications cannot "
                   "be deleted")
            return (False, (400, msg), None)

        return True, '', None
