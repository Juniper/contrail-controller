#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import base64

from Crypto.Cipher import AES
from vnc_api.gen.resource_common import PhysicalRouter

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class PhysicalRouterServer(ResourceMixin, PhysicalRouter):
    @classmethod
    def post_dbe_read(cls, obj_dict, db_conn):
        if obj_dict.get('physical_router_user_credentials') and \
           obj_dict.get('physical_router_user_credentials', {}).get(
                'password'):

            if cls.server._args.auth == 'keystone':
                admin_password = cls.server._auth_svc.args.admin_password
            else:
                admin_password = cls.server._auth_svc._conf_info.get(
                    'admin_password')

            key = admin_password.rjust(16)
            cipher = AES.new(key, AES.MODE_ECB)

            padded_text = (obj_dict.get('physical_router_user_credentials',
                                        {}).get('password')).rjust(32)
            password = base64.b64encode(cipher.encrypt(padded_text))
            obj_dict['physical_router_user_credentials']['password'] = password

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

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, pr_obj = cls.dbe_read(
            db_conn, 'physical_router', id, obj_fields=[
                'physical_router_underlay_config', 'display_name'])
        if not ok:
            return ok, pr_obj

        # do not allow RMA managed state if underlay config is not backed up
        if obj_dict.get('physical_router_managed_state'):
            state = obj_dict.get('physical_router_managed_state')
            if state == 'rma':
                config = pr_obj.get('physical_router_underlay_config')
                if config is None:
                    msg = "Underlay config not backed up for router %s" % (
                        pr_obj.get('display_name'))
                    return False, (400, msg)

        return True, ''
