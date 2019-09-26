#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import PhysicalRouter

from vnc_cfg_api_server import utils
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class PhysicalRouterServer(ResourceMixin, PhysicalRouter):

    @classmethod
    def _encrypt_password(cls, obj_dict):
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
        cls._encrypt_password(obj_dict)

        ok, result = cls.validate_telemetry_back_refs(obj_dict)
        if not ok:
            return ok, result

        return True, ''
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        # encrypt password before writing to DB
        cls._encrypt_password(obj_dict)

        ok, result = cls.validate_telemetry_back_refs(obj_dict)
        if not ok:
            return ok, result

        return True, ''
    # end pre_dbe_update
