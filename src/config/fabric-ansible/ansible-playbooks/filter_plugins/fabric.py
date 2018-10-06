#!/usr/bin/python
#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation for fabric related Ansible filter plugins
"""
import logging
import sys
import traceback
import argparse
import json
import uuid
from functools import reduce
from netaddr import IPNetwork
import jsonschema

from job_manager.job_utils import JobVncApi

from cfgm_common.exceptions import (
    RefsExistError,
    NoIdError
)
from vnc_api.vnc_api import VncApi
from vnc_api.gen.resource_client import (
    Fabric,
    FabricNamespace,
    VirtualNetwork,
    NetworkIpam,
    LogicalInterface,
    InstanceIp,
    BgpRouter,
    LogicalRouter
)
from vnc_api.gen.resource_xsd import (
    IpamSubnets,
    IpamSubnetType,
    SubnetType,
    SerialNumListType,
    VnSubnetsType,
    VirtualNetworkType,
    FabricNetworkTag,
    NamespaceValue,
    RoutingBridgingRolesType,
    SubnetListType,
    KeyValuePairs,
    KeyValuePair
)


def dump(vnc_api, res_type, fq_name):
    obj = vnc_api._object_read(res_type=res_type, fq_name=fq_name)
    dumpobj(vnc_api, obj)
# end dump


def dumpobj(vnc_api, obj):
    print json.dumps(vnc_api.obj_to_dict(obj), indent=4)
# end dumpobj


def _compare_fq_names(this_fq_name, that_fq_name):
    """
    :param this_fq_name: list<string>
    :param that_fq_name: list<string>
    :return: True if the two fq_names are the same
    """
    if not this_fq_name or not that_fq_name:
        return False
    elif len(this_fq_name) != len(that_fq_name):
        return False
    else:
        for i in range(0, len(this_fq_name)):
            if str(this_fq_name[i]) != str(that_fq_name[i]):
                return False
    return True
# end compare_fq_names


def _fabric_network_name(fabric_name, network_type):
    """
    :param fabric_name: string
    :param network_type: string (One of the constants defined in NetworkType)
    :return: string
    """
    return '%s-%s-network' % (fabric_name, network_type)
# end _fabric_network_name


def _fabric_network_ipam_name(fabric_name, network_type):
    """
    :param fabric_name: string
    :param network_type: string (One of the constants defined in NetworkType)
    :return: string
    """
    return '%s-%s-network-ipam' % (fabric_name, network_type)
# end _fabric_network_ipam_name


def _bgp_router_fq_name(device_name):
    return [
        'default-domain',
        'default-project',
        'ip-fabric',
        '__default__',
        device_name + '-bgp'
    ]
# end _bgp_router_fq_name

def _logical_router_fq_name(fabric_name):
    return [
        'default-domain',
        'admin',
        fabric_name + '-CRB-gateway-logical-router'
    ]
# end _logical_router_fq_name


def _subscriber_tag(local_mac, remote_mac):
    """
    :param local_mac: string
    :param remote_mac: string
    :return: string
    """
    macs = [local_mac, remote_mac]
    macs.sort()
    return "%s-%s" % (macs[0], macs[1])
# end _subscriber_tag


class NetworkType(object):
    """Pre-defined network types"""
    MGMT_NETWORK = 'management'
    LOOPBACK_NETWORK = 'loopback'
    FABRIC_NETWORK = 'ip-fabric'

    def __init__(self):
        pass
# end NetworkType


class FilterLog(object):
    _instance = None

    @staticmethod
    def instance():
        if not FilterLog._instance:
            FilterLog._instance = FilterLog()
        return FilterLog._instance
    # end instance

    @staticmethod
    def _init_logging():
        """
        :return: type=<logging.Logger>
        """
        logger = logging.getLogger('FabricFilter')
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.INFO)

        formatter = logging.Formatter(
            '%(asctime)s %(levelname)-8s %(message)s',
            datefmt='%Y/%m/%d %H:%M:%S'
        )
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)
        return logger
    # end _init_logging

    def __init__(self):
        self._msg = None
        self._logs = []
        self._logger = FilterLog._init_logging()
    # end __init__

    def logger(self):
        return self._logger
    # end logger

    def msg_append(self, msg):
        if msg:
            if not self._msg:
                self._msg = msg + ' ... '
            else:
                self._msg += msg + ' ... '
    # end log

    def msg_end(self):
        if self._msg:
            self._msg += 'done'
            self._logs.append(self._msg)
            self._logger.warn(self._msg)
            self._msg = None
    # end msg_end

    def dump(self):
        retval = ""
        for msg in self._logs:
            retval += msg + '\n'
        return retval
    # end dump
# end FilterLog


def _task_log(msg):
    FilterLog.instance().msg_append(msg)
# end _msg


def _task_done(msg=None):
    if msg:
        _task_log(msg)
    FilterLog.instance().msg_end()
# end _msg_end


class FilterModule(object):
    """Fabric filter plugins"""

    @staticmethod
    def _init_vnc_api(job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
              'auth_token': '0B02D162-180F-4452-96D0-E9FCAAFC4378'
            }
        :return: VncApi
        """
        return JobVncApi.vnc_init(job_ctx)
    # end _init_vnc_api

    @staticmethod
    def _validate_job_ctx(vnc_api, job_ctx, brownfield):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "fabric_fq_name": [
                        "default-global-system-config",
                        "fab01"
                    ],
                    "device_auth": {
                        "root_password": "Embe1mpls"
                    },
                    "device_count": 1,
                    "fabric_asn_pool": [
                        {
                            "asn_max": 65000,
                            "asn_min": 64000
                        },
                        {
                            "asn_max": 65100,
                            "asn_min": 65000
                        }
                    ],
                    "fabric_subnets": [
                        "30.1.1.1/24"
                    ],
                    "loopback_subnets": [
                        "20.1.1.1/24"
                    ],
                    "management_subnets": [
                        {
                            "cidr": "10.87.69.0/25",
                            "gateway": "10.87.69.1"
                        }
                    ],
                    "node_profiles": [
                        {
                            "node_profile_name": "juniper-qfx5k"
                        }
                    ]
                }
        :param brownfield: True if onboarding a brownfield fabric
        :return: job_input (Dictionary)
        """
        job_template_fqname = job_ctx.get('job_template_fqname')
        if not job_template_fqname:
            raise ValueError('Invalid job_ctx: missing job_template_fqname')

        job_input = job_ctx.get('job_input')
        if not job_input:
            raise ValueError('Invalid job_ctx: missing job_input')

        # retrieve job input schema from job template to validate the job input
        fabric_onboard_template = vnc_api.job_template_read(
            fq_name=job_template_fqname
        )
        input_schema = fabric_onboard_template.get_job_template_input_schema()
        jsonschema.validate(job_input, input_schema)

        # make sure there is management subnets are not overlapping with
        # management subnets of other existing fabrics
        fab_fq_name = job_input.get('fabric_fq_name')
        mgmt_subnets = job_input.get('management_subnets')
        FilterModule._validate_mgmt_subnets(
            vnc_api, fab_fq_name, mgmt_subnets, brownfield
        )

        # change device_auth to conform with brownfield device_auth schema
        if not brownfield:
            job_input['device_auth'] = [
                {
                    'username': 'root',
                    'password': job_input['device_auth'].get('root_password')
                }
            ]

        return job_input
    # end _validate_job_ctx

    @staticmethod
    def _validate_mgmt_subnets(vnc_api, fab_fq_name, mgmt_subnets, brownfield):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param mgmt_subnets: List<Dict>
            example:
            [
                { "cidr": "10.87.69.0/25", "gateway": "10.87.69.1" }
            ]
        :return: <boolean>
        """
        if not brownfield:
            _task_log(
                'Validating management subnets cidr prefix length to be no '
                'larger than 30 for the greenfield case'
            )
            for mgmt_subnet in mgmt_subnets:
                if int(mgmt_subnet.get('cidr').split('/')[-1]) > 30:
                    raise ValueError(
                        "Invalid mgmt subnet %s" % mgmt_subnet.get('cidr')
                    )
                if not mgmt_subnet.get('gateway'):
                    raise ValueError(
                        "missing gateway ip for subnet %s"
                        % mgmt_subnet.get('cidr')
                    )
            _task_done()

        _task_log(
            'Validating management subnets not overlapping with management '
            'subnets of any existing fabric'
        )
        mgmt_networks = [IPNetwork(sn.get('cidr')) for sn in mgmt_subnets]
        mgmt_tag = vnc_api.tag_read(fq_name=['label=fabric-management-ip'])
        for ref in mgmt_tag.get_fabric_namespace_back_refs() or []:
            namespace_obj = vnc_api.fabric_namespace_read(id=ref.get('uuid'))

            # skip namespaces belong to this fabric
            if _compare_fq_names(namespace_obj.fq_name[:-1], fab_fq_name):
                continue

            # make sure mgmt subnets not overlapping with other existing fabrics
            if str(namespace_obj.fabric_namespace_type) == 'IPV4-CIDR':
                ipv4_cidr = namespace_obj.fabric_namespace_value.ipv4_cidr
                for sn in ipv4_cidr.subnet:
                    network = IPNetwork(sn.ip_prefix + '/' + sn.ip_prefix_len)
                    for mgmt_network in mgmt_networks:
                        if network in mgmt_network or mgmt_network in network:
                            _task_done(
                                'detected overlapping management subnet %s '
                                'in fabric %s' % (
                                    str(network), namespace_obj.fq_name[-2]
                                )
                            )
                            raise ValueError("Overlapping mgmt subnet detected")
        _task_done()
    # end _validate_mgmt_subnets

    def __init__(self):
        self._logger = FilterLog.instance().logger()
    # end __init__

    def filters(self):
        """Fabric filters"""
        return {
            'onboard_fabric': self.onboard_fabric,
            'onboard_existing_fabric': self.onboard_brownfield_fabric,
            'delete_fabric': self.delete_fabric,
            'delete_devices': self.delete_fabric_devices,
            'assign_roles': self.assign_roles
        }
    # end filters

    # ***************** onboard_fabric filter *********************************
    def onboard_fabric(self, job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "fabric_fq_name": [
                        "default-global-system-config",
                        "fab01"
                    ],
                    "device_auth": {
                        "root_password": "Embe1mpls"
                    },
                    "device_count": 1,
                    "fabric_asn_pool": [
                        {
                            "asn_max": 65000,
                            "asn_min": 64000
                        },
                        {
                            "asn_max": 65100,
                            "asn_min": 65000
                        }
                    ],
                    "fabric_subnets": [
                        "30.1.1.1/24"
                    ],
                    "loopback_subnets": [
                        "20.1.1.1/24"
                    ],
                    "management_subnets": [
                        {
                            "cidr": "10.87.69.0/25",
                            "gateway": "10.87.69.1"
                        }
                    ],
                    "overlay_ibgp_asn": 64512,
                    "node_profiles": [
                        {
                            "node_profile_name": "juniper-qfx5k"
                        }
                    ]
                }
        :return: Dictionary
            if success, returns
                {
                    'status': 'success',
                    'fabric_uuid': <string: fabric_obj.uuid>,
                    'onboard_log': <string: onboard_log>
                }
            if failure, returns
                {
                    'status': 'failure',
                    'error_msg': <string: error message>,
                    'onboard_log': <string: onboard_log>
                }
        """
        try:
            vnc_api = FilterModule._init_vnc_api(job_ctx)
            fabric_info = FilterModule._validate_job_ctx(
                vnc_api, job_ctx, False
            )
            fabric_obj = self._onboard_fabric(vnc_api, fabric_info, True)
            return {
                'status': 'success',
                'fabric_uuid': fabric_obj.uuid,
                'onboard_log': FilterLog.instance().dump()

            }
        except Exception as ex:
            self._logger.error(str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'onboard_log': FilterLog.instance().dump()
            }
    # end onboard_fabric

    # ***************** onboard_brownfield_fabric filter ***********************
    def onboard_brownfield_fabric(self, job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "fabric_fq_name": [
                        "default-global-system-config",
                        "fab01"
                    ],
                    "device_auth": [{
                        "username": "root",
                        "password": "Embe1mpls"
                    }],
                    "management_subnets": [
                        {
                            "cidr": "10.87.69.0/25",
                            "gateway": "10.87.69.1"
                        }
                    ],
                    "overlay_ibgp_asn": 64512,
                    "node_profiles": [
                        {
                            "node_profile_name": "juniper-qfx5k"
                        }
                    ]
                }
        :return: Dictionary
            if success, returns
                {
                    'status': 'success',
                    'fabric_uuid': <string: fabric_obj.uuid>,
                    'onboard_log': <string: onboard_log>
                }
            if failure, returns
                {
                    'status': 'failure',
                    'error_msg': <string: error message>,
                    'onboard_log': <string: onboard_log>
                }
        """
        try:
            vnc_api = FilterModule._init_vnc_api(job_ctx)
            fabric_info = FilterModule._validate_job_ctx(
                vnc_api, job_ctx, True
            )
            fabric_obj = self._onboard_fabric(vnc_api, fabric_info, False)
            return {
                'status': 'success',
                'fabric_uuid': fabric_obj.uuid,
                'onboard_log': FilterLog.instance().dump()

            }
        except Exception as ex:
            self._logger.error(str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'onboard_log': FilterLog.instance().dump()
            }
    # end onboard_existing_fabric

    def _onboard_fabric(self, vnc_api, fabric_info, ztp):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param fabric_info: Dictionary
        :param ztp: set to True if fabric is to be ZTPed
        :return: <vnc_api.gen.resource_client.Fabric>
        """
        fabric_obj = self._create_fabric(vnc_api, fabric_info, ztp)

        # management network
        mgmt_subnets = [
            {
                'cidr': subnet.get('cidr'),
                'gateway': subnet.get('gateway')
            } for subnet in fabric_info.get('management_subnets')
        ]
        self._add_cidr_namespace(
            vnc_api,
            fabric_obj,
            'management-subnets',
            mgmt_subnets,
            'label=fabric-management-ip'
        )
        self._add_fabric_vn(
            vnc_api,
            fabric_obj,
            NetworkType.MGMT_NETWORK,
            mgmt_subnets,
            False
        )

        # loopback network
        if fabric_info.get('loopback_subnets'):
            loopback_subnets = [
                {
                    'cidr': subnet
                } for subnet in fabric_info.get('loopback_subnets')
            ]
            self._add_cidr_namespace(
                vnc_api,
                fabric_obj,
                'loopback-subnets',
                loopback_subnets,
                'label=fabric-loopback-ip'
            )
            self._add_fabric_vn(
                vnc_api,
                fabric_obj,
                NetworkType.LOOPBACK_NETWORK,
                loopback_subnets,
                False
            )

        # fabric network
        if fabric_info.get('fabric_subnets'):
            fabric_subnets = [
                {
                    'cidr': subnet
                } for subnet in fabric_info.get('fabric_subnets')
            ]
            self._add_cidr_namespace(
                vnc_api,
                fabric_obj,
                'fabric-subnets',
                fabric_subnets,
                'label=fabric-peer-ip'
            )
            peer_subnets = self._carve_out_peer_subnets(fabric_subnets)
            self._add_fabric_vn(
                vnc_api,
                fabric_obj,
                NetworkType.FABRIC_NETWORK,
                peer_subnets,
                True
            )

        # ANS pool for underlay eBGP
        if fabric_info.get('fabric_asn_pool'):
            self._add_asn_range_namespace(
                vnc_api,
                fabric_obj,
                'eBGP-ASN-pool',
                [{
                    'asn_min': int(asn_range.get('asn_min')),
                    'asn_max': int(asn_range.get('asn_max'))
                } for asn_range in fabric_info.get('fabric_asn_pool')],
                'label=fabric-ebgp-as-number'
            )

        # ASN for iBGP
        if fabric_info.get('overlay_ibgp_asn'):
            self._add_overlay_asn_namespace(
                vnc_api,
                fabric_obj,
                'overlay_ibgp_asn',
                fabric_info.get('overlay_ibgp_asn'),
                'label=fabric-as-number'
            )

        # add node profiles
        self._add_node_profiles(
            vnc_api,
            fabric_obj,
            fabric_info.get('node_profiles')
        )

        return fabric_obj
    # end onboard_fabric

    @staticmethod
    def _carve_out_peer_subnets(subnets):
        """
        :param subnets: type=list<Dictionary>
            example:
            [
                { 'cidr': '192.168.10.1/24', 'gateway': '192.168.10.1 }
            ]
        :return: list<Dictionary>
            example:
            [
                { 'cidr': '192.168.10.1/30'}
            ]
        """
        carved_subnets = []
        for subnet in subnets:
            slash_30_subnets = IPNetwork(subnet.get('cidr')).subnet(30)
            for slash_30_sn in slash_30_subnets:
                carved_subnets.append({'cidr': str(slash_30_sn)})
        return carved_subnets
    # end _carve_out_peer_subnets

    @staticmethod
    def _create_fabric(vnc_api, fabric_info, ztp):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param fabric_info: dynamic object from job input schema via
                            python_jsonschema_objects
        :param ztp: set to True if fabric is to be ZTPed
        :return: <vnc_api.gen.resource_client.Fabric>
        """
        fq_name = fabric_info.get('fabric_fq_name')
        fab_name = fq_name[-1]
        fab_display_name = fabric_info.get('fabric_display_name', fab_name)
        _task_log('creating fabric: %s' % fab_name)
        fab = Fabric(
            name=fab_name,
            fq_name=fq_name,
            display_name=fab_display_name,
            parent_type='global-system-config',
            fabric_credentials={
                'device_credential': [
                    {
                        'credential': device_auth
                    } for device_auth in fabric_info.get('device_auth')
                ]
            },
            fabric_ztp=ztp
        )
        fab.set_annotations(KeyValuePairs([
            KeyValuePair(key='user_input', value=json.dumps(fabric_info))
        ]))

        try:
            vnc_api.fabric_create(fab)
        except RefsExistError:
            _task_log(
                "Fabric '%s' already exists, hence updating it" % fab_name
            )
            vnc_api.fabric_update(fab)
        _task_done()

        return vnc_api.fabric_read(fq_name=fq_name)
    # end _create_fabric

    @staticmethod
    def _add_cidr_namespace(vnc_api, fab, ns_name, ns_subnets, tag):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param fab: <vnc_api.gen.resource_client.Fabric>
        :param ns_name:
        :param ns_subnets:
        :param tag:
        :return:
        """
        _task_log(
            'adding management ip namespace "%s" to fabric "%s"'
            % (ns_name, fab.name)
        )
        subnets = []
        for subnet in ns_subnets:
            ip_prefix = subnet['cidr'].split('/')
            subnets.append(SubnetType(
                ip_prefix=ip_prefix[0], ip_prefix_len=ip_prefix[1]))

        ns_fq_name = fab.fq_name + [ns_name]
        namespace = FabricNamespace(
            name=ns_name,
            fq_name=ns_fq_name,
            parent_type='fabric',
            fabric_namespace_type='IPV4-CIDR',
            fabric_namespace_value=NamespaceValue(
                ipv4_cidr=SubnetListType(subnet=subnets)
            )
        )

        namespace.set_tag_list([{'to': [tag]}])
        try:
            vnc_api.fabric_namespace_create(namespace)
        except RefsExistError:
            _task_log("Fabric namespace '%s' already exists, updating"
                      % ns_name)
            vnc_api.fabric_namespace_update(namespace)
        _task_done()

        namespace = vnc_api.fabric_namespace_read(fq_name=ns_fq_name)
        return namespace
    # end _add_cidr_namespace

    @staticmethod
    def _add_overlay_asn_namespace(vnc_api, fab, ns_name, asn, tag):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param fab: <vnc_api.gen.resource_client.Fabric>
        :param ns_name: string, namespace name
        :param asn: list<Dictionary>
            [
                { 'asn_min': 1000, 'asn_max': 2000 }
            ]
        :param tag: string
        :return: <vnc_api.gen.resource_client.FabricNamespace>
        """
        _task_log(
            'adding ASN range namespace "%s" to fabric "%s"'
            % (ns_name, fab.name)
        )
        ns_fq_name = fab.fq_name + [ns_name]
        namespace = FabricNamespace(
            name=ns_name,
            fq_name=ns_fq_name,
            parent_type='fabric',
            fabric_namespace_type='ASN',
            fabric_namespace_value=NamespaceValue(asn={
                'asn': [asn]
            })
        )
        namespace.set_tag_list([{'to': [tag]}])
        try:
            vnc_api.fabric_namespace_create(namespace)
        except RefsExistError:
            _task_log("Fabric namespace '%s' already exists, updating"
                      % ns_name)
            vnc_api.fabric_namespace_update(namespace)
        _task_done()

        namespace = vnc_api.fabric_namespace_read(fq_name=ns_fq_name)
        return namespace
    # end _add_overlay_asn_namespace

    @staticmethod
    def _add_asn_range_namespace(vnc_api, fab, ns_name, asn_ranges, tag):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param fab: <vnc_api.gen.resource_client.Fabric>
        :param ns_name: string, namespace name
        :param asn_ranges: list<Dictionary>
            [
                { 'asn_min': 1000, 'asn_max': 2000 }
            ]
        :param tag: string
        :return: <vnc_api.gen.resource_client.FabricNamespace>
        """
        _task_log(
            'adding ASN range namespace "%s" to fabric "%s"'
            % (ns_name, fab.name)
        )
        ns_fq_name = fab.fq_name + [ns_name]
        namespace = FabricNamespace(
            name=ns_name,
            fq_name=ns_fq_name,
            parent_type='fabric',
            fabric_namespace_type='ASN_RANGE',
            fabric_namespace_value=NamespaceValue(asn_ranges=asn_ranges)
        )
        namespace.set_tag_list([{'to': [tag]}])
        try:
            vnc_api.fabric_namespace_create(namespace)
        except RefsExistError:
            _task_log("Fabric namespace '%s' already exists, updating"
                      % ns_name)
            vnc_api.fabric_namespace_update(namespace)
        _task_done()

        namespace = vnc_api.fabric_namespace_read(fq_name=ns_fq_name)
        return namespace
    # end _add_asn_range_namespace

    @staticmethod
    def _add_node_profiles(vnc_api, fabric_obj, node_profiles):
        """
        assign node profiles to the fabric
        :param vnc_api: <vnc_api.VncApi>
        :param fabric_obj: <vnc_api.gen.resource_client.Fabric>
        :param node_profiles: dynamic object by python_jsonschema_objects
                              based on job input schema
        :return:
        """
        node_profile_objs = []
        node_profile_names = []
        for node_profile in node_profiles:
            name = str(node_profile.get('node_profile_name'))
            node_profile_names.append(name)
            np_fq_name = ['default-global-system-config', name]
            node_profile_obj = vnc_api.node_profile_read(fq_name=np_fq_name)
            node_profile_objs.append(node_profile_obj)

        for node_profile_obj in node_profile_objs:
            fabric_obj.add_node_profile(
                node_profile_obj, SerialNumListType(serial_num=[]))
        _task_log(
            'assigning node profiles %s to fabric %s'
            % (node_profile_names, fabric_obj.name)
        )
        vnc_api.fabric_update(fabric_obj)
        _task_done()
    # end _add_node_profiles

    @staticmethod
    def _add_virtual_network(vnc_api, network_name):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param network_name: string
        :return: <vnc_api.gen.resource_client.VirtualNetwork>
        """
        nw_fq_name = ['default-domain', 'default-project', network_name]
        _task_log('creating virtual network %s' % network_name)
        network = VirtualNetwork(
            name=network_name,
            fq_name=nw_fq_name,
            parent_type='project',
            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
            address_allocation_mode='flat-subnet-only')
        try:
            vnc_api.virtual_network_create(network)
        except RefsExistError as ex:
            _task_log(
                "virtual network '%s' already exists or other conflict: %s"
                % (network_name, str(ex))
            )
            vnc_api.virtual_network_update(network)

        network = vnc_api.virtual_network_read(fq_name=nw_fq_name)
        _task_done()
        return network
    # end _add_virtual_network

    @staticmethod
    def _new_subnet(cidr):
        """
        :param cidr: string, example: '10.1.1.1/24'
        :return: <vnc_api.gen.resource_xsd.SubnetType>
        """
        split_cidr = cidr.split('/')
        return SubnetType(ip_prefix=split_cidr[0], ip_prefix_len=split_cidr[1])
    # end _new_subnet

    @staticmethod
    def _add_network_ipam(vnc_api, ipam_name, subnets, subnetting):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param ipam_name: string
        :param subnets: list<Dictionary>
            [
                { 'cidr': '10.1.1.1/24', 'gateway': '10.1.1.1' }
            ]
        :param subnetting: boolean
        :return: <vnc_api.gen.resource_client.NetworkIpam>
        """
        ipam_fq_name = ['default-domain', 'default-project', ipam_name]
        _task_log("creating network-ipam %s" % ipam_name)
        ipam = NetworkIpam(
            name=ipam_name,
            fq_name=ipam_fq_name,
            parent_type='project',
            ipam_subnets=IpamSubnets([
                IpamSubnetType(
                    subnet=FilterModule._new_subnet(sn.get('cidr')),
                    default_gateway=sn.get('gateway'),
                    subnet_uuid=str(uuid.uuid1())
                ) for sn in subnets if int(sn.get('cidr').split('/')[-1]) < 31
            ]),
            ipam_subnet_method='flat-subnet',
            ipam_subnetting=subnetting
        )
        try:
            vnc_api.network_ipam_create(ipam)
        except RefsExistError as ex:
            _task_log(
                "network IPAM '%s' already exists or other conflict: %s"
                % (ipam_name, str(ex))
            )
            vnc_api.network_ipam_update(ipam)
        _task_done()
        return vnc_api.network_ipam_read(fq_name=ipam_fq_name)
    # end _add_network_ipam

    def _add_fabric_vn(
            self, vnc_api, fabric_obj, network_type, subnets, subnetting):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param fabric_obj: <vnc_api.gen.resource_client.Fabric>
        :param network_type: string, one of the constants defined in NetworkType
        :param subnets: list<Dictionary>
            [
                { 'cidr': '10.1.1.1/24', 'gateway': '10.1.1.1' }
            ]
        :param subnetting: boolean
        :return: <vnc_api.gen.resource_client.VirtualNetwork>
        """
        # create vn and ipam
        network_name = _fabric_network_name(
            str(fabric_obj.name), network_type)
        network = self._add_virtual_network(vnc_api, network_name)

        ipam_name = _fabric_network_ipam_name(
            str(fabric_obj.name), network_type)
        ipam = self._add_network_ipam(vnc_api, ipam_name, subnets, subnetting)

        # add vn->ipam link
        _task_log('adding ipam %s to network %s' % (ipam.name, network.name))
        network.add_network_ipam(ipam, VnSubnetsType([]))
        vnc_api.virtual_network_update(network)
        _task_done()

        # add fabric->vn link
        _task_log(
            'adding network %s to fabric %s' % (network.name, fabric_obj.name)
        )
        fabric_obj.add_virtual_network(
            network, FabricNetworkTag(network_type=network_type))
        vnc_api.fabric_update(fabric_obj)
        _task_done()
        return network
    # end _add_fabric_vn

    # ***************** delete_fabric filter **********************************
    def delete_fabric(self, job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "fabric_fq_name": [
                        "default-global-system-config",
                        "fab01"
                    ]
                }
            }
        :return: type=Dictionary
            if success, returns
                {
                    'status': 'success',
                    'delete_log': <string: deletion log>
                }
            if failure, returns
                {
                    'status': 'failure',
                    'error_msg': <string: error message>,
                    'delete_log': <string: deletion log>
                }
        """
        try:
            vnc_api = FilterModule._init_vnc_api(job_ctx)

            fabric_info = job_ctx.get('job_input')
            fabric_fq_name = fabric_info.get('fabric_fq_name')
            fabric_name = fabric_fq_name[-1]
            try:
                fabric_obj = vnc_api.fabric_read(
                    fq_name=fabric_fq_name, fields=['fabric_namespaces']
                )
            except NoIdError:
                fabric_obj = None

            # validate fabric deletion
            self._validate_fabric_deletion(vnc_api, fabric_obj)

            # delete fabric
            self._delete_fabric(vnc_api, fabric_obj)

            # delete fabric networks
            self._delete_fabric_network(
                vnc_api, fabric_name, NetworkType.MGMT_NETWORK
            )
            self._delete_fabric_network(
                vnc_api, fabric_name, NetworkType.LOOPBACK_NETWORK
            )
            self._delete_fabric_network(
                vnc_api, fabric_name, NetworkType.FABRIC_NETWORK
            )

            return {
                'status': 'success',
                'deletion_log': FilterLog.instance().dump()
            }
        except Exception as ex:
            self._logger.error(str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'deletion_log': FilterLog.instance().dump()
            }
    # end delete_fabric

    @staticmethod
    def _validate_fabric_deletion(vnc_api, fabric_obj):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param fabric_obj: <vnc_api.gen.resource_client.Fabric>
        :return:
        """
        _task_log('Validating no tenant virtual network created on the fabric')
        if fabric_obj and fabric_obj.get_physical_router_back_refs():
            vn_refs = vnc_api.virtual_networks_list().get('virtual-networks')
            for vn_ref in vn_refs or []:
                vn_fq_name = vn_ref.get('fq_name')
                if vn_fq_name[1] != 'default-project':
                    _task_done(
                        'Please delete tenant virtual network %s/%s first '
                        'before deleting this fabric' % (
                            vn_fq_name[1], vn_fq_name[2]
                        )
                    )
                    raise ValueError(
                        'Failed to delete fabric %s due to existing tenant '
                        'virtual network %s/%s.' % (
                            fabric_obj.name, vn_fq_name[1], vn_fq_name[2]
                        )
                    )
        _task_done()
    # end _validate_fabric_deletion

    def _delete_fabric(self, vnc_api, fabric_obj):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param fabric_obj: <vnc_api.gen.resource_client.Fabric>
        :return: None
        """
        if fabric_obj:
            # delete all fabric devices
            for device_ref in fabric_obj.get_physical_router_back_refs() or []:
                device_uuid = str(device_ref.get('uuid'))
                self._delete_fabric_device(vnc_api, device_uuid,
                                           fabric_fq_name=fabric_obj.fq_name)

            # delete all fabric namespaces
            for ns_ref in list(fabric_obj.get_fabric_namespaces() or []):
                _task_log(
                    'Deleting fabric namespace "%s"' % str(ns_ref['to'][-1])
                )
                vnc_api.fabric_namespace_delete(id=ns_ref.get('uuid'))
                _task_done()

            # un-assign node profiles
            _task_log('Unassigning node profiles from fabric')
            fabric_obj.set_node_profile_list([])
            vnc_api.fabric_update(fabric_obj)
            _task_done()

            # un-assign virtual networks
            fabric_obj.set_virtual_network_list([])
            _task_log('Unassigning virtual networks from fabric')
            vnc_api.fabric_update(fabric_obj)
            _task_done()

            _task_log('Deleting fabric "%s"' % fabric_obj.fq_name[-1])
            vnc_api.fabric_delete(fq_name=fabric_obj.fq_name)
            _task_done()
    # end _delete_fabric

    def _delete_fabric_device(
            self, vnc_api, device_uuid=None, device_fq_name=None,
            fabric_fq_name=None):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param device_uuid: string
        :param device_fq_name: list<string>: optional if missing device_uuid
        :param fabric_fq_name: list<string>
        """
        device_obj = None
        try:
            if device_uuid:
                device_obj = vnc_api.physical_router_read(
                    id=device_uuid, fields=['physical_interfaces']
                )
            elif device_fq_name:
                device_obj = vnc_api.physical_router_read(
                    fq_name=device_fq_name, fields=['physical_interfaces']
                )
        except NoIdError:
            _task_done(
                'Deleting device %s ... device not found'
                % (device_uuid if device_obj else device_fq_name)
            )
            return

        # delete loopback iip
        loopback_iip_name = "%s/lo0.0" % device_obj.name
        try:
            _task_log("deleting loopback instance-ip %s" % loopback_iip_name)
            vnc_api.instance_ip_delete(fq_name=[loopback_iip_name])
            _task_done()
        except NoIdError:
            _task_done("lookback instance-ip not found")

        # delete all interfaces
        for pi_ref in list(device_obj.get_physical_interfaces() or []):
            pi_uuid = str(pi_ref.get('uuid'))
            pi_obj = vnc_api.physical_interface_read(id=pi_uuid)

            # delete all the instance-ips for the fabric interfaces
            pi_mac = self._get_pi_mac(pi_obj)
            if pi_mac:
                iip_fq_name = [self._get_pi_mac(pi_obj).replace(':', '')]
                try:
                    _task_log(
                        "Deleting instance-ip %s for fabric interface %s"
                        % (iip_fq_name, pi_obj.fq_name)
                    )
                    vnc_api.instance_ip_delete(fq_name=iip_fq_name)
                    _task_done()
                except NoIdError:
                    _task_done(
                        "No instance_ip found for physical interface %s" %
                        pi_obj.fq_name
                    )

            # delete all the logical interfaces for this physical interface
            for li_ref in list(pi_obj.get_logical_interfaces() or []):
                li_uuid = str(li_ref.get('uuid'))
                li_obj = vnc_api.logical_interface_read(id=li_uuid)
                _task_log(
                    "Deleting logical interface %s => %s"
                    % (str(li_obj.fq_name[1]), str(li_obj.fq_name[3]))
                )
                vnc_api.logical_interface_delete(id=li_uuid)
                _task_done()

            _task_log(
                "Deleting physical interface %s => %s"
                % (str(pi_obj.fq_name[1]), str(pi_obj.fq_name[2]))
            )
            vnc_api.physical_interface_delete(id=pi_uuid)
            _task_done()

        # delete the corresponding bgp-router if exist
        self._delete_bgp_router(vnc_api, device_obj)

        # Now we can delete the device finally
        _task_log("Deleting deivce %s" % device_obj.display_name)
        vnc_api.physical_router_delete(id=device_obj.uuid)
        _task_done()
    # end _delete_fabric_device

    def _delete_bgp_router(self, vnc_api, device_obj):
        """
        delete corresponding bgp-router for a specific device
        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        :return: None
        """
        try:
            bgp_router_obj = vnc_api.bgp_router_read(
                fq_name=_bgp_router_fq_name(device_obj.name)
            )
            _task_log(
                "Removing bgp-router for device %s" % device_obj.name
            )
            device_obj.del_bgp_router(bgp_router_obj)
            vnc_api.physical_router_update(device_obj)
            vnc_api.bgp_router_delete(id=bgp_router_obj.uuid)
            _task_done()
        except NoIdError:
            self._logger.debug(
                'bgp-router for device %s does not exist' % device_obj.name
            )
    # end _delete_bgp_router

    def _delete_logical_router(self, vnc_api, device_obj, fabric_name):
        """
        delete reference from logical-router and logical-router itself if
        this is the last device
        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        :return: None
        """
        try:
            logical_router_fq_name = _logical_router_fq_name(fabric_name)
            logical_router_obj = vnc_api.logical_router_read(
                fq_name=logical_router_fq_name
            )
            _task_log(
                "Removing logical-router ref for device %s" % device_obj.name
            )
            logical_router_obj.del_physical_router(device_obj)
            vnc_api.logical_router_update(logical_router_obj)
            logical_router_obj = vnc_api.logical_router_read(
                fq_name=logical_router_fq_name
            )
            prouter_refs = logical_router_obj.get_physical_router_refs() or []
            # if no more physical-routers attached, delete the logical-router
            if len(prouter_refs) == 0:
                _task_log(
                    "Removing logical-router %s" % logical_router_fq_name
                )
                vnc_api.logical_router_delete(id=logical_router_obj.uuid)
            _task_done()
        except NoIdError:
            self._logger.debug(
                'logical-router for device %s does not exist' % device_obj.name
            )
    # end _delete_logical_router

    def _delete_fabric_network(self, vnc_api, fabric_name, network_type):
        """
        :param vnc_api: type=VncApi
        :param fabric_name: type=string
        :param network_type: type=enum {'management', 'loopback', 'ip-fabric'}
        """
        network_name = _fabric_network_name(fabric_name, network_type)
        network_fq_name = ['default-domain', 'default-project', network_name]
        try:
            vn_obj = vnc_api.virtual_network_read(
                fq_name=network_fq_name, fields=['routing_instances'])
        except NoIdError:
            self._logger.warn('Fabric network "%s" not found', network_name)
            vn_obj = None
        if vn_obj:
            if vn_obj.get_network_ipam_refs():
                _task_log(
                    'Unassigning network ipam from "%s" network' % network_type
                )
                vn_obj.set_network_ipam_list([])
                vnc_api.virtual_network_update(vn_obj)
                _task_done()

            for ri_ref in list(vn_obj.get_routing_instances() or []):
                _task_log(
                    'Deleting routing instance for fabric "%s"' % fabric_name
                )
                vnc_api.routing_instance_delete(id=ri_ref.get('uuid'))
                _task_done()

            _task_log('Deleting fabric network "%s"' % network_name)
            vnc_api.virtual_network_delete(fq_name=network_fq_name)
            _task_done()

        ipam_name = _fabric_network_ipam_name(fabric_name, network_type)
        ipam_fq_name = ['default-domain', 'default-project', ipam_name]
        try:
            if vnc_api.fq_name_to_id('network-ipam', ipam_fq_name):
                _task_log('Deleting network ipam "%s"' % ipam_name)
                vnc_api.network_ipam_delete(fq_name=ipam_fq_name)
                _task_done()
        except NoIdError:
            _task_done('network ipam "%s" not found' % ipam_name)
    # end _delete_fabric_network

    # ***************** delete_devices filter **********************************
    def delete_fabric_devices(self, job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "fabric_fq_name": [
                        "default-global-system-config",
                        "fab01"
                    ],
                    "devices": [
                        "DK588", "VF173"
                    ]
                }
            }
        :return: type=Dictionary
            if success, returns
                {
                    'status': 'success',
                    'delete_log': <string: deletion log>
                }
            if failure, returns
                {
                    'status': 'failure',
                    'error_msg': <string: error message>,
                    'delete_log': <string: deletion log>
                }
        """
        try:
            vnc_api = FilterModule._init_vnc_api(job_ctx)

            fabric_info = job_ctx.get('job_input')
            fabric_fq_name = fabric_info.get('fabric_fq_name')
            fabric_obj = None
            if fabric_fq_name:
                try:
                    fabric_obj = vnc_api.fabric_read(fq_name=fabric_fq_name)
                except NoIdError:
                    self._logger.debug(
                        'Fabric does not exist: %s', fabric_fq_name
                    )

            for device_name in job_ctx.get('job_input', {}).get('devices') or[]:
                device_fq_name = ['default-global-system-config', device_name]
                self._delete_fabric_device(
                    vnc_api, device_fq_name=device_fq_name,
                    fabric_fq_name=fabric_fq_name)

            return {
                'status': 'success',
                'deletion_log': FilterLog.instance().dump()
            }
        except Exception as ex:
            self._logger.error(str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'deletion_log': FilterLog.instance().dump()
            }
    # end delete_fabric

    # ***************** assign_roles filter ***********************************
    def assign_roles(self, job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "role_assignments": [
                        {
                            "device_fq_name": [
                                "default-global-system-config",
                                "qfx-10"
                            ],
                            "physical_role": "leaf",
                            "routing_bridging_roles": [ "CRB-Access" ]
                        }
                    ]
                }
            }
        :return: Dictionary
            if success, returns
                {
                    'status': 'success',
                    'log': <string: role assignment log>
                }
            if failure, returns
                {
                    'status': 'failure',
                    'error_msg': <string: error message>,
                    'log': <string: role assignment log>
                }
        """
        try:
            vnc_api = FilterModule._init_vnc_api(job_ctx)

            fabric_info = job_ctx.get('job_input')
            role_assignments = fabric_info.get('role_assignments', [])

            # load device objects into each device's role assignment data
            for device_roles in role_assignments:
                device_obj = vnc_api.physical_router_read(
                    fq_name=device_roles.get('device_fq_name'),
                    fields=[
                        'physical_router_vendor_name',
                        'physical_router_product_name',
                        'physical_interfaces',
                        'fabric_refs',
                        'node_profile_refs'
                    ]
                )
                device_roles['device_obj'] = device_obj
                node_profile_refs = device_obj.get_node_profile_refs()
                if not node_profile_refs:
                    self._logger.info(
                        "Capable role info not populated in physical router "
                        "(no node_profiles attached, cannot assign role for "
                        "device : %s" % device_obj.get(
                        'physical_router_management_ip'))
                else:
                    node_profile_fq_name = node_profile_refs[0].get('to')
                    node_profile_obj = vnc_api.node_profile_read(
                        fq_name=node_profile_fq_name,
                        fields=['node_profile_roles']
                    )
                    device_roles['supported_roles'] = \
                        node_profile_obj.get_node_profile_roles().\
                            get_role_mappings()

            # validate role assignment against device's supported roles
            self._validate_role_assignment(role_assignments)

            # before assigning roles, let's assign IPs to the loopback and
            # fabric interfaces, create bgp-router and logical-router, etc.
            for device_roles in role_assignments:
                device_obj = device_roles.get('device_obj')
                self._add_loopback_interface(vnc_api, device_obj)
                self._add_logical_interfaces_for_fabric_links(
                    vnc_api, device_obj
                )
                self._add_bgp_router(vnc_api, device_obj)


            # now we are ready to assign the roles to trigger DM to invoke
            # fabric_config playbook to push the role-based configuration to
            # the devices
            for device_roles in role_assignments:
                self._assign_device_roles(vnc_api, device_roles)

            return {
                'status': 'success',
                'assignment_log': FilterLog.instance().dump()
            }
        except Exception as ex:
            self._logger.error(str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'assignment_log': FilterLog.instance().dump()
            }
    # end assign_roles

    @staticmethod
    def _validate_role_assignment(role_assignments):
        """
        :param role_assignments: list<Dictionary>
            example:
            [
                {
                    "device_fq_name": [
                        "default-global-system-config",
                        "qfx-10"
                    ],
                    "physical_role": "leaf",
                    "routing_bridging_roles": [ "CRB-Access" ]
                }
            ]
        """
        for device_roles in role_assignments:
            device_obj = device_roles.get('device_obj')

            assigned_roles = []
            phys_role = device_roles.get('physical_role')
            if not phys_role:
                raise ValueError(
                    'No physical role assigned to %s' % device_obj.display_name
                )

            rb_roles = device_roles.get('routing_bridging_roles')
            if not rb_roles:
                rb_roles = ['null']

            supported_roles = device_roles.get('supported_roles')
            for role in supported_roles:
                if str(role.get_physical_role()) == phys_role:
                    if (set(rb_roles) < set(role.get_rb_roles())) or \
                            (set(rb_roles) == set(role.get_rb_roles())):
                        continue
                    else:
                        raise ValueError(
                            'role "%s : %s" is not supported. Here are the '
                            'supported roles : %s' % (phys_role, rb_roles,
                            supported_roles)
                        )
    # end _validate_role_assignments

    @staticmethod
    def _get_fabric_name(device_obj):
        # get fabric object that this device belongs to
        fabric_refs = device_obj.get_fabric_refs() or []
        if len(fabric_refs) != 1:
            raise ValueError(
                "Unable to assign roles for device %s that does not belong to "
                "any fabric" % str(device_obj.fq_name)
            )
        return str(fabric_refs[0].get('to')[-1])
    # end _get_fabric_name

    @staticmethod
    def _get_device_network(vnc_api, device_obj, network_type):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        :param network_type: string (One of constants defined in NetworkType)
        :return: <vnc_api.gen.resource_client.VirtualNetwork>
        """
        fabric_name = FilterModule._get_fabric_name(device_obj)

        # get network-ipam object for the fabric network
        try:
            network_name = _fabric_network_name(fabric_name, network_type)
            network_obj = vnc_api.virtual_network_read(
                fq_name=['default-domain', 'default-project', network_name]
            )
        except NoIdError:
            network_obj = None
        return network_obj
    # end _get_device_network

    def _add_loopback_interface(self, vnc_api, device_obj):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        """
        loopback_network_obj = self._get_device_network(
            vnc_api, device_obj, NetworkType.LOOPBACK_NETWORK
        )
        if not loopback_network_obj:
            self._logger.debug(
                "Loopback network does not exist, thereofore skip the loopback\
                 interface creation.")
            return

        # create loopback logical interface if needed
        loopback_li_fq_name = device_obj.fq_name + ['lo0', 'lo0.0']
        try:
            loopback_li_obj = vnc_api.logical_interface_read(
                fq_name=loopback_li_fq_name
            )
        except NoIdError:
            loopback_li_obj = LogicalInterface(
                name='lo0.0',
                fq_name=loopback_li_fq_name,
                parent_type='physical-interface',
                logical_interface_type='l3'
            )
            _task_log(
                'creating looback interface lo0.0 on device %s'
                % device_obj.name
            )
            vnc_api.logical_interface_create(loopback_li_obj)
            _task_done()

        # assgin instance IP to the loopback interface
        iip_name = "%s/lo0.0" % device_obj.name
        try:
            iip_obj = vnc_api.instance_ip_read(fq_name=[iip_name])
        except NoIdError:
            iip_obj = InstanceIp(name=iip_name, instant_ip_family='v4')
            iip_obj.set_logical_interface(loopback_li_obj)
            iip_obj.set_virtual_network(loopback_network_obj)
            _task_log(
                'Create instance ip for lo0.0 on device %s' % device_obj.name
            )
            iip_uuid = vnc_api.instance_ip_create(iip_obj)
            iip_obj = vnc_api.instance_ip_read(id=iip_uuid)
            _task_done()

        # update device level properties
        device_obj.physical_router_loopback_ip \
            = iip_obj.get_instance_ip_address()
        device_obj.physical_router_dataplane_ip \
            = iip_obj.get_instance_ip_address()
    # end _add_loopback_interface

    def _add_bgp_router(self, vnc_api, device_obj):
        """
        Add corresponding bgp-router object for this device. This bgp-router is
        used to model the overlay iBGP mesh
        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        :return: None
        """
        bgp_router_obj = None
        if device_obj.physical_router_loopback_ip:
            bgp_router_fq_name = _bgp_router_fq_name(device_obj.name)
            bgp_router_name = bgp_router_fq_name[-1]
            try:
                bgp_router_obj = vnc_api.bgp_router_read(
                    fq_name=bgp_router_fq_name
                )
            except NoIdError:
                fabric_name = FilterModule._get_fabric_name(device_obj)
                bgp_router_obj = BgpRouter(
                    name=bgp_router_name,
                    fq_name=bgp_router_fq_name,
                    parent_type='routing-instance',
                    bgp_router_parameters={
                        'vendor': device_obj.physical_router_vendor_name,
                        'router_type': 'router',
                        'address': device_obj.physical_router_loopback_ip,
                        'identifier': device_obj.physical_router_loopback_ip,
                        'address_families': {
                            "family": [
                                "inet-vpn",
                                "inet6-vpn",
                                "route-target",
                                "e-vpn"
                            ]
                        },
                        "autonomous_system": self._get_ibgp_asn(
                            vnc_api, fabric_name
                        ),
                        "hold_time": 90
                    }
                )
                vnc_api.bgp_router_create(bgp_router_obj)

            device_obj.add_bgp_router(bgp_router_obj)
        # end if
        return bgp_router_obj
    # end _add_bgp_router

    def _add_logical_router(self, vnc_api, device_obj, device_roles,
                            fabric_name):
        """
        Add logical-router object for this device if CRB gateway role
        :param vnc_api: <vnc_api.VncApi>
        :param device_roles: Dictionary
            example:
            {
                'device_obj': <vnc_api.gen.resource_client.PhysicalRouter>
                'device_fq_name': ['default-global-system-config', 'qfx-10'],
                'physical_role": 'leaf',
                'routing_bridging_roles": ['CRB-Gateway']
            }
        :param fabric_info: Fabric information containing fabric name
        :return: None
        """
        logical_router_obj = None
        try:
            rb_roles = device_roles.get('routing_bridging_roles') or []
            logical_router_fq_name = _logical_router_fq_name(fabric_name)
            logical_router_name = logical_router_fq_name[-1]
            logical_router_obj = vnc_api.logical_router_read(
                fq_name=logical_router_fq_name
            )
            if logical_router_obj:
                if 'CRB-Gateway' in rb_roles:
                    logical_router_obj.add_physical_router(device_obj)
                    vnc_api.logical_router_update(logical_router_obj)
                else:
                    if device_obj.get_logical_router_back_refs():
                        # delete the logical-router
                        self._delete_logical_router(vnc_api, device_obj, fabric_name)
                        logical_router_obj = None
        except NoIdError:
            if 'CRB-Gateway' in rb_roles:
                logical_router_obj = LogicalRouter(
                    name=logical_router_name,
                    fq_name=logical_router_fq_name,
                    parent_type='project'
                )
                vnc_api.logical_router_create(logical_router_obj)
                logical_router_obj.add_physical_router(device_obj)
                vnc_api.logical_router_update(logical_router_obj)
        return logical_router_obj
    # end _add_logical_router

    @staticmethod
    def _get_ibgp_asn(vnc_api, fabric_name):
        try:
            ibgp_asn_namespace_obj = vnc_api.fabric_namespace_read(fq_name=[
                'defaut-global-system-config', fabric_name, 'overlay_ibgp_asn'
            ])
            return ibgp_asn_namespace_obj.fabric_namespace_value.asn.asn[0]
        except NoIdError:
            gsc_obj = vnc_api.global_system_config_read(
                fq_name=['default-global-system-config']
            )
            return gsc_obj.autonomous_system
    # end _get_ibgp_asn

    def _add_logical_interfaces_for_fabric_links(self, vnc_api, device_obj):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        """

        # get fabric object that this device belongs to
        fabric_network_obj = self._get_device_network(
            vnc_api, device_obj, NetworkType.FABRIC_NETWORK
        )
        if not fabric_network_obj:
            self._logger.debug(
                "fabric network does not exist, hence skip the fabric\
                 interface creation.")
            return

        # create logical interfaces for all the fabric links from this device's
        # physical interfaces and assign instance-ip to the logical interface
        # if not assigned yet
        for link in self._get_device_fabric_links(vnc_api, device_obj) or []:
            local_pi = link.get('local_pi')
            remote_pi = link.get('remote_pi')

            local_li_name = self._build_li_name(device_obj, local_pi.name, 0)
            local_li_fq_name = local_pi.fq_name + [local_li_name]
            try:
                local_li = vnc_api.logical_interface_read(
                    fq_name=local_li_fq_name
                )
            except NoIdError:
                local_li = LogicalInterface(
                    name=local_li_name,
                    fq_name=local_li_fq_name,
                    parent_type='physical-interface',
                    logical_interface_type='l3'
                )
                _task_log(
                    'creating logical interface %s for physical link from %s to'
                    ' %s' % (local_li.name, local_pi.fq_name, remote_pi.fq_name)
                )
                vnc_api.logical_interface_create(local_li)
                _task_done()

            iip_refs = local_li.get_instance_ip_back_refs()
            if not iip_refs:
                local_mac = self._get_pi_mac(local_pi)
                if not local_mac:
                    raise ValueError(
                        "MAC address not found: %s" % str(local_pi.fq_name)
                    )

                remote_mac = self._get_pi_mac(remote_pi)
                if not remote_mac:
                    raise ValueError(
                        "MAC address not found: %s" % str(remote_pi.fq_name)
                    )

                subscriber_tag = _subscriber_tag(local_mac, remote_mac)
                iip_obj = InstanceIp(
                    name=local_mac.replace(':', ''),
                    instance_ip_family='v4',
                    instance_ip_subscriber_tag=subscriber_tag
                )
                iip_obj.set_virtual_network(fabric_network_obj)
                iip_obj.set_logical_interface(local_li)
                try:
                    _task_log(
                        'Create instance ip for logical interface %s'
                        % local_li.fq_name
                    )
                    vnc_api.instance_ip_create(iip_obj)
                    _task_done()
                except RefsExistError as ex:
                    _task_log(
                        'instance ip already exists for logical interface %s '
                        'or other conflict: %s' % (local_li.fq_name, str(ex))
                    )
                    vnc_api.instance_ip_update(iip_obj)
                    _task_done()
    # end _add_logical_interfaces_for_fabric_links

    @staticmethod
    def _build_li_name(
            device_obj, physical_interface_name, logical_interface_index):
        """
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        :param physical_interface_name: string
        :param logical_interface_index: string
        :return:
        """
        if device_obj.physical_router_vendor_name \
                and device_obj.physical_router_vendor_name.lower() == 'juniper':
            return "%s.%d" % (physical_interface_name, logical_interface_index)
        elif not device_obj.vendor:
            raise ValueError(
                "vendor not found for device %s" % str(device_obj.fq_name)
            )
        else:
            raise ValueError(
                "%s: no _build_li_name() implementation for vendor %s"
                % (str(device_obj.fq_name), str(device_obj.vendor))
            )
    # end _build_li_name

    @staticmethod
    def _get_pi_mac(phys_interface):
        """
        :param phys_interface: <vnc_api.gen.resource_client.PhysicalInterface>
        :return: physical interface mac address (type: string)
        """
        macs = phys_interface.physical_interface_mac_addresses
        pi_mac = None
        if macs and macs.get_mac_address():
            pi_mac = str(macs.get_mac_address()[0])
        return pi_mac
    # end _get_pi_mac

    @staticmethod
    def _get_device_fabric_links(vnc_api, device_obj):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        :return: list<Dictionary>
            [
                {
                   'local_pi': <vnc_api.gen.resource_client.PhysicalInterface>,
                   'remote_pi': <vnc_api.gen.resource_client.PhysicalInterface>
                }
            ]
        """
        physical_links = []
        pi_refs = device_obj.get_physical_interfaces()
        for ref in pi_refs or []:
            pi_obj = vnc_api.physical_interface_read(id=str(ref.get('uuid')))
            peer_pi_refs = pi_obj.get_physical_interface_refs()
            if peer_pi_refs:
                peer_pi_obj = vnc_api.physical_interface_read(
                    id=str(peer_pi_refs[0].get('uuid'))
                )
                physical_links.append({
                    'local_pi': pi_obj,
                    'remote_pi': peer_pi_obj
                })
        return physical_links
    # end _get_device_fabric_links

    @staticmethod
    def _assign_device_roles(vnc_api, device_roles):
        """
        :param vnc_api: VncApi
        :param device_roles: Dictionary
            example:
            {
                'device_obj': <vnc_api.gen.resource_client.PhysicalRouter>
                'device_fq_name': ['default-global-system-config', 'qfx-10'],
                'physical_role": 'leaf',
                'routing_bridging_roles": ['CRB-Gateway']
            }
        :return: None
        """
        device_obj = device_roles.get('device_obj')
        device_obj.physical_router_role \
            = device_roles.get('physical_role')

        rb_roles = device_roles.get('routing_bridging_roles', [])
        device_obj.routing_bridging_roles = RoutingBridgingRolesType(
            rb_roles=rb_roles
        )

        _task_log('Assign roles to device "%s"' % device_obj.fq_name)
        vnc_api.physical_router_update(device_obj)
        _task_done()
    # end _assign_device_roles


# ***************** tests *****************************************************
def _mock_job_ctx_onboard_fabric():
    return {
        "auth_token": "",
        "config_args": {
            "collectors": [
                "10.155.75.181:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "job_execution_id": "c37b199a-effb-4469-aefa-77f531f77758",
        "job_input": {
            "fabric_fq_name": ["default-global-system-config", "fab01"],
            "device_auth": {
                "root_password": "Embe1mpls"
            },
            "fabric_asn_pool": [
                {
                    "asn_max": 65000,
                    "asn_min": 64000
                },
                {
                    "asn_max": 65100,
                    "asn_min": 65000
                }
            ],
            "fabric_subnets": [
                "30.1.1.1/24"
            ],
            "loopback_subnets": [
                "20.1.1.1/24"
            ],
            "management_subnets": [
                {"cidr": "10.1.1.1/24", "gateway": "10.1.1.1"}
            ],
            "node_profiles": [
                {
                    "node_profile_name": "juniper-qfx5k"
                }
            ],
            "device_count": 5
        },
        "job_template_fqname": [
            "default-global-system-config",
            "fabric_onboard_template"
        ]
    }
# end _mock_job_ctx_onboard_fabric


def _mock_job_ctx_onboard_brownfield_fabric():
    return {
        "auth_token": "",
        "config_args": {
            "collectors": [
                "10.155.75.181:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "job_execution_id": "c37b199a-effb-4469-aefa-77f531f77758",
        "job_input": {
            "fabric_fq_name": ["default-global-system-config", "fab01"],
            "device_auth": [{
                "username": "root",
                "password": "Embe1mpls"
            }],
            "management_subnets": [
                {"cidr": "10.1.1.1/24"}
            ],
            "overlay_ibgp_asn": 64600,
            "node_profiles": [
                {
                    "node_profile_name": "juniper-qfx5k"
                }
            ]
        },
        "job_template_fqname": [
            "default-global-system-config",
            "existing_fabric_onboard_template"
        ]
    }
# end _mock_job_ctx_onboard_fabric


def _mock_job_ctx_delete_fabric():
    return {
        "auth_token": "",
        "config_args": {
            "collectors": [
                "10.155.75.181:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "job_execution_id": "c37b199a-effb-4469-aefa-77f531f77758",
        "job_input": {
            "fabric_fq_name": ["default-global-system-config", "fab01"]
        },
        "job_template_fqname": [
            "default-global-system-config",
            "fabric_deletion_template"
        ]
    }
# end _mock_job_ctx_delete_fabric


def _mock_job_ctx_delete_devices():
    return {
        "auth_token": "",
        "config_args": {
            "collectors": [
                "10.155.75.181:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "job_execution_id": "c37b199a-effb-4469-aefa-77f531f77758",
        "job_input": {
            "fabric_fq_name": ["default-global-system-config", "fab01"],
            "devices": ['DK588', 'VF3717350117']
        },
        "job_template_fqname": [
            "default-global-system-config",
            "device_deletion_template"
        ]
    }
# end _mock_job_ctx_delete_fabric


def _mock_job_ctx_assign_roles():
    return {
        "auth_token": "",
        "config_args": {
            "collectors": [
                "10.155.75.181:8086"
            ],
            "fabric_ansible_conf_file": [
                "/etc/contrail/contrail-keystone-auth.conf",
                "/etc/contrail/contrail-fabric-ansible.conf"
            ]
        },
        "job_execution_id": "c37b199a-effb-4469-aefa-77f531f77758",
        "job_input": {
            "fabric_fq_name": ["default-global-system-config", "fab01"],
            "role_assignments": [
                {
                    "device_fq_name": [
                        "default-global-system-config",
                        "DK588"
                    ],
                    "physical_role": "spine",
                    "routing_bridging_roles": ["CRB-Gateway"]
                },
                {
                    "device_fq_name": [
                        "default-global-system-config",
                        "VF3717350117"
                    ],
                    "physical_role": "leaf",
                    "routing_bridging_roles": ["CRB-Access"]
                }
            ]
        },
        "job_template_fqname": [
            "default-global-system-config",
            "role_assignment_template"
        ]
    }
# end _mock_job_ctx_delete_fabric


def _mock_supported_roles():
    return {
        "juniper-qfx5100-48s-6q": [
            "CRB-Access@leaf",
            "null@spine"
        ],
        "juniper-qfx10002-72q": [
            "null@spine",
            "CRB-Gateway@spine",
            "DC-Gateway@spine",
            "CRB-Access@leaf",
            "CRB-Gateway@leaf",
            "DC-Gateway@leaf"
        ]
    }
# end _mock_supported_roles


def _parse_args():
    arg_parser = argparse.ArgumentParser(description='fabric filters tests')
    arg_parser.add_argument('-c', '--create_fabric',
                            action='store_true', help='Onbaord fabric')
    arg_parser.add_argument('-ce', '--create_existing_fabric',
                            action='store_true', help='Onbaord existing fabric')
    arg_parser.add_argument('-df', '--delete_fabric',
                            action='store_true', help='Delete fabric')
    arg_parser.add_argument('-dd', '--delete_devices',
                            action='store_true', help='Delete devices')
    arg_parser.add_argument('-a', '--assign_roles',
                            action='store_true', help='Assign roles')
    return arg_parser.parse_args()
# end _parse_args


def __main__():
    _parse_args()

    fabric_filter = FilterModule()
    parser = _parse_args()
    results = {}
    if parser.create_fabric:
        results = fabric_filter.onboard_fabric(_mock_job_ctx_onboard_fabric())
    elif parser.create_existing_fabric:
        results = fabric_filter.onboard_brownfield_fabric(
            _mock_job_ctx_onboard_brownfield_fabric()
        )
    elif parser.delete_fabric:
        results = fabric_filter.delete_fabric(_mock_job_ctx_delete_fabric())
    elif parser.delete_devices:
        results = fabric_filter.delete_fabric_devices(
            _mock_job_ctx_delete_devices()
        )
    elif parser.assign_roles:
        results = fabric_filter.assign_roles(
            _mock_job_ctx_assign_roles(), _mock_supported_roles()
        )
    print results
# end __main__


if __name__ == '__main__':
    __main__()
