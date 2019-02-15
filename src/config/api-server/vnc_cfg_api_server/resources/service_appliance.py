#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from vnc_api.gen.resource_common import ServiceAppliance

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class ServiceApplianceServer(ResourceMixin, ServiceAppliance):
    @staticmethod
    def _get_left_right_attachment_points(obj_dict):
        left_intf_list = []
        right_intf_list = []

        if obj_dict.get('service_appliance_properties') is not None:
            kvps = obj_dict.get('service_appliance_properties').get(
                'key_value_pair')
            if kvps is not None:
                for d in kvps:
                    if d.get('key') == 'left-attachment-point':
                        value = d.get('value')
                        left_intf_list = value.split(',')
                    elif d.get('key') == 'right-attachment-point':
                        value = d.get('value')
                        right_intf_list = value.split(',')

        return left_intf_list, right_intf_list

    @classmethod
    def check_phys_intf_belongs_to_pnf(cls, obj_dict):
        # Validate if the referenced physical interfaces belongs to physical
        # router with 'pnf' physical role
        db_conn = cls.db_conn
        for phys_intf_ref in obj_dict.get('physical_interface_refs') or []:
            if phys_intf_ref.get('uuid') is None:
                try:
                    pi_uuid = db_conn.fq_name_to_uuid(
                        'physical_interface', phys_intf_ref.get('to'))
                except NoIdError as e:
                    return False, (400, str(e))
            else:
                pi_uuid = phys_intf_ref['uuid']

            ok, phys_intf_result = cls.dbe_read(
                db_conn, 'physical_interface', pi_uuid, obj_fields=[
                    'parent_type', 'parent_uuid'])
            if not ok:
                return ok, phys_intf_result
            if phys_intf_result.get('parent_type') == 'physical-router':
                phys_router_uuid = phys_intf_result.get('parent_uuid')
                ok, phys_router_result = cls.dbe_read(
                    db_conn, 'physical_router', phys_router_uuid,
                    obj_fields=['physical_router_role'])
                if not ok:
                    return ok, phys_router_result
                if phys_router_result.get('physical_router_role') != 'pnf':
                    msg = ("Referenced physical interface(%s) does not belong "
                           "to PNF device" % (phys_intf_ref['uuid']))
                    return False, (400, msg)
        return True, ''

    @classmethod
    def check_sa_has_left_right_attachment_point(cls, obj_dict):
        # Validate if left and right attachment points are defined as
        # key-value pairs in service appliance properties

        if obj_dict.get('physical_interface_refs') is not None:
            left_intf_list, right_intf_list = \
                cls._get_left_right_attachment_points(obj_dict)
            if len(left_intf_list) < 1 or len(right_intf_list) < 1:
                msg = (
                    "There should be atleast one left/right attachment point"
                    " defined")
                return False, (400, msg)

        return True, ''

    @classmethod
    def add_delete_physical_interface_refs(cls, obj_dict, op):
        # Add/Delete physical interface refs depending on 'op' -
        # Ref from PNF device interface to service chaining device interface
        virtualization_type = obj_dict.get(
            'service_appliance_virtualization_type')
        if virtualization_type == 'physical-device':
            api_server = cls.server
            db_conn = cls.db_conn
            left_intf_list, right_intf_list =\
                cls._get_left_right_attachment_points(obj_dict)
            for phys_intf_ref in obj_dict.get('physical_interface_refs') or []:
                if phys_intf_ref.get('uuid') is None:
                    try:
                        pi_uuid = db_conn.fq_name_to_uuid(
                            'physical_interface', phys_intf_ref.get('to'))
                    except NoIdError as e:
                        return False, (400, str(e))
                else:
                    pi_uuid = phys_intf_ref['uuid']

                if phys_intf_ref['attr'].get('interface_type') == 'left':
                    for intf in left_intf_list:
                        try:
                            api_server.internal_request_ref_update(
                                'physical-interface', pi_uuid, op,
                                'physical-interface', None, intf.split(':'))
                        except HttpError as e:
                            return False, (e.status_code, e.content)
                elif phys_intf_ref['attr'].get('interface_type') == 'right':
                    for intf in right_intf_list:
                        try:
                            api_server.internal_request_ref_update(
                                'physical-interface', pi_uuid, op,
                                'physical-interface', None, intf.split(':'))
                        except HttpError as e:
                            return False, (e.status_code, e.content)

        return True, ''

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.check_phys_intf_belongs_to_pnf(obj_dict)
        if not ok:
            return ok, result

        ok, result = cls.check_sa_has_left_right_attachment_point(obj_dict)
        if not ok:
            return ok, result

        return True, ''
    # end pre_dbe_create

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.add_delete_physical_interface_refs(obj_dict, 'ADD')
        if not ok:
            return ok, result

        return True, ''
    # end post_dbe_create

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        ok, result = cls.add_delete_physical_interface_refs(obj_dict, 'DELETE')
        if not ok:
            return ok, result

        return True, ''
    # end post_dbe_delete
# end class ServiceApplianceServer
