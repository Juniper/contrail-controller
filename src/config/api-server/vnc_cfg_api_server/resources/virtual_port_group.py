#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import VirtualPortGroup

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class VirtualPortGroupServer(ResourceMixin, VirtualPortGroup):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if ('vpg-internal' in obj_dict['fq_name'][2] and
           obj_dict.get('virtual_port_group_user_created', True)):
            msg = "Virtual port group(%s) with name vpg-internal as prefix "\
                  "can only be created internally"\
                  % (obj_dict['uuid'])
            return False, (400, msg)
        return True, ''

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        # If the user deletes VPG, make sure that all the referring
        # VMIs are deleted.
        if obj_dict.get('virtual_machine_interface_refs'):
            msg = "Virtual port group(%s) can not be deleted as refernces "\
                  "to VMI and BMS instance association still exists."\
                  % (obj_dict['uuid'])
            return (False, (400, msg), None)

        return True, '', None
