#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import PhysicalRouter

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class PhysicalRouterServer(ResourceMixin, PhysicalRouter):
    @classmethod
    def post_dbe_read(cls, obj_dict, db_conn):
        if obj_dict.get('physical_router_user_credentials', {}).get(
                'password'):
            obj_dict['physical_router_user_credentials']['password'] =\
                "**Password Hidden**"
        return True, ''

    @classmethod
    def post_dbe_list(cls, obj_result_list, db_conn):
        for obj_result in obj_result_list:
            if obj_result.get('physical-router'):
                ok, err_msg = cls.post_dbe_read(obj_result['physical-router'],
                                                db_conn)
                if not ok:
                    return ok, err_msg
        return True, ''
