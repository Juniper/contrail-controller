#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import AddressGroup

from vnc_cfg_api_server.resources._security_base import SecurityResourceBase


class AddressGroupServer(SecurityResourceBase, AddressGroup):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls.check_draft_mode_state(obj_dict)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.check_draft_mode_state(obj_dict)
