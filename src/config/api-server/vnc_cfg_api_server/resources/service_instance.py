#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from builtins import str

from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from vnc_api.gen.resource_common import ServiceInstance

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class ServiceInstanceServer(ResourceMixin, ServiceInstance):
    @staticmethod
    def _get_left_right_svc_unit(obj_dict):
        left_svc_unit = None
        right_svc_unit = None

        if obj_dict.get('service_instance_bindings') is not None:
            kvps = obj_dict.get('service_instance_bindings').get(
                'key_value_pair')
            if kvps is not None:
                kvp_dict = dict((kvp['key'], kvp['value']) for kvp in kvps)
                right_svc_unit = kvp_dict.get('right-svc-unit')
                left_svc_unit = kvp_dict.get('left-svc-unit')

        return left_svc_unit, right_svc_unit

    @staticmethod
    def _check_svc_inst_belongs_to_pnf(obj_dict):
        params = obj_dict.get('service_instance_properties')
        if params:
            virtualization_type = params.get('service_virtualization_type')
            if virtualization_type == 'physical-device':
                return True

        return False

    @classmethod
    def validate_svc_inst_annotations(cls, obj_dict):
        msg = ""
        if obj_dict.get('annotations') is not None:
            if obj_dict.get('annotations').get(
                    'key_value_pair') is not None:
                kvps = obj_dict.get('annotations').get('key_value_pair')
                kvp_dict = dict((kvp['key'], kvp['value']) for kvp in kvps)
                if kvp_dict.get('left-svc-vlan') is None:
                    msg = "Left svc vlan should be defined in svc "
                    "instance annotations"
                elif kvp_dict.get('right-svc-vlan') is None:
                    msg = "Right svc vlan should be defined in svc "
                    "instance annotations"
                elif kvp_dict.get('left-svc-asns') is None:
                    msg = "Left svc asn should be defined in svc "
                    "instance annotations"
                elif kvp_dict.get('right-svc-asns') is None:
                    msg = "Right svc asn should be defined in svc "
                    "instance annotations"
            else:
                msg = "svc attributes should be defined as key_value_pairs"
        else:
            msg = "svc attributes should be defined as key_value_pairs "
            "in annotations"
        if msg:
            return (False, (400, msg))

        return True, ''
    # end validate_svc_inst_annotations

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        pnf_svc_inst = cls._check_svc_inst_belongs_to_pnf(obj_dict)
        if pnf_svc_inst:
            ok, result = cls.validate_svc_inst_annotations(obj_dict)
            if not ok:
                return ok, result

            # Allocate unit number for service IRB interfaces for left and
            # right VRF on the service chaining device
            left_svc_fq_name = ':'.join(obj_dict['fq_name']) + 'left_svc'
            right_svc_fq_name = ':'.join(obj_dict['fq_name']) + 'right_svc'

            left_svc_unit = cls.vnc_zk_client.alloc_vn_id(left_svc_fq_name)

            def undo_left_svc_unit():
                cls.vnc_zk_client.free_vn_id(left_svc_unit,
                                             left_svc_fq_name)
                return True, ''
            get_context().push_undo(undo_left_svc_unit)

            right_svc_unit = cls.vnc_zk_client.alloc_vn_id(right_svc_fq_name)

            def undo_right_svc_unit():
                cls.vnc_zk_client.free_vn_id(right_svc_unit,
                                             right_svc_fq_name)
                return True, ''
            get_context().push_undo(undo_right_svc_unit)

            db_conn.config_log(
                "Allocated IRB units, left_unit: %s "
                "right_unit: %s" %
                (left_svc_unit, right_svc_unit), level=SandeshLevel.SYS_DEBUG)

            # Store these unit-id's as key value pairs in
            # service_instance_bindings
            obj_dict['service_instance_bindings'] = {}
            obj_dict['service_instance_bindings']['key_value_pair'] = []
            obj_dict['service_instance_bindings']['key_value_pair'].append(
                {'key': 'left-svc-unit', 'value': str(left_svc_unit)})
            obj_dict['service_instance_bindings']['key_value_pair'].append(
                {'key': 'right-svc-unit', 'value': str(right_svc_unit)})

        return True, ''
    # end pre_dbe_create

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        pnf_svc_inst = cls._check_svc_inst_belongs_to_pnf(obj_dict)
        if pnf_svc_inst:
            # Delete all the port tuples associated with this service instance
            api_server = cls.server
            for pt in obj_dict.get('port_tuples') or []:
                try:
                    pt_uuid = db_conn.fq_name_to_uuid(
                        'port_tuple', pt.get('to'))
                except NoIdError:
                    continue
                ok, pt_result = cls.dbe_read(
                    db_conn, 'port_tuple',
                    pt_uuid,
                    obj_fields=[
                        'virtual_machine_interface_back_refs'])
                if not ok:
                    return ok, pt_result, None
                if pt_result.get(
                        'virtual_machine_interface_back_refs') is None:
                    try:
                        api_server.internal_request_delete(
                            'port_tuple', pt_uuid)
                    except HttpError as e:
                        return False, (e.status_code, e.content), None

        return True, '', None
    # end pre_dbe_delete

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        # Deallocate left and right allocated service IRB unit ID
        pnf_svc_inst = cls._check_svc_inst_belongs_to_pnf(obj_dict)
        if pnf_svc_inst:
            left_svc_unit, right_svc_unit =\
                cls._get_left_right_svc_unit(obj_dict)
            if left_svc_unit:
                cls.vnc_zk_client.free_vn_id(
                    int(left_svc_unit), ':'.join(
                        obj_dict['fq_name']) + 'left_svc')
            if right_svc_unit:
                cls.vnc_zk_client.free_vn_id(
                    int(right_svc_unit), ':'.join(
                        obj_dict['fq_name']) + 'right_svc')
        return True, ''
    # end post_dbe_delete

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        left_svc_unit, right_svc_unit =\
            cls._get_left_right_svc_unit(obj_dict)
        if right_svc_unit:
            cls.vnc_zk_client.alloc_vn_id(
                ':'.join(obj_dict['fq_name']) + 'right_svc',
                int(right_svc_unit))
        if left_svc_unit:
            cls.vnc_zk_client.alloc_vn_id(
                ':'.join(obj_dict['fq_name']) + 'left_svc',
                int(left_svc_unit))

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        left_svc_unit, right_svc_unit =\
            cls._get_left_right_svc_unit(obj_dict)
        if right_svc_unit:
            cls.vnc_zk_client.free_vn_id(
                int(right_svc_unit),
                ':'.join(obj_dict['fq_name']) + 'right_svc',
                notify=True)
        if left_svc_unit:
            cls.vnc_zk_client.free_vn_id(
                int(left_svc_unit),
                ':'.join(obj_dict['fq_name']) + 'left_svc',
                notify=True)

        return True, ''
# end class ServiceInstanceServer
