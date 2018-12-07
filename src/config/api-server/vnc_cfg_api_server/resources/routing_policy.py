#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.exceptions import VncError
from vnc_api.gen.resource_common import RoutingPolicy

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class RoutingPolicyServer(ResourceMixin, RoutingPolicy):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        asn_list = None
        rp_entries = obj_dict.get('routing_policy_entries')
        if rp_entries:
            term = rp_entries.get('term')[0]
            if term:
                action_list = term.get('term_action_list')
                if action_list:
                    action = action_list.get('update')
                    if action:
                        as_path = action.get('as_path')
                        if as_path:
                            expand = as_path.get('expand')
                            if expand:
                                asn_list = expand.get('asn_list')

        try:
            global_asn = cls.server.global_autonomous_system
        except VncError as e:
            return False, (400, str(e))

        if asn_list and global_asn in asn_list:
            msg = ("ASN can't be same as global system config asn")
            return False, (400, msg)
        return True, ""
