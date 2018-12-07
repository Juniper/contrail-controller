#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import DOMAIN_SHARING_PERMS
from vnc_api.gen.resource_common import Domain

from vnc_cfg_api_server.resources import ResourceMixin


class DomainServer(ResourceMixin, Domain):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # enable domain level sharing for domain template
        share_item = {
            'tenant': 'domain:%s' % obj_dict.get('uuid'),
            'tenant_access': DOMAIN_SHARING_PERMS
        }
        obj_dict['perms2']['share'].append(share_item)
        return True, ""
