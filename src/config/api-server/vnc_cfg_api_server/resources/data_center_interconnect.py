#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import json

from cfgm_common import _obj_serializer_all
from cfgm_common import get_dci_internal_vn_name
from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from vnc_api.gen.resource_common import DataCenterInterconnect
from vnc_api.gen.resource_common import VirtualNetwork
from vnc_api.gen.resource_xsd import IdPermsType
from vnc_api.gen.resource_xsd import RouteTargetList
from vnc_api.gen.resource_xsd import VirtualNetworkType

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class DataCenterInterconnectServer(ResourceMixin, DataCenterInterconnect):
    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls.create_dci_vn(obj_dict, db_conn)

    @classmethod
    def create_dci_vn(cls, obj_dict, db_conn):
        vn_int_name = get_dci_internal_vn_name(obj_dict.get('uuid'))
        vn_obj = VirtualNetwork(name=vn_int_name)
        id_perms = IdPermsType(enable=True, user_visible=False)
        vn_obj.set_id_perms(id_perms)

        int_vn_property = VirtualNetworkType(forwarding_mode='l3')
        if 'data_center_interconnect_vxlan_network_identifier' in obj_dict:
            vni_id = obj_dict[
                'data_center_interconnect_vxlan_network_identifier']
            int_vn_property.set_vxlan_network_identifier(vni_id)
        vn_obj.set_virtual_network_properties(int_vn_property)

        rt_list = obj_dict.get(
            'data_center_interconnect_configured_route_target_list',
            {}).get('route_target')
        if rt_list:
            vn_obj.set_route_target_list(RouteTargetList(rt_list))

        vn_int_dict = json.dumps(vn_obj, default=_obj_serializer_all)
        api_server = cls.server
        try:
            return api_server.internal_request_create('virtual-network',
                                                      json.loads(vn_int_dict))
        except HttpError as e:
            return False, (e.status_code, e.content)

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        vn_int_fqname = ["default-domain", "default-project"]
        vn_int_name = get_dci_internal_vn_name(obj_dict.get('uuid'))
        vn_int_fqname.append(vn_int_name)
        vn_int_uuid = db_conn.fq_name_to_uuid('virtual_network', vn_int_fqname)

        api_server = cls.server
        try:
            api_server.internal_request_ref_update(
                'data-center-interconnect',
                obj_dict['uuid'],
                'DELETE',
                'virtual-network',
                vn_int_uuid,
                vn_int_fqname)
            api_server.internal_request_delete('virtual-network', vn_int_uuid)
        except HttpError as e:
            if e.status_code != 404:
                return False, (e.status_code, e.content), None
        except NoIdError as e:
            pass

        def undo_dci_vn_delete():
            return cls.create_dci_vn_and_ref(obj_dict, db_conn)
        get_context().push_undo(undo_dci_vn_delete)

        return True, '', None

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # make sure referenced LRs belongs to different fabrics
        return cls._make_sure_lrs_belongs_to_different_fabrics(
            db_conn, obj_dict)

    @classmethod
    def _make_sure_lrs_belongs_to_different_fabrics(cls, db_conn, dci):
        lr_list = []
        for lr_ref in dci.get('logical_router_refs') or []:
            lr_uuid = lr_ref.get('uuid')
            if lr_uuid:
                lr_list.append(lr_uuid)

        if not lr_list:
            return True, ''

        for lr_uuid in lr_list:
            ok, read_result = cls.dbe_read(
                db_conn, 'logical_router', lr_uuid, obj_fields=[
                    'physical_router_refs',
                    'data_center_interconnect_back_refs'])

            if not ok:
                return False, read_result

            lr = read_result
            # check there are no more than one DCI back ref for this LR
            # it is acceptable that dci object can associate with a lr,
            # but lr not associated with any PRs yet
            # make sure LR update should check for this association
            if len(lr.get('data_center_interconnect_back_refs') or []) > 1:
                msg = ("Logical router can not associate with  more than one "
                       "DCI: %s" % lr_uuid)
                return False, (400, msg)
            if len(lr.get('data_center_interconnect_back_refs') or []) == 1:
                dci_ref = lr.get('data_center_interconnect_back_refs')[0]
                if dci.get('fq_name') != dci_ref.get('to'):
                    msg = ("Logical router can not associate with more than "
                           "one DCI: %s" % lr_uuid)
                    return False, (400, msg)
            init_fab = None
            for pr_ref in read_result.get('physical_router_refs') or []:
                pr_uuid = pr_ref.get('uuid')
                status, pr_result = cls.dbe_read(
                    db_conn, 'physical_router', pr_uuid, obj_fields=[
                        'fabric_refs'])
                if not status:
                    return False, pr_result

                if pr_result.get("fabric_refs"):
                    # pr can be associated to only one Fabric, if not, many
                    # other system components will fail fabric implementation
                    # must ensure this, no need to double check
                    fab_id = pr_result["fabric_refs"][0].get('uuid')
                    if init_fab and init_fab != fab_id:
                        msg = ("Logical router can not associate with PRs "
                               "belonging to different DCI connected Fabrics: "
                               "%s" % lr_uuid)
                        return False, (400, msg)
                    else:
                        init_fab = fab_id
            if not init_fab:
                msg = ("DCI Logical router is not associated to any fabric: %s"
                       % lr_uuid)
                return False, (400, msg)
        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, read_result = cls.dbe_read(db_conn, 'data_center_interconnect',
                                       id, obj_fields=['logical_router_refs'])
        if not ok:
            return ok, read_result

        # make sure referenced LRs belongs to different fabrics
        return cls._make_sure_lrs_belongs_to_different_fabrics(
            db_conn, read_result)
