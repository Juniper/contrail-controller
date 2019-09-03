#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import SflowProfile

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class SflowProfileServer(ResourceMixin, SflowProfile):
    @staticmethod
    def is_profile_editable(obj_dict):
        is_default_profile = obj_dict.get('sflow_profile_is_default')
        if is_default_profile:
            return (False, (400, "Default profile %s is non-editable " % (
                            obj_dict.get('fq_name'))))
        return True, ''
    # end is_profile_editable

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.is_profile_editable(obj_dict)
    # end pre_dbe_update
