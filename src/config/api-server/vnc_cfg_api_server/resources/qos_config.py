#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import QosConfig

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class QosConfigServer(ResourceMixin, QosConfig):
    @staticmethod
    def _check_qos_values(obj_dict, db_conn):
        fc_pair = 'qos_id_forwarding_class_pair'
        if 'dscp_entries' in obj_dict:
            for qos_id_pair in obj_dict['dscp_entries'].get(fc_pair, []):
                dscp = qos_id_pair.get('key')
                if dscp and dscp < 0 or dscp > 63:
                    return (False, (400, "Invalid DSCP value %d"
                                    % qos_id_pair.get('key')))

        if 'vlan_priority_entries' in obj_dict:
            for qos_id_pair in obj_dict['vlan_priority_entries'].get(fc_pair,
                                                                     []):
                vlan_priority = qos_id_pair.get('key')
                if vlan_priority and vlan_priority < 0 or vlan_priority > 7:
                    return (False, (400, "Invalid 802.1p value %d"
                                    % qos_id_pair.get('key')))

        if 'mpls_exp_entries' in obj_dict:
            for qos_id_pair in obj_dict['mpls_exp_entries'].get(fc_pair) or []:
                mpls_exp = qos_id_pair.get('key')
                if mpls_exp and mpls_exp < 0 or mpls_exp > 7:
                    return (False, (400, "Invalid MPLS EXP value %d"
                                    % qos_id_pair.get('key')))
        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        obj_dict['global_system_config_refs'] = [
            {'to': ['default-global-system-config']}]
        return cls._check_qos_values(obj_dict, db_conn)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls._check_qos_values(obj_dict, db_conn)
