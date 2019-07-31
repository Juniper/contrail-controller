#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import base64

from Crypto.Cipher import AES
from vnc_api.gen.resource_common import Fabric

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class FabricServer(ResourceMixin, Fabric):
    @classmethod
    def post_dbe_read(cls, obj_dict, db_conn):
        if obj_dict.get('fabric_credentials', {}).get(
                'device_credential'):

            if cls.server._args.auth == 'keystone':
                admin_password = cls.server._auth_svc.args.admin_password
            else:
                admin_password = cls.server._auth_svc._conf_info.get(
                    'admin_password')

            key = admin_password.rjust(16)
            cipher = AES.new(key, AES.MODE_ECB)

            creds = obj_dict.get('fabric_credentials', {}).get(
                'device_credential')

            for idx in range(len(creds)):
                padded_text = (creds[idx].get('credential', {}).get(
                    'password')).rjust(32)
                password = base64.b64encode(cipher.encrypt(padded_text))

                obj_dict.get('fabric_credentials', {}).get(
                    'device_credential')[idx]['credential']['password'] = \
                    password

        return True, ''

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
        # Disallow transition from sp-style to enterprise-style
        ok, read_result = cls.dbe_read(db_conn, 'fabric', id)
        if not ok:
            return ok, read_result
        cur_enterprise_style = read_result.get('fabric_enterprise_style')
        new_enterprise_style = obj_dict.get('fabric_enterprise_style')
        if cur_enterprise_style is False and new_enterprise_style is True:
            return (False,
                    (403, "Cannot change from sp-style to enterprise-style"))
        return True, ''
