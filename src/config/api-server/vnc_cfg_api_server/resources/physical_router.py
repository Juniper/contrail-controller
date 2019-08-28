#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import utils
from vnc_api.gen.resource_common import PhysicalRouter

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class PhysicalRouterServer(ResourceMixin, PhysicalRouter):

    @classmethod
    def _encrypt_password(cls, obj_dict, db_conn):
        api_server = db_conn.get_api_server()

        # encrypt password before updating to DB
        if obj_dict.get('physical_router_user_credentials') and \
                obj_dict.get('physical_router_user_credentials', {}).get(
                    'password'):
            dict_password = obj_dict.get('physical_router_user_credentials',
                                         {}).get('password')
            encryption_type = obj_dict.get('physical_router_encryption_type',
                                           'none')
            # if pwd is not encrypted, do it now
            if encryption_type == 'none':
                password = utils.encrypt_password(obj_dict['uuid'],
                                                  dict_password)
                obj_dict['physical_router_user_credentials']['password'] =\
                    password
                obj_dict['physical_router_encryption_type'] = 'local'
                api_server.internal_request_update(
                    'physical-router',
                    obj_dict['uuid'],
                    obj_dict)

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if obj_dict.get('physical_router_managed_state'):
            state = obj_dict.get('physical_router_managed_state')
            if state == 'rma' or state == 'activating':
                msg = "Managed state cannot be %s for router %s" % (
                    state, obj_dict.get('fq_name'))
                return False, (400, msg)

        cls._encrypt_password(obj_dict, db_conn)
        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        cls._encrypt_password(obj_dict, db_conn)
        return True, ""
