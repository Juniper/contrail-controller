#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
#
# This file contains code/hooks at different point during processing a request,
# specific to type of resource. For eg. allocation of mac/ip-addr for a port
# during its creation.
import copy
from cfgm_common import jsonutils as json
import re
import itertools
import socket

import cfgm_common
import cfgm_common.utils
import cfgm_common.exceptions
import netaddr
import uuid
from vnc_quota import QuotaHelper

from context import get_context
from gen.resource_xsd import *
from gen.resource_common import *
from netaddr import IPNetwork
from pprint import pformat
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel


class ResourceDbMixin(object):
    generate_default_instance = True

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return True, ''

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return True, ''

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
            prop_collection_updates=None, ref_update=None):
        return True, ''

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn,
            prop_collection_updates=None, ref_update=None):
        return True, ''

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        return True, ''

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        return True, ''

    @classmethod
    def dbe_create_notification(cls, obj_ids, obj_dict):
        pass
    #end dbe_create_notification

    @classmethod
    def dbe_update_notification(cls, obj_ids):
        pass
    #end dbe_update_notification

    @classmethod
    def dbe_delete_notification(cls, obj_ids, obj_dict):
        pass
    #end dbe_delete_notification

# end class ResourceDbMixin

class Resource(ResourceDbMixin):
    server = None

    @classmethod
    def dbe_read(cls, db_conn, res_type, obj_uuid, obj_fields=None):
        try:
            ok, result = db_conn.dbe_read(res_type,
                                          {'uuid': obj_uuid}, obj_fields)
        except cfgm_common.exceptions.NoIdError:
            return (False, (404, 'No %s: %s' %(res_type, obj_uuid)))
        if not ok:
            return (False, (500, 'Error in dbe_read of %s %s: %s' %(
                                 res_type, obj_uuid, pformat(result))))

        return (True, result)
    # end dbe_read
# end class Resource

class GlobalSystemConfigServer(Resource, GlobalSystemConfig):
    @classmethod
    def _check_asn(cls, obj_dict, db_conn):
        global_asn = obj_dict.get('autonomous_system')
        if not global_asn:
            return (True, '')
        (ok, result) = db_conn.dbe_list('virtual-network')
        if not ok:
            return (ok, (500, 'Error in dbe_list: %s' %(result)))
        for vn_name, vn_uuid in result:
            ok, result = cls.dbe_read(db_conn, 'virtual-network', vn_uuid,
                                      obj_fields=['route_target_list'])

            if not ok:
                code, msg = result
                if code == 404:
                    continue
                return ok, (code, 'Error checking ASN: %s' %(msg))

            rt_dict = result.get('route_target_list', {})
            for rt in rt_dict.get('route_target', []):
                (_, asn, target) = rt.split(':')
                if (int(asn) == global_asn and
                    int(target) >= cfgm_common.BGP_RTGT_MIN_ID):
                    return (False, (400, "Virtual network %s is configured "
                            "with a route target with this ASN and route "
                            "target value in the same range as used by "
                            "automatically allocated route targets" % vn_name))
        return (True, '')
    # end _check_asn

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        ok, result = cls._check_asn(obj_dict, db_conn)
        if not ok:
            return ok, result
        return True, ''
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls._check_asn(obj_dict, db_conn)
        if not ok:
            return ok, result
        return True, ''
    # end pre_dbe_update

# end class GlobalSystemConfigServer


class FloatingIpServer(Resource, FloatingIp):
    generate_default_instance = False

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        if 'project_refs' not in obj_dict:
            return False, (400, 'Floating Ip should have project reference')

        proj_dict = obj_dict['project_refs'][0]
        if 'uuid' in proj_dict:
            proj_uuid = proj_dict['uuid']
        else:
            proj_uuid = db_conn.fq_name_to_uuid('project', proj_dict['to'])

        user_visibility = obj_dict['id_perms'].get('user_visible', True)
        verify_quota_kwargs = {'db_conn': db_conn,
                               'fq_name': obj_dict['fq_name'],
                               'resource': 'floating_ip_back_refs',
                               'obj_type': 'floating-ip',
                               'user_visibility': user_visibility,
                               'proj_uuid': proj_uuid}
        (ok, response) = QuotaHelper.verify_quota_for_resource(
            **verify_quota_kwargs)

        if not ok:
            return (ok, response)

        vn_fq_name = obj_dict['fq_name'][:-2]
        req_ip = obj_dict.get("floating_ip_address")
        if req_ip and cls.addr_mgmt.is_ip_allocated(req_ip, vn_fq_name):
            return (False, (403, 'Ip address already in use'))
        try:
            fip_addr = cls.addr_mgmt.ip_alloc_req(vn_fq_name,
                                                  asked_ip_addr=req_ip,
                                                  alloc_id=obj_dict['uuid'])
            def undo():
                db_conn.config_log(
                    'AddrMgmt: free FIP %s for vn=%s tenant=%s, on undo'
                        % (fip_addr, vn_fq_name, tenant_name),
                           level=SandeshLevel.SYS_DEBUG)
                cls.addr_mgmt.ip_free_req(fip_addr, vn_fq_name)
                return True, ""
            # end undo
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (500, str(e)))

        obj_dict['floating_ip_address'] = fip_addr
        db_conn.config_log('AddrMgmt: alloc %s FIP for vn=%s, tenant=%s, askip=%s' \
            % (obj_dict['floating_ip_address'], vn_fq_name, tenant_name,
               req_ip), level=SandeshLevel.SYS_DEBUG)

        return True, ""
    # end pre_dbe_create


    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        vn_fq_name = obj_dict['fq_name'][:-2]
        fip_addr = obj_dict['floating_ip_address']
        db_conn.config_log('AddrMgmt: free FIP %s for vn=%s'
                           % (fip_addr, vn_fq_name),
                           level=SandeshLevel.SYS_DEBUG)
        cls.addr_mgmt.ip_free_req(fip_addr, vn_fq_name)

        return True, ""
    # end post_dbe_delete


    @classmethod
    def dbe_create_notification(cls, obj_ids, obj_dict):
        fip_addr = obj_dict['floating_ip_address']
        vn_fq_name = obj_dict['fq_name'][:-2]
        cls.addr_mgmt.ip_alloc_notify(fip_addr, vn_fq_name)
    # end dbe_create_notification

    @classmethod
    def dbe_delete_notification(cls, obj_ids, obj_dict):
        fip_addr = obj_dict['floating_ip_address']
        vn_fq_name = obj_dict['fq_name'][:-2]
        cls.addr_mgmt.ip_free_notify(fip_addr, vn_fq_name)
    # end dbe_delete_notification

# end class FloatingIpServer


class InstanceIpServer(Resource, InstanceIp):
    generate_default_instance = False

    @classmethod
    def _get_subnet_name(cls, vn_dict, subnet_uuid):
        ipam_refs = vn_dict.get('network_ipam_refs', [])
        subnet_name = None
        for ipam in ipam_refs:
            ipam_subnets = ipam['attr'].get('ipam_subnets', [])
            for subnet in ipam_subnets:
                if subnet['subnet_uuid'] == subnet_uuid:
                    subnet_dict = subnet['subnet']
                    subnet_name = subnet_dict['ip_prefix'] + '/' + str(
                                  subnet_dict['ip_prefix_len'])
                    return subnet_name

    @classmethod
    def _is_gateway_ip(cls, vn_dict, ip_addr):
        ipam_refs = vn_dict.get('network_ipam_refs', [])
        for ipam in ipam_refs:
            ipam_subnets = ipam['attr'].get('ipam_subnets', [])
            for subnet in ipam_subnets:
                if subnet['default_gateway'] == ip_addr:
                    return True

        return False

    @classmethod
    def _vmi_has_vm_ref(cls, db_conn, iip_dict):
        # is this iip linked to a vmi that is not ref'd by a router
        vmi_refs = iip_dict.get('virtual_machine_interface_refs')
        for vmi_ref in vmi_refs or []:
            ok, result = cls.dbe_read(db_conn, 'virtual-machine-interface',
                                      vmi_ref['uuid'],
                                      obj_fields=['virtual_machine_refs'])
            if not ok:
                continue
            if result.get('virtual_machine_refs'):
                return True
        # end for all vmi ref
        return False
    # end _vmi_has_vm_ref

    @classmethod
    def is_gateway_ip(cls, db_conn, iip_uuid):
        ok, iip_dict = cls.dbe_read(db_conn, 'instance-ip', iip_uuid)
        if not ok:
            return False

        try:
            vn_fq_name = iip_dict['virtual_network_refs'][0]['to']
            vn_uuid = iip_dict['virtual_network_refs'][0]['uuid']
            iip_addr = iip_dict['instance_ip_address']
        except Exception:
            return False
        if ((vn_fq_name == cfgm_common.IP_FABRIC_VN_FQ_NAME) or
                (vn_fq_name == cfgm_common.LINK_LOCAL_VN_FQ_NAME)):
            # Ignore ip-fabric and link-local address allocations
            return False

        ok, vn_dict = cls.dbe_read(db_conn, 'virtual-network', vn_uuid,
                                   ['network_ipam_refs'])
        if not ok:
            return False

        return cls._is_gateway_ip(vn_dict, iip_addr)
    # end is_gateway_ip

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        vn_fq_name = obj_dict['virtual_network_refs'][0]['to']
        if ((vn_fq_name == cfgm_common.IP_FABRIC_VN_FQ_NAME) or
                (vn_fq_name == cfgm_common.LINK_LOCAL_VN_FQ_NAME)):
            # Ignore ip-fabric and link-local address allocations
            return True,  ""

        req_ip = obj_dict.get("instance_ip_address", None)

        vn_id = db_conn.fq_name_to_uuid('virtual-network', vn_fq_name)
        ok, result = cls.dbe_read(db_conn, 'virtual-network', vn_id,
                         obj_fields=['router_external', 'network_ipam_refs'])
        if not ok:
            return ok, result

        vn_dict = result
        subnet_uuid = obj_dict.get('subnet_uuid', None)
        sub = cls._get_subnet_name(vn_dict, subnet_uuid) if subnet_uuid else None
        if subnet_uuid and not sub:
            return (False, (404, "Subnet id " + subnet_uuid + " not found"))

        req_ip_family = obj_dict.get("instance_ip_family", None)
        if req_ip is None and subnet_uuid:
            # pickup the version from subnet
            req_ip_version = IPNetwork(sub).version
        else:
            req_ip_version = 4 # default ip v4

        if req_ip_family == "v6": req_ip_version = 6

        # if request has ip and not g/w ip, report if already in use.
        # for g/w ip, creation allowed but only can ref to router port.
        if req_ip and cls.addr_mgmt.is_ip_allocated(req_ip, vn_fq_name):
            if not cls._is_gateway_ip(vn_dict, req_ip):
                return (False, (403, 'Ip address already in use'))
            elif cls._vmi_has_vm_ref(db_conn, obj_dict):
                return (False, 
                    (403, 'Gateway IP cannot be used by VM port'))
        # end if request has ip addr

        try:
            ip_addr = cls.addr_mgmt.ip_alloc_req(
                vn_fq_name, vn_dict=vn_dict, sub=sub, asked_ip_addr=req_ip,
                asked_ip_version=req_ip_version,
                alloc_id=obj_dict['uuid'])

            def undo():
                db_conn.config_log('AddrMgmt: free IP %s, vn=%s tenant=%s on post fail'
                                   % (ip_addr, vn_fq_name, tenant_name),
                                   level=SandeshLevel.SYS_DEBUG)
                cls.addr_mgmt.ip_free_req(ip_addr, vn_fq_name)
                return True, ""
            # end undo
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (500, str(e)))
        obj_dict['instance_ip_address'] = ip_addr
        db_conn.config_log('AddrMgmt: alloc %s for vn=%s, tenant=%s, askip=%s'
            % (obj_dict['instance_ip_address'],
               vn_fq_name, tenant_name, req_ip),
            level=SandeshLevel.SYS_DEBUG)
        return True, ""
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                       prop_collection_updates=None, **kwargs):
        # if instance-ip is of g/w ip, it cannot refer to non router port
        req_iip_dict = obj_dict
        ok, result = cls.dbe_read(db_conn, 'instance-ip', id,
                                  obj_fields=['virtual_network_refs'])
        if not ok:
            return ok, result
        db_iip_dict = result

        vn_uuid = db_iip_dict['virtual_network_refs'][0]['uuid']
        vn_fq_name = db_iip_dict['virtual_network_refs'][0]['to']
        if ((vn_fq_name == cfgm_common.IP_FABRIC_VN_FQ_NAME) or
                (vn_fq_name == cfgm_common.LINK_LOCAL_VN_FQ_NAME)):
            # Ignore ip-fabric and link-local address allocations
            return True,  ""

        ok, result = cls.dbe_read(db_conn, 'virtual-network',
                                  vn_uuid,
                                  obj_fields=['network_ipam-refs'])
        if not ok:
            return ok, result

        vn_dict = result
        if cls._is_gateway_ip(vn_dict,
                              db_iip_dict.get('instance_ip_address')):
            if cls._vmi_has_vm_ref(db_conn, req_iip_dict):
                return (False, (403, 'Gateway IP cannot be used by VM port'))
        # end if gateway ip

        return True, ""
    # end pre_dbe_update

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        vn_fq_name = obj_dict['virtual_network_refs'][0]['to']
        if ((vn_fq_name == cfgm_common.IP_FABRIC_VN_FQ_NAME) or
                (vn_fq_name == cfgm_common.LINK_LOCAL_VN_FQ_NAME)):
            # Ignore ip-fabric and link-local address allocations
            return True,  ""

        ip_addr = obj_dict.get('instance_ip_address')
        if not ip_addr:
            db_conn.config_log('instance_ip_address missing for object %s'
                           % (obj_dict['uuid']),
                           level=SandeshLevel.SYS_NOTICE)
            return True, ""

        db_conn.config_log('AddrMgmt: free IP %s, vn=%s'
                           % (ip_addr, vn_fq_name),
                           level=SandeshLevel.SYS_DEBUG)
        cls.addr_mgmt.ip_free_req(ip_addr, vn_fq_name)

        return True, ""
    # end post_dbe_delete

    @classmethod
    def dbe_create_notification(cls, obj_ids, obj_dict):
        ip_addr = obj_dict['instance_ip_address']
        vn_fq_name = obj_dict['virtual_network_refs'][0]['to']
        cls.addr_mgmt.ip_alloc_notify(ip_addr, vn_fq_name)
    # end dbe_create_notification

    @classmethod
    def dbe_delete_notification(cls, obj_ids, obj_dict):
        try:
            ip_addr = obj_dict['instance_ip_address']
        except KeyError:
            return
        vn_fq_name = obj_dict['virtual_network_refs'][0]['to']
        cls.addr_mgmt.ip_free_notify(ip_addr, vn_fq_name)
    # end dbe_delete_notification

# end class InstanceIpServer


class LogicalRouterServer(Resource, LogicalRouter):
    generate_default_instance = False

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        user_visibility = obj_dict['id_perms'].get('user_visible', True)
        verify_quota_kwargs = {'db_conn': db_conn,
                               'fq_name': obj_dict['fq_name'],
                               'resource': 'logical_routers',
                               'obj_type': 'logical-router',
                               'user_visibility': user_visibility}

        return QuotaHelper.verify_quota_for_resource(**verify_quota_kwargs)
    # end pre_dbe_create

# end class LogicalRouterServer


class VirtualMachineInterfaceServer(Resource, VirtualMachineInterface):
    generate_default_instance = False

    portbindings = {}
    portbindings['VIF_TYPE_VROUTER'] = 'vrouter'
    portbindings['VIF_TYPE_HW_VEB'] = 'hw_veb'
    portbindings['VNIC_TYPE_NORMAL'] = 'normal'
    portbindings['VNIC_TYPE_DIRECT'] = 'direct'
    portbindings['PORT_FILTER'] = True

    @staticmethod
    def _kvp_to_dict(kvps):
        return dict((kvp['key'], kvp['value']) for kvp in kvps)
    # end _kvp_to_dict

    @classmethod
    def _check_vrouter_link(cls, vmi_data, kvp_dict, obj_dict, db_conn):
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
            vrouter_id = db_conn.fq_name_to_uuid('virtual-router', vrouter_fq_name)
        except cfgm_common.exceptions.NoIdError:
            return

        #if virtual_machine_refs is an empty list delete vrouter link
        if 'virtual_machine_refs' in obj_dict and not obj_dict['virtual_machine_refs']:
            cls.server.internal_request_ref_update('virtual-router',
                                    vrouter_id, 'DELETE',
                                    'virtual_machine',vm_refs[0]['uuid'])
            return

        cls.server.internal_request_ref_update('virtual-router',
                               vrouter_id, 'ADD',
                               'virtual_machine', vm_refs[0]['uuid'])

    # end _check_vrouter_link

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        vn_dict = obj_dict['virtual_network_refs'][0]
        vn_uuid = vn_dict.get('uuid')
        if not vn_uuid:
            vn_fq_name = vn_dict.get('to')
            if not vn_fq_name:
                msg = 'Bad Request: Reference should have uuid or fq_name: %s'\
                      %(pformat(vn_dict))
                return (False, (400, msg))
            vn_uuid = db_conn.fq_name_to_uuid('virtual-network', vn_fq_name)

        ok, result = cls.dbe_read(db_conn, 'virtual-network', vn_uuid,
                                  obj_fields=['parent_uuid'])
        if not ok:
            return ok, result

        vn_dict = result
        proj_uuid = vn_dict['parent_uuid']
        user_visibility = obj_dict['id_perms'].get('user_visible', True)
        verify_quota_kwargs = {'db_conn': db_conn,
                               'fq_name': obj_dict['fq_name'],
                               'resource': 'virtual_machine_interfaces',
                               'obj_type': 'virtual-machine-interface',
                               'user_visibility': user_visibility,
                               'proj_uuid': proj_uuid}

        (ok, response) = QuotaHelper.verify_quota_for_resource(
            **verify_quota_kwargs)

        if not ok:
            return (ok, response)

        inmac = None
        if 'virtual_machine_interface_mac_addresses' in obj_dict:
            mc = obj_dict['virtual_machine_interface_mac_addresses']
            if 'mac_address' in mc:
                if len(mc['mac_address']) == 1:
                    inmac = [m.replace("-",":") for m in mc['mac_address']]
        if inmac != None:
            mac_addrs_obj = MacAddressesType(inmac)
        else:
            mac_addr = cls.addr_mgmt.mac_alloc(obj_dict)
            mac_addrs_obj = MacAddressesType([mac_addr])
        mac_addrs_json = json.dumps(
            mac_addrs_obj,
            default=lambda o: dict((k, v)
                                   for k, v in o.__dict__.iteritems()))
        mac_addrs_dict = json.loads(mac_addrs_json)
        obj_dict['virtual_machine_interface_mac_addresses'] = mac_addrs_dict

        aap_config = obj_dict.get(
            'virtual_machine_interface_allowed_address_pairs', {})
        for aap in aap_config.get('allowed_address_pair', []):
            if not aap.get('mac', None):
                aap['mac'] = mac_addrs_dict['mac_address'][0]

        if 'virtual_machine_interface_bindings' in obj_dict:
            bindings = obj_dict['virtual_machine_interface_bindings']
            kvps = bindings['key_value_pair']
            kvp_dict = cls._kvp_to_dict(kvps)

            if kvp_dict.get('vnic_type') == cls.portbindings['VNIC_TYPE_DIRECT']:
                if not 'provider_properties' in  vn_dict:
                    msg = 'No provider details in direct port'
                    return (False, (400, msg))
                kvp_dict['vif_type'] = cls.portbindings['VIF_TYPE_HW_VEB']
                vif_type = {'key': 'vif_type',
                            'value': cls.portbindings['VIF_TYPE_HW_VEB']}
                kvps.append(vif_type)
                vlan = vn_dict['provider_properties']['segmentation_id']
                vif_params = {'port_filter': cls.portbindings['PORT_FILTER'],
                              'vlan': str(vlan)}
                vif_details = {'key': 'vif_details', 'value': vif_params}
                kvps.append(vif_details)

            if 'vif_type' not in kvp_dict:
                vif_type = {'key': 'vif_type',
                            'value': cls.portbindings['VIF_TYPE_VROUTER']}
                kvps.append(vif_type)

            if 'vnic_type' not in kvp_dict:
                vnic_type = {'key': 'vnic_type',
                             'value': cls.portbindings['VNIC_TYPE_NORMAL']}
                kvps.append(vnic_type)

        return True, ""
    # end pre_dbe_create

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # Create ref to native/vn-default routing instance
        vn_refs = obj_dict.get('virtual_network_refs', [])
        if not vn_refs:
            return True, ''

        vn_fq_name = vn_refs[0].get('to')
        if not vn_fq_name:
            vn_uuid = vn_refs[0]['uuid']
            vn_fq_name = db_conn.uuid_to_fq_name(vn_uuid)

        ri_fq_name = vn_fq_name[:]
        ri_fq_name.append(vn_fq_name[-1])
        ri_uuid = db_conn.fq_name_to_uuid(
            'routing-instance', ri_fq_name)

        attr = PolicyBasedForwardingRuleType(direction="both")
        attr_as_dict = attr.__dict__
        cls.server.internal_request_ref_update(
            'virtual-machine-interface', obj_dict['uuid'], 'ADD',
            'routing-instance', ri_uuid,
            attr_as_dict)

        return True, ''
    # end post_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn,
                       prop_collection_updates=None, **kwargs):

        ok, read_result = cls.dbe_read(
                              db_conn, 'virtual-machine-interface', id)
        if not ok:
            return ok, read_result

        # check if vmi is going to point to vm and if its using
        # gateway address in iip, disallow
        for iip_ref in read_result.get('instance_ip_back_refs') or []:
            if (obj_dict.get('virtual_machine_refs') and
                InstanceIpServer.is_gateway_ip(db_conn, iip_ref['uuid'])):
                return (False, (403, 'Gateway IP cannot be used by VM port'))

        if ('virtual_machine_interface_refs' in obj_dict and
                'virtual_machine_interface_refs' in read_result):
            for ref in read_result['virtual_machine_interface_refs']:
                if ref not in obj_dict['virtual_machine_interface_refs']:
                    # Dont allow remove of vmi ref during update
                    msg = "VMI ref delete not allowed during update"
                    return (False, (409, msg))

        aap_config = obj_dict.get(
            'virtual_machine_interface_allowed_address_pairs', {})
        for aap in aap_config.get('allowed_address_pair', []):
            if not aap.get('mac', None):
                aap['mac'] = read_result[
                    'virtual_machine_interface_mac_addresses']['mac_address'][0]

        bindings = read_result.get('virtual_machine_interface_bindings', {})
        kvps = bindings.get('key_value_pair', [])
        kvp_dict = cls._kvp_to_dict(kvps)
        old_vnic_type = kvp_dict.get('vnic_type', cls.portbindings['VNIC_TYPE_NORMAL'])

        bindings = obj_dict.get('virtual_machine_interface_bindings', {})
        kvps = bindings.get('key_value_pair', [])

        for oper_param in prop_collection_updates or []:
            if (oper_param['field'] == 'virtual_machine_interface_bindings' and
                    oper_param['operation'] == 'set'):
                kvps.append(oper_param['value'])

        if kvps:
            kvp_dict = cls._kvp_to_dict(kvps)
            new_vnic_type = kvp_dict.get('vnic_type', old_vnic_type)
            if (old_vnic_type  != new_vnic_type):
                return (False, (409, "Vnic_type can not be modified"))

        if old_vnic_type == cls.portbindings['VNIC_TYPE_DIRECT']:
            cls._check_vrouter_link(read_result, kvp_dict, obj_dict, db_conn)

        return True, ""
    # end pre_dbe_update

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        if ('virtual_machine_interface_refs' in obj_dict and
               'virtual_machine_interface_properties' in obj_dict):
            vmi_props = obj_dict['virtual_machine_interface_properties']
            if 'sub_interface_vlan_tag' not in vmi_props:
                msg = "Cannot delete vmi with existing ref to sub interface"
                return (False, (409, msg))

        bindings = obj_dict.get('virtual_machine_interface_bindings')
        if bindings:
            kvps = bindings.get('key_value_pair', [])
            kvp_dict = cls._kvp_to_dict(kvps)
            delete_dict = {'virtual_machine_refs' : []}
            cls._check_vrouter_link(obj_dict, kvp_dict, delete_dict, db_conn)

        return True, ""
    # end pre_dbe_delete

# end class VirtualMachineInterfaceServer

class ServiceApplianceSetServer(Resource, ServiceApplianceSet):
    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        (ok, result) = db_conn.dbe_list('loadbalancer-pool', back_ref_uuids=[id])
        if not ok:
            return (ok, (500, 'Error in dbe_list: %s' %(result)))
        if len(result) > 0:
            msg = "Service appliance set can not be updated as loadbalancer "\
                  "pools are using it"
            return (False, (409, msg))
        return True, ""
    # end pre_dbe_update
# end class ServiceApplianceSetServer

class VirtualNetworkServer(Resource, VirtualNetwork):

    @classmethod
    def _check_route_targets(cls, obj_dict, db_conn):
        if 'route_target_list' not in obj_dict:
            return (True, '')
        config_uuid = db_conn.fq_name_to_uuid('global_system_config', ['default-global-system-config'])
        config = db_conn.uuid_to_obj_dict(config_uuid)
        global_asn = config.get('prop:autonomous_system')
        if not global_asn:
            return (True, '')
        global_asn = json.loads(global_asn)
        rt_dict = obj_dict.get('route_target_list')
        if not rt_dict:
            return (True, '')
        for rt in rt_dict.get('route_target', []):
            try:
                (prefix, asn, target) = rt.split(':')
                if prefix != 'target':
                    raise ValueError()
                target = int(target)
                if not asn.isdigit():
                    netaddr.IPAddress(asn)
            except (ValueError, netaddr.core.AddrFormatError) as e:
                 return (False, "Route target must be of the format "
                         "'target:<asn>:<number>' or 'target:<ip>:number'")
            if asn == global_asn and target >= cfgm_common.BGP_RTGT_MIN_ID:
                 return (False, "Configured route target must use ASN that is "
                         "different from global ASN or route target value must"
                         " be less than %d" % cfgm_common.BGP_RTGT_MIN_ID)

        return (True, '')
    # end _check_route_targets

    @classmethod
    def _check_provider_details(cls, obj_dict, db_conn, create):

        properties = obj_dict.get('provider_properties')
        if not properties:
            return (True, '')

        if not create:
            ok, result = cls.dbe_read(db_conn, 'virtual-network',
                             obj_dict['uuid'],
                             obj_fields=['virtual_machine_interface_back_refs',
                                         'provider_properties'])
            if not ok:
                return ok, result

            old_properties = result.get('provider_properties')
            if 'virtual_machine_interface_back_refs' in result:
                if old_properties != properties:
                    return (False, "Provider values can not be changed when VMs are already using")

            if old_properties:
                if not properties.get('segmentation_id'):
                    properties['segmentation_id'] = old_properties.get('segmentation_id')

                if not properties.get('physical_network'):
                    properties['physical_network'] = old_properties.get('physical_network')

        if not properties.get('segmentation_id'):
            return (False, "Segmenation id must be configured")

        if not properties.get('physical_network'):
            return (False, "physical network must be configured")

        return (True, '')

    @classmethod
    def _is_multi_policy_service_chain_supported(cls, obj_dict, read_result=None):
        if not ('multi_policy_service_chains_enabled' in obj_dict or
                'route_target_list' in obj_dict or
                'import_route_target_list' in obj_dict or
                'export_route_target_list' in obj_dict):
            return (True, '')

        # Create Request
        if not read_result:
            read_result = {}

        result_obj_dict = copy.deepcopy(read_result)
        result_obj_dict.update(obj_dict)
        if result_obj_dict.get('multi_policy_service_chains_enabled'):
            import_export_targets = result_obj_dict.get('route_target_list', {})
            import_targets = result_obj_dict.get('import_route_target_list', {})
            export_targets = result_obj_dict.get('export_route_target_list', {})
            import_targets_set = set(import_targets.get('route_target', []))
            export_targets_set = set(export_targets.get('route_target', []))
            targets_in_both_import_and_export = \
                    import_targets_set.intersection(export_targets_set)
            if (import_export_targets.get('route_target', []) or
                    targets_in_both_import_and_export):
                msg = "Multi policy service chains are not supported, "
                msg += "with both import export external route targets"
                return (False, (409, msg))

        return (True, '')

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        (ok, response) = cls._is_multi_policy_service_chain_supported(obj_dict)
        if not ok:
            return (ok, response)
        user_visibility = obj_dict['id_perms'].get('user_visible', True)
        verify_quota_kwargs = {'db_conn': db_conn,
                               'fq_name': obj_dict['fq_name'],
                               'resource': 'virtual_networks',
                               'obj_type': 'virtual-network',
                               'user_visibility': user_visibility}

        (ok, response) = QuotaHelper.verify_quota_for_resource(
            **verify_quota_kwargs)
        if not ok:
            return (ok, response)

        db_conn.update_subnet_uuid(obj_dict)

        (ok, result) = cls.addr_mgmt.net_check_subnet_overlap(obj_dict)
        if not ok:
            return (ok, (409, result))

        (ok, result) = cls.addr_mgmt.net_check_subnet(obj_dict)
        if not ok:
            return (ok, (409, result))

        (ok, error) =  cls._check_route_targets(obj_dict, db_conn)
        if not ok:
            return (False, (400, error))

        (ok, error) = cls._check_provider_details(obj_dict, db_conn, True)
        if not ok:
            return (False, (400, error))

        try:
            cls.addr_mgmt.net_create_req(obj_dict)
            def undo():
                cls.addr_mgmt.net_delete_req(obj_dict)
                return True, ""
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (500, str(e)))

        return True, ""
    # end pre_dbe_create

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        # Create native/vn-default routing instance
        ri_fq_name = obj_dict['fq_name'][:]
        ri_fq_name.append(obj_dict['fq_name'][-1])
        ri_obj = RoutingInstance(
            parent_type='virtual-network', fq_name=ri_fq_name,
            routing_instance_is_default=True)

        cls.server.internal_request_create(
            'routing-instance',
            ri_obj.serialize_to_json())

        return True, ''
    # end post_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        if ((fq_name == cfgm_common.IP_FABRIC_VN_FQ_NAME) or
                (fq_name == cfgm_common.LINK_LOCAL_VN_FQ_NAME)):
            # Ignore ip-fabric subnet updates
            return True,  ""

        (ok, error) =  cls._check_route_targets(obj_dict, db_conn)
        if not ok:
            return (False, (400, error))

        (ok, error) = cls._check_provider_details(obj_dict, db_conn, False)
        if not ok:
            return (False, (409, error))

        if 'network_ipam_refs' not in obj_dict:
            # NOP for addr-mgmt module
            return True,  ""

        ok, read_result = cls.dbe_read(db_conn, 'virtual-network', id,
                                       obj_fields=['network_ipam_refs'])
        if not ok:
            return ok, read_result

        (ok, response) = cls._is_multi_policy_service_chain_supported(obj_dict,
                                                                      read_result)
        if not ok:
            return (ok, response)

        (ok, result) = cls.addr_mgmt.net_check_subnet(obj_dict)
        if not ok:
            return (ok, (409, result))

        (ok, result) = cls.addr_mgmt.net_check_subnet_quota(read_result,
                                                            obj_dict, db_conn)
        if not ok:
            return (ok, (403, result))
        (ok, result) = cls.addr_mgmt.net_check_subnet_overlap(obj_dict)
        if not ok:
            return (ok, (409, result))
        (ok, result) = cls.addr_mgmt.net_check_subnet_delete(read_result,
                                                             obj_dict)
        if not ok:
            return (ok, (409, result))

        db_conn.update_subnet_uuid(obj_dict)

        try:
            cls.addr_mgmt.net_update_req(fq_name, read_result, obj_dict, id)
            def undo():
                # failed => update with flipped values for db_dict and req_dict
                cls.addr_mgmt.net_update_req(fq_name, obj_dict, read_result, id)
            # end undo
            get_context().push_undo(undo)
        except Exception as e:
            return (False, (500, str(e)))

        return True, ""
    # end pre_dbe_update


    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        cls.addr_mgmt.net_delete_req(obj_dict)
        def undo():
            cls.addr_mgmt.net_create_req(obj_dict)
        get_context().push_undo(undo)
        return True, ""
    # end pre_dbe_delete

    @classmethod
    def post_dbe_delete(cls, id, obj_dict, db_conn):
        # Delete native/vn-default routing instance
        # For this find backrefs and remove their ref to RI
        ri_fq_name = obj_dict['fq_name'][:]
        ri_fq_name.append(obj_dict['fq_name'][-1])
        ri_uuid = db_conn.fq_name_to_uuid(
            'routing-instance', ri_fq_name)

        backref_fields = RoutingInstance.backref_fields
        children_fields = RoutingInstance.children_fields
        ok, result = cls.dbe_read(db_conn,
            'routing-instance', ri_uuid,
            obj_fields=backref_fields|children_fields)
        if not ok:
            return ok, result

        ri_obj_dict = result
        backref_field_types = RoutingInstance.backref_field_types
        for backref_field in backref_fields:
            obj_type = backref_field_types[backref_field][0]
            def drop_ref(obj_uuid):
                # drop ref from ref_uuid to ri_uuid
                cls.server.internal_request_ref_update(
                    obj_type, obj_uuid, 'DELETE',
                    'routing-instance', ri_uuid)
            # end drop_ref
            for backref in ri_obj_dict.get(backref_field, []):
                drop_ref(backref['uuid'])

        children_field_types = RoutingInstance.children_field_types
        for child_field in children_fields:
            obj_type = children_field_types[child_field][0]
            for child in ri_obj_dict.get(child_field, []):
                cls.server.internal_request_delete(obj_type, child['uuid'])

        cls.server.internal_request_delete('routing-instance', ri_uuid)

        return True, ""
    # end post_dbe_delete


    @classmethod
    def ip_alloc(cls, vn_fq_name, subnet_name, count, family=None):
        ip_version = 6 if family == 'v6' else 4
        ip_list = [cls.addr_mgmt.ip_alloc_req(vn_fq_name, sub=subnet_name,
                                              asked_ip_version=ip_version,
                                              alloc_id='user-opaque-alloc')
                   for i in range(count)]
        msg = 'AddrMgmt: reserve %d IP for vn=%s, subnet=%s - %s' \
            % (count, vn_fq_name, subnet_name if subnet_name else '', ip_list)
        cls.addr_mgmt.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        return {'ip_addr': ip_list}
    # end ip_alloc

    @classmethod
    def ip_free(cls, vn_fq_name, subnet_name, ip_list):
        msg = 'AddrMgmt: release IP %s for vn=%s, subnet=%s' \
            % (ip_list, vn_fq_name, subnet_name if subnet_name else '')
        cls.addr_mgmt.config_log(msg, level=SandeshLevel.SYS_DEBUG)
        for ip_addr in ip_list:
            cls.addr_mgmt.ip_free_req(ip_addr, vn_fq_name, subnet_name)
    # end ip_free

    @classmethod
    def subnet_ip_count(cls, vn_fq_name, subnet_list):
        ip_count_list = []
        for item in subnet_list:
            ip_count_list.append(cls.addr_mgmt.ip_count_req(vn_fq_name, item))
        return {'ip_count_list': ip_count_list}
    # end subnet_ip_count

    @classmethod
    def dbe_create_notification(cls, obj_ids, obj_dict):
        cls.addr_mgmt.net_create_notify(obj_ids, obj_dict)
    # end dbe_create_notification

    @classmethod
    def dbe_update_notification(cls, obj_ids):
        cls.addr_mgmt.net_update_notify(obj_ids)
    # end dbe_update_notification

    @classmethod
    def dbe_delete_notification(cls, obj_ids, obj_dict):
        cls.addr_mgmt.net_delete_notify(obj_ids, obj_dict)
    # end dbe_update_notification

# end class VirtualNetworkServer


class NetworkIpamServer(Resource, NetworkIpam):
    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        user_visibility = obj_dict['id_perms'].get('user_visible', True)
        verify_quota_kwargs = {'db_conn': db_conn,
                               'fq_name': obj_dict['fq_name'],
                               'resource': 'network_ipams',
                               'obj_type': 'network-ipam',
                               'user_visibility': user_visibility}

        return QuotaHelper.verify_quota_for_resource(
            **verify_quota_kwargs)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, read_result = cls.dbe_read(db_conn, 'network-ipam', id)
        if not ok:
            return ok, read_result

        old_ipam_mgmt = read_result.get('network_ipam_mgmt')
        new_ipam_mgmt = obj_dict.get('network_ipam_mgmt')
        if not old_ipam_mgmt or not new_ipam_mgmt:
            return True, ""
        old_dns_method = old_ipam_mgmt.get('ipam_dns_method')
        new_dns_method = new_ipam_mgmt.get('ipam_dns_method')
        if not cls.is_change_allowed(old_dns_method, new_dns_method,
                                     read_result, db_conn):
            return (False, (409, "Cannot change DNS Method " +
                    " with active VMs referring to the IPAM"))
        return True, ""
    # end pre_dbe_update

    @classmethod
    def is_change_allowed(cls, old, new, obj_dict, db_conn):
        active_vm_present = cls.is_active_vm_present(obj_dict, db_conn)
        if active_vm_present:
            if (old == "default-dns-server" or old == "virtual-dns-server"):
                if (new == "tenant-dns-server" or new == None):
                    return False
            if (old == "tenant-dns-server" and new != old):
                return False
            if (old == None and new != old):
                return False
        return True
    # end is_change_allowed

    @classmethod
    def is_active_vm_present(cls, obj_dict, db_conn):
        for vn in obj_dict.get('virtual_network_back_refs') or []:
            ok, result = cls.dbe_read(db_conn, 'virtual-network', vn['uuid'],
                            obj_fields=['virtual_machine_interface_back_refs'])
            if not ok:
                code, msg = result
                if code == 404:
                    continue
                db_conn.config_log('Error in active vm check %s'
                                   %(result),
                                   level=SandeshLevel.SYS_ERR)
                # Cannot determine, err on side of caution
                return True
            if read_result.get('virtual_machine_interface_back_refs'):
                return True
        return False
    # end is_active_vm_present

# end class NetworkIpamServer


class VirtualDnsServer(Resource, VirtualDns):
    generate_default_instance = False

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls.validate_dns_server(obj_dict, db_conn)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.validate_dns_server(obj_dict, db_conn)
    # end pre_dbe_update

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        vdns_name = ":".join(obj_dict['fq_name'])
        if 'parent_uuid' in obj_dict:
            ok, read_result = cls.dbe_read(db_conn, 'domain', id)
            if not ok:
                return ok, read_result
            virtual_DNSs = read_result.get('virtual_DNSs', [])
            for vdns in virtual_DNSs:
                vdns_uuid = vdns['uuid']
                vdns_id = {'uuid': vdns_uuid}
                ok, read_result = cls.dbe_read(db_conn, 'virtual-DNS',
                                               vdns['uuid'])
                if not ok:
                    code, msg = read_result
                    if code == 404:
                        continue
                    return ok, (code, msg)

                vdns_data = read_result['virtual_DNS_data']
                if 'next_virtual_DNS' in vdns_data:
                    if vdns_data['next_virtual_DNS'] == vdns_name:
                        return (
                            False,
                            (403,
                             "Virtual DNS server is referred"
                             " by other virtual DNS servers"))
        return True, ""
    # end pre_dbe_delete

    @classmethod
    def is_valid_dns_name(cls, name):
        if len(name) > 255:
            return False
        if name.endswith("."):  # A single trailing dot is legal
            # strip exactly one dot from the right, if present
            name = name[:-1]
        disallowed = re.compile("[^A-Z\d-]", re.IGNORECASE)
        return all(  # Split by labels and verify individually
            (label and len(label) <= 63  # length is within proper range
             # no bordering hyphens
             and not label.startswith("-") and not label.endswith("-")
             and not disallowed.search(label))  # contains only legal char
            for label in name.split("."))
    # end is_valid_dns_name

    @classmethod
    def is_valid_ipv4_address(cls, address):
        parts = address.split(".")
        if len(parts) != 4:
            return False
        for item in parts:
            try:
                if not 0 <= int(item) <= 255:
                    return False
            except ValueError:
                return False
        return True
    # end is_valid_ipv4_address

    @classmethod
    def is_valid_ipv6_address(cls, address):
        try:
            socket.inet_pton(socket.AF_INET6, address)
        except socket.error:
            return False
        return True
    # end is_valid_ipv6_address

    @classmethod
    def validate_dns_server(cls, obj_dict, db_conn):
        if 'fq_name' in obj_dict:
            virtual_dns = obj_dict['fq_name'][1]
            disallowed = re.compile("[^A-Z\d-]", re.IGNORECASE)
            if disallowed.search(virtual_dns) or virtual_dns.startswith("-"):
                return (False, (403,
                        "Special characters are not allowed in " +
                        "Virtual DNS server name"))

        vdns_data = obj_dict['virtual_DNS_data']
        if not cls.is_valid_dns_name(vdns_data['domain_name']):
            return (
                False,
                (403, "Domain name does not adhere to DNS name requirements"))

        record_order = ["fixed", "random", "round-robin"]
        if not str(vdns_data['record_order']).lower() in record_order:
            return (False, (403, "Invalid value for record order"))

        ttl = vdns_data['default_ttl_seconds']
        if ttl < 0 or ttl > 2147483647:
            return (False, (403, "Invalid value for TTL"))

        if 'next_virtual_DNS' in vdns_data:
            vdns_next = vdns_data['next_virtual_DNS']
            if not vdns_next or vdns_next is None:
                return True, ""
            next_vdns = vdns_data['next_virtual_DNS'].split(":")
            # check that next vdns exists
            try:
                next_vdns_uuid = db_conn.fq_name_to_uuid(
                    'virtual_DNS', next_vdns)
            except Exception as e:
                if not cls.is_valid_ipv4_address(
                        vdns_data['next_virtual_DNS']):
                    return (
                        False,
                        (403,
                         "Invalid Virtual Forwarder(next virtual dns server)"))
                else:
                    return True, ""
            # check that next virtual dns servers arent referring to each other
            # above check doesnt allow during create, but entry could be
            # modified later
            ok, read_result = cls.dbe_read(db_conn, 'virtual-DNS',
                                           next_vdns_uuid)
            if ok:
                next_vdns_data = read_result['virtual_DNS_data']
                if 'next_virtual_DNS' in next_vdns_data:
                    vdns_name = ":".join(obj_dict['fq_name'])
                    if next_vdns_data['next_virtual_DNS'] == vdns_name:
                        return (
                            False,
                            (403,
                             "Cannot have Virtual DNS Servers "
                             "referring to each other"))
        return True, ""
    # end validate_dns_server
# end class VirtualDnsServer


class VirtualDnsRecordServer(Resource, VirtualDnsRecord):
    generate_default_instance = False

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls.validate_dns_record(obj_dict, db_conn)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls.validate_dns_record(obj_dict, db_conn)
    # end pre_dbe_update

    @classmethod
    def validate_dns_record(cls, obj_dict, db_conn):
        rec_data = obj_dict['virtual_DNS_record_data']
        rec_types = ["a", "cname", "ptr", "ns", "mx", "aaaa"]
        rec_type = str(rec_data['record_type']).lower()
        if not rec_type in rec_types:
            return (False, (403, "Invalid record type"))
        if str(rec_data['record_class']).lower() != "in":
            return (False, (403, "Invalid record class"))

        rec_name = rec_data['record_name']
        rec_value = rec_data['record_data']

        # check rec_name validity
        if rec_type == "ptr":
            if (not VirtualDnsServer.is_valid_ipv4_address(rec_name) and
                    not "in-addr.arpa" in rec_name.lower()):
                return (
                    False,
                    (403,
                     "PTR Record name has to be IP address"
                     " or reverse.ip.in-addr.arpa"))
        elif not VirtualDnsServer.is_valid_dns_name(rec_name):
            return (
                False,
                (403, "Record name does not adhere to DNS name requirements"))

        # check rec_data validity
        if rec_type == "a":
            if not VirtualDnsServer.is_valid_ipv4_address(rec_value):
                return (False, (403, "Invalid IP address"))
        elif rec_type == "aaaa":
            if not VirtualDnsServer.is_valid_ipv6_address(rec_value):
                return (False, (403, "Invalid IPv6 address"))
        elif rec_type == "cname" or rec_type == "ptr" or rec_type == "mx":
            if not VirtualDnsServer.is_valid_dns_name(rec_value):
                return (
                    False,
                    (403,
                     "Record data does not adhere to DNS name requirements"))
        elif rec_type == "ns":
            try:
                vdns_name = rec_value.split(":")
                vdns_uuid = db_conn.fq_name_to_uuid('virtual_DNS', vdns_name)
            except Exception as e:
                if (not VirtualDnsServer.is_valid_ipv4_address(rec_value) and
                        not VirtualDnsServer.is_valid_dns_name(rec_value)):
                    return (
                        False,
                        (403, "Invalid virtual dns server in record data"))

        ttl = rec_data['record_ttl_seconds']
        if ttl < 0 or ttl > 2147483647:
            return (False, (403, "Invalid value for TTL"))

        if rec_type == "mx":
            preference = rec_data['record_mx_preference']
            if preference < 0 or preference > 65535:
                return (False, (403, "Invalid value for MX record preference"))

        return True, ""
    # end validate_dns_record
# end class VirtualDnsRecordServer

def _check_policy_rules(entries, network_policy_rule=False):
    if not entries:
        return True, ""
    rules = entries.get('policy_rule') or []
    rules_no_uuid = [dict((k, v) for k, v in r.items() if k != 'rule_uuid')
                     for r in rules]
    for index, rule in enumerate(rules_no_uuid):
        rules_no_uuid[index] = None
        if rule in rules_no_uuid:
            try:
                rule_uuid = rules[index]['rule_uuid']
            except KeyError:
                rule_uuid = None
            return (False, (409, 'Rule already exists : %s' % rule_uuid))
    for rule in rules:
        if not rule.get('rule_uuid'):
            rule['rule_uuid'] = str(uuid.uuid4())
        protocol = rule['protocol']
        if protocol.isdigit():
            if int(protocol) < 0 or int(protocol) > 255:
                return (False, (400, 'Rule with invalid protocol : %s' %
                                protocol))
        else:
            valids = ['any', 'icmp', 'tcp', 'udp', 'icmp6']
            if protocol not in valids:
                return (False, (400, 'Rule with invalid protocol : %s' %
                                protocol))

        if network_policy_rule:
            if rule.get('action_list') is None:
                return (False, (400, 'Action is required'))
        else:
            ethertype = rule.get('ethertype')
            if ethertype is not None:
                for addr in itertools.chain(rule.get('src_addresses', []),
                                            rule.get('dst_addresses', [])):
                    if addr.get('subnet') is not None:
                        ip_prefix = addr["subnet"].get('ip_prefix')
                        ip_prefix_len = addr["subnet"].get('ip_prefix_len')
                        network = IPNetwork("%s/%s" % (ip_prefix, ip_prefix_len))
                        if not ethertype == "IPv%s" % network.version:
                            return (False, (400, "Rule subnet %s doesn't match ethertype %s" %
                                            (network, ethertype)))
            src_sg = [addr.get('security_group') for addr in
                      rule.get('src_addresses', [])]
            dst_sg = [addr.get('security_group') for addr in
                      rule.get('dst_addresses', [])]
            if ('local' not in src_sg and 'local' not in dst_sg):
                return (False, (400, "At least one of source or destination"
                                     " addresses must be 'local'"))
    return True, ""
# end _check_policy_rules

class SecurityGroupServer(Resource, SecurityGroup):
    generate_default_instance = False

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        user_visibility = obj_dict['id_perms'].get('user_visible', True)
        verify_quota_kwargs = {'db_conn': db_conn,
                               'fq_name': obj_dict['fq_name'],
                               'resource': 'security_groups',
                               'obj_type': 'security-group',
                               'user_visibility': user_visibility}

        (ok, response) = QuotaHelper.verify_quota_for_resource(
            **verify_quota_kwargs)
        if not ok:
            return (ok, response)

        return _check_policy_rules(obj_dict.get('security_group_entries'))
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls.dbe_read(db_conn, 'security-group', id)
        if not ok:
            return ok, result

        sec_dict = result
        (ok, proj_dict) = QuotaHelper.get_project_dict_for_quota(
            sec_dict['parent_uuid'], db_conn)
        if not ok:
            return (False, (500, 'Bad Project error : ' + pformat(proj_dict)))

        obj_type = 'security-group-rule'
        if ('security_group_entries' in obj_dict and
            QuotaHelper.get_quota_limit(proj_dict, obj_type) >= 0):
            rule_count = len(obj_dict['security_group_entries']['policy_rule'])
            for sg in proj_dict.get('security_groups', []):
                if sg['uuid'] == sec_dict['uuid']:
                    continue
                try:
                    ok, result = cls.dbe_read(db_conn,
                                          'security-group', sg['uuid'])
                    sg_dict = result
                    sge = sg_dict.get('security_group_entries', {})
                    rule_count += len(sge.get('policy_rule', []))
                except Exception as e:
                    ok = False
                    result = (500, 'Error in security group update: %s' %(
                                    cfgm_common.utils.detailed_traceback()))
                if not ok:
                    code, msg = result
                    if code == 404:
                        continue
                    db_conn.config_log(result, level=SandeshLevel.SYS_ERR)
                    continue
            # end for all sg in projects

            if sec_dict['id_perms'].get('user_visible', True) is not False:
                (ok, quota_limit) = QuotaHelper.check_quota_limit(
                                        proj_dict, obj_type, rule_count-1)
                if not ok:
                    return (False, (403, pformat(fq_name) + ' : ' + quota_limit))

        return _check_policy_rules(obj_dict.get('security_group_entries'))
    # end pre_dbe_update

# end class SecurityGroupServer


class NetworkPolicyServer(Resource, NetworkPolicy):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        user_visibility = obj_dict['id_perms'].get('user_visible', True)
        verify_quota_kwargs = {'db_conn': db_conn,
                               'fq_name': obj_dict['fq_name'],
                               'resource': 'network_policys',
                               'obj_type': 'network-policy',
                               'user_visibility': user_visibility}

        (ok, response) = QuotaHelper.verify_quota_for_resource(
            **verify_quota_kwargs)
        if not ok:
            return (ok, response)

        return _check_policy_rules(obj_dict.get('network_policy_entries'), True)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, result = cls.dbe_read(db_conn, 'network-policy', id)
        if not ok:
            return ok, result

        return _check_policy_rules(obj_dict.get('network_policy_entries'), True)
    # end pre_dbe_update

# end class NetworkPolicyServer

class LogicalInterfaceServer(Resource, LogicalInterface):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        (ok, msg) = cls._check_vlan(obj_dict, db_conn)
        if ok == False:
            return (False, msg)

        vlan = 0
        if 'logical_interface_vlan_tag' in obj_dict:
            vlan = obj_dict['logical_interface_vlan_tag']
        return PhysicalInterfaceServer._check_interface_name(obj_dict, db_conn, vlan)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        ok, read_result = cls.dbe_read(db_conn, 'logical-interface', id)
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
                if int(vlan) != int(read_result.get('logical_interface_vlan_tag')):
                    return (False, (403, "Cannot change Vlan id"))

        return True, ""
    # end pre_dbe_update

    @classmethod
    def _check_vlan(cls, obj_dict, db_conn):
        if 'logical_interface_vlan_tag' in obj_dict:
            vlan = obj_dict['logical_interface_vlan_tag']
            if vlan < 0 or vlan > 4094:
                return (False, (403, "Invalid Vlan id"))
        return True, ""
    # end _check_vlan

# end class LogicalInterfaceServer

class RouteTableServer(Resource, RouteTable):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._check_prefixes(obj_dict)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls._check_prefixes(obj_dict)
    # end pre_dbe_update

    @classmethod
    def _check_prefixes(cls, obj_dict):
        routes = obj_dict.get('routes') or {}
        in_routes = routes.get("route") or []
        in_prefixes = [r.get('prefix') for r in in_routes]
        in_prefixes_set = set(in_prefixes)
        if len(in_prefixes) != len(in_prefixes_set):
            return (False, (400, 'duplicate prefixes not '
                                      'allowed: %s' % obj_dict.get('uuid')))

        return (True, "")
    # end _check_prefixes

# end class RouteTableServer

class PhysicalInterfaceServer(Resource, PhysicalInterface):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._check_interface_name(obj_dict, db_conn, None)
    # end pre_dbe_create

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        # do not allow change in display name
        if 'display_name' in obj_dict:
            ok, read_result = cls.dbe_read(db_conn, 'physical-interface',
                                           id, obj_fields=['display_name'])
            if not ok:
                return ok, read_result

            if obj_dict['display_name'] != read_result.get('display_name'):
                return (False, (403, "Cannot change display name !"))

        return True, ""
    # end pre_dbe_update

    @classmethod
    def _check_interface_name(cls, obj_dict, db_conn, vlan_tag):
        interface_name = obj_dict['display_name']
        router = obj_dict['fq_name'][:2]
        try:
            router_uuid = db_conn.fq_name_to_uuid('physical-router', router)
        except cfgm_common.exceptions.NoIdError:
            return (False, (500, 'Internal error : Physical router ' +
                                 ":".join(router) + ' not found'))
        physical_interface_uuid = ""
        if obj_dict['parent_type'] == 'physical-interface':
            try:
                physical_interface_name = obj_dict['fq_name'][:3]
                physical_interface_uuid = db_conn.fq_name_to_uuid('physical-interface', physical_interface_name)
            except cfgm_common.exceptions.NoIdError:
                return (False, (500, 'Internal error : Physical interface ' +
                                     ":".join(physical_interface_name) + ' not found'))

        ok, result = cls.dbe_read(db_conn, 'physical-router', router_uuid,
                                  obj_fields=['physical_interfaces',
                                              'physical_router_product_name'])
        if not ok:
            return ok, result

        physical_router = result
        # In case of QFX, check that VLANs 1, 2 and 4094 are not used
        product_name = physical_router.get('physical_router_product_name', "")
        if product_name.lower().startswith("qfx") and vlan_tag != None:
            if vlan_tag == 1 or vlan_tag == 2 or vlan_tag == 4094:
                return (False, (403, "Vlan id " + str(vlan_tag) + " is not allowed on QFX"))

        for physical_interface in physical_router.get('physical_interfaces', []):
            # Read only the display name of the physical interface
            (ok, interface_object) = cls.dbe_read(db_conn,
                                        'physical-interface',
                                        physical_interface['uuid'],
                                        obj_fields=['display_name'])
            if not ok:
                code, msg = interface_object
                if code == 404:
                    continue
                return ok, (code, msg)

            if 'display_name' in interface_object:
                if interface_name == interface_object['display_name']:
                    return (False, (403, "Display name already used in another interface :" +
                                         physical_interface['uuid']))

            # Need to check vlan only when request is for logical interfaces and
            # When the current physical_interface is the parent
            if vlan_tag == None or \
               physical_interface['uuid'] != physical_interface_uuid:
                continue

            # Read the logical interfaces in the physical interface.
            # This isnt read in the earlier DB read to avoid reading them for
            # all interfaces.
            (ok, interface_object) = db_conn.dbe_list('logical-interface',
                    [physical_interface['uuid']])
            if not ok:
                return (False, (500, 'Internal error : Read logical interface list for ' +
                                     physical_interface['uuid'] + ' failed'))
            obj_ids_list = [{'uuid': obj_uuid} for _, obj_uuid in interface_object]
            obj_fields = [u'logical_interface_vlan_tag']
            (ok, result) = db_conn.dbe_read_multi('logical-interface',
                    obj_ids_list, obj_fields)
            if not ok:
                return (False, (500, 'Internal error : Logical interface read failed'))
            for li_object in result:
                # check vlan tags on the same physical interface
                if 'logical_interface_vlan_tag' in li_object:
                    if vlan_tag == int(li_object['logical_interface_vlan_tag']):
                        return (False, (403, "Vlan tag already used in " +
                                   "another interface : " + li_object['uuid']))

        return True, ""
    # end _check_interface_name

# end class PhysicalInterfaceServer


class LoadbalancerMemberServer(Resource, LoadbalancerMember):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        user_visibility = obj_dict['id_perms'].get('user_visible', True)
        if not user_visibility:
            return True, ""

        try:
            fq_name = obj_dict['fq_name']
            proj_uuid = db_conn.fq_name_to_uuid('project', fq_name[0:2])
        except cfgm_common.exceptions.NoIdError:
            return (False, (500, 'No Project ID error : ' + proj_uuid))

        ok, result = cls.dbe_read(db_conn, 'project', proj_uuid)
        if not ok:
            return ok, result

        proj_dict = result
        if QuotaHelper.get_quota_limit(proj_dict, 'loadbalancer-member') < 0:
            return True, ""
        lb_pools = proj_dict.get('loadbalancer_pools', [])
        quota_count = 0

        for pool in lb_pools:
            ok, result = cls.dbe_read(db_conn, 'loadbalancer-pool',
                                       pool['uuid'])
            if not ok:
                code, msg = result
                if code == 404:
                    continue
                return ok, result

            lb_pool_dict = result
            quota_count += len(lb_pool_dict.get('loadbalancer_members', []))

        (ok, quota_limit) = QuotaHelper.check_quota_limit(
            proj_dict, 'loadbalancer-member', quota_count)
        if not ok:
            return (False, (403, pformat(fq_name) + ' : ' + quota_limit))

        return True, ""

#end class LoadbalancerMemberServer


class LoadbalancerPoolServer(Resource, LoadbalancerPool):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        user_visibility = obj_dict['id_perms'].get('user_visible', True)
        verify_quota_kwargs = {'db_conn': db_conn,
                               'fq_name': obj_dict['fq_name'],
                               'resource': 'loadbalancer_pools',
                               'obj_type': 'loadbalancer-pool',
                               'user_visibility': user_visibility}
        return QuotaHelper.verify_quota_for_resource(**verify_quota_kwargs)

# end class LoadbalancerPoolServer


class LoadbalancerHealthmonitorServer(Resource, LoadbalancerHealthmonitor):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        user_visibility = obj_dict['id_perms'].get('user_visible', True)
        verify_quota_kwargs = {'db_conn': db_conn,
                               'fq_name': obj_dict['fq_name'],
                               'resource': 'loadbalancer_healthmonitors',
                               'obj_type': 'loadbalancer-healthmonitor',
                               'user_visibility': user_visibility}
        return QuotaHelper.verify_quota_for_resource(**verify_quota_kwargs)

# end class LoadbalancerHealthmonitorServer


class VirtualIpServer(Resource, VirtualIp):

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):

        user_visibility = obj_dict['id_perms'].get('user_visible', True)
        verify_quota_kwargs = {'db_conn': db_conn,
                               'fq_name': obj_dict['fq_name'],
                               'resource': 'virtual_ips',
                               'obj_type': 'virtual-ip',
                               'user_visibility': user_visibility}
        return QuotaHelper.verify_quota_for_resource(**verify_quota_kwargs)

# end class VirtualIpServer


class RouteAggregateServer(Resource, RouteAggregate):
    @classmethod
    def _check(cls, obj_dict, db_conn):
        si_refs = obj_dict.get('service_instance_refs') or []
        if len(si_refs) > 1:
            return (False, (400, 'RouteAggregate objects can refer to only '
                                 'one service instance'))
        family = None
        entries = obj_dict.get('aggregate_route_entries', {})
        for route in entries.get('route', []):
            route_family = IPNetwork(route).version
            if family and route_family != family:
                return (False, (400, 'All prefixes in a route aggregate '
                                'object must be of same ip family'))
            family = route_family
        return True, ""
    # end _check

    @classmethod
    def pre_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._check(obj_dict, db_conn)

    @classmethod
    def pre_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls._check(obj_dict, db_conn)

# end class RouteAggregateServer

