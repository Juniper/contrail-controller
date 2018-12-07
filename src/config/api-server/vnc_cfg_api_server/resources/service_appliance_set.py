#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import ServiceApplianceSet

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class ServiceApplianceSetServer(ResourceMixin, ServiceApplianceSet):
    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result, _ = db_conn.dbe_list(
            'loadbalancer_pool', back_ref_uuids=[id])
        if not ok:
            return False, result
        if len(result) > 0:
            msg = ("Service appliance set can not be updated as loadbalancer "
                   "pools are using it")
            return False, (409, msg)
        return True, ''
