#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

import json

from cfgm_common import _obj_serializer_all
from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError
from vnc_api.gen.resource_common import InstanceIp
from vnc_api.gen.resource_common import LogicalInterface
from vnc_api.gen.resource_common import PortTuple
from vnc_api.gen.resource_common import VirtualNetwork
from vnc_api.gen.resource_xsd import IdPermsType
from vnc_api.gen.resource_xsd import VirtualNetworkType

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class PortTupleServer(ResourceMixin, PortTuple):
    @classmethod
    def get_sa_from_pt(cls, obj_dict):
        sa_uuid = None
        db_conn = cls.db_conn
        svc_instance_uuid = obj_dict.get('parent_uuid')
        ok, svc_inst_result = cls.dbe_read(
            db_conn, 'service_instance',
            svc_instance_uuid,
            obj_fields=['service_template_refs'])
        if not ok:
            return ok, svc_inst_result

        if svc_inst_result.get('service_template_refs') is not None:
            svc_template_uuid = svc_inst_result.get(
                'service_template_refs')[0]['uuid']
            ok, svc_tmpl_result = cls.dbe_read(
                db_conn, 'service_template',
                svc_template_uuid,
                obj_fields=['service_appliance_set_refs'])
            if not ok:
                return ok, svc_tmpl_result

            if svc_tmpl_result.get('service_appliance_set_refs') is not None:
                svc_appliance_set_uuid = svc_tmpl_result.get(
                    'service_appliance_set_refs')[0]['uuid']
                ok, svc_appl_set_result = cls.dbe_read(
                    db_conn, 'service_appliance_set',
                    svc_appliance_set_uuid,
                    obj_fields=['service_appliances'])
                if not ok:
                    return ok, svc_appl_set_result
                sa = svc_appl_set_result.get('service_appliances')
                # Only one SA is supported per SAS for PNF support
                if len(sa) == 1:
                    sa_uuid = sa[0]['uuid']
                else:
                    if len(sa) > 1:
                        msg = (
                            "service appliance set (%s) cannot have more "
                            "than one service appliance" %
                            (svc_appliance_set_uuid))
                    else:
                        msg = (
                            "No service appliance for service appliance "
                            "set (%s)" % (svc_appliance_set_uuid))
                    return False, (400, msg)

        return True, sa_uuid

    @staticmethod
    def _construct_iip_fq_name(dev_name, link, vlan_tag):
        return "%s.%s.%s" % (dev_name, link, vlan_tag)

    @staticmethod
    def _construct_li_fq_name(dev_name, link, vlan_tag):
        li_fq_name = ['default-global-system-config', dev_name, link]
        li_fq_name = li_fq_name + ['%s.%s' % (link, vlan_tag)]
        return li_fq_name

    @staticmethod
    def _get_svc_vlans(annotations):
        left_svc_vlan = right_svc_vlan = None
        if annotations is not None:
            kvps = annotations.get('key_value_pair')
            if kvps is not None:
                for d in kvps:
                    if d.get('key') == 'left-svc-vlan':
                        left_svc_vlan = d.get('value')
                    elif d.get('key') == 'right-svc-vlan':
                        right_svc_vlan = d.get('value')

        return left_svc_vlan, right_svc_vlan

    @classmethod
    def _delete_logical_interface(cls, dev_name, link, vlan_tag):
        iip_uuid = None
        li_uuid = None
        api_server = cls.server
        db_conn = cls.db_conn

        iip_fqname = [cls._construct_iip_fq_name(dev_name, link, vlan_tag)]
        try:
            iip_uuid = db_conn.fq_name_to_uuid('instance_ip', iip_fqname)
        except NoIdError:
            pass
        if iip_uuid is not None:
            try:
                api_server.internal_request_delete('instance_ip', iip_uuid)
            except HttpError as e:
                return False, (e.status_code, e.content)

        li_fqname = cls._construct_li_fq_name(dev_name, link, vlan_tag)
        try:
            li_uuid = db_conn.fq_name_to_uuid('logical_interface', li_fqname)
        except NoIdError:
            pass
        if li_uuid is not None:
            try:
                api_server.internal_request_delete(
                    'logical_interface', li_uuid)
            except HttpError as e:
                return False, (e.status_code, e.content)

        return True, ''

    @classmethod
    def _create_logical_interface(cls, dev_name, link,
                                  vlan_tag, subscriber_tag,
                                  network_name):
        api_server = cls.server
        db_conn = cls.db_conn

        li_fqname = cls._construct_li_fq_name(dev_name, link, vlan_tag)
        li_display_name = li_fqname[-1]
        li_display_name = li_display_name.replace("_", ":")
        try:
            db_conn.fq_name_to_uuid('logical_interface', li_fqname)
        except NoIdError:
            li_obj = LogicalInterface(parent_type='physical-interface',
                                      fq_name=li_fqname,
                                      logical_interface_vlan_tag=vlan_tag,
                                      display_name=li_display_name)

            id_perms = IdPermsType(enable=True, user_visible=False)
            li_obj.set_id_perms(id_perms)
            li_int_dict = json.dumps(li_obj, default=_obj_serializer_all)
            try:
                api_server.internal_request_create(
                    'logical-interface', json.loads(li_int_dict))
            except HttpError as e:
                return False, (e.status_code, e.content)

            # Allocate IP address for this logical interface
            iip_name = cls._construct_iip_fq_name(dev_name, link, vlan_tag)
            if subscriber_tag is not None:
                iip_obj = InstanceIp(
                    name=iip_name,
                    instance_ip_family='v4',
                    instance_ip_subscriber_tag=subscriber_tag
                )
            else:
                iip_obj = InstanceIp(
                    name=iip_name,
                    instance_ip_family='v4'
                )
            nw_fq_name = ['default-domain', 'default-project', network_name]
            ok, nw_obj_result = cls.server.get_resource_class(
                'virtual_network').locate(nw_fq_name, create_it=False)
            if not ok:
                return False, nw_obj_result

            nw_obj = VirtualNetwork(name=network_name)
            iip_obj.set_virtual_network(nw_obj)
            iip_obj.set_logical_interface(li_obj)
            iip_obj.set_id_perms(id_perms)

            iip_int_dict = json.dumps(iip_obj, default=_obj_serializer_all)
            try:
                api_server.internal_request_create(
                    'instance-ip', json.loads(iip_int_dict))
            except HttpError as e:
                return False, (e.status_code, e.content)

        return True, ''

    @classmethod
    def create_pt_vn_refs(cls, obj_dict, db_conn):
        left_vn_int_name = '__contrail_pt_internal_vn_' + \
            obj_dict.get('uuid') + '-left' + '__'
        right_vn_int_name = '__contrail_pt_internal_vn_' + \
            obj_dict.get('uuid') + '-right' + '__'
        vn_list = [left_vn_int_name, right_vn_int_name]

        api_server = db_conn.get_api_server()
        for vn in vn_list:
            vn_obj = VirtualNetwork(name=vn)
            id_perms = IdPermsType(enable=True, user_visible=False)
            vn_obj.set_id_perms(id_perms)

            int_vn_property = VirtualNetworkType(forwarding_mode='l3')
            vn_obj.set_virtual_network_properties(int_vn_property)

            vn_int_dict = json.dumps(vn_obj, default=_obj_serializer_all)
            status, obj = api_server.internal_request_create(
                'virtual-network', json.loads(vn_int_dict))
            api_server.internal_request_ref_update(
                'port-tuple',
                obj_dict['uuid'],
                'ADD',
                'virtual-network',
                obj['virtual-network']['uuid'],
                obj['virtual-network']['fq_name'])
        return True, ''
    # end create_pt_vn_refs

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # Create logical interfaces for the physical interfaces using
        # vlan allocated per service instance

        # IP addresese are allocated from the fabric-service-chain subnet for
        # these logical interfaces using IPAM
        svc_instance_uuid = obj_dict.get('parent_uuid')
        if svc_instance_uuid is None:
            msg = "Port tuple(%s) not associated with service instance" % (
                obj_dict.get('uuid'))
            return (False, (400, msg))
        ok, svc_inst_result = cls.dbe_read(
            db_conn, 'service_instance', svc_instance_uuid, obj_fields=[
                'fq_name', 'service_template_refs', 'annotations'])
        if not ok:
            return ok, svc_inst_result
        svc_inst_name = svc_inst_result.get('fq_name')[-1]

        # Fetch left and right vlan from the service instance
        left_svc_vlan, right_svc_vlan = cls._get_svc_vlans(
            svc_inst_result.get('annotations'))
        if left_svc_vlan and right_svc_vlan:
            if svc_inst_result.get('service_template_refs') is not None:
                svc_template_uuid = svc_inst_result.get(
                    'service_template_refs')[0]['uuid']
                ok, svc_tmpl_result = cls.dbe_read(
                    db_conn, 'service_template',
                    svc_template_uuid,
                    obj_fields=['service_template_properties'])
                if not ok:
                    return ok, svc_tmpl_result

                ok, sa_uuid = cls.get_sa_from_pt(obj_dict)
                if sa_uuid is not None:
                    ok, svc_appl_result = cls.dbe_read(
                        db_conn, 'service_appliance',
                        sa_uuid, obj_fields=['physical_interface_refs'])
                    if not ok:
                        return ok, svc_appl_result
                    for phys_intf_ref in svc_appl_result.get(
                            'physical_interface_refs') or []:
                        ok, phys_intf_result = cls.dbe_read(
                            db_conn, 'physical_interface',
                            phys_intf_ref['uuid'], obj_fields=[
                                'fq_name', 'parent_type', 'parent_uuid',
                                'physical_interface_refs'])
                        if not ok:
                            return ok, phys_intf_result

                        phys_intf_name = phys_intf_result.get('fq_name')[-1]
                        dev_name = phys_intf_result.get('fq_name')[-2]

                        if phys_intf_result.get(
                                'parent_type') == 'physical-router':
                            phys_router_uuid = phys_intf_result.get(
                                'parent_uuid')
                            ok, phys_router_result = cls.dbe_read(
                                db_conn, 'physical_router',
                                phys_router_uuid, obj_fields=['fabric_refs'])
                            if not ok:
                                return ok, phys_router_result
                            fabric_refs = phys_router_result.get('fabric_refs')
                            if len(fabric_refs) != 1:
                                msg = "No fabric for physical router(%s)" % (
                                    phys_router_uuid)
                                return (False, (400, msg))
                            fabric_name = str(fabric_refs[0].get('to')[-1])
                            svc_chain_network_type = 'pnf-servicechain'
                            svc_chain_network_name = '%s-%s-network' % (
                                fabric_name, svc_chain_network_type)

                            if phys_intf_ref['attr'].get(
                                    'interface_type') == 'left':
                                vlan_tag = left_svc_vlan
                                svc_chain_subscriber_tag = \
                                    svc_inst_name + '-' + 'left'
                            elif phys_intf_ref['attr'].get(
                                    'interface_type') == 'right':
                                vlan_tag = right_svc_vlan
                                svc_chain_subscriber_tag = \
                                    svc_inst_name + '-' + 'right'

                            # Create logical interfaces for the PNF device
                            ok, result = cls._create_logical_interface(
                                dev_name,
                                phys_intf_name,
                                vlan_tag,
                                svc_chain_subscriber_tag,
                                svc_chain_network_name)
                            if not ok:
                                return (False, (400, result))

                            loopback_network_type = 'loopback'
                            loopback_network_name = '%s-%s-network' % (
                                fabric_name, loopback_network_type)
                            # Create one dummy loopback interface for the PNF
                            # device
                            ok, result = cls._create_logical_interface(
                                dev_name, 'lo0', left_svc_vlan,
                                None, loopback_network_name)
                            if not ok:
                                return (False, (400, result))

                            for ref in phys_intf_result.get(
                                    'physical_interface_refs') or []:
                                ok, read_result = cls.dbe_read(
                                    db_conn, 'physical_interface',
                                    ref['uuid'], obj_fields=['fq_name'])
                                if not ok:
                                    return ok, read_result
                                phys_intf_name = read_result.get('fq_name')[-1]
                                dev_name = read_result.get('fq_name')[-2]
                                # Create logical interfaces for spine
                                ok, result = cls._create_logical_interface(
                                    dev_name,
                                    phys_intf_name,
                                    vlan_tag,
                                    svc_chain_subscriber_tag,
                                    svc_chain_network_name)
                                if not ok:
                                    return (False, (400, result))

        return True, ''
    # end post_dbe_create

    @classmethod
    def delete_pt_vn_refs(cls, obj_dict, db_conn):
        vn_int_fqname = ["default-domain", "default-project"]
        left_vn_int_fqname = vn_int_fqname + \
            ['__contrail_pt_internal_vn_' +
                obj_dict.get('uuid') + '-left' + '__']
        right_vn_int_fqname = vn_int_fqname + \
            ['__contrail_pt_internal_vn_' +
                obj_dict.get('uuid') + '-right' + '__']

        vn_list = [left_vn_int_fqname, right_vn_int_fqname]

        api_server = db_conn.get_api_server()
        for vn in vn_list:
            vn_int_uuid = db_conn.fq_name_to_uuid('virtual_network', vn)

            api_server.internal_request_ref_update(
                'port-tuple',
                obj_dict['uuid'],
                'DELETE',
                'virtual-network',
                vn_int_uuid,
                vn)
            api_server.internal_request_delete('virtual-network', vn_int_uuid)

            def undo_pt_vn_delete():
                return cls.create_pt_vn_and_ref(obj_dict, db_conn)
            get_context().push_undo(undo_pt_vn_delete)

        return True, '', None
    # end delete_pt_vn_refs

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        # Delete the logical interfaces created and de-allocate IP addresses
        # from them
        svc_instance_uuid = obj_dict.get('parent_uuid')
        ok, svc_inst_result = cls.dbe_read(db_conn, 'service_instance',
                                           svc_instance_uuid,
                                           obj_fields=['annotations'])
        if not ok:
            return ok, svc_inst_result, None

        # Fetch left and right vlan from the service instance
        left_svc_vlan, right_svc_vlan = cls._get_svc_vlans(
            svc_inst_result.get('annotations'))
        if left_svc_vlan and right_svc_vlan:
            ok, sa_uuid = cls.get_sa_from_pt(obj_dict)
            if sa_uuid is not None:
                ok, svc_appl_result = cls.dbe_read(
                    db_conn, 'service_appliance', sa_uuid,
                    obj_fields=['physical_interface_refs'])
                if not ok:
                    return ok, svc_appl_result, None
                for phys_intf_ref in svc_appl_result.get(
                        'physical_interface_refs') or []:
                    ok, phys_intf_result = cls.dbe_read(
                        db_conn, 'physical_interface',
                        phys_intf_ref['uuid'], obj_fields=[
                            'fq_name', 'physical_interface_refs'])
                    if not ok:
                        return ok, phys_intf_result, None

                    phys_intf_name = phys_intf_result.get('fq_name')[-1]
                    dev_name = phys_intf_result.get('fq_name')[-2]
                    if phys_intf_ref['attr'].get('interface_type') == 'left':
                        vlan_tag = left_svc_vlan
                    elif phys_intf_ref['attr'].get(
                            'interface_type') == 'right':
                        vlan_tag = right_svc_vlan

                    # Delete logical interfaces for the PNF device
                    ok, result = cls._delete_logical_interface(
                        dev_name, phys_intf_name, vlan_tag)
                    if not ok:
                        return (False, (400, result), None)

                    # Delete dummy loopback interface for the PNF device
                    ok, result = cls._delete_logical_interface(dev_name, 'lo0',
                                                               left_svc_vlan)
                    if not ok:
                        return (False, (400, result), None)

                    for ref in phys_intf_result.get(
                            'physical_interface_refs') or []:
                        ok, read_result = cls.dbe_read(
                            db_conn, 'physical_interface',
                            ref['uuid'], obj_fields=['fq_name'])
                        if not ok:
                            return ok, read_result, None
                        phys_intf_name = read_result.get('fq_name')[-1]
                        dev_name = read_result.get('fq_name')[-2]
                        # Delete logical interfaces for spine
                        ok, result = cls._delete_logical_interface(
                            dev_name, phys_intf_name, vlan_tag)
                        if not ok:
                            return (False, (400, result), None)

        return True, '', None
    # end pre_dbe_delete
# end class PortTupleServer
