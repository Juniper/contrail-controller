#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import BgpAsAService

from vnc_cfg_api_server.resources._resource_base import ResourceMixin
from vnc_cfg_api_server.resources.global_system_config import\
    GlobalSystemConfigServer

class BgpAsAServiceServer(ResourceMixin, BgpAsAService):
    @classmethod
    def _validate_control_node_zone_dep(cls, obj_dict, db_dict={}):
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
                if (attr_type in cnz_db and cnz_db[attr_type] != ref['uuid']):
                    msg = ("BGPaaS has configured with more than one %s "
                           "control-node-zones" % attr_type)
                    return False, (400, msg)
                cnz_db[attr_type] = ref['uuid']
        return True, ""

    @classmethod
    def _check_asn(cls, obj_dict):
        local_asn = obj_dict.get('autonomous_system')
        if not local_asn:
            return True, ''

        # Read 4 byte asn flag from DB
        ok, result = GlobalSystemConfigServer.locate(
            fq_name=['default-global-system-config'],
            create_it=False,
            fields=['enable_4byte_as'])
        if not ok:
            return False, result

        enable_4byte_as_flag = result['enable_4byte_as']

        if enable_4byte_as_flag:
            # Now that 4byte AS is enabled, check if the local ASN
            # is between 1-0xffFFffFF
            if local_asn < 1 or local_asn > 0xFFFFFFFF:
                return (False,
                        (400, 'ASN out of range, should be between '
                              '1-0xFFFFFFFF'))
        else:
            # Only 2 Byte AS allowed. The range should be
            # between 1-0xffFF
            if local_asn < 1 or local_asn > 0xFFFF:
                return (False,
                        (400, 'ASN out of range, should be between 1-0xFFFF'))
        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls._check_asn(obj_dict)
        if not ok:
            return ok, result

        if 'control_node_zone_refs' in obj_dict:
            (ok, msg) = cls._validate_control_node_zone_dep(obj_dict)
            if not ok:
                return ok, msg

        if (not obj_dict.get('bgpaas_shared') is True or
                obj_dict.get('bgpaas_ip_address') is not None):
            return True, ''
        return (False, (400, 'BGPaaS IP Address needs to be ' +
                             'configured if BGPaaS is shared'))

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls._check_asn(obj_dict)
        if not ok:
            return ok, result

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
                return (False, (400, 'BGPaaS sharing cannot be modified'))

        return True, ""
