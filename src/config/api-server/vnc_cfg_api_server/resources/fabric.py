#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import utils
from vnc_api.gen.resource_common import Fabric

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class FabricServer(ResourceMixin, Fabric):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if obj_dict.get('fabric_credentials', {}).get(
                'device_credential'):
            creds = obj_dict.get('fabric_credentials', {}).get(
                'device_credential')
            for idx in range(len(creds)):
                dict_password = creds[idx].get('credential', {}).get(
                    'password')
                if not dict_password.endswith(utils.PWD_ENCRYPTED):
                    password = cls.encrypt_password(dict_password)
                    obj_dict.get('fabric_credentials', {}).get(
                        'device_credential')[idx]['credential']['password'] = \
                        password

        return True, ''

    @classmethod
    def encrypt_password(cls, dict_password):
        if cls.server._args.auth == 'keystone':
            admin_password = cls.server._auth_svc.args.admin_password
        else:
            admin_password = cls.server._auth_svc._conf_info.get(
                'admin_password')
        return utils.encrypt_password(admin_password, dict_password)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        # Disallow transition from sp-style to enterprise-style
        ok, read_result = cls.dbe_read(db_conn, 'fabric', id)
        if not ok:
            return ok, read_result
        cur_enterprise_style = read_result.get('fabric_enterprise_style')
        new_enterprise_style = obj_dict.get('fabric_enterprise_style')
        if cur_enterprise_style is False and new_enterprise_style is True:
            return (False,
                    (403, "Cannot change from sp-style to enterprise-style"))

        if obj_dict.get('fabric_credentials', {}).get(
                'device_credential'):
            creds = obj_dict.get('fabric_credentials', {}).get(
                'device_credential')
            for idx in range(len(creds)):
                dict_password = creds[idx].get('credential', {}).get(
                    'password')
                if not dict_password.endswith(utils.PWD_ENCRYPTED):
                    password = cls.encrypt_password(dict_password)
                    obj_dict.get('fabric_credentials', {}).get(
                        'device_credential')[idx]['credential']['password'] = \
                        password

        return True, ''
