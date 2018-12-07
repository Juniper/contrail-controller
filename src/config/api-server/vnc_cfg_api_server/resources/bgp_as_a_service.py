#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import BgpAsAService

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class BgpAsAServiceServer(ResourceMixin, BgpAsAService):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if (not obj_dict.get('bgpaas_shared') is True or
                obj_dict.get('bgpaas_ip_address') is not None):
            return True, ''
        return (False, (400, 'BGPaaS IP Address needs to be ' +
                             'configured if BGPaaS is shared'))

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        if 'bgpaas_shared' in obj_dict:
            ok, result = cls.dbe_read(db_conn, 'bgp_as_a_service', id)

            if not ok:
                return ok, result
            if result.get('bgpaas_shared', False) != obj_dict['bgpaas_shared']:
                return (False, (400, 'BGPaaS sharing cannot be modified'))
        return True, ""
