#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import TelemetryProfile

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class TelemetryProfileServer(ResourceMixin, TelemetryProfile):
    @staticmethod
    def validate_sflow_back_refs(obj_dict):
        sflow_profile_refs = obj_dict.get('sflow_profile_refs')
        if sflow_profile_refs and len(sflow_profile_refs) > 1:
            ref_list = [ref.get('to') for ref in sflow_profile_refs]
            return (False, (400, "Telemetry profile %s has more than one "
                            "sflow profile refs %s" % (
                                obj_dict.get('fq_name'),
                                ref_list)))

        return True, ''
    # end validate_sflow_back_refs

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls.validate_sflow_back_refs(obj_dict)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.validate_sflow_back_refs(obj_dict)
    # end pre_dbe_update
