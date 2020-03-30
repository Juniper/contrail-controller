#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import Fabric

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class FabricServer(ResourceMixin, Fabric):
    @classmethod
    def post_dbe_list(cls, obj_result_list, db_conn):
        for obj_result in obj_result_list:
            if obj_result.get('fabric'):
                ok, err_msg = cls.post_dbe_read(obj_result['fabric'],
                                                db_conn)
                if not ok:
                    return ok, err_msg
        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        # do not allow existing fabric to change from sp style to
        # enterprise style or vice-versa
        ok, read_result = cls.dbe_read(db_conn, 'fabric', id)
        if not ok:
            return ok, read_result
        cur_enterprise_style = read_result.get('fabric_enterprise_style')
        new_enterprise_style = obj_dict.get('fabric_enterprise_style')
        if cur_enterprise_style != new_enterprise_style:
            return (False,
                    (403, "Cannot change from sp-style to enterprise-style or vice-versa"))
        return True, ''
