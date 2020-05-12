#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import BgpAsAService

from vnc_cfg_api_server.resources._bgp_base import check_hold_time_in_range
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class BgpAsAServiceServer(ResourceMixin, BgpAsAService):
    @classmethod
    def _validate_control_node_zone_dep(cls, obj_dict, db_dict=None):
        if db_dict is None:
            db_dict = {}
        cnz_refs = []
        if 'control_node_zone_refs' in db_dict:
            cnz_refs.extend(db_dict['control_node_zone_refs'])
        if 'control_node_zone_refs' in obj_dict:
            cnz_refs.extend(obj_dict['control_node_zone_refs'])
        cnz_db = {}
        for ref in cnz_refs:
            if ('to' in ref and
                    ref['to'][0] == 'default-global-system-config'):
                attr_type = ref['attr']['bgpaas_control_node_zone_type']
                if attr_type in cnz_db and cnz_db[attr_type] != ref['uuid']:
                    msg = ("BGPaaS has configured with more than one %s "
                           "control-node-zones" % attr_type)
                    return False, (400, msg)
                cnz_db[attr_type] = ref['uuid']
        return True, ""

    @classmethod
    def _check_asn(cls, obj_dict):
        asn = obj_dict.get('autonomous_system')
        if asn:
            ok, result = cls.server.get_resource_class(
                'global_system_config').check_asn_range(asn)
            if not ok:
                return ok, result

        bgp_session_attr = obj_dict.get('bgpaas_session_attributes')
        if bgp_session_attr:
            local_asn = bgp_session_attr.get('local_autonomous_system')
            if local_asn:
                ok, result = cls.server.get_resource_class(
                    'global_system_config').check_asn_range(local_asn)
                if not ok:
                    return ok, result

        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        bgp_session_attr = obj_dict.get('bgpaas_session_attributes')
        if bgp_session_attr:
            hold_time = bgp_session_attr.get('hold_time')
            ok, result = check_hold_time_in_range(hold_time)
            if not ok:
                return ok, (400, result)

        ok, result = cls._check_asn(obj_dict)
        if not ok:
            return ok, (400, result)

        if 'control_node_zone_refs' in obj_dict:
            (ok, msg) = cls._validate_control_node_zone_dep(obj_dict)
            if not ok:
                return ok, msg

        if (not obj_dict.get('bgpaas_shared') is True or
                obj_dict.get('bgpaas_ip_address') is not None):
            return True, ''
        return (False, (400, 'BGPaaS IP Address needs to be '
                             'configured if BGPaaS is shared'))

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        bgp_session_attr = obj_dict.get('bgpaas_session_attributes')
        if bgp_session_attr:
            hold_time = bgp_session_attr.get('hold_time')
            ok, result = check_hold_time_in_range(hold_time)
            if not ok:
                return ok, (400, result)

        ok, result = cls._check_asn(obj_dict)
        if not ok:
            return ok, (400, result)

        result = None
        if 'control_node_zone_refs' in obj_dict:
            ok, result = cls.dbe_read(db_conn, 'bgp_as_a_service', id)
            if not ok:
                return ok, result
            (ok, msg) = cls._validate_control_node_zone_dep(obj_dict, result)
            if not ok:
                return ok, msg

        if 'bgpaas_shared' in obj_dict:
            if not result:
                ok, result = cls.dbe_read(db_conn, 'bgp_as_a_service', id)
                if not ok:
                    return ok, result
            if result.get('bgpaas_shared', False) != obj_dict['bgpaas_shared']:
                return False, (400, 'BGPaaS sharing cannot be modified')

        return True, ""
