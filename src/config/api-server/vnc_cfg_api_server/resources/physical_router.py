#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import base64

from Crypto.Cipher import AES
from vnc_api.gen.resource_common import PhysicalRouter

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class PhysicalRouterServer(ResourceMixin, PhysicalRouter):

    @classmethod
    def post_dbe_list(cls, obj_result_list, db_conn):
        for obj_result in obj_result_list:
            if obj_result.get('physical-router'):
                ok, err_msg = cls.post_dbe_read(obj_result['physical-router'],
                                                db_conn)
                if not ok:
                    return ok, err_msg
        return True, ''

    @staticmethod
    def validate_telemetry_back_refs(obj_dict):
        telemetry_profile_refs = obj_dict.get('telemetry_profile_refs')
        if telemetry_profile_refs and len(telemetry_profile_refs) > 1:
            ref_list = [ref.get('to') for ref in telemetry_profile_refs]
            return (False, (400, "Physical router %s has more than one "
                            "telemetry profile refs %s" % (
                                obj_dict.get('fq_name'),
                                ref_list)))

        return True, ''
    # end validate_telemetry_back_refs

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

            dict_password = obj_dict.get('physical_router_user_credentials',
                                         {}).get('password')
            text_padding_len = (len(dict_password) + 16 - 1) // 16
            if text_padding_len == 0:
                text_padding_len = 1
            padded_text = dict_password.rjust(16 * text_padding_len)

            password = base64.b64encode(cipher.encrypt(padded_text))
            obj_dict['physical_router_user_credentials']['password'] = password

        ok, result = cls.validate_telemetry_back_refs(obj_dict)
        if not ok:
            return ok, result

        return True, ''

    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.validate_telemetry_back_refs(obj_dict)
    # end pre_dbe_update
