#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from builtins import str

from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_api.gen.resource_common import VirtualPortGroup

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class VirtualPortGroupServer(ResourceMixin, VirtualPortGroup):

    @classmethod
    def update_physical_intf_type(cls, obj_dict=None,
                                  old_obj_dict=None):
        api_server = cls.server
        db_conn = cls.db_conn

        if not obj_dict:
            obj_dict = {}
        if not old_obj_dict:
            old_obj_dict = {}

        new_uuid_list = []
        old_uuid_list = []

        for phys_intf_ref in obj_dict.get('physical_interface_refs') or []:
            if phys_intf_ref.get('uuid') is None:
                try:
                    pi_uuid = db_conn.fq_name_to_uuid(
                        'physical_interface', phys_intf_ref.get('to'))
                except NoIdError as e:
                    return False, (400, str(e))
            else:
                pi_uuid = phys_intf_ref['uuid']

            new_uuid_list.append(pi_uuid)

        for phys_intf_ref in old_obj_dict.get(
                'physical_interface_refs') or []:
            if phys_intf_ref.get('uuid') is None:
                try:
                    pi_uuid = db_conn.fq_name_to_uuid(
                        'physical_interface', phys_intf_ref.get('to'))
                except NoIdError as e:
                    return False, (400, str(e))
            else:
                pi_uuid = phys_intf_ref['uuid']

            old_uuid_list.append(pi_uuid)

        to_be_added_pi_uuids = list(set(new_uuid_list) - set(old_uuid_list))
        to_be_deleted_pi_uuids = list(set(old_uuid_list) - set(new_uuid_list))

        for pi_uuid in to_be_added_pi_uuids or []:
            try:
                api_server.internal_request_update(
                    'physical_interface',
                    pi_uuid,
                    {'physical_interface_type': 'access'},
                )
            except HttpError as e:
                db_conn.config_log("PI (%s) add update failed (%s)" %
                                   (pi_uuid, str(e)),
                                   level=SandeshLevel.SYS_WARN)

        for pi_uuid in to_be_deleted_pi_uuids or []:
            try:
                api_server.internal_request_update(
                    'physical_interface',
                    pi_uuid,
                    {'physical_interface_type': None},
                )
            except HttpError as e:
                db_conn.config_log("PI (%s) delete update failed (%s)" %
                                   (pi_uuid, str(e)),
                                   level=SandeshLevel.SYS_WARN)

        return True, ''
    # end update_physical_intf_type

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if ('vpg-internal' in obj_dict['fq_name'][2] and
                obj_dict.get('virtual_port_group_user_created', True)):
            msg = "Virtual port group(%s) with name vpg-internal as prefix "\
                  "can only be created internally"\
                  % (obj_dict['uuid'])
            return False, (400, msg)

        if obj_dict.get('virtual_port_group_trunk_port_id'):
            primary_vmi_id = obj_dict.get('virtual_port_group_trunk_port_id')
            ok, result = db_conn.dbe_read(
                obj_type='virtual_machine_interface',
                obj_id=primary_vmi_id,
                obj_fields=['virtual_port_group_back_refs'])

            if not ok:
                return (ok, 400, result)

            if result.get('virtual_port_group_back_refs'):
                msg = ("Trunk Port(%s) already belongs to following VPG(s): "
                       % (primary_vmi_id))
                for back_ref in result.get('virtual_port_group_back_refs'):
                    msg += ("\tUUID(%s), FQ_NAME(%s)" %
                            (back_ref['uuid'], ':'.join(back_ref['to'])))
                return (False, (409, msg))

        ok, result = cls.update_physical_intf_type(obj_dict)
        if not ok:
            return ok, result

        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        # Handling both deletion and addition of interfaces here

        if obj_dict.get('physical_interface_refs'):
            # compute the already existing physical interface refs for the
            # vpg object
            ok, result = db_conn.dbe_read(
                obj_type='virtual_port_group',
                obj_id=obj_dict['uuid'],
                obj_fields=['physical_interface_refs'])
            if not ok:
                return ok, (400, result)

            ok, res = cls.update_physical_intf_type(obj_dict, result)
            if not ok:
                return ok, res

        return True, ''
    # end pre_dbe_update

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        # If the user deletes VPG, make sure that all the referring
        # VMIs are deleted.
        if obj_dict.get('virtual_machine_interface_refs'):
            msg = "Virtual port group(%s) can not be deleted as refernces "\
                  "to VMI and BMS instance association still exists."\
                  % (obj_dict['uuid'])
            return (False, (400, msg), None)

        ok, result = cls.update_physical_intf_type(old_obj_dict=obj_dict)
        if not ok:
            return (False, result, None)

        return True, '', None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        if obj_dict.get('virtual_port_group_user_created') is False:
            fq_name = obj_dict['fq_name']
            vpg_id = int(fq_name[2].split('-')[2])
            vpg_id_fqname = cls.vnc_zk_client.get_vpg_from_id(vpg_id)
            cls.vnc_zk_client.free_vpg_id(vpg_id, vpg_id_fqname)

        return True, ''

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        if obj_dict.get('virtual_port_group_user_created') is False:
            fq_name = obj_dict['fq_name']
            vpg_id = int(fq_name[2].split('-')[2])
            vpg_id_fqname = cls.vnc_zk_client.get_vpg_from_id(vpg_id)
            cls.vnc_zk_client.alloc_vpg_id(vpg_id_fqname, vpg_id)

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        fq_name = obj_dict['fq_name']
        if obj_dict.get('virtual_port_group_user_created') is False:
            vpg_id = int(fq_name[2].split('-')[2])
            vpg_id_fqname = cls.vnc_zk_client.get_vpg_from_id(vpg_id)
            cls.vnc_zk_client.free_vpg_id(vpg_id, vpg_id_fqname, notify=True)

        return True, ''
