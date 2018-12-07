#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import SubCluster

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class SubClusterServer(ResourceMixin, SubCluster):
    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                       prop_collection_updates=None, ref_update=None):
        if 'sub_cluster_asn' in obj_dict:
            return False, (400, 'Sub cluster ASN can not be modified')
        return True, ''
