#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import NetworkPolicy

from vnc_cfg_api_server.resources._policy_base import check_policy_rules
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class NetworkPolicyServer(ResourceMixin, NetworkPolicy):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return check_policy_rules(obj_dict.get('network_policy_entries'), True)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls.dbe_read(db_conn, 'network_policy', id)
        if not ok:
            return ok, result

        return check_policy_rules(obj_dict.get('network_policy_entries'), True)
