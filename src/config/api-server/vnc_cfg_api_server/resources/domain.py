#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import DOMAIN_SHARING_PERMS
from vnc_api.gen.resource_common import Domain

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class DomainServer(ResourceMixin, Domain):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # enable domain level sharing for domain template
        share_item = {
            'tenant': 'domain:%s' % obj_dict.get('uuid'),
            'tenant_access': DOMAIN_SHARING_PERMS
        }
        obj_dict['perms2'].setdefault('share', []).append(share_item)
        return True, ""

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                        prop_collection_updates=None, ref_update=None):
        if fq_name == Domain().fq_name:
            cls.server.default_domain = None
            cls.server.default_domain
        return True, ''
