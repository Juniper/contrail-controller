#!/usr/bin/python
import logging
import pprint
import sys, traceback
import argparse
import json
import python_jsonschema_objects as pjs
from netaddr import IPNetwork

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
    InstanceIp
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
    SubnetListType
)

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

class NetworkType:
    MGMT_NETWORK='management'
    LOOPBACK_NETWORK='loopback'
    FABRIC_NETWORK='ip-fabric'

class FilterModule(object):
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

    @staticmethod
    def _init_vnc_api(job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
              'auth_token': '0B02D162-180F-4452-96D0-E9FCAAFC4378'
              ...
            }
        :return: VncApi
        """
        return VncApi(
            auth_type=VncApi._KEYSTONE_AUTHN_STRATEGY,
            auth_token=job_ctx.get('auth_token'))
    # end _init_vnc_api

    @staticmethod
    def _validate_job_ctx(job_ctx):
        if not job_ctx.get('job_template_fqname'):
            raise ValueError('Invalid job_ctx: missing job_template_fqname')
    # end _validate_job_ctx

    @staticmethod
    def _get_job_input_schema(vnc_api, job_template_fq_name):
        """
        :param vnc_api: <VncApi>
        :param job_template_fq_name: list<string>
        :return: Dictionary that conforms to the job input JSON schema
        """
        fabric_onboard_template = vnc_api.job_template_read(
            fq_name=job_template_fq_name
        )
        return fabric_onboard_template.get_job_template_input_schema()
    # end _get_job_input_schema


    def __init__(self):
        self._logger = FilterModule._init_logging()
    # end __init__

    def filters(self):
        return {
            'onboard_fabric': self.onboard_fabric,
            'delete_fabric': self.delete_fabric,
            'assign_roles': self.assign_roles
        }
    # end filters

    ################### onboard_fabric filter #################################
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
                    "node_profiles": [
                        {
                            "node_profile_name": "juniper-qfx5100"
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
        onboard_log = "\n"
        try:
            FilterModule._validate_job_ctx(job_ctx)
            vnc_api = FilterModule._init_vnc_api(job_ctx)

            # validate and load job input to the FabricInfo object
            job_input_schema = FilterModule._get_job_input_schema(
                vnc_api, job_ctx.get('job_template_fqname')
            )
            ns = pjs.ObjectBuilder(job_input_schema).build_classes(strict=True)
            fabric_info = ns.FabricInfo.from_json(
                json.dumps(job_ctx.get('job_input'))
            )

            # create fabric in db
            onboard_log += "Creating fabric object ... "
            fabric_obj = self._create_fabric(vnc_api, fabric_info)
            onboard_log += "done\n"

            # add all the namespaces
            onboard_log += "Creating namespace for management network ... "
            mgmt_subnets = [
                {
                    'cidr': str(subnet.cidr),
                    'gateway': str(subnet.gateway)
                } for subnet in fabric_info.management_subnets
            ]
            self._add_cidr_namespace(
                vnc_api,
                fabric_obj,
                'management-subnets',
                mgmt_subnets,
                'label=fabric-management-ip'
            )
            onboard_log += "done\n"

            onboard_log += "Creating namespace for loopback network ... "
            loopback_subnets = [
                {
                    'cidr': str(subnet.cidr)
                } for subnet in fabric_info.management_subnets
            ]
            self._add_cidr_namespace(
                vnc_api,
                fabric_obj,
                'loopback-subnets',
                loopback_subnets,
                'label=fabric-loopback-ip'
            )
            onboard_log += "done\n"

            onboard_log += "Creating namespace for fabric network ... "
            fabric_subnets = [
                {
                    'cidr': str(subnet.cidr)
                } for subnet in fabric_info.management_subnets
            ]
            self._add_cidr_namespace(
                vnc_api,
                fabric_obj,
                'fabric-subnets',
                fabric_subnets,
                'label=fabric-peer-ip'
            )
            onboard_log += "done\n"

            onboard_log += "Creating namespace for eBGP ASN pool ... "
            self._add_asn_range_namespace(
                vnc_api,
                fabric_obj,
                'eBGP-ASN-pool',
                [{
                    'asn_min': int(asn_range.asn_min),
                    'asn_max': int(asn_range.asn_max)
                } for asn_range in fabric_info.fabric_asn_pool],
                'label=fabric-ebgp-as-number'
            )
            onboard_log += "done\n"

            # add node profiles
            onboard_log += "Assigning node profiles to fabric ... "
            self._add_node_profiles(
                vnc_api,
                fabric_obj,
                fabric_info.node_profiles
            )
            onboard_log += "done\n"

            # add management, loopback, and peer networks
            onboard_log += "Creating management network for the fabric ... "
            self._add_fabric_vn(
                vnc_api,
                fabric_obj,
                NetworkType.MGMT_NETWORK,
                mgmt_subnets,
                False
            )
            onboard_log += "done\n"

            onboard_log += "Creating loopback network for the fabric ... "
            self._add_fabric_vn(
                vnc_api,
                fabric_obj,
                NetworkType.LOOPBACK_NETWORK,
                loopback_subnets,
                False
            )
            onboard_log += "done\n"

            onboard_log += "Creating fabric network for the fabric ... "
            peer_subnets = self._carve_out_peer_subnets(fabric_subnets)
            self._add_fabric_vn(
                vnc_api,
                fabric_obj,
                NetworkType.FABRIC_NETWORK,
                peer_subnets,
                True
            )
            onboard_log += "done\n"

            self._logger.warn(onboard_log)
            return {
                'status': 'success',
                'fabric_uuid': fabric_obj.uuid,
                'onboard_log': onboard_log
            }
        except Exception as ex:
            self._logger.error(onboard_log + '\n' + str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'onboard_log': onboard_log
            }
    # end onboard_fabric

    def _carve_out_peer_subnets(self, subnets):
        """
        :param subnets: type=list<Dictionary>
            example:
            [
                { 'cidr': '192.168.10.1/24', 'gateway': '192.168.10.1 },
                ...
            ]
        :return: list<Dictionary>
            example:
            [
                { 'cidr': '192.168.10.1/30'},
                ...
            ]
        """
        carved_subnets = []
        for subnet in subnets:
            slash_30_subnets = IPNetwork(subnet.get('cidr')).subnet(30)
            for slash_30_sn in slash_30_subnets:
                carved_subnets.append({ 'cidr': str(slash_30_sn) })
        return carved_subnets
    # end _carve_out_peer_subnets

    def _create_fabric(self, vnc_api, fabric_info):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param fabric_info: dynamic object from job input schema via
                            python_jsonschema_objects
        :return: <vnc_api.gen.resource_client.Fabric>
        """
        fq_name = [ str(name) for name in fabric_info.fabric_fq_name ]
        fab_name = fq_name[-1:]
        self._logger.info('Creating fabric: %s', fab_name)
        fab = Fabric(
            name=fab_name,
            fq_name=fq_name,
            parent_type='global-system-config',
            fabric_credentials={
                'device_credential': [{
                    'credential': {
                        'username': 'root',
                        'password': str(fabric_info.device_auth.root_password)
                    }
                }]
            })
        try:
            vnc_api.fabric_create(fab)
        except RefsExistError:
            self._logger.warn(
                "Fabric '%s' already exists, hence updating it ...", fab_name)
            vnc_api.fabric_update(fab)

        fab = vnc_api.fabric_read(fq_name=fq_name)
        self._logger.info(
            "Fabric created:\n%s",
            pprint.pformat(vnc_api.obj_to_dict(fab), indent=4))
        return fab
    # end _create_fabric

    def _add_cidr_namespace(self, vnc_api, fab, ns_name, ns_subnets, tag):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param fab: <vnc_api.gen.resource_client.Fabric>
        :param ns_name:
        :param ns_subnets:
        :param tag:
        :return:
        """
        self._logger.info(
            'Adding management ip namespace "%s" to fabric "%s" ...',
            ns_name, fab.name)

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
            self._logger.warn(
                "Fabric namespace '%s' already exists, "\
                "hence updating it ...", ns_name)
            vnc_api.fabric_namespace_update(namespace)

        namespace = vnc_api.fabric_namespace_read(fq_name=ns_fq_name)
        self._logger.info(
            "Fabric namespace created:\n%s",
            pprint.pformat(vnc_api.obj_to_dict(namespace), indent=4))
        return namespace
    # end _add_cidr_namespace

    def _add_asn_range_namespace(self, vnc_api, fab, ns_name, asn_ranges, tag):
        self._logger.info(
            'Adding ASN range namespace "%s" to fabric "%s" ...',
            ns_name, fab.name)

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
            self._logger.warn(
                "Fabric namespace '%s' already exists, "\
                "hence updating it ...", ns_name)
            vnc_api.fabric_namespace_update(namespace)

        namespace = vnc_api.fabric_namespace_read(fq_name=ns_fq_name)
        self._logger.debug(
            "Fabric namespace created:\n%s",
            pprint.pformat(vnc_api.obj_to_dict(namespace), indent=4))
        return namespace
    # end _add_asn_range_namespace

    def _add_node_profiles(self, vnc_api, fabric_obj, node_profiles):
        node_profile_objs = []
        for node_profile in node_profiles:
            name = str(node_profile.node_profile_name)
            np_fq_name = ['default-global-system-config', name]
            node_profile_obj = vnc_api.node_profile_read(fq_name=np_fq_name)
            node_profile_objs.append(node_profile_obj)

        for node_profile_obj in node_profile_objs:
            fabric_obj.add_node_profile(
                node_profile_obj, SerialNumListType(serial_num=[]))
        vnc_api.fabric_update(fabric_obj)
    # end _add_node_profiles

    def _add_virtual_network(self, vnc_api, network_name):
        nw_fq_name = ['default-domain', 'default-project', network_name]
        network = VirtualNetwork(
            name=network_name,
            fq_name=nw_fq_name,
            parent_type='project',
            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
            address_allocation_mode='flat-subnet-only')
        try:
            vnc_api.virtual_network_create(network)
        except RefsExistError:
            self._logger.warn(
                "virtual network '%s' already exists, "\
                "hence updating it ...", network_name)
            vnc_api.virtual_network_update(network)

        network = vnc_api.virtual_network_read(fq_name=nw_fq_name)
        self._logger.info(
            "virtual network created:\n%s",
            pprint.pformat(vnc_api.obj_to_dict(network), indent=4))
        return network
    # end _add_virtual_network

    def _new_subnet(self, cidr):
        split_cidr = cidr.split('/')
        return SubnetType(ip_prefix=split_cidr[0], ip_prefix_len=split_cidr[1])
    # end _new_subnet

    def _add_network_ipam(self, vnc_api, ipam_name, subnets, subnetting):
        ipam_fq_name = ['default-domain', 'default-project', ipam_name]
        ipam = NetworkIpam(
            name=ipam_name,
            fq_name=ipam_fq_name,
            parent_type='project',
            ipam_subnets=IpamSubnets([
                IpamSubnetType(
                    subnet=self._new_subnet(subnet.get('cidr')),
                    default_gateway=subnet.get('gateway')
                ) for subnet in subnets
            ]),
            ipam_subnet_method='flat-subnet',
            ipam_subnetting=subnetting
        )
        try:
            vnc_api.network_ipam_create(ipam)
        except RefsExistError:
            self._logger.warn(
                "network IPAM '%s' already exists, "\
                "hence updating it ...", ipam_name)
            vnc_api.network_ipam_update(ipam)

        ipam = vnc_api.network_ipam_read(fq_name=ipam_fq_name)
        self._logger.info(
            "network ipam created:\n%s",
            pprint.pformat(vnc_api.obj_to_dict(ipam), indent=4))
        return ipam
    # end _add_network_ipam

    def _add_fabric_vn(
            self, vnc_api, fabric_obj, network_type, subnets, subnetting):
        # create vn and ipam
        network_name = _fabric_network_name(
            str(fabric_obj.name), network_type)
        network = self._add_virtual_network(vnc_api, network_name)

        ipam_name = _fabric_network_ipam_name(
            str(fabric_obj.name), network_type)
        ipam = self._add_network_ipam(vnc_api, ipam_name, subnets, subnetting)

        # add vn->ipam link
        network.add_network_ipam(ipam, VnSubnetsType([]))
        vnc_api.virtual_network_update(network)

        # add fabric->vn link
        fabric_obj.add_virtual_network(
            network, FabricNetworkTag(network_type=network_type))
        vnc_api.fabric_update(fabric_obj)
    # end _add_fabric_vn

    ################### delete_fabric filter ##################################
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
        log = "\n"
        try:
            FilterModule._validate_job_ctx(job_ctx)
            vnc_api = FilterModule._init_vnc_api(job_ctx)

            fabric_fq_name = job_ctx.get('job_input', {}).get('fabric_fq_name')
            fabric_name = fabric_fq_name[-1]
            log = self._delete_fabric(vnc_api, log, fabric_fq_name)

            # delete fabric networks
            log = self._delete_fabric_network(
                vnc_api, log, fabric_name, NetworkType.MGMT_NETWORK)
            log = self._delete_fabric_network(
                vnc_api, log, fabric_name, NetworkType.LOOPBACK_NETWORK)
            log = self._delete_fabric_network(
                vnc_api, log, fabric_name, NetworkType.FABRIC_NETWORK)

            self._logger.warn(log)
            return {
                'status': 'success',
                'delete_log': log
            }
        except Exception as ex:
            self._logger.error(log + '\n' + str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'delete_log': log
            }
    # end delete_fabric

    def _delete_fabric(self, vnc_api, log, fabric_fq_name):
        """
        :param vnc_api: VncApi
        :param log: string
        :param fabric_fq_name: list<string>
        :return: log: string
        """
        try:
            fabric_obj = vnc_api.fabric_read(
                fq_name=fabric_fq_name, fields=['fabric_namespaces'])

            # delete all fabric devices
            for device_ref in fabric_obj.get_physical_router_refs() or []:
                device_uuid = str(device_ref.get('uuid'))
                log = self._delete_fabric_device(
                    vnc_api, log, fabric_obj, device_uuid
                )

            # delete all fabric namespaces
            for ns_ref in fabric_obj.get_fabric_namespaces() or []:
                log += 'Deleting fabric namespace "%s" ...'\
                        % str(ns_ref['to'][-1])
                vnc_api.fabric_namespace_delete(id=ns_ref.get('uuid'))
                log += "done\n"

            # un-assign node profiles
            fabric_obj.set_node_profile_list([])
            log += 'Unassigning node profiles from fabric ...'
            vnc_api.fabric_update(fabric_obj)
            log += "done\n"

            # un-assign virtual networks
            fabric_obj.set_virtual_network_list([])
            log += 'Unassigning virtual networks from fabric ...'
            vnc_api.fabric_update(fabric_obj)
            log += "done\n"

            log += 'Deleting fabric "%s" ...' % fabric_fq_name[-1]
            vnc_api.fabric_delete(fq_name=fabric_fq_name)
            log += 'done\n'
        except NoIdError:
            self._logger.warn(
                'Fabric object "%s" not found' % str(fabric_fq_name))
        return log
    # end _delete_fabric

    def _delete_fabric_device(self, vnc_api, log, fabric_obj, device_uuid):
        """
        :param vnc_api: type=VncApi
        :param log: type=string
        :param device_uuid: type=string
        :return: type=string, log
        """
        device_obj = vnc_api.physical_router_read(
            id=device_uuid, fields=['physical_interfaces'])

        for pi_ref in device_obj.get_physical_interfaces() or []:
            pi_uuid = str(pi_ref.get('uuid'))
            pi_obj = vnc_api.physical_interface_read(id=pi_uuid)

            # delete all the logical interfaces for this physical interface
            for li_ref in pi_obj.get_logical_interfaces() or []:
                li_uuid = str(li_ref.get('uuid'))
                li_obj = vnc_api.logical_interface_read(id=li_uuid)
                log += "Deleting logical interface %s => %s ..."\
                    % (str(li_obj.fq_name[1]), str(li_obj.fq_name[3]))
                vnc_api.logical_interface_delete(id=li_uuid)
                log += "done\n"

            log += "Deleting physical interface %s => %s ..."\
                % (str(pi_obj.fq_name[1]), str(pi_obj.fq_name[2]))
            vnc_api.physical_interface_delete(id=pi_uuid)
            log += "done\n"

        log += "Deleting deivce %s ..." % device_obj.display_name
        fabric_obj.del_physical_router(device_obj)
        vnc_api.fabric_update(fabric_obj)
        vnc_api.physical_router_delete(id=device_uuid)
        log += "done\n"

        return log
    # end _delete_fabric_device

    def _delete_fabric_network(self, vnc_api, log, fabric_name, network_type):
        """
        :param vnc_api: type=VncApi
        :param log: type=string
        :param fabric_name: type=string
        :param network_type: type=enum {'management', 'loopback', 'ip-fabric'}
        :return: type=string, delete log
        """
        network_name = _fabric_network_name(fabric_name, network_type)
        network_fq_name = ['default-domain', 'default-project', network_name]
        try:
            vn_obj = vnc_api.virtual_network_read(
                fq_name=network_fq_name, fields=['routing_instances'])
            if vn_obj.get_network_ipam_refs():
                log += 'Unassigning network ipam from "%s" network ...'\
                        % network_type
                vn_obj.set_network_ipam_list([])
                vnc_api.virtual_network_update(vn_obj)
                log += "done\n"

            for ri_ref in vn_obj.get_routing_instances() or []:
                log += 'Deleting routing instance for fabric "%s" ...'\
                        % fabric_name
                vnc_api.routing_instance_delete(id=ri_ref.get('uuid'))
                log += 'done\n'

            log += 'Deleting fabric network "%s" ...' % network_name
            vnc_api.virtual_network_delete(fq_name=network_fq_name)
            log += 'done\n'
        except NoIdError:
            self._logger.warn('Fabric network "%s" not found' % network_name)

        ipam_name = _fabric_network_ipam_name(fabric_name, network_type)
        ipam_fq_name = ['default-domain', 'default-project', ipam_name]
        try:
            if vnc_api.fq_name_to_id('network-ipam', ipam_fq_name):
                log += 'Deleting network ipam "%s" ...' % ipam_name
                vnc_api.network_ipam_delete(fq_name=ipam_fq_name)
                log += 'done\n'
        except NoIdError:
            self._logger.warn('network ipam "%s" not found' % ipam_name)
        return log
    # end _delete_fabric_network

    ################### assign_roles filter ###################################
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
                            "physical_roles": [ "leaf" ],
                            "routing_bridging_roles": [ "CRB" ]
                        },
                        ...
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
        log = "\n"
        try:
            FilterModule._validate_job_ctx(job_ctx)
            vnc_api = FilterModule._init_vnc_api(job_ctx)

            job_input = job_ctx.get('job_input', {})
            role_assignments = job_input.get('role_assignments', [])

            for device_roles in role_assignments:
                device_fq_name = device_roles.get('device_fq_name')
                log = self._add_logical_interfaces_for_fabric_links(
                    vnc_api, log, device_fq_name
                )

            for device_roles in role_assignments:
                log = self._assign_device_roles(vnc_api, log, device_roles)

            self._logger.warn(log)
            return {
                'status': 'success',
                'log': log
            }
        except Exception as ex:
            self._logger.error(log + '\n' + str(ex))
            traceback.print_exc(file=sys.stdout)
            return {
                'status': 'failure',
                'error_msg': str(ex),
                'log': log
            }
    # end assign_roles

    def _add_logical_interfaces_for_fabric_links(
            self, vnc_api, log, device_fq_name):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param log: string
        :param device_fq_name: list<string>
        :return: log: string
        """
        device_obj = vnc_api.physical_router_read(
            fq_name=device_fq_name,
            fields=['physical_interfaces', 'fabric_back_refs']
        )

        # get fabric object that this device belongs to
        fabric_back_refs = device_obj.get_fabric_back_refs() or []
        if len(fabric_back_refs) != 1:
            raise ValueError(
                "Unable to assign roles for device %s that does not belong to "
                "any fabric" % str(device_fq_name)
            )
        fabric_obj = vnc_api.fabric_read(id=fabric_back_refs[0].get('uuid'))

        # get network-ipam object for the fabric network
        fabric_ipam_name = _fabric_network_ipam_name(
            fabric_obj.name, NetworkType.FABRIC_NETWORK
        )
        fabric_ipam_obj = vnc_api.network_ipam_read(
            fq_name=['default-global-system-config', fabric_ipam_name]
        )

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
                vnc_api.local_interface_create(local_li)

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
                    name=local_mac,
                    fq_name=[local_mac],
                    instance_ip_subscriber_tag=subscriber_tag
                )
                vnc_api.instance_ip_create(iip_obj)
                iip_obj.add_logical_interface(local_li)
                vnc_api.instance_ip_update(iip_obj)

        return log
    # end _create_logical_interfaces_for_fabric_links

    def _build_li_name(
            self, device_obj, physical_interface_name, logical_interface_index):
        """
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        :param physical_interface_name: string
        :param logical_interface_index: string
        :return:
        """
        if device_obj.vendor and device_obj.vendor.lower() == 'juniper':
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

    def _get_pi_mac(self, phys_interface):
        """
        :param phys_interface: <vnc_api.gen.resource_client.PhysicalInterface>
        :return: physical interface mac address (type: string)
        """
        macs = phys_interface.physical_interface_mac_addresses()
        if macs and macs.get_mac_address() and len(macs.get_mac_address()) > 0:
            return macs.get_mac_address()[0]
        else:
            raise ValueError(
                "MAC address not found: %s" % str(phys_interface.fq_name)
            )
    # end _get_pi_mac

    def _get_device_fabric_links(self, vnc_api, device_obj):
        """
        :param vnc_api: <vnc_api.VncApi>
        :param device_obj: <vnc_api.gen.resource_client.PhysicalRouter>
        :return: list<Dictionary>
            [
                {
                   'local_pi': <vnc_api.gen.resource_client.PhysicalInterface>,
                   'remote_pi': <vnc_api.gen.resource_client.PhysicalInterface>
                },
                ...
            ]
        """
        physical_links = []
        pi_refs = device_obj.get_physical_interfaces()
        for ref in pi_refs or []:
            pi_obj = vnc_api.physical_interface_read(id=str(ref.get('uuid')))
            peer_pi_refs = pi_obj.get_physical_interface_refs()
            if peer_pi_refs:
                peer_pi_obj = vnc_api.physical_router_read(
                    id=str(peer_pi_refs[0].get('uuid'))
                )
                physical_links.append({
                    'local_pi': pi_obj,
                    'remote_pi': peer_pi_obj
                })
        return physical_links
    # end _get_device_fabric_links

    def _assign_device_roles(self, vnc_api, log, device_roles):
        """
        :param vnc_api: VncApi
        :param log: string
        :param device_roles: Dictionary
            example:
            {
                'device_fq_name': ['default-global-system-config', 'qfx-10'],
                'physical_roles": ['leaf'],
                'routing_bridging_roles": ['CRB']
            }
        :return:
        """
        return log
    # end _assign_device_roles

################### tests #####################################################
def _mock_job_ctx_onbard_fabric():
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
            "fabric_fq_name": [ "default-global-system-config", "fab01" ],
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
                { "cidr": "10.1.1.1/24", "gateway": "10.1.1.1" }
            ],
            "node_profiles": [
                {
                    "node_profile_name": "juniper-qfx5100",
                    "serial_nums": [
                        "a",
                        "b"
                    ]
                }
            ],
            "device_count": 5
        },
        "job_template_fqname": [
            "default-global-system-config",
            "fabric_onboard_template"
        ]
    }
# end _mock_job_ctx_onbard_fabric

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
            "fabric_fq_name": [ "default-global-system-config", "fab01" ]
        },
        "job_template_fqname": [
            "default-global-system-config",
            "fabric_onboard_template"
        ]
    }
# end _mock_job_ctx_delete_fabric

def _parse_args():
    parser = argparse.ArgumentParser(description='fabric filters tests')
    parser.add_argument('-c', '--create_fabric',
                        action='store_true', help='Onbaord fabric')
    parser.add_argument('-d', '--delete_fabric',
                        action='store_true', help='Delete fabric')
    return parser.parse_args()
# end _parse_args


if __name__ == '__main__':
    args = _parse_args()

    results = None
    fabric_filter = FilterModule()
    parser = _parse_args()
    if parser.create_fabric:
        results = fabric_filter.onboard_fabric(_mock_job_ctx_onbard_fabric())
    else:
        results = fabric_filter.delete_fabric(_mock_job_ctx_delete_fabric())
    print results
