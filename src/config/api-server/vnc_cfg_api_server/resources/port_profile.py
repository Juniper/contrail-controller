#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import PortProfile

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class PortProfileServer(ResourceMixin, PortProfile):
    @staticmethod
    def validate_storm_control_back_refs(obj_dict):
        storm_profile_refs = obj_dict.get('storm_control_profile_refs')
        if storm_profile_refs and len(storm_profile_refs) > 1:
            ref_list = [ref.get('to') for ref in storm_profile_refs]
            return (False, (400, "Port profile %s has more than one "
                            "storm profile refs %s" % (
                                obj_dict.get('fq_name'),
                                ref_list)))

        return True, ''
    # end validate_storm_control_back_refs

    @staticmethod
    def validate_port_profile_params(obj_dict):

        port_profile_params = obj_dict.get('port_profile_params') or {}
        port_params = port_profile_params.get('port_params') or {}
        port_mtu = port_params.get('port_mtu')

        if port_mtu and (port_mtu < 256 or port_mtu > 9216):
            return (False, (400, "Port mtu can be only within 256"
                                 " - 9216"))

        return True, ''
    # end validate_port_profile_params

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.validate_storm_control_back_refs(obj_dict)
        if not ok:
            return ok, result

        ok, result = cls.validate_port_profile_params(obj_dict)
        if not ok:
            return ok, result

        return True, ''
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls.validate_storm_control_back_refs(obj_dict)
        if not ok:
            return ok, result

        ok, result = cls.validate_port_profile_params(obj_dict)
        if not ok:
            return ok, result

        return True, ''
    # end pre_dbe_update
