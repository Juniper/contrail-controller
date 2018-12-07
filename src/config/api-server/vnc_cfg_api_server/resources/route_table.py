#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import RouteTable

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class RouteTableServer(ResourceMixin, RouteTable):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._check_prefixes(obj_dict)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls._check_prefixes(obj_dict)

    @staticmethod
    def _check_prefixes(obj_dict):
        routes = obj_dict.get('routes') or {}
        in_routes = routes.get("route") or []
        in_prefixes = [r.get('prefix') for r in in_routes]
        in_prefixes_set = set(in_prefixes)
        if len(in_prefixes) != len(in_prefixes_set):
            msg = "Duplicate prefixes not allowed: %s" % obj_dict.get('uuid')
            return False, (400, msg)

        return True, ''
