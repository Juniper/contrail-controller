#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import utils
from vnc_api.gen.resource_common import PhysicalRouter

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class PhysicalRouterServer(ResourceMixin, PhysicalRouter):

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        # encrypt password before updating to DB
        if obj_dict.get('physical_router_user_credentials') and \
                obj_dict.get('physical_router_user_credentials', {}).get(
                    'password'):
            dict_password = obj_dict.get('physical_router_user_credentials',
                                         {}).get('password')
            # check if pwd is already encrypted
            if not dict_password.endswith(utils.PWD_ENCRYPTED):
                password = cls.encrypt_password(dict_password)
                obj_dict['physical_router_user_credentials']['password'] =\
                    password

        return True, ""

    @classmethod
    def encrypt_password(cls, dict_password):
        if cls.server._args.auth == 'keystone':
            admin_password = cls.server._auth_svc.args.admin_password
        else:
            admin_password = cls.server._auth_svc._conf_info.get(
                'admin_password')
        return utils.encrypt_password(admin_password, dict_password)

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if obj_dict.get('physical_router_managed_state'):
            state = obj_dict.get('physical_router_managed_state')
            if state == 'rma' or state == 'activating':
                msg = "Managed state cannot be %s for router %s" % (
                    state, obj_dict.get('fq_name'))
                return False, (400, msg)

        # encrypt password before writing to DB
        if obj_dict.get('physical_router_user_credentials') and \
                obj_dict.get('physical_router_user_credentials', {}).get(
                    'password'):
            dict_password = obj_dict.get('physical_router_user_credentials',
                                         {}).get('password')
            if not dict_password.endswith(utils.PWD_ENCRYPTED):
                password = cls.encrypt_password(dict_password)
                obj_dict['physical_router_user_credentials']['password'] =\
                    password

        return True, ''
