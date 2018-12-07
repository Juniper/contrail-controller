#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from vnc_api.gen.resource_common import LogicalInterface

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class LogicalInterfaceServer(ResourceMixin, LogicalInterface):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        (ok, result) = cls._check_vlan(obj_dict, db_conn)
        if not ok:
            return (ok, result)

        vlan = 0
        if 'logical_interface_vlan_tag' in obj_dict:
            vlan = obj_dict['logical_interface_vlan_tag']

        ok, result = cls.server.get_resource_class(
            'physical_interface')._check_interface_name(
                obj_dict, db_conn, vlan)
        if not ok:
            return ok, result

        ok, result = cls._check_esi(obj_dict, db_conn, vlan,
                                    obj_dict.get('parent_type'),
                                    obj_dict.get('parent_uuid'))
        if not ok:
            return ok, result

        return (True, '')

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        (ok, result) = cls._check_vlan(obj_dict, db_conn)
        if not ok:
            return (ok, result)

        ok, read_result = cls.dbe_read(db_conn, 'logical_interface', id)
        if not ok:
            return ok, read_result

        # do not allow change in display name
        if 'display_name' in obj_dict:
            if obj_dict['display_name'] != read_result.get('display_name'):
                return (False, (403, "Cannot change display name !"))

        vlan = None
        if 'logical_interface_vlan_tag' in obj_dict:
            vlan = obj_dict['logical_interface_vlan_tag']
            if 'logical_interface_vlan_tag' in read_result:
                if (int(vlan) !=
                        int(read_result.get('logical_interface_vlan_tag'))):
                    return (False, (403, "Cannot change Vlan id"))

        if vlan is None:
            vlan = read_result.get('logical_interface_vlan_tag')

        obj_dict['display_name'] = read_result.get('display_name')
        obj_dict['fq_name'] = read_result['fq_name']
        obj_dict['parent_type'] = read_result['parent_type']
        if 'logical_interface_type' not in obj_dict:
            existing_li_type = read_result.get('logical_interface_type')
            if existing_li_type:
                obj_dict['logical_interface_type'] = existing_li_type
        ok, result = cls.server.get_resource_class(
            'physical_interface')._check_interface_name(
                obj_dict, db_conn, vlan)
        if not ok:
            return ok, result

        ok, result = cls._check_esi(
            obj_dict,
            db_conn,
            read_result.get('logical_interface_vlan_tag'),
            read_result.get('parent_type'),
            read_result.get('parent_uuid'))
        if not ok:
            return ok, result

        return True, ""

    @classmethod
    def _check_esi(cls, obj_dict, db_conn, vlan, p_type, p_uuid):
        vmis_ref = obj_dict.get('virtual_machine_interface_refs')
        # Created Logical Interface does not point to a VMI.
        # Nothing to validate.
        if not vmis_ref:
            return (True, '')

        vmis = {x.get('uuid') for x in vmis_ref}
        if p_type == 'physical-interface':
            ok, result = cls.dbe_read(db_conn,
                                      'physical_interface', p_uuid)
            if not ok:
                return ok, result
            esi = result.get('ethernet_segment_identifier')
            if esi:
                filters = {'ethernet_segment_identifier': [esi]}
                obj_fields = [u'logical_interfaces']
                ok, result, _ = db_conn.dbe_list(obj_type='physical_interface',
                                                 filters=filters,
                                                 field_names=obj_fields)
                if not ok:
                    return ok, result

                for pi in result:
                    for li in pi.get('logical_interfaces') or []:
                        if li.get('uuid') == obj_dict.get('uuid'):
                            continue
                        ok, li_obj = cls.dbe_read(db_conn,
                                                  'logical_interface',
                                                  li.get('uuid'))
                        if not ok:
                            return ok, li_obj

                        # If the LI belongs to a different VLAN than the one
                        # created, then no-op.
                        li_vlan = li_obj.get('logical_interface_vlan_tag')
                        if vlan != li_vlan:
                            continue

                        peer_li_vmis = {
                            x.get('uuid')
                            for x in li_obj.get(
                                'virtual_machine_interface_refs', [])}
                        if peer_li_vmis != vmis:
                            msg = ("LI should refer to the same set of VMIs "
                                   "as peer LIs belonging to the same ESI")
                            return False, (403, msg)
        return True, ''

    @classmethod
    def _check_vlan(cls, obj_dict, db_conn):
        if 'logical_interface_vlan_tag' in obj_dict:
            vlan = obj_dict['logical_interface_vlan_tag']
            if vlan < 0 or vlan > 4094:
                return (False, (403, "Invalid Vlan id"))
        return True, ""
