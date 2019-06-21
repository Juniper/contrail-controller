#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import PERMS_RX
from vnc_api.gen.resource_common import ServiceTemplate

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class ServiceTemplateServer(ResourceMixin, ServiceTemplate):
    generate_default_instance = False

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # enable domain level sharing for service template
        domain_uuid = obj_dict.get('parent_uuid')
        if domain_uuid is None:
            domain_uuid = db_conn.fq_name_to_uuid('domain',
                                                  obj_dict['fq_name'][0:1])
        share_item = {
            'tenant': 'domain:%s' % domain_uuid,
            'tenant_access': PERMS_RX
        }
        obj_dict['perms2'].setdefault('share', []).append(share_item)
        return True, ''
