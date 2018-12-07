#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import json

from cfgm_common import _obj_serializer_all
from cfgm_common.exceptions import NoIdError
from vnc_api.gen.resource_common import ServiceInstance
from vnc_api.gen.resource_xsd import KeyValuePair
from vnc_api.gen.resource_xsd import KeyValuePairs

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class ServiceInstanceServer(ResourceMixin, ServiceInstance):
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
        pnf_svc_inst = cls._check_svc_inst_belongs_to_pnf(obj_dict)
        if pnf_svc_inst:
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

        return True, ""
    # end validate_svc_inst_annotations

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls.validate_svc_inst_annotations(obj_dict)
        if not ok:
            return ok, result

        return True, ""
    # end pre_dbe_create

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # Allocate unit number for service IRB interfaces, each for left and
        # right VRF on the service chaining device
        pnf_svc_inst = cls._check_svc_inst_belongs_to_pnf(obj_dict)
        if pnf_svc_inst:
            left_svc_fq_name = ':'.join(obj_dict['fq_name']) + 'left_svc'
            right_svc_fq_name = ':'.join(obj_dict['fq_name']) + 'right_svc'

            left_svc_unit = cls.vnc_zk_client.alloc_vn_id(left_svc_fq_name)

            def undo_left_svc_unit():
                cls.vnc_zk_client.free_vn_id(left_svc_unit,
                                             left_svc_fq_name)
                return True, ""
            get_context().push_undo(undo_left_svc_unit)

            right_svc_unit = cls.vnc_zk_client.alloc_vn_id(right_svc_fq_name)

            def undo_right_svc_unit():
                cls.vnc_zk_client.free_vn_id(right_svc_unit,
                                             right_svc_fq_name)
                return True, ""
            get_context().push_undo(undo_right_svc_unit)

            if left_svc_unit and right_svc_unit:
                # Store these unit-id's as key value pairs in
                # service_instance_bindings
                api_server = cls.server
                svc_inst_obj = ServiceInstance(obj_dict)
                svc_inst_obj.set_service_instance_bindings(
                    KeyValuePairs([KeyValuePair('left-svc-unit',
                                                str(left_svc_unit)),
                                   KeyValuePair('right-svc-unit',
                                                str(right_svc_unit))]))
                svc_inst_dict = json.dumps(
                    svc_inst_obj, default=_obj_serializer_all)
                api_server.internal_request_update('service-instance',
                                                   obj_dict['uuid'],
                                                   json.loads(svc_inst_dict))
        return True, ''
    # end post_dbe_create

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        # Delete all the port tuples associated with this service instance
        pnf_svc_inst = cls._check_svc_inst_belongs_to_pnf(obj_dict)
        if pnf_svc_inst:
            api_server = cls.server
            for pt in obj_dict.get('port_tuples') or []:
                try:
                    pt_uuid = db_conn.fq_name_to_uuid(
                        'port_tuple', pt.get('to'))
                except NoIdError:
                    pass
                if pt_uuid is not None:
                    ok, pt_result = cls.dbe_read(
                        db_conn, 'port_tuple',
                        pt_uuid,
                        obj_fields=[
                            'virtual_machine_interface_back_refs'])
                    if not ok:
                        return ok, pt_result, None
                    if pt_result.get(
                            'virtual_machine_interface_back_refs') is None:
                        api_server.internal_request_delete(
                            'port_tuple', pt_uuid)

        return True, '', None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        # Deallocate left and right allocated service IRB unit ID
        pnf_svc_inst = cls._check_svc_inst_belongs_to_pnf(obj_dict)
        if pnf_svc_inst:
            if obj_dict.get('service_instance_bindings') is not None:
                kvps = obj_dict.get('service_instance_bindings').get(
                    'key_value_pair')
                if kvps is not None:
                    for d in kvps:
                        if d.get('key') == 'right-svc-unit':
                            right_unit = d.get('value')
                            cls.vnc_zk_client.free_vn_id(
                                int(right_unit), ':'.join(
                                    obj_dict['fq_name']) + 'right_svc')
                        elif d.get('key') == 'left-svc-unit':
                            left_unit = d.get('value')
                            cls.vnc_zk_client.free_vn_id(
                                int(left_unit), ':'.join(
                                    obj_dict['fq_name']) + 'left_svc')

        return True, ''
    # end post_dbe_delete
# end class ServiceInstanceServer
