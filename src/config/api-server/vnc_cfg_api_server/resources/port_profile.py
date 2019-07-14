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

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls.validate_storm_control_back_refs(obj_dict)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.validate_storm_control_back_refs(obj_dict)
    # end pre_dbe_update
