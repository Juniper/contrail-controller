#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#


from builtins import range
from builtins import str
import os

from cfgm_common import _obj_serializer_all
from cfgm_common import jsonutils as json
from cfgm_common.exceptions import NoIdError
from cfgm_common.zkclient import ZookeeperLock
from netaddr import AddrFormatError
from netaddr import IPAddress
from vnc_api.gen.resource_common import VirtualMachineInterface
from vnc_api.gen.resource_common import VirtualPortGroup
from vnc_api.gen.resource_xsd import MacAddressesType
from vnc_api.gen.resource_xsd import PolicyBasedForwardingRuleType
from vnc_api.gen.resource_xsd import VpgInterfaceParametersType

from vnc_cfg_api_server.context import get_context
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


class VirtualMachineInterfaceServer(ResourceMixin, VirtualMachineInterface):
    portbindings = {}
    portbindings['VIF_TYPE_VROUTER'] = 'vrouter'
    portbindings['VIF_TYPE_HW_VEB'] = 'hw_veb'
    portbindings['VNIC_TYPE_NORMAL'] = 'normal'
    portbindings['VNIC_TYPE_DIRECT'] = 'direct'
    portbindings['VNIC_TYPE_BAREMETAL'] = 'baremetal'
    portbindings['VNIC_TYPE_VIRTIO_FORWARDER'] = 'virtio-forwarder'
    portbindings['PORT_FILTER'] = True
    portbindings['VIF_TYPE_VHOST_USER'] = 'vhostuser'
    portbindings['VHOST_USER_MODE'] = 'vhostuser_mode'
    portbindings['VHOST_USER_MODE_SERVER'] = 'server'
    portbindings['VHOST_USER_MODE_CLIENT'] = 'client'
    portbindings['VHOST_USER_SOCKET'] = 'vhostuser_socket'
    portbindings['VHOST_USER_VROUTER_PLUG'] = 'vhostuser_vrouter_plug'

    vhostuser_sockets_dir = '/var/run/vrouter/'
    NIC_NAME_LEN = 14

    @staticmethod
    def _kvp_to_dict(kvps):
        return dict((kvp['key'], kvp['value']) for kvp in kvps)

    @classmethod
    def _check_vrouter_link(cls, vmi_data, kvp_dict, obj_dict, db_conn):
        api_server = db_conn.get_api_server()

        host_id = kvp_dict.get('host_id')
        if not host_id:
            return

        vm_refs = vmi_data.get('virtual_machine_refs')
        if not vm_refs:
            vm_refs = obj_dict.get('virtual_machine_refs')
            if not vm_refs:
                return

        vrouter_fq_name = ['default-global-system-config', host_id]
        try:
            vrouter_id = db_conn.fq_name_to_uuid('virtual_router',
                                                 vrouter_fq_name)
        except NoIdError:
            return

        # if virtual_machine_refs is an empty list delete vrouter link
        if ('virtual_machine_refs' in obj_dict and not
                obj_dict['virtual_machine_refs']):
            api_server.internal_request_ref_update(
                'virtual-router',
                vrouter_id,
                'DELETE',
                'virtual_machine',
                vm_refs[0]['uuid'])
            return

        api_server.internal_request_ref_update(
            'virtual-router',
            vrouter_id,
            'ADD',
            'virtual-machine',
            vm_refs[0]['uuid'])

    @classmethod
    def _check_bridge_domain_vmi_association(
            cls, obj_dict, db_dict, db_conn, vn_uuid, create):
        if ('virtual_network_refs' not in obj_dict and
                'bridge_domain_refs' not in obj_dict):
            return True, ''

        vn_fq_name = []
        if vn_uuid:
            vn_fq_name = db_conn.uuid_to_fq_name(vn_uuid)

        bridge_domain_links = {}
        bd_refs = obj_dict.get('bridge_domain_refs') or []

        for bd in bd_refs:
            bd_fq_name = bd['to']
            if bd_fq_name[0:3] != vn_fq_name:
                msg = ("Virtual machine interface(%s) can only refer to "
                       "bridge domain belonging to virtual network(%s)" %
                       (obj_dict['uuid'], vn_uuid))
                return False, msg
            bd_uuid = bd.get('uuid')
            if not bd_uuid:
                bd_uuid = db_conn.fq_name_to_uuid('bridge_domain', bd_fq_name)

            bdmt = bd['attr']
            vlan_tag = bdmt['vlan_tag']
            if vlan_tag in bridge_domain_links:
                msg = ("Virtual machine interface(%s) already refers to "
                       "bridge domain(%s) for vlan tag %d" %
                       (obj_dict['uuid'], bd_uuid, vlan_tag))
                return False, msg

            bridge_domain_links[vlan_tag] = bd_uuid

        # disallow final state where bd exists but vn doesn't
        if 'virtual_network_refs' in obj_dict:
            vn_refs = obj_dict['virtual_network_refs']
        else:
            vn_refs = (db_dict or {}).get('virtual_network_refs')

        if not vn_refs:
            if 'bridge_domain_refs' not in obj_dict:
                bd_refs = db_dict.get('bridge_domain_refs')
            if bd_refs:
                msg = ("Virtual network can not be removed on virtual machine "
                       "interface(%s) while it refers a bridge domain" %
                       (obj_dict['uuid']))
                return False, msg

        return True, ''

    @classmethod
    def _check_port_security_and_address_pairs(cls, obj_dict, db_dict=None):
        if ('port_security_enabled' not in obj_dict and
                'virtual_machine_interface_allowed_address_pairs' not in
                obj_dict):
            return True, ""

        if not db_dict:
            db_dict = {}
        if 'port_security_enabled' in obj_dict:
            port_security = obj_dict.get('port_security_enabled', True)
        else:
            port_security = db_dict.get('port_security_enabled', True)

        if 'virtual_machine_interface_allowed_address_pairs' in obj_dict:
            address_pairs = obj_dict.get(
                'virtual_machine_interface_allowed_address_pairs')
        else:
            address_pairs = db_dict.get(
                'virtual_machine_interface_allowed_address_pairs')

        # Validate the format of allowed_address_pair
        if address_pairs:
            for aap in address_pairs.get('allowed_address_pair', {}):
                try:
                    IPAddress(aap.get('ip').get('ip_prefix'))
                except AddrFormatError as e:
                    return (False, (400, str(e)))

        if (not port_security and address_pairs and
                address_pairs.get('allowed_address_pair')):
            msg = "Allowed address pairs are not allowed when port "\
                  "security is disabled"
            return False, (400, msg)

        return True, ""

    @classmethod
    def _check_service_health_check_type(cls, obj_dict, db_dict, db_conn):
        service_health_check_refs = obj_dict.get('service_health_check_refs')
        if not service_health_check_refs:
            return True, ''

        if obj_dict and obj_dict.get('port_tuple_refs'):
            return True, ''

        if db_dict and db_dict.get('port_tuple_refs'):
            return True, ''

        for shc in service_health_check_refs or []:
            if 'uuid' in shc:
                shc_uuid = shc['uuid']
            else:
                shc_fq_name = shc['to']
                shc_uuid = db_conn.fq_name_to_uuid(
                    'service_health_check', shc_fq_name)
            ok, result = cls.dbe_read(
                db_conn,
                'service_health_check',
                shc_uuid,
                obj_fields=['service_health_check_properties'])
            if not ok:
                return ok, result

            shc_type = result['service_health_check_properties'
                              ]['health_check_type']
            if shc_type != 'link-local':
                msg = ("Virtual machine interface(%s) of non service vm can "
                       "only refer link-local type service health check" %
                       obj_dict['uuid'])
                return False, (400, msg)

        return True, ''

    @classmethod
    def _is_port_bound(cls, obj_dict, new_kvp_dict):
        """Check whatever port is bound.

        For any NON 'baremetal port' we assume port is bound when it is linked
        to either VM or Vrouter.
        For any 'baremetal' port we assume it is bound when it has set:
        * binding:local_link_information
        * binding:host_id

        :param obj_dict: Current port dict to check
        :param new_kvp_dict: KVP dict of port update.
        :returns: True if port is bound, False otherwise.
        """
        bindings = obj_dict['virtual_machine_interface_bindings']
        kvps = bindings['key_value_pair']
        kvp_dict = cls._kvp_to_dict(kvps)
        old_vnic_type = kvp_dict.get('vnic_type')
        new_vnic_type = new_kvp_dict.get('vnic_type')

        if new_vnic_type == cls.portbindings['VNIC_TYPE_BAREMETAL']:
            return False

        if old_vnic_type == cls.portbindings['VNIC_TYPE_BAREMETAL']:
            if (kvp_dict.get('profile', {}).get('local_link_information') and
                    kvp_dict.get('host_id')):
                return True
        else:
            return (obj_dict.get('logical_router_back_refs') or
                    obj_dict.get('virtual_machine_refs'))

    @classmethod
    def _get_port_vhostuser_socket_name(cls, id=None):
        name = 'tap' + id
        name = name[:cls.NIC_NAME_LEN]
        return cls.vhostuser_sockets_dir + 'uvh_vif_' + name

    @classmethod
    def _is_dpdk_enabled(cls, obj_dict, db_conn, host_id=None):
        if host_id is None or not host_id:
            if obj_dict.get('virtual_machine_interface_bindings'):
                bindings = obj_dict['virtual_machine_interface_bindings']
                kvps = bindings['key_value_pair']
                kvp_dict = cls._kvp_to_dict(kvps)
                host_id = kvp_dict.get('host_id')
                if not host_id or host_id == 'null':
                    return True, False
            else:
                return True, False

        vrouter = None
        vrouter_fq_name = ['default-global-system-config', host_id]
        fields = ['uuid', 'fq_name', 'virtual_router_dpdk_enabled']
        ok, result = cls.server.get_resource_class('virtual_router').locate(
            vrouter_fq_name, create_it=False, fields=fields)
        if not ok and result[0] == 404:
            ok, result, _ = db_conn.dbe_list('virtual_router',
                                             field_names=fields)
            if not ok:
                return False, result
            vrs = result
            for vr in vrs:
                fullname = vr['fq_name'][-1].split('.')
                for hname in fullname:
                    if host_id.partition('.')[0] == hname:
                        vrouter = vr
                        break
                if vr == vrouter:
                    break
            if not vrouter:
                return True, result
        elif not ok:
            return False, result
        else:
            vrouter = result

        return True, vrouter.get('virtual_router_dpdk_enabled', False)

    @classmethod
    def _kvps_update(cls, kvps, kvp):
        key = kvp['key']
        value = kvp['value']
        kvp_dict = cls._kvp_to_dict(kvps)
        if key not in list(kvp_dict.keys()):
            if value:
                kvps.append(kvp)
        else:
            for i in range(len(kvps)):
                kvps_dict = dict(kvps[i])
                if key == kvps_dict.get('key'):
                    if value:
                        kvps[i]['value'] = value
                        break
                    else:
                        del kvps[i]
                        break

    @classmethod
    def _kvps_prop_update(cls, obj_dict, kvps, prop_collection_updates,
                          vif_type, vif_details, prop_set):
        if obj_dict:
            cls._kvps_update(kvps, vif_type)
            cls._kvps_update(kvps, vif_details)
        else:
            if prop_set:
                vif_type_prop = {
                    'field': 'virtual_machine_interface_bindings',
                    'operation': 'set',
                    'value': vif_type,
                    'position': 'vif_type',
                }
                vif_details_prop = {
                    'field': 'virtual_machine_interface_bindings',
                    'operation': 'set',
                    'value': vif_details,
                    'position': 'vif_details',
                }
            else:
                vif_type_prop = {
                    'field': 'virtual_machine_interface_bindings',
                    'operation': 'delete',
                    'value': vif_type,
                    'position': 'vif_type',
                }
                vif_details_prop = {
                    'field': 'virtual_machine_interface_bindings',
                    'operation': 'delete',
                    'value': vif_details,
                    'position': 'vif_details',
                }
            prop_collection_updates.append(vif_details_prop)
            prop_collection_updates.append(vif_type_prop)

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        vn_dict = obj_dict['virtual_network_refs'][0]
        vn_uuid = vn_dict.get('uuid')
        if not vn_uuid:
            vn_fq_name = vn_dict.get('to')
            if not vn_fq_name:
                msg = ("Virtual Machine Interface must have valide Virtual "
                       "Network reference")
                return False, (400, msg)
            vn_uuid = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)

        ok, result = cls.dbe_read(
            db_conn,
            'virtual_network',
            vn_uuid,
            obj_fields=['parent_uuid', 'provider_properties'])
        if not ok:
            return ok, result
        vn_dict = result

        vlan_tag = (obj_dict.get('virtual_machine_interface_properties') or {}
                    ).get('sub_interface_vlan_tag') or 0
        if vlan_tag < 0 or vlan_tag > 4094:
            return False, (400, "Invalid sub-interface VLAN tag ID: %s" %
                           vlan_tag)
        if vlan_tag and 'virtual_machine_interface_refs' in obj_dict:
            primary_vmi_ref = obj_dict['virtual_machine_interface_refs']
            ok, primary_vmi = cls.dbe_read(
                db_conn,
                'virtual_machine_interface',
                primary_vmi_ref[0]['uuid'],
                obj_fields=['virtual_machine_interface_refs',
                            'virtual_machine_interface_properties'])
            if not ok:
                return ok, primary_vmi

            primary_vmi_vlan_tag = primary_vmi.get(
                'virtual_machine_interface_properties', {}).get(
                    'sub_interface_vlan_tag')
            if primary_vmi_vlan_tag:
                msg = ("sub interface can't have another sub interface as "
                       "it's primary port")
                return False, (400, msg)

            sub_vmi_refs = primary_vmi.get('virtual_machine_interface_refs',
                                           {})
            sub_vmi_uuids = [ref['uuid'] for ref in sub_vmi_refs]

            if sub_vmi_uuids:
                ok, sub_vmis, _ = db_conn.dbe_list(
                    'virtual_machine_interface', obj_uuids=sub_vmi_uuids,
                    field_names=['virtual_machine_interface_properties'])
                if not ok:
                    return ok, sub_vmis

                sub_vmi_vlan_tags = [((vmi.get(
                    'virtual_machine_interface_properties') or {}).get(
                        'sub_interface_vlan_tag')) for vmi in sub_vmis]
                if vlan_tag in sub_vmi_vlan_tags:
                    msg = "Two sub interfaces under same primary port "\
                          "can't have same Vlan tag"
                    return (False, (400, msg))

        ok, error = cls._check_bridge_domain_vmi_association(
            obj_dict, None, db_conn, vn_uuid, True)
        if not ok:
            return (False, (400, error))

        inmac = None
        if 'virtual_machine_interface_mac_addresses' in obj_dict:
            mc = obj_dict['virtual_machine_interface_mac_addresses']
            if 'mac_address' in mc and mc['mac_address']:
                inmac = [m.replace("-", ":") for m in mc['mac_address']]
        if inmac is not None:
            mac_addrs_obj = MacAddressesType(inmac)
        else:
            mac_addr = cls.addr_mgmt.mac_alloc(obj_dict)
            mac_addrs_obj = MacAddressesType([mac_addr])
        mac_addrs_json = json.dumps(
            mac_addrs_obj,
            default=lambda o: dict((k, v)
                                   for k, v in o.__dict__.items()))
        mac_addrs_dict = json.loads(mac_addrs_json)
        obj_dict['virtual_machine_interface_mac_addresses'] = mac_addrs_dict

        kvps = []
        if 'virtual_machine_interface_bindings' in obj_dict:
            bindings = obj_dict['virtual_machine_interface_bindings']
            kvps = bindings['key_value_pair']
            kvp_dict = cls._kvp_to_dict(kvps)

            if (kvp_dict.get('vnic_type') ==
                    cls.portbindings['VNIC_TYPE_DIRECT'] or
                    kvp_dict.get('vnic_type') ==
                    cls.portbindings['VNIC_TYPE_VIRTIO_FORWARDER']):
                # If the segmentation_id in provider_properties is set, then
                # a hw_veb VIF is requested.
                if ('provider_properties' in vn_dict and
                   vn_dict['provider_properties'] is not None and
                   'segmentation_id' in vn_dict['provider_properties']):
                    kvp_dict['vif_type'] = cls.portbindings['VIF_TYPE_HW_VEB']
                    vif_type = {
                        'key': 'vif_type',
                        'value': cls.portbindings['VIF_TYPE_HW_VEB'],
                    }
                    vlan = vn_dict['provider_properties']['segmentation_id']
                    vif_params = {
                        'port_filter': cls.portbindings['PORT_FILTER'],
                        'vlan': str(vlan),
                    }
                    vif_details = {
                        'key': 'vif_details',
                        'value': json.dumps(vif_params),
                    }
                    kvps.append(vif_details)
                    kvps.append(vif_type)
                else:
                    # An offloaded port is requested
                    vnic_type = {
                        'key': 'vnic_type',
                        'value': kvp_dict.get('vnic_type'),
                    }
                    if (kvp_dict.get('vnic_type') ==
                            cls.portbindings['VNIC_TYPE_VIRTIO_FORWARDER']):
                        vif_params = {
                            cls.portbindings['VHOST_USER_MODE']:
                            cls.portbindings['VHOST_USER_MODE_SERVER'],
                            cls.portbindings['VHOST_USER_SOCKET']:
                            cls._get_port_vhostuser_socket_name(
                                obj_dict['uuid'])
                        }
                        vif_details = {
                            'key': 'vif_details',
                            'value': json.dumps(vif_params),
                        }
                        kvps.append(vif_details)
                    kvps.append(vnic_type)

            if 'vif_type' not in kvp_dict:
                vif_type = {'key': 'vif_type',
                            'value': cls.portbindings['VIF_TYPE_VROUTER']}
                kvps.append(vif_type)

            if 'vnic_type' not in kvp_dict:
                vnic_type = {'key': 'vnic_type',
                             'value': cls.portbindings['VNIC_TYPE_NORMAL']}
                kvps.append(vnic_type)

            kvp_dict = cls._kvp_to_dict(kvps)
            if (kvp_dict.get('vnic_type') ==
                    cls.portbindings['VNIC_TYPE_NORMAL']):
                (ok, result) = cls._is_dpdk_enabled(obj_dict, db_conn)
                if not ok:
                    return ok, result
                elif result:
                    vif_type = {
                        'key': 'vif_type',
                        'value': cls.portbindings['VIF_TYPE_VHOST_USER'],
                    }
                    vif_params = {
                        cls.portbindings['VHOST_USER_MODE']:
                        cls.portbindings['VHOST_USER_MODE_SERVER'],
                        cls.portbindings['VHOST_USER_SOCKET']:
                        cls._get_port_vhostuser_socket_name(obj_dict['uuid']),
                        cls.portbindings['VHOST_USER_VROUTER_PLUG']: True
                    }
                    vif_details = {
                        'key': 'vif_details',
                        'value': json.dumps(vif_params),
                    }
                    cls._kvps_update(kvps, vif_type)
                    cls._kvps_update(kvps, vif_details)
                else:
                    vif_type = {'key': 'vif_type', 'value': None}
                    cls._kvps_update(kvps, vif_type)

        (ok, result) = cls._check_port_security_and_address_pairs(obj_dict)

        if not ok:
            return ok, result

        (ok, result) = cls._check_service_health_check_type(obj_dict, None,
                                                            db_conn)
        if not ok:
            return ok, result

        # Manage baremetal provisioning here
        if kvps:
            kvp_dict = cls._kvp_to_dict(kvps)
            vnic_type = kvp_dict.get('vnic_type')
            vpg_name = kvp_dict.get('vpg')
            tor_port_vlan_id = kvp_dict.get('tor_port_vlan_id', 0)
            if vnic_type == 'baremetal' and kvp_dict.get('profile'):
                # Process only if port profile exists and physical links are
                # specified
                phy_links = json.loads(kvp_dict.get('profile'))
                if phy_links and phy_links.get('local_link_information'):
                    links = phy_links['local_link_information']
                    is_untagged_vlan = False
                    if tor_port_vlan_id:
                        vlan_id = tor_port_vlan_id
                        is_untagged_vlan = True
                    else:
                        vlan_id = vlan_tag
                    vpg_uuid, ret_dict = cls._manage_vpg_association(
                        obj_dict['uuid'], cls.server, db_conn, links,
                        vpg_name, vn_uuid, vlan_id, is_untagged_vlan)
                    if not vpg_uuid:
                        return vpg_uuid, ret_dict
                    obj_dict['port_virtual_port_group_id'] = vpg_uuid
                    obj_dict.update(ret_dict)

        return True, ""

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        api_server = db_conn.get_api_server()

        # add a ref from VPG to this VMI
        vpg_uuid = obj_dict.get('port_virtual_port_group_id')
        if vpg_uuid:
            api_server.internal_request_ref_update(
                'virtual-port-group',
                vpg_uuid,
                'ADD',
                'virtual-machine-interface',
                obj_dict['uuid'],
                relax_ref_for_delete=True)

        # Create ref to native/vn-default routing instance
        vn_refs = obj_dict.get('virtual_network_refs')
        if not vn_refs:
            return True, ''

        vn_fq_name = vn_refs[0].get('to')
        if not vn_fq_name:
            vn_uuid = vn_refs[0]['uuid']
            vn_fq_name = db_conn.uuid_to_fq_name(vn_uuid)

        ri_fq_name = vn_fq_name[:]
        ri_fq_name.append(vn_fq_name[-1])
        ri_uuid = db_conn.fq_name_to_uuid('routing_instance', ri_fq_name)

        attr = PolicyBasedForwardingRuleType(direction="both")
        attr_as_dict = attr.__dict__
        api_server.internal_request_ref_update(
            'virtual-machine-interface', obj_dict['uuid'], 'ADD',
            'routing-instance', ri_uuid,
            attr=attr_as_dict)

        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                       prop_collection_updates=None, **kwargs):

        ok, read_result = cls.dbe_read(db_conn, 'virtual_machine_interface',
                                       id)
        if not ok:
            return ok, read_result

        vn_uuid = None
        if 'virtual_network_refs' in read_result:
            vn_uuid = read_result['virtual_network_refs'][0].get('uuid')
        ok, error = cls._check_bridge_domain_vmi_association(
            obj_dict, read_result, db_conn, vn_uuid, False)
        if not ok:
            return False, (400, error)

        # check if the vmi is a internal interface of a logical
        # router
        if (read_result.get('logical_router_back_refs') and
                obj_dict.get('virtual_machine_refs')):
            return (False,
                    (400, 'Logical router interface cannot be used by VM'))
        # check if vmi is going to point to vm and if its using
        # gateway address in iip, disallow
        iip_class = cls.server.get_resource_class('instance_ip')
        for iip_ref in read_result.get('instance_ip_back_refs') or []:
            if (obj_dict.get('virtual_machine_refs') and
                    iip_class.is_gateway_ip(db_conn, iip_ref['uuid'])):
                return (False, (400, 'Gateway IP cannot be used by VM port'))

        vpg = None
        if read_result.get('virtual_port_group_back_refs'):
            vpg_refs = read_result.get('virtual_port_group_back_refs')
            vpg_id = vpg_refs[0].get('uuid')
            ok, vpg = cls.dbe_read(
                db_conn, 'virtual_port_group',
                vpg_id, obj_fields=['virtual_port_group_trunk_port_id'])
            if not ok:
                return ok, vpg

        if ('virtual_machine_interface_refs' in obj_dict and
                'virtual_machine_interface_refs' in read_result):
            for ref in read_result['virtual_machine_interface_refs']:
                if (ref not in obj_dict['virtual_machine_interface_refs'] and
                        vpg and
                        vpg.get('virtual_port_group_trunk_port_id') != id):
                    # Dont allow remove of vmi ref during update if it's
                    # a sub port
                    msg = "VMI ref delete not allowed during update"
                    return (False, (409, msg))

        bindings = read_result.get('virtual_machine_interface_bindings') or {}
        kvps = bindings.get('key_value_pair') or []
        kvp_dict = cls._kvp_to_dict(kvps)
        old_vnic_type = kvp_dict.get('vnic_type',
                                     cls.portbindings['VNIC_TYPE_NORMAL'])

        bindings = obj_dict.get('virtual_machine_interface_bindings') or {}
        kvps = bindings.get('key_value_pair') or []

        vmib = False
        for oper_param in prop_collection_updates or []:
            if (oper_param['field'] == 'virtual_machine_interface_bindings' and
                    oper_param['operation'] == 'set'):
                vmib = True
                kvps.append(oper_param['value'])

        old_vlan = (read_result.get('virtual_machine_interface_properties') or
                    {}).get('sub_interface_vlan_tag') or 0
        new_vlan = None
        if 'virtual_machine_interface_properties' in obj_dict:
            new_vlan = (obj_dict['virtual_machine_interface_properties'] or
                        {}).get('sub_interface_vlan_tag') or 0
            # vRouter has limitations for VLAN tag update.
            # But, In case of neutron trunk port, subports are created first
            # and then associated a VLAN tag
            # To take care of limitations of vRouter for vlan tag update
            # we will limit any vlan update once a port has been made
            # a sub-interface
            if (new_vlan != old_vlan and
                    'virtual_machine_interface_refs' in read_result):
                return False, (400, "Cannot change sub-interface VLAN tag ID")

        ret_dict = None
        if kvps:
            kvp_dict = cls._kvp_to_dict(kvps)
            new_vnic_type = kvp_dict.get('vnic_type', old_vnic_type)
            tor_port_vlan_id = kvp_dict.get('tor_port_vlan_id', 0)
            # IRONIC: allow for normal->baremetal change if bindings has
            # link-local info
            if new_vnic_type != 'baremetal':
                if (old_vnic_type != new_vnic_type):
                    if cls._is_port_bound(read_result, kvp_dict):
                        msg = ("Vnic_type can not be modified when port is "
                               "linked to Vrouter or VM.")
                        return False, (409, msg)

            # Manage the bindings for baremetal servers
            vnic_type = kvp_dict.get('vnic_type')
            vpg_name = kvp_dict.get('vpg')
            if read_result.get('virtual_port_group_back_refs'):
                vpg_name_db = (read_result.get(
                    'virtual_port_group_back_refs')[0]['to'][-1])
                obj_dict['virtual_port_group_name'] = vpg_name_db

            if vnic_type == 'baremetal':
                phy_links = json.loads(kvp_dict.get('profile'))
                if phy_links and phy_links.get('local_link_information'):
                    links = phy_links['local_link_information']
                    is_untagged_vlan = False
                    if tor_port_vlan_id:
                        vlan_id = tor_port_vlan_id
                        is_untagged_vlan = True
                        if (vlan_id != old_vlan and
                           'virtual_machine_interface_refs' in read_result):
                            return False, (400, "Cannot change VLAN ID")
                    else:
                        vlan_id = new_vlan
                    vpg_uuid, ret_dict = cls._manage_vpg_association(
                        id, cls.server, db_conn, links, vpg_name,
                        vn_uuid, vlan_id, is_untagged_vlan)
                    if not vpg_uuid:
                        return vpg_uuid, ret_dict

                    obj_dict['port_virtual_port_group_id'] = vpg_uuid

        if old_vnic_type == cls.portbindings['VNIC_TYPE_DIRECT']:
            cls._check_vrouter_link(read_result, kvp_dict, obj_dict, db_conn)

        if 'virtual_machine_interface_bindings' in obj_dict or vmib:
            bindings_port = read_result.get(
                'virtual_machine_interface_bindings', {})
            kvps_port = bindings_port.get('key_value_pair') or []
            kvp_dict_port = cls._kvp_to_dict(kvps_port)
            kvp_dict = cls._kvp_to_dict(kvps)
            if (kvp_dict_port.get('vnic_type') ==
                    cls.portbindings['VNIC_TYPE_VIRTIO_FORWARDER'] and
                    kvp_dict.get('host_id') != 'null'):
                vif_type = {
                    'key': 'vif_type',
                    'value': cls.portbindings['VIF_TYPE_VROUTER'],
                }
                vif_params = {
                    cls.portbindings['VHOST_USER_MODE']:
                        cls.portbindings['VHOST_USER_MODE_SERVER'],
                    cls.portbindings['VHOST_USER_SOCKET']:
                        cls._get_port_vhostuser_socket_name(id),
                    cls.portbindings['VHOST_USER_VROUTER_PLUG']: True,
                }
                vif_details = {
                    'key': 'vif_details',
                    'value': json.dumps(vif_params),
                }
                cls._kvps_prop_update(obj_dict, kvps, prop_collection_updates,
                                      vif_type, vif_details, True)
            elif ((kvp_dict_port.get('vnic_type') ==
                    cls.portbindings['VNIC_TYPE_NORMAL'] or
                    kvp_dict_port.get('vnic_type') is None) and
                    kvp_dict.get('host_id') != 'null'):
                (ok, result) = cls._is_dpdk_enabled(obj_dict, db_conn,
                                                    kvp_dict.get('host_id'))
                if not ok:
                    return ok, result
                elif result:
                    vif_type = {
                        'key': 'vif_type',
                        'value': cls.portbindings['VIF_TYPE_VHOST_USER'],
                    }
                    vif_params = {
                        cls.portbindings['VHOST_USER_MODE']:
                            cls.portbindings['VHOST_USER_MODE_SERVER'],
                        cls.portbindings['VHOST_USER_SOCKET']:
                            cls._get_port_vhostuser_socket_name(id),
                        cls.portbindings['VHOST_USER_VROUTER_PLUG']: True,
                    }
                    vif_details = {
                        'key': 'vif_details',
                        'value': json.dumps(vif_params),
                    }
                    cls._kvps_prop_update(
                        obj_dict,
                        kvps,
                        prop_collection_updates,
                        vif_type,
                        vif_details,
                        True)
                else:
                    vif_type = {'key': 'vif_type', 'value': None}
                    vif_details = {'key': 'vif_details', 'value': None}
                    cls._kvps_prop_update(
                        obj_dict,
                        kvps,
                        prop_collection_updates,
                        vif_type,
                        vif_details,
                        False)
            else:
                vif_type = {'key': 'vif_type', 'value': None}
                vif_details = {'key': 'vif_details', 'value': None}
                if (kvp_dict_port.get('vnic_type') !=
                        cls.portbindings['VNIC_TYPE_DIRECT']):
                    if obj_dict and 'vif_details' in kvp_dict_port:
                        cls._kvps_update(kvps, vif_type)
                        cls._kvps_update(kvps, vif_details)
                    elif kvp_dict.get('host_id') == 'null':
                        vif_details_prop = {
                            'field': 'virtual_machine_interface_bindings',
                            'operation': 'delete',
                            'value': vif_details,
                            'position': 'vif_details',
                        }
                        vif_type_prop = {
                            'field': 'virtual_machine_interface_bindings',
                            'operation': 'delete',
                            'value': vif_type,
                            'position': 'vif_type',
                        }
                        prop_collection_updates.append(vif_details_prop)
                        prop_collection_updates.append(vif_type_prop)

        ok, result = cls._check_port_security_and_address_pairs(obj_dict,
                                                                read_result)
        if not ok:
            return ok, result

        ok, result = cls._check_service_health_check_type(
            obj_dict, read_result, db_conn)
        if not ok:
            return ok, result

        return True, ret_dict

    @classmethod
    def _notify_ae_id_modified(cls, obj_dict=None, notify=False):

        if (obj_dict.get('deallocated_ae_id') and
                len(obj_dict.get('deallocated_ae_id'))):
            dealloc_dict_list = obj_dict.get('deallocated_ae_id')
            for dealloc_dict in dealloc_dict_list:
                ae_id = dealloc_dict.get('ae_id')
                vpg_name = dealloc_dict.get('vpg_name')
                prouter_name = dealloc_dict.get('prouter_name')
                cls.vnc_zk_client.free_ae_id(
                    prouter_name, ae_id,
                    vpg_name, notify=notify)

        if (obj_dict.get('allocated_ae_id') and
                len(obj_dict.get('allocated_ae_id'))):
            alloc_dict_list = obj_dict.get('allocated_ae_id')
            for alloc_dict in alloc_dict_list:
                ae_id = alloc_dict.get('ae_id')
                vpg_name = alloc_dict.get('vpg_name')
                prouter_name = alloc_dict.get('prouter_name')
                cls.vnc_zk_client.alloc_ae_id(prouter_name, vpg_name, ae_id,
                                              notify=True)

    # Allocate ae_id:
    # 1. Get the ae_id from the old PI ref which is already assoc with PR
    # 2. If not, then check if it got already generated on this api call
    #    from the other PI that belongs to the same PR.
    # 3. Else allocate the new ae_id. Id allocation is per PR 0-127 and key
    #    is the vpg name.
    @classmethod
    def _check_and_alloc_ae_id(cls, links, prouter_name,
                               vpg_name, old_pi_to_pr_dict):
        if not len(links) > 1:
            return None, None

        for pr in old_pi_to_pr_dict.values():
            if (pr.get('prouter_name') == prouter_name and
                    pr.get('ae_id') is not None):
                attr_obj = VpgInterfaceParametersType(pr.get('ae_id'))
                return attr_obj, pr.get('ae_id')

        ae_num = cls.vnc_zk_client.alloc_ae_id(prouter_name, vpg_name)
        attr_obj = VpgInterfaceParametersType(ae_num)

        return attr_obj, ae_num

    # Free ae_id:
    # 1. If the PI ref is getting deleted and there in no other PI left
    #    that belongs to the same PR.
    # 2. Or if there is only one physical link to VPG.
    @classmethod
    def _check_and_free_ae_id(cls, links, prouter_dict,
                              vpg_name, pi_to_pr_dict):
        prouter_list = []
        dealloc_dict = {}
        for pr in pi_to_pr_dict.values():
            prouter_list.append(pr)

        prouter_name = prouter_dict.get('prouter_name')
        if prouter_name not in prouter_list or len(links) < 2:
            cls.vnc_zk_client.free_ae_id(prouter_name,
                                         prouter_dict.get('ae_id'),
                                         vpg_name)
            dealloc_dict['ae_id'] = prouter_dict.get('ae_id')
            dealloc_dict['prouter_name'] = prouter_dict.get('prouter_name')
            dealloc_dict['vpg_name'] = vpg_name
            prouter_dict['ae_id'] = None
            return dealloc_dict

        return

    @classmethod
    def _check_annotations(
        cls, api_server, obj_uuid,
        resource_type, annotation_key,
            annotation_value, fail_if_exists=True):
        """Check given annotation exists in the given object

           Consider availability as PASS/FAIL
           based on fail_if_exists.
        :Args
        :  api_server: api_server connection object
        :  obj_uuid: UUID of the object on which
        :    annotations to be checked
        :  resource_type: resource type of object
        :    for which annotations to be checked
        :  annotation_key: key of the annotation
        :  annotation_value: value of the annotation
        :  fail_if_exists:
        :    consider failure when annotation exists
        :Returns
        :  True, '': PASS condition
        :  (False, (<code>, msg): Fail condition
        """
        query = {'obj_uuid': obj_uuid,
                 'fields': 'annotations',
                 'position': annotation_key}
        # get this annotation.
        # if exists Fail per fail_if_exists
        result = api_server.internal_request_prop_collection_get(
            resource_type, **query)
        if fail_if_exists and result:
            # Exists but not expected
            if result.get('annotations'):
                return False, result
        return True, result

    @classmethod
    def _add_annotations(
        cls, api_server, obj_uuid,
        resource_type, annotation_key,
            annotation_value):
        """Add given annotation to the VPG object.

        :Args
        :  api_server: api_server connection object
        :  obj_uuid: UUID of the object for which
        :    annotations to be added
        :  resource_type: resource type of object
        :    for which annotations to be added
        :  annotation_key: key of the annotation
        :  annotation_value: value of the annotation
        :Returns
        :  True, '': PASS condition
        :  (False, (<code>, msg): Fail condition
        """
        # format update dict
        updates = [{'field': 'annotations',
                    'operation': 'set',
                    'value': {'key': annotation_key,
                              'value': annotation_value}}]
        ok, result = api_server.internal_request_prop_collection(
            obj_uuid, updates)
        if not ok:
            return False, result
        return True, ''

    @classmethod
    def _delete_annotations(
        cls, api_server, obj_uuid, resource_type,
            annotation_keys):
        """ Delete given annotations from the object

           :Args
           :   api_server: api_server connection object
           :   obj_uuid: UUID of the object for which
           :     annotations to be added
           :   resource_type: resource type of object
           :     for which annotations to be added
           :   annotation_keys: list of annotation keys
           : Returns
           :   True, '': PASS condition
           :   (False, (<code>, msg): Fail condition
        """

        # format update dict
        updates = [{'field': 'annotations',
                    'operation': 'delete',
                    'position': annkey} for annkey in annotation_keys]
        ok, result = api_server.internal_request_prop_collection(
            obj_uuid, updates)
        if not ok:
            return False, result
        return True, ''

    @classmethod
    def _check_unique_vn_vlan_in_vpg(
        cls, db_conn, api_server,
        vpg_uuid, vn_uuid,
            vlan_id, validation):
        """Verify given vn:vlan is unique in the VPG.

        :Args
        :  db_conn: db connection object
        :  api_server: api_server connection object
        :  vpg_uuid: UUID of virtual_port_group object
        :  vn_uuid: UUID of virtual_network
        :  vlan_id: ID of the VLAN
        :  validation:
        :    enterprise
        :    serviceprovider
        :Returns
        :  True, '': PASS condition
        :  (False, (<code>, msg): Fail condition
        """
        # annotation key for current object
        ann_key = 'validation:%s/vn:%s/vlan_id:%s' % (
            validation, vn_uuid, vlan_id)
        ann_value = None
        # ensure that it do not exists already
        ok, result = cls._check_annotations(
            api_server, vpg_uuid, 'virtual_port_group', ann_key,
            ann_value, fail_if_exists=True)
        if not ok:
            annotations = result.get('annotations') or []
            for key_d, key in annotations:
                vtype, vn, vlan = key.split('/', 2)
                msg = ("VN(%s):VLAN(%s) already exists "
                       "at VPG(%s). No more than one "
                       "Allowed in Fabric %s" % (
                           vn_uuid, vlan_id, vpg_uuid, vtype))
                return (False, (400, msg))
        return True, ''

    @classmethod
    def _add_unique_vn_vlan_in_vpg(
        cls, db_conn, api_server, vpg_uuid, vn_uuid,
            vlan_id, vmi_uuid, validation):
        """Add vn:vlan as an annotation to the VPG object.

        :Args
        :  db_conn: db connection object
        :  api_server: api_server connection object
        :  vpg_uuid: UUID of virtual_port_group object
        :  vn_uuid: UUID of virtual_network
        :  vlan_id: ID of the VLAN
        :  validation:
        :    enterprise
        :    serviceprovider
        :Returns
        :  True, '': PASS condition
        :  (False, (<code>, msg): Fail condition
        """
        # annotation key for current object
        ann_key = 'validation:%s/vn:%s/vlan_id:%d' % (
            validation, vn_uuid, vlan_id)
        ann_value = vmi_uuid
        # ensure that it do not exists already
        ok, result = cls._add_annotations(
            api_server, vpg_uuid, 'virtual_port_group', ann_key,
            ann_value)
        if not ok:
            return ok, result
        return True, ''

    @classmethod
    def _check_add_one_untagged_vlan(
        cls, db_conn, api_server, vpg_uuid,
            vlan_id, is_untagged, validation):
        """Verify no untagged VLAN exists in the VPG.

           If this VLAN is untagged ensure no untagged
           VLAN exists in the VPG
        :Args
        :  db_conn: db connection object
        :  api_server: api_server connection object
        :  vpg_uuid: UUID of virtual_port_group object
        :  vlan_id: ID of the VLAN
        :  validation:
        :    enterprise
        :    serviceprovider
        :Returns
        :  True, '': PASS condition
        :  (False, (<code>, msg): Fail condition
        """
        if not is_untagged:
            return True, ''
        ann_key = 'validation:%s/untagged_vlan_id' % validation
        ann_value = '%s' % vlan_id
        # ensure that no untagged vlan annotation
        # exists already
        ok, result = cls._check_annotations(
            api_server, vpg_uuid, 'virtual_port_group', ann_key,
            ann_value, fail_if_exists=False)
        if not ok:
            return ok, result
        annotations = result.get('annotations')
        for ann_dict, _ in annotations:
            # Fail if its not the same entry
            r_ann_value = ann_dict.get('value', '')
            if not (ann_key.startswith('validation:%s' % validation) and
                    ann_key.endswith('untagged_vlan_id')):
                continue
            if r_ann_value != ann_value:
                msg = ("Untagged VLAN(%s) already exists in "
                       "VPG(%s), This Untagged VLAN(%s) "
                       "can not be allowed in (%s) Fabric " % (
                           r_ann_value, vpg_uuid, vlan_id,
                           validation.title()))
                return (False, (400, msg))
        else:
            ok, result = cls._add_annotations(
                api_server, vpg_uuid, 'virtual_port_group',
                ann_key, ann_value)
            if not ok:
                return ok, result
        return True, ''

    @classmethod
    def _check_same_vn_vlan_exists(
        cls, db_conn, api_server, vn_uuid,
            vlan_id, fabric_uuid, validation):
        """Verify only same VN and same VLAN-ID combination exists

           For enterprise style, there can be only one combination
           of vlan:vn associated to any VPGs in the fabric
        :Args
        :  db_conn: db connection object
        :  api_server: api_server connection object
        :  vn_uuid: UUID of virtual_network
        :  vlan_id: ID of the VLAN
        :  fabric_uuid: UUID of Fabric
        :  validation:
        :    enterprise
        :    serviceprovider
        :Returns
        :  True, '': PASS condition
        :  (False, (<code>, msg): Fail condition
        """
        err_msg = ('VN({e_vn}):VLAN({e_vlan}) of '
                   'existing VPG({e_vpg}) matches '
                   'with this VN({vn_uuid}):VLAN({vlan_id}) '
                   'in fabric({fabric_uuid}). Not Allowed in '
                   'fabric {validation} style')

        # helper function to sanitize vlan/vn info
        def itemformatter(item):
            if not item:
                return None
            sitem = item.split(':', 1)
            if sitem == ['']:
                return None
            else:
                return sitem[1] if sitem[1] else None

        # helper function to convert vlan to int
        def intformatter(item):
            if item and item.isdigit():
                return int(item)

        # Get VPGs in the fabric
        ok, fabric = cls.dbe_read(db_conn, 'fabric', fabric_uuid,
                                  obj_fields=['virtual_port_groups'])
        if not ok:
            return ok, fabric

        # Get all VPGs in the fabric
        all_vpgs = fabric.get('virtual_port_groups') or []

        # No VPGs in the fabric, no further validations
        if not all_vpgs:
            return True, ''

        # Read annotations from all VPGs in the fabric
        ok, results, _ = db_conn.dbe_list(
            'virtual_port_group',
            obj_uuids=[vpg['uuid'] for vpg in all_vpgs],
            field_names=['annotations'])
        if not ok:
            return ok, results

        # retrieve annotations info from VPGs
        # check if any vn or vlan matches
        for result in results:
            existing_vpg_uuid = result.get('uuid')
            annotations = result.get('annotations') or {}
            kvps = annotations.get('key_value_pair') or []
            for kvp in kvps:
                kvp_key = kvp.get('key', '')
                if (kvp_key.startswith('validation:%s' % validation) and
                        not kvp_key.endswith('untagged_vlan_id')):
                    vtype, vninfo, vlaninfo = kvp_key.split('/', 2)
                    existing_vn = itemformatter(vninfo)
                    existing_vlan = intformatter(itemformatter(vlaninfo))
                    if ((existing_vn == vn_uuid and
                        existing_vlan != vlan_id) or
                        (existing_vlan == vlan_id and
                            existing_vn != vn_uuid)):
                        msg = err_msg.format(
                            validation=validation,
                            e_vn=existing_vn,
                            e_vlan=existing_vlan,
                            e_vpg=existing_vpg_uuid,
                            vn_uuid=vn_uuid,
                            vlan_id=vlan_id,
                            fabric_uuid=fabric_uuid)
                        return False, (400, msg)
        return True, ''

    @classmethod
    def _check_serviceprovider_fabric(
        cls, db_conn, api_server, zk_vpg_lock_path,
            vpg_uuid, vn_uuid, vlan_id, vmi_uuid, is_untagged_vlan):
        """Verify Service Provider Style Fabric's restrictions.

        :Args
        :  db_conn: db connection object
        :  api_server: api_server connection object
        :  zk_vpg_lock_path: VPG ZK Path
        :  vpg_uuid: UUID of VPG
        :  vn_uuid: UUID of virtual_network
        :  vlan_id: ID of the VLAN
        :  is_untagged_vlan:
        :    True: untagged
        :    False: tagged
        :Returns
        :  True, '': PASS condition
        :  (False, (<code>, msg): Fail condition
        """
        # verify we received a valid vlan-id
        # may be a VMI update
        if not vlan_id:
            return True, ''

        # Zookeeper lock params
        zk_vpg_args = {'zookeeper_client': db_conn._zk_db._zk_client,
                       'path': zk_vpg_lock_path, 'name': vpg_uuid,
                       'timeout': 60}
        validation = 'serviceprovider'
        # VPG ZK Lock
        with ZookeeperLock(**zk_vpg_args):
            ok, result = cls._check_unique_vn_vlan_in_vpg(
                db_conn, api_server, vpg_uuid, vn_uuid,
                vlan_id, validation)
            if not ok:
                return ok, result
            ok, result = cls._check_add_one_untagged_vlan(
                db_conn, api_server, vpg_uuid, vlan_id, is_untagged_vlan,
                validation)
            if not ok:
                return ok, result
            ok, result = cls._add_unique_vn_vlan_in_vpg(
                db_conn, api_server, vpg_uuid, vn_uuid,
                vlan_id, vmi_uuid, validation)
            if not ok:
                return ok, result
        return True, ''

    @classmethod
    def _check_enterprise_fabric(
        cls, db_conn, api_server, zk_vpg_lock_path,
        zk_fabric_lock_path, vpg_uuid, fabric_uuid,
            vn_uuid, vlan_id, vmi_uuid, is_untagged_vlan):
        """Verifies Enterprise Style Fabric's restrictions.

        :Args
        :  db_conn: db connection object
        :  api_server: api_server connection object
        :  zk_vpg_lock_path: VPG ZK Path
        :  zk_fabric_lock_path: Fabric ZK Path
        :  vpg_uuid: UUID of VPG
        :  fabric_uuid: UUID of Fabric
        :  vn_uuid: UUID of virtual_network
        :  vlan_id: ID of the VLAN
        :  is_untagged_vlan:
        :    True: untagged
        :    False: tagged
        :Returns
        :  True, '': PASS condition
        :  (False, (<code>, msg): Fail condition
        """
        # check vlan-id received is valid
        # or VMI Update
        if not vlan_id:
            return True, ''

        # Zookeeper lock params
        zk_vpg_args = {'zookeeper_client': db_conn._zk_db._zk_client,
                       'path': zk_vpg_lock_path, 'name': vpg_uuid,
                       'timeout': 60}
        zk_fab_args = {'zookeeper_client': db_conn._zk_db._zk_client,
                       'path': zk_fabric_lock_path, 'name': vpg_uuid,
                       'timeout': 60}
        validation = 'enterprise'
        # VPG ZK Lock
        with ZookeeperLock(**zk_vpg_args):
            ok, result = cls._check_unique_vn_vlan_in_vpg(
                db_conn, api_server, vpg_uuid, vn_uuid,
                vlan_id, validation)
            if not ok:
                return ok, result
            ok, result = cls._check_add_one_untagged_vlan(
                db_conn, api_server, vpg_uuid, vlan_id, is_untagged_vlan,
                validation)
            if not ok:
                return ok, result
            # fabric ZK lock
            with ZookeeperLock(**zk_fab_args):
                ok, result = cls._check_same_vn_vlan_exists(
                    db_conn, api_server, vn_uuid, vlan_id, fabric_uuid,
                    validation)
                if not ok:
                    return ok, result
                ok, result = cls._add_unique_vn_vlan_in_vpg(
                    db_conn, api_server, vpg_uuid, vn_uuid,
                    vlan_id, vmi_uuid, validation)
                if not ok:
                    return ok, result
        return True, ''

    @classmethod
    def _manage_vpg_association(cls, vmi_id, api_server, db_conn, phy_links,
                                vpg_name=None, vn_uuid=None,
                                vlan_id=None, is_untagged_vlan=False):
        fabric_name = None
        phy_interface_uuids = []
        old_phy_interface_uuids = []
        new_pi_to_pr_dict = {}
        old_pi_to_pr_dict = {}
        for link in phy_links:
            if link.get('fabric'):
                if fabric_name is not None and fabric_name != link['fabric']:
                    msg = 'Physical interfaces in the same vpg '\
                          'should belong to the same fabric'
                    return (False, (400, msg))
                fabric_name = link['fabric']
            else:  # use default fabric if it's not in link local information
                fabric_name = 'default-fabric'

            phy_interface_name = link['port_id']
            prouter_name = link['switch_info']
            pi_fq_name = ['default-global-system-config', prouter_name,
                          phy_interface_name]
            pi_uuid = db_conn.fq_name_to_uuid('physical_interface', pi_fq_name)
            phy_interface_uuids.append(pi_uuid)
            new_pi_to_pr_dict[pi_uuid] = prouter_name

        # check if new physical interfaces belongs to some other vpg
        for uuid in set(phy_interface_uuids):
            ok, phy_interface_dict = db_conn.dbe_read(
                obj_type='physical-interface',
                obj_id=uuid,
                obj_fields=['virtual_port_group_back_refs'])
            if not ok:
                return (ok, 400, phy_interface_dict)

            vpg_refs = phy_interface_dict.get('virtual_port_group_back_refs')
            if vpg_refs and vpg_name and vpg_refs[0]['to'][-1] != vpg_name:
                msg = 'Physical interface %s already belong to the vpg %s' %\
                      (phy_interface_dict.get('name'), vpg_refs[0]['to'][-1])
                return (False, (400, msg))

        if vpg_name:  # read the vpg object
            vpg_fq_name = ['default-global-system-config', fabric_name,
                           vpg_name]
            try:
                vpg_uuid = db_conn.fq_name_to_uuid('virtual_port_group',
                                                   vpg_fq_name)
            except NoIdError:
                msg = 'Vpg object %s is not found' % vpg_name
                return (False, (404, msg))

            ok, vpg_dict = db_conn.dbe_read(
                obj_type='virtual-port-group', obj_id=vpg_uuid)

            if not ok:
                return (ok, 400, vpg_dict)

        else:  # create vpg object
            fabric_fq_name = [
                'default-global-system-config',
                fabric_name,
                phy_interface_uuids[0],
            ]
            vpg_id = cls.vnc_zk_client.alloc_vpg_id(':'.join(fabric_fq_name))

            def undo_vpg_id():
                cls.vnc_zk_client.free_vpg_id(vpg_id, ':'.join(fabric_fq_name))
                return True, ""
            get_context().push_undo(undo_vpg_id)

            vpg_name = "vpg-internal-" + str(vpg_id)
            vpg_obj = VirtualPortGroup(
                parent_type='fabric',
                fq_name=['default-global-system-config', fabric_name,
                         vpg_name],
                virtual_port_group_user_created=False,
                virtual_port_group_lacp_enabled=True)
            vpg_int_dict = json.dumps(vpg_obj, default=_obj_serializer_all)

            ok, resp = api_server.internal_request_create(
                'virtual-port-group', json.loads(vpg_int_dict))

            if not ok:
                return (ok, 400, resp)

            vpg_dict = resp['virtual-port-group']
            vpg_uuid = resp['virtual-port-group']['uuid']

            def undo_vpg_create():
                cls.server.internal_request_delete('virtual-port-group',
                                                   vpg_uuid)
                return True, ''
            get_context().push_undo(undo_vpg_create)

        # Check for fabric to get vpg
        if 'parent_uuid' in vpg_dict:
            ok, read_result = cls.dbe_read(
                db_conn,
                'fabric',
                vpg_dict['parent_uuid'],
                obj_fields=['fabric_enterprise_style', 'virtual_port_groups'])
            if not ok:
                return ok, read_result
            fabric = read_result
            # In cases of upgrade, the fabric_enterprise_style flag won't
            # be set. In such cases, consider the flag to be false
            fabric_enterprise_style = (fabric.get('fabric_enterprise_style') or
                                       False)
            fabric_uuid = fabric.get('uuid')

            # define zookeeper locks
            zk_fabric_lock_path = os.path.join(
                api_server.fabric_validation_lock_prefix, 'fabrics')
            zk_vpg_lock_path = os.path.join(
                api_server.fabric_validation_lock_prefix,
                'fabrics', fabric_uuid, 'vpgs')

            # VLAN-ID sanitizer
            def vlanid_sanitizer(vlanid):
                if not vlanid:
                    return None
                elif (isinstance(vlanid, type('')) and
                      vlanid.isdigit()):
                    return int(vlanid)
                elif isinstance(vlanid, int):
                    return vlanid
            vlan_id = vlanid_sanitizer(vlan_id)

            if fabric_enterprise_style:
                ok, result = cls._check_enterprise_fabric(
                    db_conn, api_server, zk_vpg_lock_path,
                    zk_fabric_lock_path, vpg_uuid, fabric_uuid,
                    vn_uuid, vlan_id, vmi_id, is_untagged_vlan)
                if not ok:
                    return ok, result
            else:
                ok, result = cls._check_serviceprovider_fabric(
                    db_conn, api_server, zk_vpg_lock_path, vpg_uuid,
                    vn_uuid, vlan_id, vmi_id, is_untagged_vlan)
                if not ok:
                    return ok, result

        old_phy_interface_refs = vpg_dict.get('physical_interface_refs')
        for ref in old_phy_interface_refs or []:
            old_pi_to_pr_dict[ref['uuid']] = {
                'prouter_name': ref['to'][1],
                'ae_id': ref['attr'].get('ae_num') if ref['attr'] else None}
            old_phy_interface_uuids.append(ref['uuid'])

        ret_dict = {}
        ret_dict['deallocated_ae_id'] = []
        ret_dict['allocated_ae_id'] = []

        # delete old physical interfaces to the vpg
        for uuid in set(old_phy_interface_uuids) - set(phy_interface_uuids):
            prouter_dict = old_pi_to_pr_dict.get(uuid)
            dealloc_dict = cls._check_and_free_ae_id(
                phy_links, prouter_dict,
                vpg_name, new_pi_to_pr_dict)
            ret_dict['deallocated_ae_id'].append(dealloc_dict)

            api_server.internal_request_ref_update(
                'virtual-port-group',
                vpg_uuid,
                'DELETE',
                'physical-interface',
                uuid)

        # add new physical interfaces to the vpg
        pr_to_ae_id = {}
        for uuid in phy_interface_uuids:
            prouter_name = new_pi_to_pr_dict.get(uuid)
            if pr_to_ae_id.get(prouter_name) is None:
                attr_obj, ae_id = cls._check_and_alloc_ae_id(
                    phy_links, prouter_name,
                    vpg_name, old_pi_to_pr_dict)
                pr_to_ae_id[prouter_name] = ae_id

                if len(phy_links) > 1 and ae_id is not None:
                    alloc_dict = {}
                    alloc_dict['ae_id'] = ae_id
                    alloc_dict['prouter_name'] = prouter_name
                    alloc_dict['vpg_name'] = vpg_name
                    ret_dict['allocated_ae_id'].append(alloc_dict)
            else:
                attr_obj = VpgInterfaceParametersType(
                    ae_num=pr_to_ae_id.get(prouter_name))

            api_server.internal_request_ref_update(
                'virtual-port-group',
                vpg_uuid,
                'ADD',
                'physical-interface',
                uuid,
                attr=attr_obj.__dict__ if attr_obj else None,
                relax_ref_for_delete=True)

        # update intent-map with vn_id
        # read intent map object
        intent_map_name = fabric_name + '-assisted-replicator-intent-map'
        intent_map_fq_name = ['default-global-system-config',
                              intent_map_name]
        try:
            intent_map_uuid = db_conn.fq_name_to_uuid('intent_map',
                                                      intent_map_fq_name)
        except NoIdError:
            msg = 'Intent Map for Assisted Replicator object %s not ' \
                  'found for fabric %s' % (intent_map_fq_name[-1],
                                           fabric_name)
            return vpg_uuid, ret_dict

        api_server.internal_request_ref_update(
            'virtual-network',
            vn_uuid,
            'ADD',
            'intent-map',
            intent_map_uuid,
            relax_ref_for_delete=True)

        return vpg_uuid, ret_dict

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                        prop_collection_updates=None, ref_update=None):
        api_server = db_conn.get_api_server()

        bindings = obj_dict.get('virtual_machine_interface_bindings', {})
        kvps = bindings.get('key_value_pair', [])

        # ADD a ref from this VMI only if it's getting created
        # first time
        vpg_uuid = obj_dict.get('port_virtual_port_group_id')
        vpg_name = obj_dict.get('virtual_port_group_name')
        if not vpg_name and vpg_uuid:
            api_server.internal_request_ref_update(
                'virtual-port-group',
                vpg_uuid,
                'ADD',
                'virtual-machine-interface',
                id,
                relax_ref_for_delete=True)

        for oper_param in prop_collection_updates or []:
            if (oper_param['field'] == 'virtual_machine_interface_bindings' and
                    oper_param['operation'] == 'set'):
                kvps.append(oper_param['value'])

        return True, ''

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        if ('virtual_machine_interface_refs' in obj_dict and
                'virtual_machine_interface_properties' in obj_dict):
            vmi_props = obj_dict['virtual_machine_interface_properties']
            if 'sub_interface_vlan_tag' not in vmi_props:
                msg = "Cannot delete vmi with existing ref to sub interface"
                return (False, (409, msg), None)

        bindings = obj_dict.get('virtual_machine_interface_bindings')
        if bindings:
            kvps = bindings.get('key_value_pair') or []
            kvp_dict = cls._kvp_to_dict(kvps)
            delete_dict = {'virtual_machine_refs': []}
            cls._check_vrouter_link(obj_dict, kvp_dict, delete_dict, db_conn)

        # Clean ae ids associated with VPG->PIs
        for vpg_back_ref in obj_dict.get('virtual_port_group_back_refs',
                                         []):
            fqname = vpg_back_ref['to']
            vpg_uuid = db_conn.fq_name_to_uuid('virtual_port_group', fqname)
            ok, vpg_dict = db_conn.dbe_read(
                obj_type='virtual-port-group',
                obj_id=vpg_uuid,
                obj_fields=['physical_interface_refs'])
            if not ok:
                return ok, vpg_dict

            notify_dict = {}
            notify_dict['deallocated_ae_id'] = []
            for pi_ref in vpg_dict.get('physical_interface_refs') or []:
                if pi_ref['attr'] and pi_ref['attr'].get('ae_num') is not None:
                    dealloc_dict = {}
                    dealloc_dict['ae_id'] = pi_ref['attr'].get('ae_num')
                    dealloc_dict['prouter_name'] = pi_ref['to'][1]
                    dealloc_dict['vpg_name'] = fqname[2]
                    notify_dict['deallocated_ae_id'].append(dealloc_dict)
            obj_dict.update(notify_dict)

        return True, "", None

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        api_server = db_conn.get_api_server()

        # For baremetal, delete the VPG object
        for vpg_back_ref in obj_dict.get('virtual_port_group_back_refs',
                                         []):
            fqname = vpg_back_ref['to']

            # Check if VPG is involved and and it's not referring to any other
            # VMI. If yes, then clean up
            vpg_uuid = db_conn.fq_name_to_uuid('virtual_port_group', fqname)
            ok, vpg_dict = db_conn.dbe_read(
                obj_type='virtual-port-group',
                obj_id=vpg_uuid,
                obj_fields=['virtual_port_group_user_created',
                            'virtual_machine_interface_refs',
                            'annotations'])
            if not ok:
                return ok, vpg_dict

            # VMI got deleted, delete VN:VLAN-ID annotation
            # from VPG
            # retrieve annotations
            vpg_annotations = vpg_dict.get('annotations') or {}
            vpg_kvps = vpg_annotations.get('key_value_pair') or []
            if vpg_kvps and vpg_dict.get('parent_type') == 'fabric':
                vmi_uuid = obj_dict.get('uuid')
                parent_uuid = vpg_dict.get('parent_uuid')

                fab_zk_path = os.path.join(
                    api_server.fabric_validation_lock_prefix,
                    'fabrics')
                zk_fab_args = {
                    'zookeeper_client': db_conn._zk_db._zk_client,
                    'path': fab_zk_path,
                    'name': parent_uuid,
                    'timeout': 60}
                # find annotation value matching VMI's UUID
                annotation_keys = []
                for kv in vpg_kvps:
                    if (kv.get('key') and kv.get('value') == vmi_uuid):
                        annotation_keys.append(kv.get('key'))
                if annotation_keys:
                    with ZookeeperLock(**zk_fab_args):
                        # remove annotation from VPG
                        cls._delete_annotations(
                            api_server, vpg_uuid, 'virtual-port-group',
                            annotation_keys)

            if (not vpg_dict.get('virtual_machine_interface_refs') and
                    vpg_dict.get('virtual_port_group_user_created') is False):

                api_server.internal_request_delete('virtual_port_group',
                                                   vpg_uuid)

            # Clean ae ids associated with VPG->PIs
            cls._notify_ae_id_modified(obj_dict)

        return True, ""

    @classmethod
    def dbe_create_notification(cls, db_conn, obj_id, obj_dict):
        cls._notify_ae_id_modified(obj_dict)

        return True, ''

    @classmethod
    def dbe_update_notification(cls, obj_id, extra_dict=None):

        if extra_dict is not None:
            cls._notify_ae_id_modified(extra_dict, notify=True)

        return True, ''

    @classmethod
    def dbe_delete_notification(cls, obj_id, obj_dict):
        cls._notify_ae_id_modified(obj_dict, notify=True)

        return True, ''
