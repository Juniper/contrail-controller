#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import json

from cfgm_common import _obj_serializer_all
from cfgm_common import OVERLAY_LOOPBACK_FQ_PREFIX
from cfgm_common.exceptions import HttpError
from vnc_api.gen.resource_common import FabricNamespace
from vnc_api.gen.resource_common import NetworkIpam
from vnc_api.gen.resource_xsd import FabricNetworkTag
from vnc_api.gen.resource_xsd import IpamSubnets
from vnc_api.gen.resource_xsd import IpamSubnetType
from vnc_api.gen.resource_xsd import PermType2
from vnc_api.gen.resource_xsd import SubnetType
from vnc_api.gen.resource_xsd import VirtualNetworkType
from vnc_api.gen.resource_xsd import VnSubnetsType
from vnc_api.vnc_api import VirtualNetworkRoutedPropertiesType

from vnc_cfg_api_server.resources._resource_base import ResourceMixin


def get_loopback_vn_fq_name(fabric_name):
    return OVERLAY_LOOPBACK_FQ_PREFIX + [fabric_name +
                                         "-overlay-loopback-network"]


def get_loopback_ipam_fq_name(fabric_name):
    return OVERLAY_LOOPBACK_FQ_PREFIX +\
        [fabric_name + "-overlay-loopback-network-ipam"]


# Class to handle Fabric Namespace objects
class FabricNamespaceServer(ResourceMixin, FabricNamespace):

    @classmethod
    def _read_and_create_loopback_virtual_network(cls, vn_fq_name,
                                                  fn_fq_name):
        fabric_fq_name = fn_fq_name[:-1]
        ok, fabric = cls.server.get_resource_class('fabric').\
            locate(fabric_fq_name, create_it=False)
        if not ok:
            return ok, fabric

        kwargs = {'display_name': vn_fq_name[-1]}
        kwargs['parent_type'] = 'project'
        kwargs['virtual_network_properties'] = VirtualNetworkType(
            forwarding_mode='l3')
        kwargs['virtual_network_category'] = 'routed'
        kwargs['address_allocation_mode'] = "flat-subnet-only"
        kwargs['perms2'] = PermType2(global_access=7)
        kwargs['virtual_network_routed_properties'] =\
            VirtualNetworkRoutedPropertiesType(shared_across_all_lrs=True)
        ok, result = cls.server.get_resource_class('virtual_network').\
            locate(vn_fq_name, **kwargs)
        if ok:
            if len(result.get('fabric_back_refs', [])) == 0:
                op = 'ADD'
                cls.server.internal_request_ref_update(
                    'fabric',
                    fabric['uuid'],
                    op,
                    'virtual-network',
                    result['uuid'],
                    vn_fq_name,
                    attr=json.loads(json.dumps(
                        FabricNetworkTag('overlay-loopback'),
                        default=_obj_serializer_all)))

        return ok, result

    @classmethod
    def _read_and_create_network_ipam(cls, vn_obj, subnets, ipam_fq_name):
        ipam_list = []
        for subnet in subnets or []:
            ipam_sub = IpamSubnetType(subnet=SubnetType(
                subnet.get('ip_prefix'), subnet.get('ip_prefix_len')),
                default_gateway=None, enable_dhcp=False,
                addr_from_start=False)
            ipam_list.append(ipam_sub)
        if len(vn_obj.get('network_ipam_refs', [])) == 0:
            # create network IPAM
            kwargs = {'display_name': ipam_fq_name[-1]}
            kwargs['parent_type'] = 'project'
            kwargs['ipam_subnet_method'] = 'flat-subnet'
            kwargs['ipam_subnets'] = IpamSubnets(ipam_list)
            kwargs['ipam_subnetting'] = False
            ok, result = cls.server.get_resource_class('network_ipam').\
                locate(ipam_fq_name, **kwargs)
            if not ok:
                return ok, result
        else:
            # Update network IPAM
            ok, result = cls.server.get_resource_class('network_ipam').\
                locate(ipam_fq_name, create_it=False)
            if not ok:
                return ok, result
            ipam_obj = NetworkIpam(name=ipam_fq_name[-1],
                                   fq_name=ipam_fq_name,
                                   parent_type='project',
                                   ipam_subnet_method='flat-subnet',
                                   ipam_subnets=IpamSubnets(ipam_list),
                                   ipam_subnetting=False)
            ipam_dict = json.dumps(ipam_obj, default=_obj_serializer_all)
            try:
                cls.server.internal_request_update('network-ipam',
                                                   result['uuid'],
                                                   json.loads(ipam_dict))
            except HttpError as e:
                return False, (e.status_code, e.content)

        ipam_uuid = result['uuid']

        # Do this only for VN and IPAM creation.Skip for update. Currently we
        # support only one IPAM
        if len(vn_obj.get('network_ipam_refs', [])) == 0:
            try:
                op = 'ADD'
                cls.server.internal_request_ref_update(
                    'virtual-network',
                    vn_obj['uuid'],
                    op,
                    'network-ipam',
                    ipam_uuid,
                    ipam_fq_name,
                    attr=json.loads(json.dumps(VnSubnetsType(
                        []), default=_obj_serializer_all)))
            except HttpError as e:
                return False, (e.status_code, e.content)

        return True, result

    @classmethod
    def _check_and_allocate_overlay_vn_subnet(cls, obj_dict):
        fn_fq_name = obj_dict['fq_name'][-1]
        fabric_name = obj_dict['fq_name'][-2]
        subnets = []
        if "overlay-loopback-subnets" == fn_fq_name:
            # fetch subnets from overlay-loopback FN
            fn_val = obj_dict.get('fabric_namespace_value', None)
            if fn_val:
                ipv4_cidr = fn_val.get('ipv4_cidr', None)
                if ipv4_cidr:
                    subnets = ipv4_cidr.get('subnet', None)

            vn_name = get_loopback_vn_fq_name(fabric_name)
            ok, vn_obj = cls._read_and_create_loopback_virtual_network(
                vn_name,
                obj_dict['fq_name'])
            if not ok:
                return ok, vn_obj

            ipam_fq_name = get_loopback_ipam_fq_name(fabric_name)
            ok, result = cls._read_and_create_network_ipam(vn_obj,
                                                           subnets,
                                                           ipam_fq_name)
            if not ok:
                return ok, result

        return True, ''

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._check_and_allocate_overlay_vn_subnet(obj_dict)

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls._check_and_allocate_overlay_vn_subnet(obj_dict)
