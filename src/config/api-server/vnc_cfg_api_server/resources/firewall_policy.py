#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import FirewallPolicy
from vnc_api.gen.resource_common import FirewallRule

from vnc_cfg_api_server.resources._security_base import SecurityResourceBase


class FirewallPolicyServer(SecurityResourceBase, FirewallPolicy):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        return cls.check_associated_firewall_resource_in_same_scope(
            obj_dict['uuid'],
            obj_dict['fq_name'],
            obj_dict,
            FirewallRule,
        )

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls.check_draft_mode_state(obj_dict)
        if not ok:
            return False, result

        return cls.check_associated_firewall_resource_in_same_scope(
            id, fq_name, obj_dict, FirewallRule)
