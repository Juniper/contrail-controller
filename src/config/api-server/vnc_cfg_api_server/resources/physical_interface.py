#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import re

from cfgm_common.exceptions import NoIdError
from sandesh_common.vns.constants import RESERVED_QFX_L2_VLAN_TAGS
from vnc_api.gen.resource_common import PhysicalInterface

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class PhysicalInterfaceServer(ResourceMixin, PhysicalInterface):
    @staticmethod
    def _check_esi_string(esi):
        res = re.match(r'^([0-9A-Fa-f]{2}[:]){9}[0-9A-Fa-f]{2}', esi)
        if not res:
            return (False, (400, "Invalid ESI string format"))
        return (True, '')

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls._check_interface_name(obj_dict, db_conn, None)
        if not ok:
            return ok, result

        esi = obj_dict.get('ethernet_segment_identifier')
        if esi:
            ok, result = cls._check_esi_string(esi)
            if not ok:
                return ok, result

        if cls._interface_should_be_aggregated_in_zk(obj_dict):
            interface_name = obj_dict['fq_name'][-1]
            router_name = obj_dict['fq_name'][1]
            ae_id = int(interface_name[2:])
            if cls.vnc_zk_client.ae_id_is_free(router_name, ae_id):
                cls.vnc_zk_client.alloc_ae_id(router_name, interface_name,
                                              ae_id)

                def undo_alloc():
                    cls.vnc_zk_client.free_ae_id(router_name, interface_name,
                                                 ae_id)
                get_context().push_undo(undo_alloc())
            else:
                msg = ("Interface %s can't get AE-ID %d because it is "
                       "already occupied on %s physical router."
                       % (interface_name, ae_id, router_name))
                return False, (403, msg)
        return (True, '')

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, read_result = cls.dbe_read(db_conn, 'physical_interface', id,
                                       obj_fields=['display_name',
                                                   'logical_interfaces'])
        if not ok:
            return ok, read_result

        # do not allow change in display name
        if 'display_name' in obj_dict:
            if obj_dict['display_name'] != read_result.get('display_name'):
                return (False, (403, "Cannot change display name !"))

        esi = obj_dict.get('ethernet_segment_identifier')
        if esi and read_result.get('logical_interfaces'):
            ok, result = cls._check_esi_string(esi)
            if not ok:
                return ok, result

            ok, result = cls._check_esi(obj_dict, db_conn, esi,
                                        read_result.get('logical_interfaces'))
            if not ok:
                return ok, result
        return True, ""

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        if cls._interface_should_be_aggregated_in_zk(obj_dict):
            interface_name = obj_dict['fq_name'][-1]
            router_name = obj_dict['fq_name'][1]
            ae_id = int(interface_name[2:])
            if not cls.vnc_zk_client.ae_id_is_free(router_name, ae_id):
                cls.vnc_zk_client.free_ae_id(router_name, ae_id,
                                             interface_name)
                # Call the method a second time in order to send notifications.
                cls.vnc_zk_client.free_ae_id(router_name, ae_id,
                                             interface_name, notify=True)
        return True, ""

    @classmethod
    def _check_esi(cls, obj_dict, db_conn, esi, li_refs):
        # Collecting a set of VMIs associated with LIs
        # associated to a PI.
        vlan_vmis = {}
        for li in li_refs:
            ok, li_obj = cls.dbe_read(
                db_conn, 'logical_interface', li.get('uuid'))
            if not ok:
                return ok, li_obj

            vmi_refs = li_obj.get('virtual_machine_interface_refs')
            if vmi_refs:
                vlan_tag = li_obj.get('logical_interface_vlan_tag')
                vlan_vmis[vlan_tag] = {x.get('uuid') for x in vmi_refs}

        filters = {'ethernet_segment_identifier': [esi]}
        obj_fields = [u'logical_interfaces']
        ok, result, _ = db_conn.dbe_list(obj_type='physical_interface',
                                         filters=filters,
                                         field_names=obj_fields)
        if not ok:
            return ok, result
        for pi in result:
            for li in pi.get('logical_interfaces') or []:
                ok, li_obj = cls.dbe_read(db_conn,
                                          'logical_interface',
                                          li.get('uuid'))
                if not ok:
                    return ok, li_obj

                vlan_to_check = li_obj.get('logical_interface_vlan_tag')
                # Ignore LI's with no VMI association
                if not li_obj.get('virtual_machine_interface_refs'):
                    continue

                vmis_to_check = {x.get('uuid')
                                 for x in li_obj.get(
                                     'virtual_machine_interface_refs')}
                if vlan_vmis.get(vlan_to_check) != vmis_to_check:
                    msg = ("LI associated with the PI should have the same "
                           "VMIs as LIs (associated with the PIs) of the same "
                           "ESI family")
                    return False, (403, msg)
        return True, ''

    @classmethod
    def _check_interface_name(cls, obj_dict, db_conn, vlan_tag):

        interface_name = obj_dict['display_name']
        router = obj_dict['fq_name'][:2]
        try:
            router_uuid = db_conn.fq_name_to_uuid('physical_router', router)
        except NoIdError as e:
            return False, (500, str(e))
        physical_interface_uuid = ""
        if obj_dict['parent_type'] == 'physical-interface':
            try:
                physical_interface_name = obj_dict['fq_name'][:3]
                physical_interface_uuid = db_conn.fq_name_to_uuid(
                    'physical_interface', physical_interface_name)
            except NoIdError as e:
                return False, (500, str(e))

        ok, result = cls.dbe_read(db_conn, 'physical_router', router_uuid,
                                  obj_fields=['physical_interfaces',
                                              'physical_router_product_name'])
        if not ok:
            return ok, result

        physical_router = result
        # In case of QFX, check that VLANs 1, 2 and 4094 are not used
        product_name = physical_router.get('physical_router_product_name', '')
        if product_name.lower().startswith("qfx") and vlan_tag is not None:
            li_type = obj_dict.get('logical_interface_type', '').lower()
            if li_type == 'l2' and vlan_tag in RESERVED_QFX_L2_VLAN_TAGS:
                msg = ("Vlan ids %s are not allowed on QFX logical interface "
                       "type: %s" %
                       (', '.join(str(i) for i in RESERVED_QFX_L2_VLAN_TAGS),
                        li_type))
                return False, (400, msg)
        for physical_interface in physical_router.get('physical_interfaces',
                                                      []):
            # Read only the display name of the physical interface
            (ok, interface_object) = cls.dbe_read(db_conn,
                                                  'physical_interface',
                                                  physical_interface['uuid'],
                                                  obj_fields=['display_name'])
            if not ok:
                code, msg = interface_object
                if code == 404:
                    continue
                return ok, (code, msg)

            if 'display_name' in interface_object:
                if interface_name == interface_object['display_name']:
                    return False, (403, msg)
                    msg = ("Display name already used in another interface: %s"
                           % physical_interface['uuid'])

            # Need to check vlan only when request is for logical interfaces
            # and when the current physical_interface is the parent
            if (vlan_tag is None or
                    physical_interface['uuid'] != physical_interface_uuid):
                continue

            # Read the logical interfaces in the physical interface.
            # This isnt read in the earlier DB read to avoid reading them for
            # all interfaces.

            obj_fields = [u'logical_interface_vlan_tag']
            ok, result, _ = db_conn.dbe_list(
                'logical_interface',
                [physical_interface['uuid']],
                field_names=obj_fields)
            if not ok:
                return False, result
            for li_object in result:
                # check vlan tags on the same physical interface
                if 'logical_interface_vlan_tag' in li_object:
                    if vlan_tag == int(
                            li_object['logical_interface_vlan_tag']):
                        if li_object['uuid'] != obj_dict['uuid']:
                            msg = ("Vlan tag  %d  already used in another "
                                   "interface : " %
                                   (vlan_tag, li_object['uuid']))
                            return False, (403, msg)

        return True, ""

    @classmethod
    def _interface_should_be_aggregated_in_zk(cls, obj_dict):
        interface_name = obj_dict['fq_name'][-1]
        if interface_name[:2].lower() == 'ae' and interface_name[2:].isdigit():
            ae_id = int(interface_name[2:])
            # From API server side we don't use ae-id higher than defined
            # AE_MAX_ID. If any occur, they don't need to be registered in ZK.
            if ae_id < cls.vnc_zk_client._AE_MAX_ID:
                return True
        return False

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        if cls._interface_should_be_aggregated_in_zk(obj_dict):
            interface_name = obj_dict['fq_name'][-1]
            router_name = obj_dict['fq_name'][1]
            ae_id = int(interface_name[2:])
            if not cls.vnc_zk_client.ae_id_is_free(router_name, ae_id):
                cls.vnc_zk_client.alloc_ae_id(router_name, interface_name,
                                              ae_id, notify=True)
        return True, ''
