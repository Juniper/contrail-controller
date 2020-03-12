#!/usr/bin/python
#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
# This file contains implementation for fabric related Ansible filter plugins
#
from __future__ import print_function

import argparse
from builtins import object
from builtins import range
from builtins import str
import copy
import ipaddress
import json
import socket
import struct
import sys
import traceback
import uuid

from cfgm_common.exceptions import (
    NoIdError,
    RefsExistError
)
import jsonschema
from netaddr import IPNetwork
from vnc_api.gen.resource_client import (
    BgpRouter,
    Fabric,
    FabricNamespace,
    InstanceIp,
    LogicalInterface,
    LogicalRouter,
    NetworkIpam,
    VirtualNetwork,
    IntentMap
)
from vnc_api.gen.resource_xsd import (
    FabricNetworkTag,
    IpamSubnets,
    IpamSubnetType,
    KeyValuePair,
    KeyValuePairs,
    NamespaceValue,
    RoutingBridgingRolesType,
    SerialNumListType,
    SubnetListType,
    SubnetType,
    VirtualNetworkType,
    VnSubnetsType
)

from job_manager.job_utils import (
    JobAnnotations,
    JobVncApi
)

sys.path.append("/opt/contrail/fabric_ansible_playbooks/module_utils")
from filter_utils import (  # noqa
    _task_debug_log,
    _task_done,
    _task_error_log,
    _task_log,
    _task_warn_log,
    FilterLog,
    vnc_bulk_get,
    get_job_transaction,
    set_job_transaction
)


GSC = 'default-global-system-config'


def _compare_fq_names(this_fq_name, that_fq_name):
    """Compare FQ names.

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

def _fabric_intent_map_name(fabric_name, intent_map):
    """Fabric intent map name.

    :param fabric_name: string
    :param intent_map: string AR intent map name
    :return: string
    """
    return '%s-%s' % (fabric_name, intent_map)
# end _fabric_intent_map_name

def _fabric_network_name(fabric_name, network_type):
    """Fabric network name.

    :param fabric_name: string
    :param network_type: string (One of the constants defined in NetworkType)
    :return: string
    """
    return '%s-%s-network' % (fabric_name, network_type)
# end _fabric_network_name


def _fabric_network_ipam_name(fabric_name, network_type):
    """Fabric network IPAM name.

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
    macs = [local_mac, remote_mac]
    macs.sort()
    return "%s-%s" % (macs[0], macs[1])
# end _subscriber_tag


def _ip2int(addr):
    return struct.unpack("!I", socket.inet_aton(addr))[0]


def _int2ip(addr):
    return socket.inet_ntoa(struct.pack("!I", addr))


class NetworkType(object):
    """Pre-defined network types."""

    MGMT_NETWORK = 'management'
    LOOPBACK_NETWORK = 'loopback'
    FABRIC_NETWORK = 'ip-fabric'
    PNF_SERVICECHAIN_NETWORK = 'pnf-servicechain'

    def __init__(self):
        """Init."""
        pass
# end NetworkType


class FilterModule(object):
    """Fabric filter plugins."""

    @staticmethod
    def _validate_job_ctx(vnc_api, job_ctx, brownfield):
        """Validate job context.

        :param vnc_api: <vnc_api.VncApi>
        :param job_ctx: Dictionary
        example
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
            fq_name=job_template_fqname, fields=['job_template_input_schema']
        )
        input_schema = fabric_onboard_template.get_job_template_input_schema()
        input_schema = json.loads(input_schema)
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

        return job_input, job_template_fqname
    # end _validate_job_ctx

    @staticmethod
    def _validate_mgmt_subnets(vnc_api, fab_fq_name, mgmt_subnets, brownfield):
        """Validate management subnets.

        :param vnc_api: <vnc_api.VncApi>
        :param mgmt_subnets: List<Dict>
        example
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
        mgmt_tag = vnc_api.tag_read(fq_name=['label=fabric-management-ip'],
                                    fields=['fabric_namespace_back_refs'])
        for ref in mgmt_tag.get_fabric_namespace_back_refs() or []:
            namespace_obj = vnc_api.fabric_namespace_read(
                id=ref.get('uuid'),
                fields=['fq_name',
                        'fabric_namespace_type',
                        'fabric_namespace_value'])

            # skip namespaces belong to this fabric
            if _compare_fq_names(namespace_obj.fq_name[:-1], fab_fq_name):
                continue

            # make sure mgmt subnets not overlapping with other existing
            # fabrics
            if str(namespace_obj.fabric_namespace_type) == 'IPV4-CIDR':
                ipv4_cidr = namespace_obj.fabric_namespace_value.ipv4_cidr
                for sn in ipv4_cidr.subnet:
                    network = IPNetwork(str(sn.ip_prefix) + '/' +
                                        str(sn.ip_prefix_len))
                    for mgmt_network in mgmt_networks:
                        if network in mgmt_network or mgmt_network in network:
                            _task_done(
                                'detected overlapping management subnet %s '
                                'in fabric %s' % (
                                    str(network), namespace_obj.fq_name[-2]
                                )
                            )
                            raise ValueError("Overlapping mgmt subnet"
                                             "detected")
        _task_done()
    # end _validate_mgmt_subnets

    def filters(self):
        """Fabric filters."""
        return {
            'onboard_fabric': self.onboard_fabric,
            'onboard_existing_fabric': self.onboard_brownfield_fabric,
            'delete_fabric': self.delete_fabric,
            'delete_devices': self.delete_fabric_devices,
            'assign_roles': self.assign_roles,
            'validate_device_functional_group':
                self.validate_device_functional_group,
            'update_physical_router_annotations':
                self.update_physical_router_annotations
        }
    # end filters

    # ***************** onboard_fabric filter *********************************
    def onboard_fabric(self, job_ctx):
        """Onboard fabric.

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
            FilterLog.instance("FabricOnboardFilter")
            vnc_api = JobVncApi.vnc_init(job_ctx)
            fabric_info, job_template_fqname = FilterModule._validate_job_ctx(
                vnc_api, job_ctx, False
            )
            fabric_obj = self._onboard_fabric(
                vnc_api, fabric_info, job_template_fqname
            )
            return {
                'status': 'success',
                'fabric_uuid': fabric_obj.uuid,
                'manage_underlay':
                    job_ctx.get('job_input').get('manage_underlay', True),
                'onboard_log': FilterLog.instance().dump()
            }
        except Exception as ex:
            errmsg = "Unexpected error: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'onboard_log': FilterLog.instance().dump()
            }
    # end onboard_fabric

    # ***************** onboard_brownfield_fabric filter *********************
    def onboard_brownfield_fabric(self, job_ctx):
        """Onboard brownfield fabric.

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
            FilterLog.instance("FabricOnboardBrownfieldFilter")
            vnc_api = JobVncApi.vnc_init(job_ctx)
            fabric_info, job_template_fqname = FilterModule._validate_job_ctx(
                vnc_api, job_ctx, True
            )
            fabric_obj = self._onboard_fabric(
                vnc_api, fabric_info, job_template_fqname
            )
            return {
                'status': 'success',
                'fabric_uuid': fabric_obj.uuid,
                'manage_underlay':
                    job_ctx.get('job_input').get('manage_underlay', False),
                'onboard_log': FilterLog.instance().dump()
            }
        except Exception as ex:
            errmsg = "Unexpected error: %s\n%s" % (
                str(ex), traceback.format_exc()
            )
            _task_error_log(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'onboard_log': FilterLog.instance().dump()
            }
    # end onboard_existing_fabric

    def _onboard_fabric(self, vnc_api, fabric_info, job_template_fqname):
        """Onboard fabric.

        :param vnc_api: <vnc_api.VncApi>
        :param fabric_info: Dictionary
        :param job_template_fqname: job template fqname
        :return: <vnc_api.gen.resource_client.Fabric>
        """
        # Validate that each hostname is not already configured with a
        # different serial number
        self._validate_device_to_ztp(vnc_api, fabric_info)

        # Validate the inputs.
        self._validate_fabric_input(fabric_info)

        # Create fabric object and install in database
        fabric_obj = self._create_fabric(vnc_api, fabric_info)

        # add fabric annotations
        self._add_fabric_annotations(
            vnc_api,
            fabric_obj,
            job_template_fqname,
            fabric_info
        )

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
            peer_subnets = self._carve_out_subnets(fabric_subnets, 30)
            self._add_fabric_vn(
                vnc_api,
                fabric_obj,
                NetworkType.FABRIC_NETWORK,
                peer_subnets,
                True
            )

        # PNF Servicechain network
        if fabric_info.get('pnf_servicechain_subnets'):
            pnf_servicechain_subnets = [
                {
                    'cidr': subnet
                } for subnet in fabric_info.get('pnf_servicechain_subnets')
            ]
            self._add_cidr_namespace(
                vnc_api,
                fabric_obj,
                'pnf-servicechain-subnets',
                pnf_servicechain_subnets,
                'label=fabric-pnf-servicechain-ip'
            )
            pnf_sc_subnets = self._carve_out_subnets(
                pnf_servicechain_subnets, 29
            )
            self._add_fabric_vn(
                vnc_api,
                fabric_obj,
                NetworkType.PNF_SERVICECHAIN_NETWORK,
                pnf_sc_subnets,
                True
            )

        # ASN pool for underlay eBGP
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

        # Add os_version
        if fabric_info.get('os_version'):
            self._add_fabric_os_version(vnc_api, fabric_obj,
                                        fabric_info.get('os_version'))

        # Add enterprise style
        if fabric_info.get('enterprise_style') is not None:
            self._add_fabric_enterprise_style(
                vnc_api, fabric_obj, fabric_info.get('enterprise_style'))

        # add node profiles
        self._add_node_profiles(
            vnc_api,
            fabric_obj,
            fabric_info.get('node_profiles')
        )

        # create default logical router to be used to attach routed
        # virtual-networks which are part of master routing table on
        # leaf/spine devices
        self._add_default_logical_router(vnc_api)

        #add intent-map object
        self._add_intent_maps(
            vnc_api,
            fabric_obj
        )

        return fabric_obj
    # end onboard_fabric

    @staticmethod
    def _validate_fabric_input(fabric_info):
        if fabric_info.get('fabric_asn_pool'):
            for asn_range in fabric_info.get('fabric_asn_pool'):
                asn_min = asn_range.get('asn_min')
                asn_max = asn_range.get('asn_max')
                if asn_min > asn_max:
                    raise ValueError(
                        'asn_min {} is greater than asn_max {} '.
                        format(asn_min, asn_max))
                elif asn_min == asn_max:
                    raise ValueError(
                        "asn_min={} and asn_max={} are same.".
                        format(asn_min, asn_max))

    def _valid_loopback_ip(self, fabric_info, dtz):
        loopback_ip = dtz.get('loopback_ip')
        if not loopback_ip:
            return True
        loopback_subnets = fabric_info.get('loopback_subnets', [])
        for loopback_subnet in loopback_subnets:
            if loopback_ip in IPNetwork(loopback_subnet):
                return True
        _task_warn_log("loopback_ip {} out of range for device {}".format(
            loopback_ip, dtz.get('hostname')))
        return False

    def _valid_mgmt_ip(self, fabric_info, dtz):
        mgmt_ip = dtz.get('mgmt_ip')
        if not mgmt_ip:
            return True
        mgmt_subnets = fabric_info.get('management_subnets', [])
        for mgmt_subnet in mgmt_subnets:
            if mgmt_ip in IPNetwork(mgmt_subnet.get('cidr')):
                return True
        _task_warn_log("mgmt_ip {} out of range for device {}".format(
            mgmt_ip, dtz.get('hostname')))
        return False

    def _valid_underlay_asn(self, fabric_info, dtz):
        asn = dtz.get('underlay_asn')
        if not asn:
            return True
        asn = int(asn)
        dtz['underlay_asn'] = asn
        asn_pool = fabric_info.get('fabric_asn_pool', [])
        for asn_range in asn_pool:
            if asn_range['asn_min'] <= asn <= asn_range['asn_max']:
                return True
        _task_warn_log("underlay_asn {} out of range for device {}".format(
            asn, dtz.get('hostname')))
        return False

    def _validate_device_to_ztp(self, vnc_api, fabric_info):
        # Make sure physical router does not already exist with a different
        # serial number
        final_device_to_ztp = []
        for dtz in fabric_info.get('device_to_ztp', []):
            ser_num = dtz.get('serial_number')
            device_name = dtz.get('hostname')
            to_ztp = dtz.get('to_ztp')
            device_fq_name = ['default-global-system-config', device_name]

            # Validate the loopback_ip is within range
            if not self._valid_loopback_ip(fabric_info, dtz):
                del dtz['loopback_ip']

            # Validate the mgmt_ip is within range
            if not self._valid_mgmt_ip(fabric_info, dtz):
                del dtz['mgmt_ip']

            # Validate the underlay_asn is within range
            if not self._valid_underlay_asn(fabric_info, dtz):
                del dtz['underlay_asn']

            try:
                device_obj = vnc_api.physical_router_read(
                    fq_name=device_fq_name,
                    fields=['physical_router_serial_number'])
            except NoIdError:
                # Device not found, that's OK
                # Run ZTP on this device unless to_ztp flag is false
                if not to_ztp or to_ztp is True:
                    final_device_to_ztp.append(dtz)
                continue

            # verify that serial number in the input is the same as that
            # stored on the object
            if device_obj:
                obj_ser_num = device_obj.get_physical_router_serial_number()
                if obj_ser_num != ser_num:
                    raise ValueError(
                        'Device {} found in database with '
                        'different serial number: {} vs {}'.
                        format(device_name, obj_ser_num, ser_num))

            # Device is already discovered, but ZTP anyway
            if to_ztp and to_ztp is True:
                final_device_to_ztp.append(dtz)

        # We have removed entries which have already been ZTP'd
        # or which have been marked to skip
        fabric_info['device_to_ztp'] = final_device_to_ztp

    @staticmethod
    def _carve_out_subnets(subnets, cidr):
        """Carve out subnets.

        :param subnets: type=list<Dictionary>
        :param cidr: type=int
            example:
            [
                { 'cidr': '192.168.10.1/24', 'gateway': '192.168.10.1 }
            ]
            cidr = 30
        :return: list<Dictionary>
            example:
            [
                { 'cidr': '192.168.10.1/30'}
            ]
        """
        carved_subnets = []
        for subnet in subnets:
            slash_x_subnets = IPNetwork(subnet.get('cidr')).subnet(cidr)
            for slash_x_sn in slash_x_subnets:
                carved_subnets.append({'cidr': str(slash_x_sn)})
        return carved_subnets
    # end _carve_out_subnets

    @staticmethod
    def _create_fabric(vnc_api, fabric_info):
        """Create fabric.

        :param vnc_api: <vnc_api.VncApi>
        :param fabric_info: dynamic object from job input schema via
                            python_jsonschema_objects
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
            parent_type='global-system-config'
        )
        # Hide credentials when caching job input
        device_auth = copy.deepcopy(fabric_info.get('device_auth', []))
        fabric_info['device_auth'] = []
        fab.set_annotations(KeyValuePairs([
            KeyValuePair(key='user_input', value=json.dumps(fabric_info))
        ]))
        fabric_info['device_auth'] = device_auth

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
        """Add CIDR namespace.

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
        """Add overlay ASN namespace.

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
    def _add_fabric_os_version(vnc_api, fab, os_version):
        _task_log(
            'adding fabric os version "%s" to fabric "%s"'
            % (os_version, fab.name)
        )
        fab.set_fabric_os_version(os_version)
        vnc_api.fabric_update(fab)

    @staticmethod
    def _add_asn_range_namespace(vnc_api, fab, ns_name, asn_ranges, tag):
        """Add ASN range namespace.

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
    def _add_fabric_enterprise_style(vnc_api, fab, enterprise_style):
        _task_log(
            'adding enterprise style "%s" to fabric "%s"'
            % (enterprise_style, fab.name)
        )
        fab.set_fabric_enterprise_style(enterprise_style)
        vnc_api.fabric_update(fab)

    @staticmethod
    def _add_node_profiles(vnc_api, fabric_obj, node_profiles):
        """Add node profiles.

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
            np_fq_name = [GSC, name]
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
    def _add_default_logical_router(vnc_api):
        """Add default logical router

        :param vnc_api: <vnc_api.VncApi>
        :return: <vnc_api.gen.resource_client.LogicalRouter>
        """
        lr_fq_name = ['default-domain', 'admin', 'master-LR']

        # read admin project, master LR belongs to admin project
        admin_project = vnc_api.project_read(['default-domain', 'admin'])
        _task_log("creating logical router network, parent %s"
                  % (admin_project.get_uuid()))

        master_lr = LogicalRouter(
            name='master-LR',
            fq_name=lr_fq_name,
            logical_router_gateway_external=False,
            logical_router_type='vxlan-routing',
            parent_obj=admin_project)
        try:
            vnc_api.logical_router_create(master_lr)
        except RefsExistError as ex:
            _task_log(
                "Logical router already exists or other conflict: %s"
                % (str(ex)))
            vnc_api.logical_router_update(master_lr)

        master_lr = vnc_api.logical_router_read(fq_name=lr_fq_name)
        _task_done()
        return master_lr

    @staticmethod
    def _add_intent_maps(vnc_api, fabric_obj):
        """Add intent map objects.

        create intent-map object for assisted-replicator intent-type and add it
        to the fabric
        :param vnc_api: <vnc_api.VncApi>
        :param fabric_obj: <vnc_api.gen.resource_client.Fabric>
        :return:
        """

        intent_map_name = _fabric_intent_map_name(
            str(fabric_obj.name), 'assisted-replicator-intent-map')


        intent_map_fq_name = ['default-global-system-config',
                              intent_map_name]

        _task_log('Creating Intent Map %s' % intent_map_fq_name)
        intent_obj = IntentMap(
            name=intent_map_name,
            fq_name=intent_map_fq_name,
            intent_map_intent_type='assisted-replicator'
        )
        try:
            vnc_api.intent_map_create(intent_obj)
        except RefsExistError as ex:
            _task_log(
                "Intent Map '%s' already exists or other conflict: %s"
                % (intent_map_fq_name, str(ex))
            )
            vnc_api.intent_map_update(intent_obj)

        fabric_obj.add_intent_map(intent_obj)
        _task_log(
            'assigning %s intent map to fabric %s'
            % (intent_map_fq_name[-1], fabric_obj.name)
        )
        vnc_api.fabric_update(fabric_obj)
        _task_done()

    @staticmethod
    def _add_virtual_network(vnc_api, network_name):
        """Add virtual network.

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
            virtual_network_properties=VirtualNetworkType(
                forwarding_mode='l3'),
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
        """Create a new subnet.

        :param cidr: string, example: '10.1.1.1/24'
        :return: <vnc_api.gen.resource_xsd.SubnetType>
        """
        split_cidr = cidr.split('/')
        return SubnetType(ip_prefix=split_cidr[0], ip_prefix_len=split_cidr[1])
    # end _new_subnet

    @staticmethod
    def _add_network_ipam(vnc_api, ipam_name, subnets, subnetting):
        """Add network IPAM.

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
        """Add fabric VN.

        :param vnc_api: <vnc_api.VncApi>
        :param fabric_obj: <vnc_api.gen.resource_client.Fabric>
        :param network_type: string, one of the NetworkType constants
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

    @staticmethod
    def _add_fabric_annotations(
            vnc_api, fabric_obj, onboard_job_template_fqname, job_input):
        """Add fabric annotations.

        :param vnc_api: <vnc_api.VncApi>
        :param fabric_obj: <vnc_api.gen.resource_client.Fabric>
        :param onboard_job_template_fqname: string
        :param job_input: dict
        """
        # First add default annotations for all job templates
        jt_refs = vnc_api.job_templates_list().get('job-templates')
        ja = JobAnnotations(vnc_api)
        for jt_ref in jt_refs:
            job_template_fqname = jt_ref.get('fq_name')
            def_job_input = ja.generate_default_json(job_template_fqname)
            ja.cache_job_input(fabric_obj.uuid, job_template_fqname[-1],
                               def_job_input)

        # Hide credentials when caching job input
        device_auth = copy.deepcopy(job_input.get('device_auth', []))
        job_input['device_auth'] = []
        # Now update the fabric onboarding annotation with the current
        # job input
        ja.cache_job_input(fabric_obj.uuid, onboard_job_template_fqname[-1],
                           job_input)
        job_input['device_auth'] = device_auth
    # end _add_fabric_annotations

    # ***************** delete_fabric filter **********************************
    def delete_fabric(self, job_ctx):
        """Delete fabric.

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
            FilterLog.instance("FabricDeleteFilter")
            vnc_api = JobVncApi.vnc_init(job_ctx)
            fabric_info = job_ctx.get('job_input')
            fabric_fq_name = fabric_info.get('fabric_fq_name')
            fabric_name = fabric_fq_name[-1]
            fabric_obj = self._read_fabric_obj(vnc_api, fabric_fq_name)
            job_transaction_info = get_job_transaction(job_ctx)

            # validate fabric deletion
            self._validate_fabric_deletion(vnc_api, fabric_obj)

            # delete fabric
            self._delete_fabric(vnc_api, fabric_obj, job_transaction_info)

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
            self._delete_fabric_network(
                vnc_api, fabric_name, NetworkType.PNF_SERVICECHAIN_NETWORK
            )

            #delete intent map object
            self._delete_intent_maps(vnc_api, fabric_name)

            return {
                'status': 'success',
                'deletion_log': FilterLog.instance().dump()
            }
        except Exception as ex:
            _task_error_log(str(ex))
            _task_error_log(traceback.format_exc())
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'deletion_log': FilterLog.instance().dump()
            }
    # end delete_fabric

    @staticmethod
    def _read_fabric_obj(vnc_api, fq_name, fields=None):
        try:
            fabric_obj = vnc_api.fabric_read(
                fq_name=fq_name,
                fields=fields
            )
        except NoIdError:
            fabric_obj = None
        return fabric_obj
    # end _get_fabric

    def _auto_generated_virtual_network(self, vn_fq_name, fabric_name):
        # If tenant virtual network, it's not auto-generated
        if vn_fq_name[1] != 'default-project':
            return False
        # If it's one of the VNs always generated in default-project
        if vn_fq_name[2] in ["dci-network",
                             "default-virtual-network",
                             "__link_local__",
                             "ip-fabric"]:
            return True
        # If it's one of the fabric-specific VNs in default-project
        for vn_name in [NetworkType.MGMT_NETWORK,
                        NetworkType.LOOPBACK_NETWORK,
                        NetworkType.FABRIC_NETWORK,
                        NetworkType.PNF_SERVICECHAIN_NETWORK]:
            if fabric_name + '-' + vn_name == vn_fq_name[2]:
                return True
        # Otherwise false
        return False
    # end _auto_generated_virtual_network

    def _get_virtual_network_refs(self, device, fabric_obj):
        # Get virtual networks used by fabric
        vn_list = set()
        for vn_ref in device.get('virtual_network_refs') or []:
            vn_fq_name = vn_ref.get('to')
            if not self._auto_generated_virtual_network(vn_fq_name,
                                                        fabric_obj.name):
                vn_list.add(':'.join(vn_fq_name))
        return vn_list
    # end _get_virtual_network_refs

    def _get_logical_router_refs(self, device):
        # Get logical routers used by fabric
        lr_list = set()
        for lr_ref in device.get('logical_router_back_refs') or []:
            lr_fq_name = lr_ref.get('to')
            lr_list.add(':'.join(lr_fq_name))
        return lr_list
    # end _get_logical_router_refs

    def _get_virtual_port_group_refs(self, vnc_api, fabric_obj, device_uuids,
                                     is_device_del):
        vpg_list = set()
        vpg_refs = fabric_obj.get_virtual_port_groups() or []
        for vpg_ref in vpg_refs:
            vpg_fq_name = vpg_ref.get('to')
            if is_device_del:
                # If device deletion, only check for VPGs on these devices
                vpg_uuid = vpg_ref.get('uuid')
                vpg_obj = vnc_api.virtual_port_group_read(id=vpg_uuid)
                pi_refs = vpg_obj.get_physical_interface_refs() or []
                for pi_ref in pi_refs:
                    pi_uuid = pi_ref.get('uuid')
                    pi_obj = vnc_api.physical_interface_read(id=pi_uuid)
                    device_uuid = pi_obj.parent_uuid
                    if device_uuid in device_uuids:
                        vpg_list.add(vpg_fq_name[1] + ':' + vpg_fq_name[2])
                        break
            else:
                # Fabric deletion, so check all VPGs
                vpg_list.add(vpg_fq_name[1] + ':' + vpg_fq_name[2])
        return vpg_list
    # end _get_virtual_port_group_refs

    def _get_pnf_service_refs(self, vnc_api, device_names):
        # Get PNF services used by fabric
        svc_list = set()
        sa_refs = vnc_api.service_appliances_list(
            fields=['physical_interface_refs']).\
            get('service-appliances')
        for sa_ref in sa_refs or []:
            svc_fq_name = sa_ref.get('fq_name')
            svc_name = svc_fq_name[2].replace('-appliance', '')
            pi_refs = sa_ref.get('physical_interface_refs') or []
            for pi_ref in pi_refs:
                pi_fq_name = pi_ref.get('to')
                device_name = pi_fq_name[1]
                if device_name in device_names:
                    svc_list.add(svc_name)
                    break
        return svc_list
    # end _get_pnf_service_refs

    def _get_vmi_refs(self, vnc_api, device_uuids):
        vmi_list = set()

        # Get physical interface refs for all the devices in fabric
        pi_refs = vnc_bulk_get(
            vnc_api, 'physical_interfaces', parent_uuids=device_uuids
        )

        # Create list of physical interface UUIDs
        pi_uuids = [ref['uuid'] for ref in pi_refs]

        # Get all logical interfaces refs for all physical interfaces in fabric
        li_refs = vnc_bulk_get(
            vnc_api, 'logical_interfaces', parent_uuids=pi_uuids
        )

        # Create list of physical interface UUIDs
        li_uuids = [ref['uuid'] for ref in li_refs]

        # Get actual logical interface object
        li_list = vnc_bulk_get(
            vnc_api, 'logical_interfaces', obj_uuids=li_uuids,
            fields=['virtual_machine_interface_refs']
        )

        # Find VMI UUIDs on logical interfaces
        vmi_uuids = []
        for li in li_list:
            for vmi_ref in li.get('virtual_machine_interface_refs') or []:
                vmi_uuids.append(vmi_ref.get('uuid'))

        # Get VMI objects on all logical interfaces in fabric
        vmi_refs = vnc_bulk_get(
            vnc_api, 'virtual_machine_interfaces', obj_uuids=vmi_uuids,
            fields=['virtual_machine_interface_mac_addresses']
        )

        # Extract MAC address for each VMI and add to list
        for vmi_ref in vmi_refs:
            vmi_mac_addrs = vmi_ref.get(
                'virtual_machine_interface_mac_addresses')
            if vmi_mac_addrs:
                vmi_list.add(vmi_mac_addrs['mac_address'][0])

        return vmi_list
    # end _get_vmi_refs

    def _check_object_references(self, vnc_api, fabric_obj, device_uuids,
                                 is_device_delete):
        """Validate fabric deletion.

        :param vnc_api: <vnc_api.VncApi>
        :param fabric_obj: <vnc_api.gen.resource_client.Fabric>
        :param device_uuids: list of device IDs
        :return:
        """
        _task_log('Validating no Virtual Networks, Logical Routers, '
                  'Virtual Port Groups, or PNF Services '
                  'referenced')
        vn_list = set()
        lr_list = set()
        device_names = set()

        devices = vnc_bulk_get(
            vnc_api, 'physical_routers', obj_uuids=device_uuids,
            fields=['logical_router_back_refs', 'virtual_network_refs']
        )

        for device in devices:
            # Save device name in list
            device_names.add(device.get('fq_name')[1])

            # Check for virtual network references
            vn_list |= self._get_virtual_network_refs(device, fabric_obj)

            # Check for logical router references
            lr_list |= self._get_logical_router_refs(device)

        # Check for virtual port groups in this fabric
        vpg_list = self._get_virtual_port_group_refs(vnc_api, fabric_obj,
                                                     device_uuids,
                                                     is_device_delete)

        # Check for PNF services
        svc_list = self._get_pnf_service_refs(vnc_api, device_names)

        # Check for VMI and instance IPs in this fabric
        vmi_list = self._get_vmi_refs(vnc_api, device_uuids)

        # If references found, create error string
        err_msg = ""

        if vn_list:
            err_msg += 'Virtual Networks: {}, '.format(list(vn_list))

        if lr_list:
            err_msg += 'Logical Routers: {}, '.format(list(lr_list))

        if vpg_list:
            err_msg += 'Virtual Port Groups: {}, '.format(list(vpg_list))

        if svc_list:
            err_msg += 'PNF Services: {}, '.format(list(svc_list))

        if vmi_list:
            err_msg += 'Virtual Machine Interfaces (MAC): {}, '.format(
                list(vmi_list))

        # If no references found, just return
        if err_msg == "":
            _task_done('OK to delete')
            return

        _task_done('Failed to delete. Please delete the following'
                   ' overlay objects: {}'.format(err_msg))

        raise ValueError(
            'Failed to delete due to references from '
            'the following overlay objects: {}'.format(err_msg)
        )
    # end _gather_object_references

    def _validate_fabric_deletion(self, vnc_api, fabric_obj):
        """Validate fabric deletion.

        :param vnc_api: <vnc_api.VncApi>
        :param fabric_obj: <vnc_api.gen.resource_client.Fabric>
        :return:
        """
        if not fabric_obj:
            return

        device_refs = fabric_obj.get_physical_router_back_refs() or []
        device_uuids = [ref['uuid'] for ref in device_refs]

        self._check_object_references(vnc_api, fabric_obj, device_uuids, False)

    # end _validate_fabric_deletion

    def _delete_fabric(self, vnc_api, fabric_obj, job_transaction_info):
        """Delete fabric.

        :param vnc_api: <vnc_api.VncApi>
        :param fabric_obj: <vnc_api.gen.resource_client.Fabric>
        :return: None
        """
        if fabric_obj:
            # delete all fabric devices
            for device_ref in fabric_obj.get_physical_router_back_refs() or []:
                device_uuid = str(device_ref.get('uuid'))
                self._delete_fabric_device(
                    vnc_api, device_uuid,
                    job_transaction_info=job_transaction_info)

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

            # un-assign intent maps
            fabric_obj.set_intent_map_list([])
            _task_log('Unassigning intent maps from fabric')
            vnc_api.fabric_update(fabric_obj)
            _task_done()

            _task_log('Deleting fabric "%s"' % fabric_obj.fq_name[-1])
            vnc_api.fabric_delete(fq_name=fabric_obj.fq_name)
            _task_done()
    # end _delete_fabric

    def _delete_fabric_device(self, vnc_api, device_uuid=None,
                              device_fq_name=None, job_transaction_info=None):
        """Delete fabric device.

        :param vnc_api: <vnc_api.VncApi>
        :param device_uuid: string
        :param device_fq_name: list<string>: optional if missing device_uuid
        """
        device_obj = None
        try:
            if device_uuid:
                device_obj = vnc_api.physical_router_read(
                    id=device_uuid, fields=['physical_interfaces',
                                            'bgp_router_refs',
                                            'device_chassis_refs',
                                            'display_name']
                )
            elif device_fq_name:
                device_obj = vnc_api.physical_router_read(
                    fq_name=device_fq_name, fields=['physical_interfaces',
                                                    'bgp_router_refs',
                                                    'device_chassis_refs',
                                                    'display_name']
                )
        except NoIdError:
            _task_done(
                'Deleting device %s ... device not found'
                % (device_uuid if device_obj else device_fq_name)
            )
            return

        # Set transaction info for this device
        set_job_transaction(device_obj, vnc_api, job_transaction_info)

        # delete loopback iip
        loopback_iip_name = "%s/lo0.0" % device_obj.name
        try:
            _task_log("deleting loopback instance-ip %s" % loopback_iip_name)
            vnc_api.instance_ip_delete(fq_name=[loopback_iip_name])
            _task_done()
        except NoIdError:
            _task_done("lookback instance-ip not found")

        # delete assisted-replicator loopback iip
        loopback_iip_name = "%s-assisted-replicator/lo0.0" % device_obj.name
        try:
            _task_log("deleting assisted-replicator loopback instance-ip %s"
                      % loopback_iip_name)
            vnc_api.instance_ip_delete(fq_name=[loopback_iip_name])
            _task_done()
        except NoIdError:
            _task_done("assisted-replicator lookback instance-ip not found")

        # delete all interfaces
        for pi_ref in list(device_obj.get_physical_interfaces() or []):
            pi_uuid = str(pi_ref.get('uuid'))
            pi_obj = vnc_api.physical_interface_read(id=pi_uuid, fields=[
                'fq_name', 'physical_interface_mac_addresses',
                'logical_interfaces'])

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
                li_fq_name = str(li_ref.get('fq_name'))
                _task_log(
                    "Deleting logical interface %s => %s"
                    % (str(li_fq_name[1]), str(li_fq_name[3]))
                )
                vnc_api.logical_interface_delete(id=li_uuid)
                _task_done()

            _task_log(
                "Deleting physical interface %s => %s"
                % (str(pi_obj.fq_name[1]), str(pi_obj.fq_name[2]))
            )
            vnc_api.physical_interface_delete(id=pi_uuid)
            _task_done()

        # delete all the hardware inventory items
        for hardware_item in list(device_obj.get_hardware_inventorys() or []):
            hr_uuid = str(hardware_item.get('uuid'))
            hr_fq_name = str(hardware_item.get('fq_name'))
            _task_log(
                "Deleting the hardware inventory object %s" % (hr_fq_name))
            vnc_api.hardware_inventory_delete(id=hr_uuid)
            _task_done()

        # Delete the cli-config obj if exists
        for cli_config_item in list(device_obj.get_cli_configs() or []):
            cli_config_uuid = str(cli_config_item.get('uuid'))
            cli_config_fq_name = str(cli_config_item.get('fq_name'))
            _task_log(
                "Deleting cli-config object %s" % (cli_config_fq_name))
            vnc_api.cli_config_delete(id=cli_config_uuid)
            _task_done()

        # delete the corresponding bgp-router if exist
        self._delete_bgp_router(vnc_api, device_obj)

        # delete the corresponding device_chassis if exist
        self._delete_device_chassis(vnc_api, device_obj)

        # Now we can delete the device finally
        _task_log("Deleting device %s" % device_obj.display_name)
        vnc_api.physical_router_delete(id=device_obj.uuid)
        _task_done()
    # end _delete_fabric_device

    @staticmethod
    def _delete_device_chassis(vnc_api, device_obj):
        """Delete device chassis object(s) for router.

        delete corresponding device_chassis for a specific device
        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        :return: None
        """
        for chassis_ref_obj in device_obj.get_device_chassis_refs() or []:
            try:
                chassis_uuid = chassis_ref_obj.get('uuid')
                chassis_obj = vnc_api.device_chassis_read(id=chassis_uuid)
                _task_log("Deleting device-chassis %s for device %s" %
                          (chassis_obj.name, device_obj.display_name))
                device_obj.del_device_chassis(chassis_obj)
                vnc_api.physical_router_update(device_obj)
                vnc_api.device_chassis_delete(id=chassis_obj.uuid)
                _task_done()
            except NoIdError:
                _task_debug_log(
                    'device-chassis for device %s does not exist' %
                    device_obj.name)
    # end _delete_device_chassis

    @staticmethod
    def _delete_bgp_router(vnc_api, device_obj):
        """Delete BGP router.

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
            _task_debug_log(
                'bgp-router for device %s does not exist' % device_obj.name
            )
    # end _delete_bgp_router

    @staticmethod
    def _delete_logical_router(vnc_api, device_obj, fabric_name):
        """Delete logical router.

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
            _task_debug_log(
                'logical-router for device %s does not exist' % device_obj.name
            )
    # end _delete_logical_router

    @staticmethod
    def _delete_intent_maps(vnc_api, fabric_name):
        """Delete intent map.

        :param vnc_api: type=VncApi
        """
        intent_map_name = _fabric_intent_map_name(
            fabric_name, 'assisted-replicator-intent-map')

        intent_map_fq_name = ['default-global-system-config',
                              intent_map_name]
        try:
            _task_log('Deleting intent map "%s"' % intent_map_name)
            vnc_api.intent_map_delete(fq_name=intent_map_fq_name)
            _task_done()
        except NoIdError:
            _task_done('Intent map "%s" not found' % intent_map_name)

    @staticmethod
    def _delete_fabric_network(vnc_api, fabric_name, network_type):
        """Delete fabric network.

        :param vnc_api: type=VncApi
        :param fabric_name: type=string
        :param network_type: type=enum {'management', 'loopback', 'ip-fabric'}
        """
        network_name = _fabric_network_name(fabric_name, network_type)
        network_fq_name = ['default-domain', 'default-project', network_name]
        try:
            _task_log('Deleting fabric network "%s"' % network_name)
            vnc_api.virtual_network_delete(fq_name=network_fq_name)
            _task_done()
        except NoIdError:
            _task_warn_log('Fabric network "%s" not found' % network_name)

        ipam_name = _fabric_network_ipam_name(fabric_name, network_type)
        ipam_fq_name = ['default-domain', 'default-project', ipam_name]
        try:
            _task_log('Deleting network ipam "%s"' % ipam_name)
            vnc_api.network_ipam_delete(fq_name=ipam_fq_name)
            _task_done()
        except NoIdError:
            _task_done('network ipam "%s" not found' % ipam_name)
    # end _delete_fabric_network

    # ***************** delete_devices filter ********************************
    def delete_fabric_devices(self, job_ctx):
        """Delete fabric devices.

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
            FilterLog.instance("FabricDevicesDeleteFilter")
            vnc_api = JobVncApi.vnc_init(job_ctx)

            fabric_info = job_ctx.get('job_input')
            self._validate_fabric_device_deletion(vnc_api, fabric_info)
            # Set transaction info first
            job_transaction_info = get_job_transaction(job_ctx)

            for dev_name in job_ctx.get('job_input', {}).get('devices') or []:
                device_fq_name = [GSC, dev_name]
                self._delete_fabric_device(
                    vnc_api, device_fq_name=device_fq_name,
                    job_transaction_info=job_transaction_info)

            return {
                'status': 'success',
                'deletion_log': FilterLog.instance().dump()
            }
        except Exception as ex:
            errmsg = str(ex)
            _task_error_log('%s\n%s' % (errmsg, traceback.format_exc()))
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'deletion_log': FilterLog.instance().dump()
            }
    # end delete_fabric

    def _validate_fabric_device_deletion(self, vnc_api, fabric_info):
        devices_to_delete = [[GSC, name]
                             for name in fabric_info.get('devices')]

        fabric_fq_name = fabric_info.get('fabric_fq_name')
        fabric_obj = self._read_fabric_obj(vnc_api, fabric_fq_name)

        try:
            self._validate_fabric_rr_role_assigned(
                vnc_api, fabric_fq_name,
                devices_to_delete, True
            )
        except ValueError as ex:
            raise ValueError(
                '%s You are deleting the last spine device with '
                '"Route-Reflector" role before deleting other devices with '
                'routing-bridging role assigned.' % str(ex)
            )

        device_uuids = []

        for dev_fqname in devices_to_delete:
            device_uuid = vnc_api.fq_name_to_id('physical_router', dev_fqname)
            device_uuids.append(device_uuid)

        self._check_object_references(vnc_api, fabric_obj, device_uuids, True)

    # end _validate_fabric_device_deletion

    def _assign_lb_li_bgpr(self, vnc_api, role_assignments,
                           fabric_fq_name, devicefqname2_phy_role_map,
                           assign_static=False):
        static_ips = {}

        if assign_static:
            # read device_to_ztp, get all static loopback IP assignments
            fabric_uuid = vnc_api.fq_name_to_id('fabric', fabric_fq_name)
            job_input = JobAnnotations(vnc_api).fetch_job_input(
                fabric_uuid, 'fabric_onboard_template')
            device_to_ztp = job_input.get('device_to_ztp', [])
            for dev in device_to_ztp:
                ip_addr = dev.get('loopback_ip')
                if ip_addr:
                    dev_name = dev.get('hostname')
                    if dev_name:
                        static_ips[dev_name] = ip_addr

        for device_roles in role_assignments:
            ar_flag = False
            # this check ensures that roles are assigned
            # to the device only if node_profile_refs are present
            # in the device
            if "AR-Replicator" in device_roles.get(
                    'routing_bridging_roles'):
                ar_flag = True
                # add PR to intent-map

                intent_map_name = _fabric_intent_map_name(
                    fabric_fq_name[-1], 'assisted-replicator-intent-map')

                intent_map_fq_name = ['default-global-system-config',
                                      intent_map_name]
                vnc_api.ref_update("physical_router",
                                   device_roles.get(
                                       'device_obj').get_uuid(),
                                   "intent_map",
                                   vnc_api.fq_name_to_id(
                                       'intent_map',
                                       intent_map_fq_name), None, 'ADD')
            if device_roles.get('supported_roles'):
                device_obj = device_roles.get('device_obj')
                dev_name = device_obj.name
                ip_addr = None
                if dev_name in static_ips:
                    ip_addr = static_ips[dev_name]
                self._add_loopback_interface(vnc_api, device_obj,
                                             ar_flag, ip_addr=ip_addr)
                self._add_logical_interfaces_for_fabric_links(
                    vnc_api, device_obj, devicefqname2_phy_role_map
                )
                self._add_bgp_router(vnc_api, device_roles)

    # ***************** assign_roles filter ***********************************
    def assign_roles(self, job_ctx):
        """Assign roles.

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
        vnc_api = None
        errmsg = None
        try:
            FilterLog.instance("RoleAssignmentFilter")
            vnc_api = JobVncApi.vnc_init(job_ctx)

            fabric_info = job_ctx.get('job_input')
            fabric_fq_name = fabric_info.get('fabric_fq_name')
            role_assignments = fabric_info.get('role_assignments', [])

            device2roles_mappings = {}
            devicefqname2_phy_role_map = {}

            for device_roles in role_assignments:
                device_fq_name = device_roles.get('device_fq_name')
                device_obj = vnc_api.physical_router_read(
                    fq_name=device_fq_name,
                    fields=[
                        'physical_router_vendor_name',
                        'physical_router_product_name',
                        'physical_interfaces',
                        'fabric_refs',
                        'node_profile_refs',
                        'physical_role_refs',
                        'device_functional_group_refs',
                        'physical_router_loopback_ip',
                        'physical_router_management_ip',
                        'physical_router_os_version',
                        'physical_router_underlay_managed',
                        'display_name',
                        'annotations',
                        'physical_router_role'
                    ]
                )
                device_roles['device_obj'] = device_obj

                device2roles_mappings[device_obj] = device_roles
                devicefqname2_phy_role_map[':'.join(device_fq_name)] = \
                    device_roles.get('physical_role')

            # disable ibgp auto mesh to avoid O(n2) issue in schema transformer
            self._enable_ibgp_auto_mesh(vnc_api, False)

            # load supported roles from node profile assigned to the device
            for device_obj, device_roles in \
                    list(device2roles_mappings.items()):
                node_profile_refs = device_obj.get_node_profile_refs()
                if not node_profile_refs:
                    _task_warn_log(
                        "Capable role info not populated in physical router "
                        "(no node_profiles attached, cannot assign role for "
                        "device : %s" %
                        device_obj.physical_router_management_ip
                    )
                else:
                    node_profile_fq_name = node_profile_refs[0].get('to')
                    node_profile_obj = vnc_api.node_profile_read(
                        fq_name=node_profile_fq_name,
                        fields=['node_profile_roles']
                    )
                    device_roles['supported_roles'] = node_profile_obj\
                        .get_node_profile_roles().get_role_mappings()

            # validate role assignment against device's supported roles
            self._validate_role_assignments(
                vnc_api, fabric_fq_name, role_assignments
            )

            # Set the job transaction ID and description for role assignment
            self._install_role_assignment_job_transaction(
                job_ctx, vnc_api, role_assignments)

            # before assigning roles, let's assign IPs to the loopback and
            # fabric interfaces, create bgp-router and logical-router, etc.

            # First handle static loopback IP assignments to reserve those
            # addresses before assigning dynamic IPs
            self._assign_lb_li_bgpr(vnc_api, role_assignments,
                                    fabric_fq_name,
                                    devicefqname2_phy_role_map,
                                    assign_static=True)

            # Now handle dynamic loopback IP assignments
            self._assign_lb_li_bgpr(vnc_api, role_assignments,
                                    fabric_fq_name,
                                    devicefqname2_phy_role_map,
                                    assign_static=False)

            # now we are ready to assign the roles to trigger DM to invoke
            # fabric_config playbook to push the role-based configuration to
            # the devices
            for device_roles in role_assignments:
                # This is a workaround for the dummy route JUNOS issue
                self._assign_private_dummy_ip(
                    device_roles.get('device_obj'), vnc_api)
                if device_roles.get('supported_roles'):
                    self._assign_device_roles(vnc_api, device_roles)
                device_obj = device_roles.get('device_obj')
                self._update_physical_router_annotations(device_obj, vnc_api)
        except Exception as ex:
            errmsg = str(ex)
            _task_error_log('%s\n%s' % (errmsg, traceback.format_exc()))
        finally:
            # make sure ibgp auto mesh is enabled for all cases
            self._enable_ibgp_auto_mesh(vnc_api, True)
            return {
                'status': 'failure' if errmsg else 'success',
                'error_msg': errmsg,
                'assignment_log': FilterLog.instance().dump()
            }
    # end assign_roles

# ***************** validate dfg filter ***********************************
    def validate_device_functional_group(self, job_ctx, device_name,
                                         prouter_uuid,
                                         device_to_ztp):
        final_dict = None
        try:
            vnc_api = JobVncApi.vnc_init(job_ctx)
            if device_name and device_to_ztp:
                device_map = dict((d.get('hostname', d.get('serial_number')),
                                   d) for d in device_to_ztp)
                existing_device_functional_group = \
                    vnc_api.device_functional_groups_list()
                existing_device_functional_list = \
                    existing_device_functional_group[
                        'device-functional-groups']
                if device_name in device_map:
                    if device_map[device_name].get(
                            'device_functional_group') is not None:
                        given_dfg = device_map[device_name].get(
                            'device_functional_group')
                        final_dict = dict()
                        for item in existing_device_functional_list:
                            if item['fq_name'][2] == given_dfg:
                                dfg_uuid = item['uuid']
                                dfg_item = \
                                    vnc_api.device_functional_group_read(
                                        id=dfg_uuid)
                                dfg_item_dict = vnc_api.obj_to_dict(dfg_item)
                                # check rb_role compatibility
                                self._check_rb_role_compatibity(vnc_api,
                                                                prouter_uuid,
                                                                dfg_item_dict)
                                # delete the previous dfg reference before
                                # creating new one
                                self._delete_dfg_ref(vnc_api, prouter_uuid)
                                vnc_api.ref_update("physical_router",
                                                   prouter_uuid,
                                                   "device_functional_group",
                                                   dfg_uuid, None, 'ADD')

                                final_dict['os_version'] = dfg_item_dict.get(
                                    'device_functional_group_os_version')
                                physical_role_refs = dfg_item_dict.get(
                                    'physical_role_refs')
                                if physical_role_refs is not None:
                                    final_dict['physical_role'] = \
                                        physical_role_refs[0]['to'][-1]
                                else:
                                    final_dict['physical_role'] = \
                                        physical_role_refs
                                rb_roles_dict = dfg_item_dict.get(
                                    'device_functional_group_routing_bridging_roles')
                                if rb_roles_dict!= None:
                                    final_dict['rb_roles'] = rb_roles_dict.get('rb_roles')
                                else:
                                    final_dict['rb_roles'] = rb_roles_dict
                                rr_flag = device_map[device_name].get('route_reflector')
                                if rr_flag != None:
                                    if not final_dict['rb_roles']:
                                        final_dict['rb_roles'] = []
                                        final_dict['rb_roles'].append('Route-Reflector')
                                    elif 'Route-Reflector' not in final_dict['rb_roles']:
                                        final_dict['rb_roles'].append('Route-Reflector')
        except Exception as ex:
            errmsg = str(ex)
            _task_error_log('%s\n%s' % (errmsg, traceback.format_exc()))

        if isinstance(final_dict,dict) and len(final_dict) == 0:
            raise NoIdError("The given DFG for device %s is not "
                            "defined" % str(device_name))
        return final_dict
        # end validate_device_functional_group

    # update physical router annotations
    def update_physical_router_annotations(self, job_ctx, pr_uuid):
        vnc_api = JobVncApi.vnc_init(job_ctx)
        pr_obj = vnc_api.physical_router_read(id=pr_uuid)
        self._update_physical_router_annotations(pr_obj, vnc_api)

    def _update_physical_router_annotations(self, pr_obj, vnc_api):
        try:
            set_dfg_flag = False
            dfg_refs = pr_obj.get_device_functional_group_refs()
            if dfg_refs!= None:
                dfg_uuid = dfg_refs[0]['uuid']
                dfg_obj = vnc_api.device_functional_group_read(id=dfg_uuid)
                dfg_item_dict = vnc_api.obj_to_dict(dfg_obj)
                # Match the image
                if dfg_item_dict.get('device_functional_group_os_version')!= None:
                    pr_os_version = pr_obj.physical_router_os_version
                    if pr_os_version!= dfg_item_dict.get(
                            'device_functional_group_os_version'):
                        set_dfg_flag = "device_functional_group_os_version"
                # Match physical role
                if dfg_item_dict.get('physical_role_refs')!= None:
                    if pr_obj.get_physical_role_refs()!= None:
                        pr_physical_role = pr_obj.physical_role_refs[0]['to'][-1]
                        dfg_phy_role_refs = dfg_item_dict.get('physical_role_refs')
                        dfg_physical_role = dfg_phy_role_refs[0]['to'][-1]
                        if pr_physical_role!= dfg_physical_role:
                            set_dfg_flag = "physical_role_refs"
                # Match rb roles
                if dfg_item_dict.get\
                            ('device_functional_group_routing_bridging_roles')!= None:
                    dfg_rb_roles_dict = dfg_item_dict.get\
                        ('device_functional_group_routing_bridging_roles')
                    dfg_rb_roles_list = dfg_rb_roles_dict.get('rb_roles')
                    if pr_obj.get_routing_bridging_roles()!= None:
                        pr_rb_roles = pr_obj.get_routing_bridging_roles().rb_roles
                        if pr_rb_roles!= dfg_rb_roles_list:
                            set_dfg_flag = "device_functional_group_routing_bridging_roles"
            # Update the pr annotations
            pr_annot = pr_obj.get_annotations()
            kvs = []
            #Always first delete the key-value pair and append if necessary
            if pr_annot:
                kvs = pr_annot.get_key_value_pair() or []
                for kv in kvs:
                    if kv.get_key() == 'dfg_flag':
                        kvs.remove(kv)
                        break
            if set_dfg_flag != False:
                dfg_flag_value = set_dfg_flag
                kvs.append(KeyValuePair(key="dfg_flag", value=dfg_flag_value))
            if not pr_annot:
                pr_annot = KeyValuePairs()
            pr_annot.set_key_value_pair(kvs)
            pr_obj.set_annotations(pr_annot)
            vnc_api.physical_router_update(pr_obj)
        except Exception as ex:
            errmsg = str(ex)
            _task_error_log('%s\n%s' % (errmsg, traceback.format_exc()))

    # end _update_physical_router_annotations

    def _delete_dfg_ref(self,vnc_api, pr_uuid):
        try:
            pr_obj= vnc_api.physical_router_read(id=pr_uuid)
            dfg_refs = pr_obj.get_device_functional_group_refs()
            if dfg_refs!= None:
                for item in dfg_refs:
                    vnc_api.ref_update("physical_router",pr_uuid,
                                       "device_functional_group",item['uuid'],
                                       None, "DELETE")
                vnc_api.physical_router_update(pr_obj)
        except Exception as ex:
            _task_log("No dfg references found for this device")
    # end _delete_dfg_ref

    def _check_rb_role_compatibity(self, vnc_api, pr_uuid, role_assgn_dict):
        pr_obj = vnc_api.physical_router_read(id=pr_uuid, fields =[
            'node_profile_refs','physical_router_management_ip'])
        node_profile_refs = pr_obj.get_node_profile_refs()
        if not node_profile_refs:
            _task_warn_log(
                "Capable role info not populated in physical router "
                "(no node_profiles attached, cannot assign role for "
                "device : %s" %
                pr_obj.physical_router_management_ip
            )
        else:
            node_profile_fq_name = node_profile_refs[0].get('to')
            node_profile_obj = vnc_api.node_profile_read(
                fq_name=node_profile_fq_name,
                fields=['node_profile_roles']
            )
            supported_roles_np = node_profile_obj.\
                get_node_profile_roles().get_role_mappings()
            phy_role = role_assgn_dict.get('physical_role')
            rb_roles = role_assgn_dict.get('routing_bridging_roles')
            for role in supported_roles_np:
                if str(role.get_physical_role()) == phy_role:
                    supp_rb_roles = role.get_rb_roles()
                    if (set(rb_roles) < set(supp_rb_roles)) or \
                            (set(rb_roles) == set(supp_rb_roles)):
                        continue
                    else:
                        raise ValueError(
                            'role "%s : %s" is not supported. Here are the '
                            'supported roles : %s' % (
                                phy_role, rb_roles, supported_roles_np
                            )
                    )
    # end _check_rb_role_compatibility

    def _install_role_assignment_job_transaction(self, job_ctx, vnc_api,
                                                 role_assignments):
        job_transaction_info = get_job_transaction(job_ctx)

        for device_roles in role_assignments:
            set_job_transaction(device_roles.get('device_obj'), vnc_api,
                                job_transaction_info)

    def _read_and_increment_dummy_ip(self, vnc_api):
        gsc_obj = vnc_api.global_system_config_read(
            fq_name=[GSC], fields=['annotations'])
        dummy_ip = None
        kvs = []
        annotations = gsc_obj.get_annotations()
        if annotations:
            kvs = annotations.get_key_value_pair() or []
            for kv in kvs:
                if kv.get_key() == 'next_dummy_ip':
                    dummy_ip = kv.get_value()
                    dummy_ip = _int2ip(_ip2int(dummy_ip) + 1)
                    kv.set_value(dummy_ip)
                    break

        if not dummy_ip:
            dummy_ip = "172.16.0.1"
            kvs.append(KeyValuePair(key="next_dummy_ip", value=dummy_ip))

        if not annotations:
            annotations = KeyValuePairs()
        annotations.set_key_value_pair(kvs)
        gsc_obj.set_annotations(annotations)
        vnc_api.global_system_config_update(gsc_obj)
        return gsc_obj, dummy_ip

    def _get_device_dummy_ip(self, device_obj):
        annotations = device_obj.get_annotations()
        if annotations:
            for kv in annotations.get_key_value_pair() or []:
                if kv.get_key() == "dummy_ip":
                    return kv.get_value()
        return None
    # end _get_device_dummy_ip

    def _update_device_dummy_ip(self, vnc_api, device_obj, dummy_ip):
        annotations = device_obj.get_annotations()
        if not annotations:
            annotations = KeyValuePairs()
        annotations.add_key_value_pair(
            KeyValuePair(key='dummy_ip', value=dummy_ip))
        device_obj.set_annotations(annotations)
        vnc_api.physical_router_update(device_obj)
    # end _update_device_dummy_ip

    def _assign_private_dummy_ip(self, device_obj, vnc_api):
        """Assign private dummy.

        This method's a hack to workaround the junos issue on type-5 routes.
        Due to this JUNOS limitation, we must configure one dummy static route
        per fabric device to resolve the issue. We are allocating a private IP
        from 172.16.0.0/12 subnets and store it as one key-value annotation of
        the physical-router object
        """
        if vnc_api and device_obj:
            dummy_ip = self._get_device_dummy_ip(device_obj)
            if not dummy_ip:
                gsc_obj, dummy_ip = self._read_and_increment_dummy_ip(vnc_api)
                self._update_device_dummy_ip(vnc_api, device_obj, dummy_ip)
    # end _assign_private_dummy_ip

    @staticmethod
    def _enable_ibgp_auto_mesh(vnc_api, enable):
        """Enable iBGP auto mesh.

        :param vnc_api: <vnc_api.VncApi>
        :param enable: set to True to enable
        """
        if vnc_api:
            gsc_obj = vnc_api.global_system_config_read(
                fq_name=[GSC])
            gsc_obj.set_ibgp_auto_mesh(enable)
            vnc_api.global_system_config_update(gsc_obj)
    # end _enable_ibgp_auto_mesh

    def _validate_role_assignments(
            self, vnc_api, fabric_fq_name, role_assignments):
        """Validate role assignments.

        :param vnc_api: <vnc_api.VncApi>
        :param fabric_fq_name: list<string>
        :param role_assignments: list<Dictionary>
            example:
            [
                {
                    "device_fq_name": [
                        "default-global-system-config",
                        "qfx-10"
                    ],
                    "device_obj": <vnc_api.vnc_api.gen.PhysicalRouter>
                    "physical_role": "leaf",
                    "routing_bridging_roles": [ "CRB-Access" ]
                }
            ]
        """
        self._validate_against_supported_roles(role_assignments)

        self._validate_rr_role_assigned(
            vnc_api, fabric_fq_name, role_assignments
        )

        self._validate_ucast_mcast_role_exclusive(role_assignments)
    # end _validate_role_assignments

    @staticmethod
    def _validate_against_supported_roles(role_assignments):
        """Validate against supported roles.

        This method validates the assigned device roles are supported
        roles on the device according node profile
        :param role_assignments:
            example:
            [
                {
                    "device_fq_name": [
                        "default-global-system-config",
                        "qfx-10"
                    ],
                    "device_obj": <vnc_api.vnc_api.gen.PhysicalRouter>
                    "physical_role": "leaf",
                    "routing_bridging_roles": [ "CRB-Access" ]
                }
            ]
        """
        for device_roles in role_assignments:
            device_obj = device_roles.get('device_obj')
            phys_role = device_roles.get('physical_role')
            if not phys_role:
                raise ValueError(
                    'No physical role assigned to %s' % device_obj.display_name
                )

            rb_roles = device_roles.get('routing_bridging_roles')
            if not rb_roles:
                rb_roles = ['lean']

            supported_roles = device_roles.get('supported_roles', [])
            for role in supported_roles:
                if str(role.get_physical_role()) == phys_role:
                    if (set(rb_roles) < set(role.get_rb_roles())) or \
                            (set(rb_roles) == set(role.get_rb_roles())):
                        continue
                    else:
                        raise ValueError(
                            'role "%s : %s" is not supported. Here are the '
                            'supported roles : %s' % (
                                phys_role, rb_roles, supported_roles
                            )
                        )
    # end _validate_against_supported_roles

    def _validate_rr_role_assigned(
            self, vnc_api, fabric_fq_name, role_assignments):
        """Validate RR role assigned.

        This method validates at least one device in the fabric is assigned
        with 'Route-Reflector' role
        :param vnc_api: <vnc_api.VncApi>
        :param fabric_fq_name: list<string>
        :param role_assignments:
            example:
            [
                {
                    "device_fq_name": [
                        "default-global-system-config",
                        "qfx-10"
                    ],
                    "device_obj": <vnc_api.vnc_api.gen.PhysicalRouter>
                    "physical_role": "leaf",
                    "routing_bridging_roles": [ "CRB-Access" ]
                }
            ]
        """
        # check if any RR role exists in the role assignments
        role_assignment_devices = []
        fabric_name = fabric_fq_name[-1]
        for device_roles in role_assignments:
            device_obj = device_roles.get('device_obj')
            role_assignment_devices.append(device_obj.get_fq_name())

            # validate devices are in the specified fabric
            assigned_fabric = self._get_assigned_fabric(device_obj)
            if assigned_fabric != fabric_name:
                raise ValueError(
                    '%s is not in the specific fabric: %s' % (
                        device_obj.get_fq_name()[-1], fabric_name
                    )
                )

            # validate the RR roles is assigned
            rb_roles = device_roles.get('routing_bridging_roles') or []
            if 'Route-Reflector' in rb_roles:
                return

        # check if RR role is assigned to other devices that are not in the
        # current role_assignments
        try:
            self._validate_fabric_rr_role_assigned(
                vnc_api, fabric_fq_name, role_assignment_devices, False
            )
        except ValueError as ex:
            raise ValueError(
                '%s Please assign "Route-Reflector" role to at least one '
                'device and retry the role assignment' % str(ex)
            )
    # end _validate_rr_role_assigned

    def _validate_ucast_mcast_role_exclusive(self, role_assignments):
        """Validate ucast mcast role exclusive.

        This method validates that both UCAST and MCAST roles are not assigned
        to the same device
        """
        for device_roles in role_assignments:
            device_obj = device_roles.get('device_obj')
            rb_roles = list(device_roles.get('routing_bridging_roles', []))
            if device_obj.get_routing_bridging_roles():
                assigned_roles = device_obj.get_routing_bridging_roles().\
                    get_rb_roles() or []
                rb_roles += assigned_roles

            has_ucast_role = any('ucast' in r.lower() for r in rb_roles)
            has_mcast_role = any('mcast' in r.lower() for r in rb_roles)

            if has_ucast_role and has_mcast_role:
                raise ValueError('Cannot assign a UCAST role and a MCAST role '
                                 'to the same device: %s' %
                                 device_obj.get_fq_name()[-1])
    # end _validate_ucast_mcast_role_exclusive

    def _validate_fabric_rr_role_assigned(
            self, vnc_api, fabric_fq_name,
            devices_to_exclude, ok_with_no_role_assigned):
        """Validate fabric rr role assigned.

        This method validates that there exists at least one device assigned
        with Route-Reflector role in the fabric (excluding those devices
        specified in devices_to_exclude)
        :param vnc_api: <vnc_api.VncApi>
        :param fabric_fq_name: fabric FQ name
        :param devices_to_exclude:
            list of fq names for the devices to exclude from the check
        :param ok_with_no_role_assigned:
            set to True if no role assigned to any device
        :return:
        """
        fabric_obj = self._read_fabric_obj(vnc_api, fabric_fq_name)
        fabric_devices = fabric_obj.get_physical_router_back_refs() or []
        no_role_assigned = True
        for dev in fabric_devices:
            if dev.get('to') in devices_to_exclude:
                continue

            device_obj = vnc_api.physical_router_read(id=dev.get('uuid'))
            phys_role = device_obj.get_physical_router_role()
            rb_roles = device_obj.get_routing_bridging_roles()
            if phys_role or (rb_roles and rb_roles.get_rb_roles()):
                no_role_assigned = False
            if rb_roles and 'Route-Reflector' in (
                    rb_roles.get_rb_roles() or []):
                return

        if ok_with_no_role_assigned and no_role_assigned:
            return

        # no RR role found in any devices in the fabric
        raise ValueError(
            'Need at least one device in fabric "%s" assigned with '
            '"Route-Reflector" role! ' % fabric_fq_name[-1]
        )
    # end _validate_fabric_rr_role_assigned

    @staticmethod
    def _get_assigned_fabric(device_obj):
        # get fabric object that this device belongs to
        fabric_refs = device_obj.get_fabric_refs() or []
        if len(fabric_refs) != 1:
            raise ValueError(
                "Unable to assign roles for device %s that does not belong to "
                "any fabric" % str(device_obj.fq_name)
            )
        return str(fabric_refs[0].get('to')[-1])
    # end _get_assigned_fabric

    @staticmethod
    def _get_device_network(vnc_api, device_obj, network_type):
        """Get device network.

        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        :param network_type: string (One of constants defined in NetworkType)
        :return: <vnc_api.gen.resource_client.VirtualNetwork>
        """
        fabric_name = FilterModule._get_assigned_fabric(device_obj)

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

    def _add_loopback_interface(self, vnc_api, device_obj, ar_flag,
                                ip_addr=None):
        """Add loopback interface.

        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        """
        if device_obj.get_physical_router_underlay_managed() \
                or ar_flag:
            loopback_network_obj = self._get_device_network(
                vnc_api, device_obj, NetworkType.LOOPBACK_NETWORK
            )
            if not loopback_network_obj:
                _task_debug_log(
                    "Loopback network does not exist, thereofore skip the\
                     loopback interface creation.")
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
                iip_obj = InstanceIp(name=iip_name, instant_ip_family='v4',
                                     instance_ip_address=ip_addr)
                iip_obj.set_logical_interface(loopback_li_obj)
                iip_obj.set_virtual_network(loopback_network_obj)
                _task_log(
                    'create instance ip for lo0.0 on device %s'
                    % device_obj.name
                )
                iip_uuid = vnc_api.instance_ip_create(iip_obj)
                iip_obj = vnc_api.instance_ip_read(id=iip_uuid)
                _task_done()

            # update device level properties
            if device_obj.get_physical_router_underlay_managed():
                device_obj.physical_router_loopback_ip \
                    = iip_obj.get_instance_ip_address()
                device_obj.physical_router_dataplane_ip \
                    = iip_obj.get_instance_ip_address()

            if ar_flag:
                # assign assisted replicator IP to the loopback interface
                iip_name = "%s-assisted-replicator/lo0.0" % device_obj.name
                try:
                    iip_obj = vnc_api.instance_ip_read(fq_name=[iip_name])
                except NoIdError:
                    iip_obj = InstanceIp(name=iip_name, instant_ip_family='v4')
                    iip_obj.set_logical_interface(loopback_li_obj)
                    iip_obj.set_virtual_network(loopback_network_obj)
                    _task_log(
                        'Create assisted replicator IP for lo0.0 on device %s'
                        % device_obj.name
                    )
                    iip_uuid = vnc_api.instance_ip_create(iip_obj)
                    iip_obj = vnc_api.instance_ip_read(id=iip_uuid)
                    _task_done()
                device_obj.physical_router_replicator_loopback_ip \
                    = iip_obj.get_instance_ip_address()
    # end _add_loopback_interface

    def _add_bgp_router(self, vnc_api, device_roles):
        """Add BGP router.

        Add corresponding bgp-router object for this device. This bgp-router is
        used to model the overlay iBGP mesh
        :param vnc_api: <vnc_api.VncApi>
        :param device_roles: Dictionary
            example:
            {
                'device_obj': <vnc_api.gen.resource_client.PhysicalRouter>
                'device_fq_name': ['default-global-system-config', 'qfx-10'],
                'physical_role": 'leaf',
                'routing_bridging_roles": ['CRB-Gateway', 'Route-Reflector']
            }
        :return: None
        """
        bgp_router_obj = None
        device_obj = device_roles.get('device_obj')
        rb_roles = device_roles.get('routing_bridging_roles', [])
        phys_role = device_roles.get('physical_role')
        if phys_role == 'pnf':
            return
        if device_obj.physical_router_loopback_ip:
            bgp_router_fq_name = _bgp_router_fq_name(device_obj.name)
            bgp_router_name = bgp_router_fq_name[-1]
            cluster_id = None
            if 'Route-Reflector' in rb_roles:
                loopback_ip_int = FilterModule.get_int_for_lo0_ip(
                    device_obj.physical_router_loopback_ip)
                cluster_id = loopback_ip_int
            try:
                bgp_router_obj = vnc_api.bgp_router_read(
                    fq_name=bgp_router_fq_name
                )
                params = bgp_router_obj.get_bgp_router_parameters()
                if params:
                    params.set_cluster_id(cluster_id)
                    bgp_router_obj.set_bgp_router_parameters(params)
                    vnc_api.bgp_router_update(bgp_router_obj)
            except NoIdError:
                fabric_name = FilterModule._get_assigned_fabric(device_obj)
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
                        "hold_time": 90,
                        "cluster_id": cluster_id
                    }
                )
                vnc_api.bgp_router_create(bgp_router_obj)

            device_obj.add_bgp_router(bgp_router_obj)
        else:
            _task_warn_log(
                "Loopback interfaces are not found on device '%s', therefore"
                "not creating the bgp router object" % device_obj.name
            )
        # end if
        return bgp_router_obj
    # end _add_bgp_router

    def _add_logical_router(
            self, vnc_api, device_obj, device_roles, fabric_name):
        """Add logical router.

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
        :param fabric_name: fabric name
        :return: None
        """
        logical_router_obj = None
        rb_roles = device_roles.get('routing_bridging_roles') or []
        logical_router_fq_name = _logical_router_fq_name(fabric_name)
        logical_router_name = logical_router_fq_name[-1]
        try:
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
                        self._delete_logical_router(
                            vnc_api, device_obj, fabric_name
                        )
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
    def get_int_for_lo0_ip(phy_router_loopback_ip):
        ip_obj = ipaddress.ip_address(unicode(str(
            phy_router_loopback_ip
        )))
        return int(ip_obj)
    # end get_int_for_lo0_ip

    @staticmethod
    def _get_ibgp_asn(vnc_api, fabric_name):
        try:
            ibgp_asn_namespace_obj = vnc_api.fabric_namespace_read(fq_name=[
                'default-global-system-config', fabric_name, 'overlay_ibgp_asn'
            ], fields=['fabric_namespace_value'])
            return ibgp_asn_namespace_obj.fabric_namespace_value.asn.asn[0]
        except NoIdError:
            gsc_obj = vnc_api.global_system_config_read(
                fq_name=[GSC], fields=['autonomous_system']
            )
            return gsc_obj.autonomous_system
    # end _get_ibgp_asn

    def _instance_ip_creation(self, vnc_api, device_obj, local_pi,
                              remote_pi, fabric_network_obj):

        li_name = self._build_li_name(device_obj, local_pi.name, 0)
        li_fq_name = local_pi.fq_name + [li_name]

        try:
            local_li = vnc_api.logical_interface_read(
                fq_name=li_fq_name
            )
        except NoIdError:
            local_li = LogicalInterface(
                name=li_name,
                fq_name=li_fq_name,
                display_name=li_name.replace('_', ':'),
                parent_type='physical-interface',
                logical_interface_type='l3'
            )
            _task_log(
                'creating logical interface %s for physical link from'
                ' %s to %s' % (local_li.name, local_pi.fq_name,
                               remote_pi.fq_name)
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

    def _verify_physical_roles(self, device_obj, remote_device_obj,
                               devicefqname2_phy_role_map,
                               local_pi, remote_pi):

        # this function verifies that
        # 1. local prouter and the remote
        #    prouter are not of the same leaf role in
        #    order to avoid loops.
        # 2. The physical interface refs are not
        #    from and to the same physical routers

        local_physical_role = devicefqname2_phy_role_map.get(
            device_obj.get_fq_name_str(),
            device_obj.get_physical_router_role()
        )

        remote_physical_role = devicefqname2_phy_role_map.get(
            remote_device_obj.get_fq_name_str(),
            remote_device_obj.get_physical_router_role()
        )

        # mark link type as service if link is between
        # 1. Leaf and PNF device
        # 2. Spine and PNF device
        if ((local_physical_role in ['leaf', 'spine'] and
             remote_physical_role == 'pnf') or
            (local_physical_role == 'pnf' and
             remote_physical_role in ['leaf', 'spine'])):
            _task_log(
                "Not creating instance ips as links are between "
                " %s and %s"
                % (local_physical_role, remote_physical_role))
            local_pi.set_physical_interface_type('service')
            remote_pi.set_physical_interface_type('service')
            return False

        if (local_physical_role ==
                remote_physical_role == 'leaf'):
            _task_log(
                "Not creating instance ips as both "
                "physical routers are of the same role type %s"
                % local_physical_role)
            local_pi.set_physical_interface_type('fabric')
            remote_pi.set_physical_interface_type('fabric')
            return False

        if device_obj.get_uuid() == remote_device_obj.get_uuid():
            _task_log(
                "Not creating instance ips as physical "
                "interface refs are from and to the same device: %s"
                % device_obj.get_fq_name())
            return False

        if (local_physical_role in ['leaf', 'spine'] and
                remote_physical_role in ['leaf', 'spine']):
            local_pi.set_physical_interface_type('fabric')
            remote_pi.set_physical_interface_type('fabric')

        return True

    def _add_logical_interfaces_for_fabric_links(self, vnc_api, device_obj,
                                                 devicefqname2_phy_role_map):
        """Add logical interfaces for fabric links.

        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        """
        underlay_managed = device_obj.get_physical_router_underlay_managed()
        # get fabric object that this device belongs to
        fabric_network_obj = self._get_device_network(
            vnc_api, device_obj, NetworkType.FABRIC_NETWORK
        )
        if underlay_managed and not fabric_network_obj:
            _task_debug_log("fabric network does not exist for managed\
                            underlay, skip the fabric interface creation.")
            return

        # create logical interfaces for all the fabric links from this device's
        # physical interfaces and assign instance-ip to the logical interface
        # if not assigned yet
        for link in self._get_device_fabric_links(vnc_api, device_obj) or []:
            local_pi = link.get('local_pi')
            remote_pi = link.get('remote_pi')

            remote_device_obj = \
                vnc_api.physical_router_read(
                    fq_name=remote_pi.get_parent_fq_name())

            if self._verify_physical_roles(device_obj, remote_device_obj,
                                           devicefqname2_phy_role_map,
                                           local_pi, remote_pi):
                if underlay_managed:
                    # local_pi
                    self._instance_ip_creation(vnc_api, device_obj, local_pi,
                                               remote_pi, fabric_network_obj)

                    # remote_pi
                    self._instance_ip_creation(vnc_api, remote_device_obj,
                                               remote_pi, local_pi,
                                               fabric_network_obj)
            vnc_api.physical_interface_update(local_pi)
            vnc_api.physical_interface_update(remote_pi)
    # end _add_logical_interfaces_for_fabric_links

    @staticmethod
    def _build_li_name(
            device_obj, physical_interface_name, logical_interface_index):
        """Build LI name.

        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        :param physical_interface_name: string
        :param logical_interface_index: string
        :return:
        """
        if device_obj.physical_router_vendor_name and \
            device_obj.physical_router_vendor_name.lower() == \
                'juniper':
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
        """Get PI mac.

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
        """Get device fabric links.

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
        """Assign device roles.

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
        "api_server_host": ['10.87.13.3'],
        "config_args": {
            "collectors": [
                "10.87.13.3:8086"
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
                        "5a12-qfx7"
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
            "DCI-Gateway@spine",
            "CRB-Access@leaf",
            "CRB-Gateway@leaf",
            "DC-Gateway@leaf"
            "DCI-Gateway@leaf"
        ]
    }
# end _mock_supported_roles


def _parse_args():
    arg_parser = argparse.ArgumentParser(description='fabric filters tests')
    arg_parser.add_argument('-c', '--create_fabric',
                            action='store_true', help='Onbaord fabric')
    arg_parser.add_argument('-ce', '--create_existing_fabric',
                            action='store_true',
                            help='Onbaord existing fabric')
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
        results = fabric_filter.assign_roles(_mock_job_ctx_assign_roles())

    print(results)
# end __main__


if __name__ == '__main__':
    __main__()
