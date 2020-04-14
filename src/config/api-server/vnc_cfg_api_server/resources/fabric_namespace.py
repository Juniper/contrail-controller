#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import json

from cfgm_common import _obj_serializer_all
from cfgm_common.exceptions import HttpError
from cfgm_common.exceptions import NoIdError

from vnc_api.gen.resource_common import FabricNamespace
from vnc_api.gen.resource_common import VirtualNetwork
from vnc_api.gen.resource_common import NetworkIpam
from vnc_api.gen.resource_xsd import IpamSubnetType
from vnc_api.gen.resource_xsd import SubnetType
from vnc_api.gen.resource_xsd import IpamSubnets
from vnc_api.gen.resource_xsd import VnSubnetsType
from vnc_api.gen.resource_xsd import VirtualNetworkType
from vnc_api.vnc_api import VirtualNetworkRoutedPropertiesType
from vnc_cfg_api_server.resources._resource_base import ResourceMixin


# Class to handle Fabric Namespace objects
class FabricNamespaceServer(ResourceMixin, FabricNamespace):

    @classmethod
    def _get_loopback_vn_fq_name(cls, fabric_name):
        return ["default-domain",
                "default-project",
                fabric_name + "-overlay-loopback"
                ]

    @classmethod
    def _get_project_fq_name(cls):
        return ["default-domain",
                "default-project"
                ]

    @classmethod
    def _get_network_ipam_fq_name(cls):
        return ["default-domain",
                "default-project",
                "overlay_loopback_ipam"
                ]

    @classmethod
    def _read_and_create_loopback_virtual_network(cls, vn_fq_name, db_conn):
        try:
            vn_uuid = db_conn.fq_name_to_uuid('virtual_network', vn_fq_name)
            ok, vn = cls.dbe_read(db_conn, 'virtual_network', vn_uuid)

        except NoIdError:
            # could not find VN create one
            vn_obj = VirtualNetwork(
                name=vn_fq_name[-1],
                parent_type='project',
                virtual_network_properties=VirtualNetworkType(
                    forwarding_mode='l3'),
                virtual_network_category='routed',
                address_allocation_mode="flat-subnet-only")
            vn_routed_props = VirtualNetworkRoutedPropertiesType()
            # vn_routed_props.set_shared_across_all_lrs(True)
            vn_obj.set_virtual_network_routed_properties(vn_routed_props)
            vn_dict = json.dumps(vn_obj, default=_obj_serializer_all)
            try:
                ok, vn = cls.server.internal_request_create(
                    'virtual-network', json.loads(vn_dict))
                vn = vn.get('virtual-network')
            except HttpError as e:
                return None, (e.status_code, e.content)

        return (vn, "Success")

    @classmethod
    def _read_and_create_network_ipam(cls, vn_uuid, db_conn, subnets):
        obj_type = 'network-ipam'
        # create network IPAM
        ipam_list = []
        for subnet in subnets or []:
            ipam_sub = IpamSubnetType(subnet=SubnetType(
                subnet.get('ip_prefix'), subnet.get('ip_prefix_len')),
                default_gateway=None, enable_dhcp=False)
            ipam_list.append(ipam_sub)
        ipam_obj = NetworkIpam(name=cls._get_network_ipam_fq_name()[-1],
                               fq_name=cls._get_network_ipam_fq_name(),
                               parent_type='project',
                               ipam_subnet_method='flat-subnet',
                               ipam_subnets=IpamSubnets(ipam_list),
                               ipam_subnetting=False)
        ipam_dict = json.dumps(ipam_obj, default=_obj_serializer_all)
        try:
            ipam_uuid = db_conn.fq_name_to_uuid('network_ipam',
                                                cls._get_network_ipam_fq_name()
                                                )
            if ipam_uuid:
                cls.server.internal_request_update('network-ipam',
                                                   ipam_uuid,
                                                   json.loads(ipam_dict))

        except NoIdError:
            try:
                ok, ipam_obj = cls.server.internal_request_create(
                    obj_type, json.loads(ipam_dict))
                ipam_obj = ipam_obj.get(obj_type)
                ipam_uuid = ipam_obj.get('uuid')

            except HttpError as e:
                return None, (e.status_code, e.content)
        try:
            op = 'ADD'
            cls.server.internal_request_ref_update(
                'virtual-network',
                vn_uuid,
                op,
                'network-ipam',
                ipam_uuid,
                cls._get_network_ipam_fq_name(),
                attr=json.loads(json.dumps(VnSubnetsType([]),
                                           default=_obj_serializer_all)))
        except HttpError as e:
            return None, (e.status_code, e.content)

        return (ipam_obj, "Successfully read/create IPAM")

    @classmethod
    def _check_and_allocate_overlay_vn_subnet(cls, obj_dict, db_conn):
        fn_fq_name = obj_dict['fq_name'][-1]
        fabric_name = obj_dict['fq_name'][-2]
        subnets = []
        if "overlay-loopback-subnets" in fn_fq_name:
            # fetch subnets from overlay-loopback FN
            fn_val = obj_dict.get('fabric_namespace_value', None)
            if fn_val:
                ipv4_cidr = fn_val.get('ipv4_cidr', None)
                if ipv4_cidr:
                    subnets = ipv4_cidr.get('subnet', None)

            vn_name = cls._get_loopback_vn_fq_name(fabric_name)
            vn_obj, ret = cls._read_and_create_loopback_virtual_network(
                vn_name,
                db_conn)
            vn_uuid = vn_obj.get('uuid')
            if vn_obj:
                ipam_obj, ret = cls._read_and_create_network_ipam(vn_uuid,
                                                                  db_conn,
                                                                  subnets)
                if not ipam_obj:
                    return False, ret
            else:
                return False, ret

        return True, ''

    @classmethod
    def post_dbe_create(cls, tenant_name, obj_dict, db_conn):
        return cls._check_and_allocate_overlay_vn_subnet(obj_dict,
                                                         db_conn)

    @classmethod
    def post_dbe_update(cls, id, fq_name, obj_dict, db_conn, **kwargs):
        return cls._check_and_allocate_overlay_vn_subnet(obj_dict,
                                                         db_conn)

    @classmethod
    def _delete_loopback_fabric_namespace(cls, fn_fq_name,
                                          fabric_name, db_conn):
        if "overlay-loopback-subnets" in fn_fq_name:
            vn_fq_name = cls._get_loopback_vn_fq_name(fabric_name)
            try:
                vn_uuid = db_conn.fq_name_to_uuid('virtual_network',
                                                  vn_fq_name)
                ok, vn_obj = cls.dbe_read(db_conn, 'virtual_network', vn_uuid)
            except NoIdError:
                return True, '', None

            ok, iip_refs, _ = db_conn.dbe_list('virtual_network',
                                               obj_uuids=[vn_obj['uuid']],
                                               field_names=[
                                                   'instance_ip_back_refs',
                                                   'uuid'])
            if iip_refs[-1].get('instance_ip_back_refs', None):
                iip_refs = iip_refs[-1].get('instance_ip_back_refs')
                iip_uuid_list = [(iip_ref['uuid']) for iip_ref in iip_refs]
                ok, iip_list, _ = db_conn.dbe_list('instance_ip',
                                                   obj_uuids=iip_uuid_list,
                                                   field_names=[
                                                       'instance_ip_back_refs',
                                                       'uuid'])

                try:
                    for iip in iip_list or []:
                        db_conn.get_api_server().internal_request_delete(
                            'instance_ip',
                            iip['uuid'])
                except HttpError as e:
                    return False, (e.status_code, e.content)

            try:

                db_conn.get_api_server().internal_request_delete(
                    'virtual_network',
                    vn_obj.get('uuid'))
                ipam_uuid = db_conn.fq_name_to_uuid(
                    'network_ipam',
                    cls._get_network_ipam_fq_name())
                db_conn.get_api_server().internal_request_delete(
                    'network_ipam',
                    ipam_uuid)
            except HttpError as e:
                return False, (e.status_code, e.content)
        return True, '', None

    @classmethod
    def pre_dbe_delete(cls, id, obj_dict, db_conn):
        fn_fq_name = obj_dict['fq_name'][-1]
        fabric_name = obj_dict['fq_name'][-2]
        return cls._delete_loopback_fabric_namespace(fn_fq_name,
                                                     fabric_name,
                                                     db_conn)
