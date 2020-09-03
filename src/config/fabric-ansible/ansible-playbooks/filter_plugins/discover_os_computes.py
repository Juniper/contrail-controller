#!/usr/bin/python

#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import logging
import traceback
import re
import sys
from builtins import object
from job_manager.job_utils import JobVncApi

sys.path.append('/opt/contrail/fabric_ansible_playbooks/filter_plugins')
sys.path.append('/opt/contrail/fabric_ansible_playbooks/common')
from contrail_command import CreateCCResource
from import_server import FilterModule as FilterModuleImportServer

DOCUMENTATION = '''
---
Discover OS Computes.

This file contains implementation of identifying all leaf nodes in provided fabric network and creating OS compute

find_leaf_devices filter:

Collect all devices which are added to a given fabric network.
Identify physical_router_role for each device and collect data only for leaf devices.
If device is leaf, credentials are gathered and returned to run "show lldp neighbors detail" command.

create_os_node_filter filter:

For output data from command "show lldp neighbors detail" collect needed data to create os node object.
Then return list of all objects founded in network in format:
nodes:
  - name: node-1
    node_type: ovs-compute
    ports:
      - name: ens224
        mac_address: 00:0c:29:13:37:bb
        switch_name: VM283DD71D00
'''

LEAF = 'leaf'
OVS = "ovs"
SRIOV = "sriov"
OVS_COMPUTE = "ovs-compute"
SRIOV_COMPUTE = "sriov-compute"
REGEX_NODE_TYPE = r"node_type: (\w+)"
FAILURE = 'failure'
SUCCESS = 'success'
STATUS = 'status'
ERRMSG = 'errmsg'
LEAF_DEVICES = 'leaf_devices'
OS_COMPUTE_NODES = 'os_compute_nodes'


class FilterModule(object):
    """Fabric filter plugins."""

    @staticmethod
    def _init_logging():
        """
        :return: type=<logging.Logger>
        """
        logger = logging.getLogger('OsComputesDiscoveryFilter')
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.WARN)

        formatter = logging.Formatter('%(asctime)s %(levelname)-8s %(message)s',
                                      datefmt='%Y/%m/%d %H:%M:%S')
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)

        return logger

    def __init__(self):
        self._logger = FilterModule._init_logging()

    def filters(self):
        """
        Return filters that will be used.
        """
        return {
            'find_leaf_devices_filter': self.find_leaf_devices_filter,
            'create_os_node_filter': self.create_os_node_filter
        }

    @staticmethod
    def _validate_job_ctx(job_ctx):
        """
        Validate input params.
        """
        job_input = job_ctx.get('input')
        if not job_input:
            raise ValueError('Invalid job_ctx: missing job_input')
        if not job_ctx.get('job_template_fqname'):
            raise ValueError('Invalid job_ctx: missing job_template_fqname')
        if not job_input.get('fabric_uuid'):
            raise ValueError('Invalid job_ctx: missing fabric_uuid')

    @staticmethod
    def _get_password(device_obj):
        """
        Get and return decrypted password.
        """
        password = device_obj.physical_router_user_credentials.get_password()
        return JobVncApi.decrypt_password(encrypted_password=password, pwd_key=device_obj.uuid)

    @staticmethod
    def get_fabric_name(fabric):
        """
        Get and return fabric_name.

        :param fabric: string
        :return fabric_name: string
        """
        return fabric.get_fq_name_str()

    @staticmethod
    def get_physical_router_devices(fabric):
        """
        Get and return list of physical routers in provided fabric.

        :param fabric: string
        :return physical_router_refs: list
        """
        physical_router_refs = fabric.get_physical_router_back_refs()
        if physical_router_refs is None:
            physical_router_refs = []
        return physical_router_refs

    # ***************** find_leaf_devices filter *********************************

    def find_leaf_devices(self, fabric_uuid, vnc_api):
        """
        Find and return all Leaf devices for given Fabric Network.

        For found devices, collect and return authentication data.
        Credentials data will be used to run commands directly on a device.

        :param fabric_uuid: string
        :param vnc_api: vnc_api established connection
        :return:
            example
            [
                {
                    "host": "10.10.10.2",
                    "password": "admin",
                    "username": "admin"
                },
                {
                    "host": "10.10.10.4",
                    "password": "admin",
                    "username": "admin"
                }
            ]
        """
        fabric = vnc_api.fabric_read(id=fabric_uuid)
        fabric_name = FilterModule.get_fabric_name(fabric)
        self._logger.info("Begin process of discovering leaf devices in fabric network %s" % fabric_name)
        physical_router_refs = FilterModule.get_physical_router_devices(fabric)
        self._logger.info(
            "In fabric %s Found the following list of physical routers %s" % (fabric_name, physical_router_refs))
        results = []
        for p_router in physical_router_refs:
            physical_router = vnc_api.physical_router_read(id=p_router['uuid'])
            if physical_router.physical_router_role != LEAF:
                continue
            host_details = {
                'username': physical_router.physical_router_user_credentials.username,
                'password': (FilterModule._get_password(physical_router)),
                'host': physical_router.physical_router_management_ip
            }
            results.append(host_details)
            self._logger.info("In fabric %s Found the following leaf device %s "
                              "On this device 'show lldp neighbor details' command will be applied"
                              % (fabric_name, physical_router.physical_router_management_ip))
        return results

    def find_leaf_devices_filter(self, job_ctx):
        """
        Validate input and call method to find leaf devices in provided fabric.

        :param job_ctx: Dictionary
            example:
             {
                'job_transaction_descr': 'Discover OS Computes',
                'fabric_uuid': '123412341234-123412341234',
                'contrail_command_host': '10.10.10.10:9091',
                'cc_username': 'root',
                'cc_password': "root"
            }
        :return: Dictionary
            if success, returns
            {
                'status': 'success',
                'leaf_devices': [
                        {
                            'username': u'admin',
                            'host': '10.10.10.4',
                            'password': 'admin'
                        }
                    ]
            }
            if failure, returns
            {
                'status': 'failure',
                'error_msg': <string: error message>
            }
        """
        try:
            FilterModule._validate_job_ctx(job_ctx)
            job_input = job_ctx.get('input')
            vnc_api = JobVncApi.vnc_init(job_ctx)
            fabric_uuid = job_input['fabric_uuid']
            leaf_devices = self.find_leaf_devices(fabric_uuid, vnc_api)
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            return {
                STATUS: FAILURE,
                ERRMSG: errmsg,
            }

        return {
            STATUS: SUCCESS,
            LEAF_DEVICES: leaf_devices,
        }

    # ***************** create_os_node_filter filter *********************************

    def get_mapping_ip_to_hostname(self, vnc_api, fabric_uuid):
        """
        Create a dictionary with mapping IP address to device Hostname.

        :param vnc_api: vnc_api class established connection
        :param fabric_uuid: string
        :return Dictionary
            example:
             {
                '10.10.10.4': 'Router_1',
                '10.10.10.7': 'Router_2'
            }
        """
        fabric = vnc_api.fabric_read(id=fabric_uuid)
        physical_router_refs = FilterModule.get_physical_router_devices(fabric)
        ip_to_hostname = {}
        for dev in physical_router_refs:
            physical_router = vnc_api.physical_router_read(id=dev['uuid'])
            device_ip_address = physical_router.physical_router_management_ip
            device_hostname = physical_router.get_physical_router_hostname()
            ip_to_hostname[device_ip_address] = device_hostname
        self._logger.debug("Found the following IP to Hostname mapping dictionary:  %s" % ip_to_hostname)
        return ip_to_hostname

    @staticmethod
    def get_node_type(system_description):
        """
        Basing on provided system_description verify and return node_type of OS compute.

        There are 2 possibilities: OVS and SRiOV, system description is mandatory value that
        should be set in LLDP system description on connected OS Compute node.

        :param system_description: string
            example: "node_type: OVS"
        :return: string or None
            example: "ovs-compute"
        """
        node_type = re.search(REGEX_NODE_TYPE, system_description)
        if not node_type:
            return None
        if node_type.group(1).lower() == OVS:
            return OVS_COMPUTE
        elif node_type.group(1).lower() == SRIOV:
            return SRIOV_COMPUTE

    @staticmethod
    def create_node_properties(device_neighbor_details, node_type, device_display_name):
        """
        Create and return node properties.

        :param device_neighbor_details: Dictionary
            example:
             {
                'lldp-remote-system-name': 'node-4',
                'lldp-local-interface': 'xe-0/0/3',
                'lldp-remote-port-description': u'ens224',
                'lldp-remote-port-id': '00:0c:29:8b:ef:26',
                (...)
            }
        :param node_type: String
        :param device_display_name: String
        :return: Dictionary
            example:
            {
                'nodes_type': 'ovs-compute',
                'name': u'node-1',
                'ports':
                    [{
                        'mac_address': u'00:0c:29:13:37:bb',
                        'port_name': u'xe-0/0/0',
                        'switch_name': u'VM283DD71D00',
                        'name': u'ens224'
                    }]
            }
        """
        port = {
            'port_name': str(device_neighbor_details['lldp-local-interface']),
            'switch_name': device_display_name,
            'name': str(device_neighbor_details['lldp-remote-port-description']),
            'mac_address': str(device_neighbor_details['lldp-remote-port-id'])
        }
        node = {
            'node_type': node_type,
            'name': str(device_neighbor_details['lldp-remote-system-name']),
            'ports': [port]
        }

        return node

    @staticmethod
    def import_nodes_to_contrail(all_nodes, cc_client):
        """
        Using import_server job trigger, import nodes to CC.

        :param all_nodes: Dictionary
        :param cc_client: CreateCCResource object class
        :return: None
        """
        logging.info("Begin adding nodes {} to Contrail Command".format(str(all_nodes)))
        FilterModuleImportServer().import_nodes(all_nodes, cc_client)

    @staticmethod
    def get_switch_name(node):
        """
        Get and return switch_name.

        There is always only one element in a list.
        """
        return node['ports'][0]['switch_name']

    @staticmethod
    def get_ip_address(device_command_output):
        """
        Get and return IP address of a device.

        The structure of input Dictionary is gathered directly from Juniper device.
        """
        return device_command_output['item']['host']

    @staticmethod
    def get_dev_neighbors_details(device_command_output):
        """
        Get and return LLDP neighbor details.

        The structure of input Dictionary is gathered directly from Juniper device.
        """
        return device_command_output['parsed_output']['lldp-neighbors-information']['lldp-neighbor-information']

    @staticmethod
    def get_system_description(device_neighbor_details):
        """
        Get and return LLDP neighbor system description.

        The structure of input Dictionary is gathered directly from Juniper device.
        """
        return device_neighbor_details['lldp-system-description']['lldp-remote-system-description']

    @staticmethod
    def get_hostname(ip_to_hostname_mapping, device_ip_address):
        """
        Get and return hostname.
        """
        return ip_to_hostname_mapping[device_ip_address]

    def create_os_node(self, vnc_api, devices_command_output, fabric_uuid, cc_client):
        """
        Create and return list of OS Object nodes and its properties.

        Nodes are created basing on devices_command_output.
        Device that is going to be created as a node in Autodiscovery process must have
        contain "node_type: <ovs/sriov>" information in its LLDP description.
        If this description is not added, the device will be skipped.

        :param cc_client: CreateCCResource object class
        :param fabric_uuid: String
        :param vnc_api: vnc_api class established connection:
        :param devices_command_output: Dictionary

        :return: list
        example:
        [
            {
                'nodes_type': 'ovs-compute',
                'name': u'node-1',
                'ports':
                    [{
                        'mac_address': u'00:0c:29:13:37:bb',
                        'port_name': u'xe-0/0/0',
                        'switch_name': u'VM283DD71D00',
                        'name': u'ens224'
                    }]
            }
        ]
        """
        self._logger.info("Begin process of creating OS nodes object in fabric network")
        nodes = []
        ip_to_hostname = self.get_mapping_ip_to_hostname(vnc_api, fabric_uuid)
        for device_command_output in devices_command_output['results']:
            device_ip_address = FilterModule.get_ip_address(device_command_output)
            device_hostname = FilterModule.get_hostname(ip_to_hostname, device_ip_address)
            devices_neighbors_details = FilterModule.get_dev_neighbors_details(device_command_output)
            for device_neighbor_details in devices_neighbors_details:
                system_description = FilterModule.get_system_description(device_neighbor_details)
                node_type = FilterModule.get_node_type(system_description)
                if node_type is None:
                    continue
                node = FilterModule.create_node_properties(device_neighbor_details, node_type, device_hostname)
                nodes.append(node)
                switch_name = FilterModule.get_switch_name(node)
                self._logger.info("On device %s found node: %s connected to %s" % (device_hostname, node, switch_name))
        created_nodes = {
            'nodes': nodes
        }
        self._logger.info("Nodes found and created: %s" % created_nodes)
        FilterModule.import_nodes_to_contrail(created_nodes, cc_client)
        return created_nodes

    def create_os_node_filter(self, job_ctx, devices_command_outputs):
        """
        Param (devices_command_outputs) is a result from "show lldp neighbors detail" command.

        This param was gathered automatically in previous task, when above command was run on all
        leaf devices in fabric.

        :param devices_command_outputs: Dictionary
            example:
            {
                'msg': u'All items completed',
                'changed': False,
                'results': [
                    {
                        "parsed_output": {
                            "lldp-neighbors-information": {
                                "lldp-neighbor-information": [
                                    {
                                        (...)
                                        "lldp-local-interface": "xe-0/0/0",
                                        (...)
                                        "lldp-remote-management-address": "10.5.5.5",
                                       (...)
                                        "lldp-remote-port-description": "ens256",
                                        "lldp-remote-port-id": "00:0c:29:13:37:c5"
                                    }
                                    ]
                                }
                            }
                        }
                    }
                ]
            }
        :param job_ctx: Dictionary
            example:
            {
                'job_transaction_descr': 'Discover OS Computes',
                'fabric_uuid': '123412341234-123412341234',
                'contrail_command_host': '10.10.10.10:9091',
                'cc_username': 'root',
                'cc_password': "root"
            }
        :return: Dictionary
            if success, returns
            {
                'status': 'success'
                'os_compute_nodes':
                    {
                        'nodes':
                        [
                            {
                                'name': 'node-1'
                                'node_type': 'ovs-compute',
                                'ports': [{
                                    'address': '00:0c:29:13:37:c5',
                                    'port_name': 'xe-0/0/0',
                                    'switch_name': 'VM283DF6BA00',
                                    'name': 'ens256'
                                }]
                            }
                        ]
                    }
            }
            if failure, returns
            {
                'status': 'failure',
                'error_msg': <string: error message>
            }
        """
        try:
            FilterModule._validate_job_ctx(job_ctx)
            job_input = job_ctx.get('input')
            vnc_api = JobVncApi.vnc_init(job_ctx)
            fabric_uuid = job_input['fabric_uuid']
            cluster_id = job_ctx.get('contrail_cluster_id')
            cluster_token = job_ctx.get('auth_token')
            cc_host = job_input['contrail_command_host']
            cc_username = job_input['cc_username']
            cc_password = job_input['cc_password']
            cc_client = CreateCCResource(cc_host, cluster_id, cluster_token, cc_username, cc_password)
            os_compute_nodes = self.create_os_node(vnc_api, devices_command_outputs, fabric_uuid, cc_client)
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            return {
                STATUS: FAILURE,
                ERRMSG: errmsg,
            }

        return {
            STATUS: SUCCESS,
            OS_COMPUTE_NODES: os_compute_nodes,
        }
