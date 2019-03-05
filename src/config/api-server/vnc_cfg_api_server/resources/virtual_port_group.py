#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
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
        # If the user deletes LAG before associated VMIs, make sure that
        # all the referring VMIs gets deleted before.
        for vmi_ref in obj_dict.get('virtual_machine_interface_refs') or []:
            ok, vmi_dict = cls.dbe_read(
                db_conn,
                'virtual_machine_interface',
                vmi_ref['uuid'],
                obj_fields=['instance_ip_back_refs'])

            if not ok:
                return ok, vmi_dict

            try:
                for iip_ref in vmi_dict.get('instance_ip_back_refs') or []:
                    cls.server.internal_request_delete(
                        'instance-ip',
                        iip_ref['uuid'])
                cls.server.internal_request_delete('virtual-machine-interface',
                                                   vmi_ref['uuid'])
            except HttpError or NoIdError:
                raise

        return True, '', None
