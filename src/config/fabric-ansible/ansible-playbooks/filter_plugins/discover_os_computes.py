#!/usr/bin/python

#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

import sys
from builtins import object
import logging
import traceback
import re
from job_manager.job_utils import JobVncApi
sys.path.append('/opt/contrail/fabric_ansible_playbooks/filter_plugins')
from import_server import FilterModule as FilterModuleImportServer
sys.path.append('/opt/contrail/fabric_ansible_playbooks/common')
from contrail_command import CreateCCNode

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


class FilterModule(object):

    def filters(self):
        return {
            'find_leaf_devices_filter': self.find_leaf_devices_filter,
            'create_os_node_filter': self.create_os_node_filter
        }

    @staticmethod
    def _validate_job_ctx(job_ctx):
        vnc_api = JobVncApi.vnc_init(job_ctx)
        job_input = job_ctx.get('input')
        if not job_input:
            raise ValueError('Invalid job_ctx: missing job_input')
        if not job_ctx.get('job_template_fqname'):
            raise ValueError('Invalid job_ctx: missing job_template_fqname')
        if not job_input.get('fabric_uuid'):
            raise ValueError('Invalid job_ctx: missing fabric_uuid')

        return job_input, vnc_api

    @staticmethod
    def _get_password(device_obj):
        return JobVncApi.decrypt_password(
            encrypted_password=device_obj.physical_router_user_credentials.get_password(),
            pwd_key=device_obj.uuid)

    # ***************** find_leaf_devices filter *********************************

    @staticmethod
    def find_leaf_devices(fabric_uuid, vnc_api):
        """
        Find and return all Leaf devices for given Fabric Network.
        For found devices, collect and return authentication data. Authentication data will be used in next step
        to run commands directly on a device.
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
        logging.info("Begin process of discovering leaf devices in fabric network {}".format
                     (str(fabric_uuid)))
        fabric = vnc_api.fabric_read(id=str(fabric_uuid))
        physical_router_refs = fabric.get_physical_router_back_refs()
        logging.info(
            "In fabric {} Found the following list of physical routers {}".format(str(fabric_uuid),
                                                                                  str(physical_router_refs)))
        results = []
        for p_router in physical_router_refs:
            physical_router = vnc_api.physical_router_read(id=p_router['uuid'])
            if physical_router.physical_router_role != 'leaf':
                continue

            host_details = dict(username=str(physical_router.physical_router_user_credentials.username),
                                password=str(FilterModule._get_password(physical_router)),
                                host=str(physical_router.physical_router_management_ip))

            results.append(host_details)
            logging.info("In fabric {} Found the following leaf device {} "
                         "On this device 'show lldp neighbor details' command will be applied".
                         format(str(fabric_uuid), str(physical_router.physical_router_management_ip)))

        return results

    @staticmethod
    def find_leaf_devices_filter(job_ctx):
        """
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
            [
                <list: found devices>
            ]
            if failure, returns
            {
                'status': 'failure',
                'error_msg': <string: error message>
            }
        """
        try:
            job_input, vnc_api = FilterModule._validate_job_ctx(job_ctx)
            fabric_uuid = job_input['fabric_uuid']
            leaf_devices = FilterModule.find_leaf_devices(fabric_uuid, vnc_api)
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            return {
                'status': 'failure',
                'errmsg': errmsg,
            }

        return {
            'status': 'success',
            'leaf_devices': leaf_devices,
        }

    # ***************** create_os_node_filter filter *********************************

    @staticmethod
    def check_display_name(vnc_api, device_ip_address, fabric_uuid):
        """
        Check and return Display Name for given device IP address.

        :param device_ip_address: string
        :param vnc_api: vnc_api class established connection
        :param fabric_uuid: string
        :return: string
            example: "Switch1"
        """

        fabric = vnc_api.fabric_read(id=str(fabric_uuid))
        fabric_devices = fabric.get_physical_router_back_refs()
        display_name = None
        for dev in fabric_devices:
            physical_router = vnc_api.physical_router_read(id=dev['uuid'])
            if physical_router.physical_router_management_ip == device_ip_address:
                display_name = physical_router.display_name
                break
        if display_name is None:
            raise Exception(
                'Display name with IP address {} not found in Fabric {}'.format(device_ip_address, str(fabric_uuid)))
        return display_name

    @staticmethod
    def check_node_type(system_description):
        """
        Basing on provided system_description verify and return node_type of OS compute.
        There are 2 possibilities: OVS and SRiOV, system description is mandatory value that should be set in
        LLDP system description on connected OS Compute node.

        :param system_description: string
            example: "node_type: OVS"
        :return: string or None
            example: "ovs-compute"
        """
        node_type = re.search(r"node_type: (\w+)", system_description)
        try:
            if (node_type.group(1)).lower() == "ovs":
                return "ovs-compute"
            elif (node_type.group(1)).lower() == "sriov":
                return "sriov-compute"
            else:
                return None
        except AttributeError:
            return None

    @staticmethod
    def create_node_properties(device_neighbor_details, node_type, device_display_name):
        """
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
        port = {}
        node = {}
        ports = []
        node['node_type'] = str(node_type)
        port['port_name'] = str(device_neighbor_details['lldp-local-interface'])
        port['switch_name'] = str(device_display_name)
        port['name'] = str(device_neighbor_details['lldp-remote-port-description'])
        port['mac_address'] = str(device_neighbor_details['lldp-remote-port-id'])
        node['name'] = str(device_neighbor_details['lldp-remote-system-name'])
        ports.append(port)
        node['ports'] = ports

        return node

    @staticmethod
    def import_nodes_to_contrail(all_nodes, cc_node_obj):
        """
        :param all_nodes: Dictionary
        :param cc_node_obj: CreateCCNode object class
        :return: None
        """
        logging.info("Begin adding nodes {} to Contrail Command".format(str(all_nodes)))
        FilterModuleImportServer().import_nodes(all_nodes, cc_node_obj)

    @staticmethod
    def create_os_node(vnc_api, devices_command_output, fabric_uuid, cc_node_obj):
        """
        Create and return list of OS Object nodes and its properties.
        Nodes are created basing on devices_command_output which is an output from checking LLDP devices connected to
        each network device.
        Device that should be created as a node in Autodiscovery process must have "node_type"
        added in its LLDP description. If this description is not added, the device will be skipped.

        :param cc_node_obj: CreateCCNode object class
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
        errmsg = None
        logging.info("Begin process of creating OS nodes object in fabric network {}".format(str(fabric_uuid)))
        created_nodes = {}
        nodes = []
        for device_command_output in devices_command_output['results']:
            device_ip_address = device_command_output['item']['host']
            device_display_name = FilterModule.check_display_name(vnc_api, device_ip_address, fabric_uuid)
            for device_neighbor_details in device_command_output['parsed_output']['lldp-neighbors-information'][
                'lldp-neighbor-information']:
                node_type = FilterModule.check_node_type(device_neighbor_details['lldp-system-description']
                                                         ['lldp-remote-system-description'])
                if node_type is None:
                    continue
                node = FilterModule.create_node_properties(device_neighbor_details, node_type,
                                                           device_display_name)
                nodes.append(node)
                logging.info("On device {} found node: {} ".format(str(device_display_name), str(nodes)))

        created_nodes['nodes'] = nodes
        FilterModule.import_nodes_to_contrail(created_nodes, cc_node_obj)
        logging.info("Nodes found and created:" + str(created_nodes))
        return created_nodes, errmsg

    @staticmethod
    def create_os_node_filter(job_ctx, devices_command_outputs):
        """
        :param devices_command_outputs: Dictionary
            devices_command_outputs is a result from "show lldp neighbors detail" command,
            this param was gathered automatically in previous task, when above command was run on
            all leaf devices in fabric.
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
                <list: found OS nodes>
            }
            if failure, returns
            {
                'status': 'failure',
                'error_msg': <string: error message>
            }
        """
        errmsg = None
        try:
            job_input, vnc_api = FilterModule._validate_job_ctx(job_ctx)
            fabric_uuid = job_input['fabric_uuid']
            cluster_id = job_ctx.get('contrail_cluster_id')
            cluster_token = job_ctx.get('auth_token')
            cc_host = job_input['contrail_command_host']
            cc_username = job_input['cc_username']
            cc_password = job_input['cc_password']
            cc_node_obj = CreateCCNode(cc_host, cluster_id, cluster_token, cc_username, cc_password)
            os_compute_nodes = FilterModule.create_os_node(vnc_api, devices_command_outputs, fabric_uuid, cc_node_obj)
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            return {
                'status': 'failure',
                'errmsg': errmsg,
            }

        return {
            'status': 'success',
            'os_compute_nodes': os_compute_nodes,
            'error_msg': errmsg
        }
