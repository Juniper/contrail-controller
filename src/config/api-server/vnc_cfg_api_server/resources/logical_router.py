#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from cfgm_common import _obj_serializer_all
from cfgm_common import get_bgp_rtgt_max_id
from cfgm_common import get_bgp_rtgt_min_id
from cfgm_common import get_lr_internal_vn_name
from cfgm_common import jsonutils as json
from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from cfgm_common.exceptions import ResourceExistsError
from vnc_api.gen.resource_common import LogicalRouter
from vnc_api.gen.resource_common import Project
from vnc_api.gen.resource_common import VirtualNetwork
from vnc_api.gen.resource_xsd import IdPermsType
from vnc_api.gen.resource_xsd import LogicalRouterVirtualNetworkType
from vnc_api.gen.resource_xsd import RouteTargetList
from vnc_api.gen.resource_xsd import VirtualNetworkType

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class LogicalRouterServer(ResourceMixin, LogicalRouter):
    @classmethod
    def is_port_in_use_by_vm(cls, obj_dict, db_conn):
        for vmi_ref in obj_dict.get('virtual_machine_interface_refs') or []:
            vmi_id = vmi_ref['uuid']
            ok, read_result = cls.dbe_read(
                db_conn, 'virtual_machine_interface', vmi_ref['uuid'])
            if not ok:
                return ok, read_result
            if (read_result['parent_type'] == 'virtual-machine' or
                    read_result.get('virtual_machine_refs')):
                msg = ("Port(%s) already in use by virtual-machine(%s)" %
                       (vmi_id, read_result['parent_uuid']))
                return False, (409, msg)
        return True, ''

    @classmethod
    def is_port_gateway_in_same_network(cls, db_conn, vmi_refs, vn_refs):
        interface_vn_uuids = []
        for vmi_ref in vmi_refs:
            ok, vmi_result = cls.dbe_read(
                db_conn, 'virtual_machine_interface', vmi_ref['uuid'])
            if not ok:
                return ok, vmi_result
            interface_vn_uuids.append(
                vmi_result['virtual_network_refs'][0]['uuid'])
            for vn_ref in vn_refs:
                if vn_ref['uuid'] in interface_vn_uuids:
                    msg = ("Logical router interface and gateway cannot be in"
                           "VN(%s)" % vn_ref['uuid'])
                    return False, (400, msg)
        return True, ''

    @classmethod
    def check_port_gateway_not_in_same_network(cls, db_conn, obj_dict,
                                               lr_id=None):
        if ('virtual_network_refs' in obj_dict and
                'virtual_machine_interface_refs' in obj_dict):
            ok, result = cls.is_port_gateway_in_same_network(
                db_conn,
                obj_dict['virtual_machine_interface_refs'],
                obj_dict['virtual_network_refs'])
            if not ok:
                return ok, result
        # update
        if lr_id:
            if ('virtual_network_refs' in obj_dict or
                    'virtual_machine_interface_refs' in obj_dict):
                ok, read_result = cls.dbe_read(db_conn, 'logical_router',
                                               lr_id)
                if not ok:
                    return ok, read_result
            if 'virtual_network_refs' in obj_dict:
                ok, result = cls.is_port_gateway_in_same_network(
                    db_conn,
                    read_result.get('virtual_machine_interface_refs') or [],
                    obj_dict['virtual_network_refs'])
                if not ok:
                    return ok, result
            if 'virtual_machine_interface_refs' in obj_dict:
                ok, result = cls.is_port_gateway_in_same_network(
                    db_conn,
                    obj_dict['virtual_machine_interface_refs'],
                    read_result.get('virtual_network_refs') or [])
                if not ok:
                    return ok, result
        return True, ''

    @classmethod
    def is_vxlan_routing_enabled(cls, db_conn, obj_dict):
        # The function expects the obj_dict to have
        # either the UUID of the LR object or the
        # parent's uuid (for the create case)
        if 'parent_uuid' not in obj_dict:
            if 'uuid' not in obj_dict:
                msg = "No input to derive parent for Logical Router"
                return False, (400, msg)
            ok, lr = db_conn.dbe_read('logical_router', obj_dict['uuid'])
            if not ok:
                return False, (400, 'Logical Router not found')
            project_uuid = lr.get('parent_uuid')
        else:
            project_uuid = obj_dict['parent_uuid']

        ok, project = db_conn.dbe_read('project', project_uuid)
        if not ok:
            return ok, (400, 'Parent project for Logical Router not found')
        vxlan_routing = project.get('vxlan_routing', False)

        return True, vxlan_routing

    @staticmethod
    def _check_vxlan_id_in_lr(obj_dict):
        # UI as of July 2018 can still send empty vxlan_network_identifier
        # the empty value is one of 'None', None or ''.
        # Handle all these scenarios
        if ('vxlan_network_identifier' in obj_dict):
            vxlan_network_identifier = obj_dict['vxlan_network_identifier']
            if (vxlan_network_identifier != 'None' and
                    vxlan_network_identifier is not None and
                    vxlan_network_identifier != ''):
                return vxlan_network_identifier
            else:
                obj_dict['vxlan_network_identifier'] = None
                return None
        else:
            return None

    @staticmethod
    def check_lr_type(obj_dict):
        if 'logical_router_type' in obj_dict:
            return obj_dict['logical_router_type']

        return None

    @classmethod
    def _ensure_lr_dci_association(cls, lr):
        # if no DCI refs, no need to validate LR - Fabric relationship
        if not lr.get('data_center_interconnect_back_refs'):
            return True, ''

        # make sure lr should have association with only one fab PRs
        fab_list = []
        for pr_ref in lr.get('physical_router_refs') or []:
            pr_uuid = pr_ref.get('uuid')
            status, pr_result = cls.dbe_read(cls.db_conn, 'physical_router',
                                             pr_uuid, obj_fields=[
                                                 'fabric_refs'])
            if not status:
                return False, pr_result
            if pr_result.get("fabric_refs"):
                fab_id = pr_result["fabric_refs"][0].get('uuid')
                if fab_id in fab_list:
                    msg = ("LR can not associate with PRs from different "
                           "Fabrics, if DCI is enabled")
                    return False, (400, msg)
                else:
                    fab_list.append(fab_id)
            else:
                msg = ("DCI LR can not associate to PRs which are not part of "
                       "any Fabrics")
                return False, (400, msg)
        return True, ''

    @classmethod
    def _check_type(cls, obj_dict, read_result=None):
        logical_router_type = cls.check_lr_type(obj_dict)
        if read_result is None:
            if logical_router_type is None:
                # If logical_router_type not specified in obj_dict,
                # set it to default 'snat-routing'
                obj_dict['logical_router_type'] = 'snat-routing'
        else:
            logical_router_type_in_db = cls.check_lr_type(read_result)

            if (logical_router_type and
                    logical_router_type != logical_router_type_in_db):
                msg = ("Cannot update logical_router_type for a "
                       "Logical Router")
                return False, (400, msg)
        return (True, '')

    @classmethod
    def _check_route_targets(cls, obj_dict):
        rt_dict = obj_dict.get('configured_route_target_list')
        if not rt_dict:
            return True, ''

        route_target_list = rt_dict.get('route_target')
        if not route_target_list:
            return True, ''

        global_asn = cls.server.global_autonomous_system
        for idx, rt in enumerate(route_target_list):
            ok, result, new_rt = cls.server.get_resource_class(
                'route_target').validate_route_target(rt)
            if not ok:
                return False, result
            user_defined_rt = result
            if not user_defined_rt:
                return (False, "Configured route target must use ASN that is "
                        "different from global ASN or route target value must"
                        " be less than %d and greater than %d" %
                        (get_bgp_rtgt_min_id(global_asn),
                         get_bgp_rtgt_max_id(global_asn)))
            if new_rt:
                route_target_list[idx] = new_rt
        return (True, '')

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls._ensure_lr_dci_association(obj_dict)
        if not ok:
            return ok, result

        ok, result = cls.check_port_gateway_not_in_same_network(db_conn,
                                                                obj_dict)
        if not ok:
            return ok, result

        ok, result = cls.is_port_in_use_by_vm(obj_dict, db_conn)
        if not ok:
            return ok, result

        (ok, error) = cls._check_route_targets(obj_dict)
        if not ok:
            return (False, (400, error))

        ok, result = cls._check_type(obj_dict)
        if not ok:
            return ok, result

        vxlan_id = cls._check_vxlan_id_in_lr(obj_dict)
        logical_router_type = cls.check_lr_type(obj_dict)

        if vxlan_id and logical_router_type == 'vxlan-routing':
            # If input vxlan_id is not None, that means we need to reserve it.

            # First, if vxlan_id is not None, set it in Zookeeper and set the
            # undo function for when any failures happen later.
            # But first, get the internal_vlan name using which the resource
            # in zookeeper space will be reserved.

            vxlan_fq_name = '%s:%s_vxlan' % (
                ':'.join(obj_dict['fq_name'][:-1]),
                get_lr_internal_vn_name(obj_dict['uuid']),
            )

            try:
                # Now that we have the internal VN name, allocate it in
                # zookeeper only if the resource hasn't been reserved already
                cls.vnc_zk_client.alloc_vxlan_id(vxlan_fq_name, int(vxlan_id))
            except ResourceExistsError:
                msg = ("Cannot set VXLAN_ID: %s, it has already been set"
                       % vxlan_id)
                return False, (400, msg)

            def undo_vxlan_id():
                cls.vnc_zk_client.free_vxlan_id(int(vxlan_id), vxlan_fq_name)
                return True, ""
            get_context().push_undo(undo_vxlan_id)

        # Check if type of all associated BGP VPN are 'l3'
        ok, result = cls.server.get_resource_class(
            'bgpvpn').check_router_supports_vpn_type(obj_dict)
        if not ok:
            return ok, result

        # Check if we can reference the BGP VPNs
        return cls.server.get_resource_class(
            'bgpvpn').check_router_has_bgpvpn_assoc_via_network(obj_dict)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls._ensure_lr_dci_association(obj_dict)
        if not ok:
            return ok, result

        ok, result = cls.check_port_gateway_not_in_same_network(
            db_conn, obj_dict, id)
        if not ok:
            return ok, result

        ok, result = cls.is_port_in_use_by_vm(obj_dict, db_conn)
        if not ok:
            return ok, result

        (ok, error) = cls._check_route_targets(obj_dict)
        if not ok:
            return (False, (400, error))

        # To get the current vxlan_id, read the LR from the DB
        ok, result = cls.dbe_read(cls.db_conn,
                                  'logical_router',
                                  id,
                                  obj_fields=['virtual_network_refs',
                                              'logical_router_type',
                                              'vxlan_network_identifier'])
        if not ok:
            return ok, result

        read_result = result

        ok, result = cls._check_type(obj_dict, read_result)
        if not ok:
            return ok, result

        logical_router_type_in_db = cls.check_lr_type(read_result)

        if ('vxlan_network_identifier' in obj_dict and
                logical_router_type_in_db == 'vxlan-routing'):

            new_vxlan_id = cls._check_vxlan_id_in_lr(obj_dict)
            old_vxlan_id = cls._check_vxlan_id_in_lr(read_result)

            if new_vxlan_id != old_vxlan_id:
                int_fq_name = None
                for vn_ref in read_result['virtual_network_refs']:
                    if (vn_ref.get('attr', {}).get(
                            'logical_router_virtual_network_type') ==
                            'InternalVirtualNetwork'):
                        int_fq_name = vn_ref.get('to')
                        break

                if int_fq_name is None:
                    msg = "Internal FQ name not found"
                    return False, (400, msg)

                vxlan_fq_name = ':'.join(int_fq_name) + '_vxlan'
                if new_vxlan_id is not None:
                    # First, check if the new_vxlan_id being updated exist for
                    # some other VN.
                    new_vxlan_fq_name_in_db = cls.vnc_zk_client.get_vn_from_id(
                        int(new_vxlan_id))
                    if new_vxlan_fq_name_in_db is not None:
                        if new_vxlan_fq_name_in_db != vxlan_fq_name:
                            msg = ("Cannot set VXLAN_ID: %s, it has already "
                                   "been set" % new_vxlan_id)
                            return False, (400, msg)

                    # Second, set the new_vxlan_id in Zookeeper.
                    cls.vnc_zk_client.alloc_vxlan_id(vxlan_fq_name,
                                                     int(new_vxlan_id))

                    def undo_alloc():
                        cls.vnc_zk_client.free_vxlan_id(
                            int(old_vxlan_id), vxlan_fq_name)
                    get_context().push_undo(undo_alloc)

                # Third, check if old_vxlan_id is not None, if so, delete it
                # from Zookeeper
                if old_vxlan_id is not None:
                    cls.vnc_zk_client.free_vxlan_id(int(old_vxlan_id),
                                                    vxlan_fq_name)

                    def undo_free():
                        cls.vnc_zk_client.alloc_vxlan_id(
                            vxlan_fq_name, int(old_vxlan_id))
                    get_context().push_undo(undo_free)

        # Check if type of all associated BGP VPN are 'l3'
        ok, result = cls.server.get_resource_class(
            'bgpvpn').check_router_supports_vpn_type(obj_dict)
        if not ok:
            return ok, result

        # Check if we can reference the BGP VPNs
        ok, result = cls.dbe_read(
            db_conn,
            'logical_router',
            id,
            obj_fields=['bgpvpn_refs', 'virtual_machine_interface_refs'])
        if not ok:
            return ok, result
        return cls.server.get_resource_class(
            'bgpvpn').check_router_has_bgpvpn_assoc_via_network(
                obj_dict, result)

    @classmethod
    def get_parent_project(cls, obj_dict, db_conn):
        proj_uuid = obj_dict.get('parent_uuid')
        ok, proj_dict = cls.dbe_read(db_conn, 'project', proj_uuid)
        return ok, proj_dict

    @classmethod
    def post_dbe_update(cls, uuid, fq_name, obj_dict, db_conn,
                        prop_collection_updates=None):

        ok, result = db_conn.dbe_read(
            'logical_router',
            obj_dict['uuid'],
            obj_fields=['virtual_network_refs', 'logical_router_type'])
        if not ok:
            return ok, result
        lr_orig_dict = result

        if (obj_dict.get('configured_route_target_list') is None and
                'vxlan_network_identifier' not in obj_dict):
            return True, ''

        logical_router_type_in_db = cls.check_lr_type(lr_orig_dict)
        if logical_router_type_in_db == 'vxlan-routing':
            # If logical_router_type was set to vxlan-routing in DB,
            # it means that an existing LR used for VXLAN
            # support was updated to either change the
            # vxlan_network_identifer or configured_route_target_list

            vn_int_name = get_lr_internal_vn_name(obj_dict.get('uuid'))
            vn_id = None
            for vn_ref in lr_orig_dict.get('virtual_network_refs') or []:
                if (vn_ref.get('attr', {}).get(
                        'logical_router_virtual_network_type') ==
                        'InternalVirtualNetwork'):
                    vn_id = vn_ref.get('uuid')
                    break
            if vn_id is None:
                return True, ''
            ok, vn_dict = db_conn.dbe_read(
                'virtual_network',
                vn_id,
                obj_fields=['route_target_list',
                            'fq_name',
                            'uuid',
                            'parent_uuid',
                            'virtual_network_properties'])
            if not ok:
                return ok, vn_dict
            vn_rt_dict_list = vn_dict.get('route_target_list')
            vn_rt_list = []
            if vn_rt_dict_list:
                vn_rt_list = vn_rt_dict_list.get('route_target', [])
            lr_rt_list_obj = obj_dict.get('configured_route_target_list')
            lr_rt_list = []
            if lr_rt_list_obj:
                lr_rt_list = lr_rt_list_obj.get('route_target', [])

            vxlan_id_in_db = vn_dict.get('virtual_network_properties', {}).get(
                'vxlan_network_identifier')

            if(vxlan_id_in_db != obj_dict.get('vxlan_network_identifier') or
                    set(vn_rt_list) != set(lr_rt_list)):
                ok, proj_dict = db_conn.dbe_read('project',
                                                 vn_dict['parent_uuid'])
                if not ok:
                    return ok, proj_dict
                proj_obj = Project(name=vn_dict.get('fq_name')[-2],
                                   parent_type='domain',
                                   fq_name=proj_dict.get('fq_name'))

                vn_obj = VirtualNetwork(name=vn_int_name, parent_obj=proj_obj)

                if (set(vn_rt_list) != set(lr_rt_list)):
                    vn_obj.set_route_target_list(lr_rt_list_obj)

                # If vxlan_id has been set, we need to propogate it to the
                # internal VN.
                if vxlan_id_in_db != obj_dict.get('vxlan_network_identifier'):
                    prop = vn_dict.get('virtual_network_properties', {})
                    prop['vxlan_network_identifier'] =\
                        obj_dict['vxlan_network_identifier']
                    vn_obj.set_virtual_network_properties(prop)

                vn_int_dict = json.dumps(vn_obj, default=_obj_serializer_all)
                status, obj = cls.server.internal_request_update(
                    'virtual-network',
                    vn_dict['uuid'],
                    json.loads(vn_int_dict))
        return True, ''

    @classmethod
    def create_intvn_and_ref(cls, obj_dict):
        vn_fq_name = (obj_dict['fq_name'][:-1] +
                      [get_lr_internal_vn_name(obj_dict['uuid'])])
        kwargs = {'id_perms': IdPermsType(user_visible=False, enable=True)}
        kwargs['display_name'] = 'LR::%s' % obj_dict['fq_name'][-1]
        vn_property = VirtualNetworkType(forwarding_mode='l2_l3')
        if 'vxlan_network_identifier' in obj_dict:
            vn_property.set_vxlan_network_identifier(
                obj_dict['vxlan_network_identifier'])
        kwargs['virtual_network_properties'] = vn_property
        rt_list = obj_dict.get(
            'configured_route_target_list', {}).get('route_target')
        if rt_list:
            kwargs['route_target_list'] = RouteTargetList(rt_list)
        ok, result = cls.server.get_resource_class(
            'virtual_network').locate(vn_fq_name, **kwargs)
        if not ok:
            return False, result

        attr_obj = LogicalRouterVirtualNetworkType('InternalVirtualNetwork')
        attr_dict = attr_obj.__dict__
        api_server = cls.server

        try:
            api_server.internal_request_ref_update(
                'logical-router',
                obj_dict['uuid'],
                'ADD',
                'virtual-network',
                result['uuid'],
                result['fq_name'],
                attr=attr_dict)
        except HttpError as e:
            return False, (e.status_code, e.content)
        return True, ''

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        logical_router_type = cls.check_lr_type(obj_dict)

        # If VxLAN routing is enabled for this LR
        # then create an internal VN to export the routes
        # in the private VNs to the VTEPs.
        if logical_router_type == 'vxlan-routing':
            ok, result = cls.create_intvn_and_ref(obj_dict)
            if not ok:
                return ok, result

        return True, ''

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        logical_router_type = cls.check_lr_type(obj_dict)
        if logical_router_type == 'vxlan-routing':
            vn_int_fqname = (obj_dict['fq_name'][:-1] +
                             [get_lr_internal_vn_name(obj_dict['uuid'])])
            vn_int_uuid = db_conn.fq_name_to_uuid('virtual_network',
                                                  vn_int_fqname)

            api_server = cls.server
            try:
                api_server.internal_request_ref_update(
                    'logical-router',
                    obj_dict['uuid'],
                    'DELETE',
                    'virtual-network',
                    vn_int_uuid,
                    vn_int_fqname)
                api_server.internal_request_delete('virtual-network',
                                                   vn_int_uuid)
            except HttpError as e:
                if e.status_code != 404:
                    return False, (e.status_code, e.content), None
            except NoIdError:
                pass

            def undo_int_vn_delete():
                return cls.create_intvn_and_ref(obj_dict)
            get_context().push_undo(undo_int_vn_delete)

        return True, '', None
