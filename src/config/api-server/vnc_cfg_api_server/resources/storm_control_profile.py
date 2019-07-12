#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import StormControlProfile

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class StormControlProfileServer(ResourceMixin, StormControlProfile):
    @staticmethod
    def validate_storm_control_params(obj_dict):
        params = obj_dict.get('storm_control_parameters')
        if params:
            bandwidth_percent = params.get('bandwidth_percent')
            if (bandwidth_percent <= 0 or bandwidth_percent > 100):
                return (False, (400, "Invalid bandwidth percentage %d"
                                % bandwidth_percent))

            recovery_timeout = params.get('recovery_timeout')
            if recovery_timeout and (recovery_timeout < 10 or
                    recovery_timeout > 3600):
                return (False, (400, "Invalid recovery timeout %d"
                                % recovery_timeout))

        return True, ''
    # end validate_storm_control_params

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls.validate_storm_control_params(obj_dict)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.validate_storm_control_params(obj_dict)
    # end pre_dbe_update
