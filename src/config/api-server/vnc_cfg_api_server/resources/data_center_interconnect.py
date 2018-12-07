#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import json

from cfgm_common import _obj_serializer_all
from cfgm_common import get_dci_internal_vn_name
from cfgm_common.exceptions import ResourceExistsError
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
        return cls.create_dci_vn_and_ref(obj_dict, db_conn)

    @classmethod
    def create_dci_vn_and_ref(cls, obj_dict, db_conn):
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
        status, obj = cls.server.internal_request_create(
            'virtual-network', json.loads(vn_int_dict))
        cls.server.internal_request_ref_update(
            'data-center-interconnect',
            obj_dict['uuid'],
            'ADD',
            'virtual-network', obj['virtual-network']['uuid'],
            obj['virtual-network']['fq_name'])
        return True, ''

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        ok, proj_dict = cls.get_parent_project(obj_dict, db_conn)
        vn_int_fqname = proj_dict.get('fq_name')
        vn_int_name = get_dci_internal_vn_name(obj_dict.get('uuid'))
        vn_int_fqname.append(vn_int_name)
        vn_int_uuid = db_conn.fq_name_to_uuid('virtual_network', vn_int_fqname)

        cls.server.internal_request_ref_update(
            'data-center-interconnect',
            obj_dict['uuid'],
            'DELETE',
            'virtual-network',
            vn_int_uuid,
            vn_int_fqname)
        cls.server.internal_request_delete('virtual-network', vn_int_uuid)

        def undo_dci_vn_delete():
            return cls.create_dci_vn_and_ref(obj_dict, db_conn)
        get_context().push_undo(undo_dci_vn_delete)

        return True, '', None

    @staticmethod
    def _check_vxlan_id_in_dci(obj_dict):
        if 'data_center_interconnect_vxlan_network_identifier' in obj_dict:
            vxlan_network_identifier = obj_dict[
                'data_center_interconnect_vxlan_network_identifier']
            if vxlan_network_identifier not in [None, '', 'None']:
                return vxlan_network_identifier
        obj_dict['data_center_interconnect_vxlan_network_identifier'] = None

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # make sure referenced LRs belongs to different fabrics
        if not cls._make_sure_lrs_belongs_to_different_fabrics(db_conn,
                                                               obj_dict):
            msg = ("Each Logical Router should belong to different Fabric "
                   "Physical Routers")
            return False, (400, msg)

        vxlan_id = cls._check_vxlan_id_in_dci(obj_dict)
        if vxlan_id:
            # If input vxlan_id is not None, that means we need to reserve it.
            # First, check if vxlan_id is set for other fq_name
            existing_fq_name = cls.vnc_zk_client.get_vn_from_id(int(vxlan_id))
            if existing_fq_name is not None:
                msg = ("Cannot set VXLAN_ID: %s, it has already been used" %
                       vxlan_id)
                return False, (400, msg)

            # Second, if vxlan_id is not None, set it in Zookeeper and set the
            # undo function for when any failures happen later.
            # But first, get the internal_vlan name using which the resource
            # in zookeeper space will be reserved.

            vn_int_name = get_dci_internal_vn_name(obj_dict.get('uuid'))
            vn_obj = VirtualNetwork(name=vn_int_name)
            try:
                vxlan_fq_name = ':'.join(vn_obj.fq_name) + '_vxlan'
                # Now that we have the internal VN name, allocate it in
                # zookeeper only if the resource hasn't been reserved already
                cls.vnc_zk_client.alloc_vxlan_id(vxlan_fq_name, int(vxlan_id))
            except ResourceExistsError:
                msg = ("Cannot allocate VXLAN_ID: %s, it has already been used"
                       % vxlan_id)
                return False, (400, msg)

            def undo_vxlan_id():
                cls.vnc_zk_client.free_vxlan_id(int(vxlan_id), vxlan_fq_name)
                return True, ""
            get_context().push_undo(undo_vxlan_id)
        return True, ''

    @classmethod
    def _make_sure_lrs_belongs_to_different_fabrics(cls, db_conn, dci):
        lr_list = []
        for lr_ref in dci['logical_router_refs']:
            lr_uuid = lr_ref.get('uuid')
            if lr_uuid:
                lr_list.append(lr_uuid)

        if not lr_list:
            return True
        fab_list = []
        for lr_uuid in lr_list:
            ok, read_result = cls.dbe_read(db_conn, 'logical_router', lr_uuid)
            if ok:
                for pr_ref in read_result['physical_router_refs']:
                    pr_uuid = pr_ref.get('uuid')
                    status, pr_result = cls.dbe_read(
                        db_conn, 'physical_router', pr_uuid)
                    if status and pr_result["fabric_refs"]:
                        fab_id = pr_result["fabric_refs"][0].get('uuid')
                        if fab_id in fab_list:
                            return False
                        else:
                            fab_list.append(fab_id)
                            break
        return True

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, read_result = cls.dbe_read(db_conn, 'data_center_interconnect', id)
        if not ok:
            return ok, read_result

        # make sure referenced LRs belongs to different fabrics
        if not cls._make_sure_lrs_belongs_to_different_fabrics(db_conn,
                                                               read_result):
            msg = ("Each Logical Router should belong to different Fabric "
                   "Physical Routers")
            return False, msg

        if ('data_center_interconnect_vxlan_network_identifier' in obj_dict):
            new_vxlan_id = None
            old_vxlan_id = None
            new_vxlan_id = cls._check_vxlan_id_in_lr(obj_dict)
            # To get the current vxlan_id, read the LR from the DB

            old_vxlan_id = cls._check_vxlan_id_in_dci(read_result)

            if(new_vxlan_id != old_vxlan_id):
                int_fq_name = None
                for vn_ref in read_result['virtual_network_refs']:
                    int_fq_name = vn_ref.get('to')
                    break
                if int_fq_name is None:
                    msg = "DCI Internal VN FQ name not found"
                    return False, (400, msg)
                vxlan_fq_name = ':'.join(int_fq_name) + '_vxlan'
                if new_vxlan_id is not None:
                    # First, check if the new_vxlan_id being updated exist for
                    # some other VN.
                    new_vxlan_fq_name_in_db = cls.vnc_zk_client.get_vn_from_id(
                        int(new_vxlan_id))
                    if new_vxlan_fq_name_in_db is not None:
                        if(new_vxlan_fq_name_in_db != vxlan_fq_name):
                            msg = ("Cannot set VXLAN_ID: %s, it has already "
                                   "been used" % new_vxlan_id)
                            return False, (400, msg)

                    # Second, set the new_vxlan_id in Zookeeper.
                    cls.vnc_zk_client.alloc_vxlan_id(vxlan_fq_name,
                                                     int(new_vxlan_id))

                    def undo_alloc():
                        cls.vnc_zk_client.free_vxlan_id(int(old_vxlan_id),
                                                        vxlan_fq_name)
                    get_context().push_undo(undo_alloc)

                # Third, check if old_vxlan_id is not None, if so, delete it
                # from Zookeeper
                if old_vxlan_id is not None:
                    cls.vnc_zk_client.free_vxlan_id(int(old_vxlan_id),
                                                    vxlan_fq_name)

                    def undo_free():
                        cls.vnc_zk_client.alloc_vxlan_id(vxlan_fq_name,
                                                         int(old_vxlan_id))
                    get_context().push_undo(undo_free)

        return True, ''
