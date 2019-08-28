#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import base64

from Crypto.Cipher import AES
from vnc_api.gen.resource_common import Fabric

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class FabricServer(ResourceMixin, Fabric):
    PWD_ENCRYPTED = "-encrypted"

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if obj_dict.get('fabric_credentials', {}).get(
                'device_credential'):
            creds = obj_dict.get('fabric_credentials', {}).get(
                'device_credential')
            for idx in range(len(creds)):
                dict_password = creds[idx].get('credential', {}).get(
                    'password')
                if cls.PWD_ENCRYPTED not in dict_password:
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

        # AES is a block cipher that only works with block of 16 chars.
        # We need to pad both key and text so that their length is equal
        # to next higher multiple of 16
        # Used https://stackoverflow.com/a/33299416

        key_padding_len = (len(admin_password) + 16 - 1) // 16
        if key_padding_len == 0:
            key_padding_len = 1
        key = admin_password.rjust(16 * key_padding_len)
        cipher = AES.new(key, AES.MODE_ECB)

        text_padding_len = (len(dict_password) + 16 - 1) // 16
        if text_padding_len == 0:
            text_padding_len = 1
        padded_text = dict_password.rjust(16 * text_padding_len)

        password = base64.b64encode(cipher.encrypt(padded_text))
        password = password + cls.PWD_ENCRYPTED
        return password

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
                if cls.PWD_ENCRYPTED not in dict_password:
                    password = cls.encrypt_password(dict_password)
                    obj_dict.get('fabric_credentials', {}).get(
                        'device_credential')[idx]['credential']['password'] = \
                        password

        return True, ''
