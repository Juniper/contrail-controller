#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import BridgeDomain

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class BridgeDomainServer(ResourceMixin, BridgeDomain):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        vn_uuid = obj_dict.get('parent_uuid')
        if vn_uuid is None:
            vn_uuid = db_conn.fq_name_to_uuid(
                'virtual_network', obj_dict['fq_name'][0:3])
        ok, result = cls.dbe_read(db_conn, 'virtual_network', vn_uuid,
                                  obj_fields=['bridge_domains'])
        if not ok:
            return False, result
        if 'bridge_domains' in result and len(result['bridge_domains']) == 1:
            msg = ("Virtual network(%s) can have only one bridge domain. "
                   "Bridge domain(%s) is already created under this virtual "
                   "network" % (vn_uuid, result['bridge_domains'][0]['uuid']))
            return False, (400, msg)
        return True, ''
