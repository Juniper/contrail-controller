#!/usr/bin/python

#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

from builtins import object
import logging
import traceback
import sys
import re

sys.path.append('/opt/contrail/fabric_ansible_playbooks/filter_plugins')
sys.path.append('/opt/contrail/fabric_ansible_playbooks/common')
from job_manager.job_utils import JobVncApi

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
            encrypted_password=device_obj.physical_router_user_credentials.
                get_password(),
            pwd_key=device_obj.uuid)

    def filters(self):
        return {
            'find_leaf_devices_filter': self.find_leaf_devices_filter,
            'create_os_node_filter': self.create_os_node_filter
        }

    # ***************** find_leaf_devices filter *********************************

    @staticmethod
    def find_leaf_devices(job_input, vnc_api):
        """
        Find and return all Leaf devices for provided in job_input Fabric Network.
        For found devices, collect and return authentication data. Authentication data will be used in next step
        to run commands directly on a device.
        
        TODO: verify if physical router is enough to get all devices
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
                     (str(job_input['fabric_uuid'])))
        fabric = vnc_api.fabric_read(id=str(job_input['fabric_uuid']))
        physical_router_refs = fabric.get_physical_router_back_refs()
        logging.info(
            "In fabric {} Found the following list of physical routers {}".format(str(job_input['fabric_uuid']),
                                                                                  physical_router_refs))
        results = []
        for i in physical_router_refs:
            physical_router = vnc_api.physical_router_read(id=i['uuid'])
            if physical_router.physical_router_role != 'leaf':
                continue

            host_details = dict(username=str(physical_router.physical_router_user_credentials.username),
                                password=str(FilterModule._get_password(physical_router)),
                                host=str(physical_router.physical_router_management_ip))

            results.append(host_details)
            logging.info("In fabric {} Found the following leaf device {} "
                         "On this device 'show lldp neighbor details' command will be applied".
                         format(str(job_input['fabric_uuid']), str(physical_router.physical_router_management_ip)))

        # TODO: information to UI log if NO devices found
        if len(results) != 0:
            logging.info("No leaf devices were found in provided fabric {} ".format(str(job_input['fabric_uuid'])))
        return results

    @staticmethod
    def find_leaf_devices_filter(job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "encoded_nodes": "....",
                    "contrail_command_host": "....",
                    "encoded_file": "...."
                }
            }
        :return: Dictionary
            if success, returns
            [
                <list: discovered devices>
            ]
            if failure, returns
            {
                'status': 'failure',
                'error_msg': <string: error message>,
                'import_log': <string: import_log>
            }
        """
        try:
            job_input, vnc_api = FilterModule._validate_job_ctx(job_ctx)
            leaf_devices = FilterModule.find_leaf_devices(job_input, vnc_api)
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
    def check_display_name(router_ip, vnc_api, fabric_uuid):
        """
        TODO: check if possibility to make it with vnc_api built-in method
        
        Check and return Display Name value for given switch leaf IP address.
        Match IP address with all IP addresses in
        
        :param router_ip: string (ip_address)
        :param vnc_api: vnc_api class established connedction
        :param fabric_uuid: string
        :return: string
        example: "Switch1"
        """
        fabric = vnc_api.fabric_read(id=str(fabric_uuid))
        fabric_devices = fabric.get_physical_router_back_refs()
        display_name = None
        for dev in fabric_devices:
            physical_router = vnc_api.physical_router_read(id=dev['uuid'])
            if physical_router.physical_router_management_ip == router_ip:
                display_name = physical_router.display_name
                break
        if display_name is None:
            raise ValueError('Router IP address {} not found in provided Fabric {}'.format(router_ip, str(fabric_uuid)))
        return display_name

    @staticmethod
    def check_node_type(system_description):
        """
        Basing on provided system_description verify and return node_type of OS compute.
        There are 2 possibilities: OVS and SRiOV, system description is mandatory value that should be set in
        LLDP system description on connected OS Compute node.

        :param system_description: string
            example:
            node_type: OVS
        :return: string
            example: "ovs-compute"
        """
        node_type = re.search(r"node_type: (\w+)", system_description)
        try:
            if (node_type.group(1)).lower() == "ovs":
                return "ovs-compute"
            elif (node_type.group(1)).lower() == "SRiOV":
                return "sriov-compute"
        except AttributeError:
            return None

    @staticmethod
    def create_os_node(vnc_api, leaf_devices_command_output, job_input):
        """
        Create and return OS Object node. In playbook_input param provided is full output from command 
        "show lldp neighbors detail", which was run on all devices found in one task ago.
        The output from command is parsed and all data is gathered to create object node as example below.
        
        :param vnc_api: vnc_api class established connection:
        :param job_input: Dictionary
        :param leaf_devices_command_output: Dictionary
        example:
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

        :return: list
        example:
        [
            {
                'nodes_type': 'ovs-compute',
                'name': u'node-1',
                'ports':
                    {
                        'mac_address': u'00:0c:29:13:37:bb',
                        'port_name': u'xe-0/0/0',
                        'switch_name': u'VM283DD71D00',
                        'name': u'ens224'
                    }
            }
        ]
        """
        logging.info("Begin process of creating OS nodes object in fabric network {}".format
                     (str(job_input['fabric_uuid'])))
        all_nodes = {}
        nodes = []
        for device_output in leaf_devices_command_output['results']:
            leaf_device_ip_address = (device_output['item']['host'])
            leaf_device_display_name = FilterModule.check_display_name(leaf_device_ip_address, vnc_api,
                                                                       job_input['fabric_uuid'])
            logging.info("Checking LLDP output on device {}".format(str(leaf_device_display_name)))

            for neighbor in device_output['parsed_output']['lldp-neighbors-information']['lldp-neighbor-information']:
                port = {}
                node = {}
                node_type = FilterModule.check_node_type(neighbor['lldp-system-description']['lldp-remote-system-description'])
                if node_type is None:
                    continue
                node['nodes_type'] = node_type
                port['port_name'] = neighbor['lldp-local-interface']
                port['switch_name'] = leaf_device_display_name
                port['name'] = neighbor['lldp-remote-port-description']
                port['mac_address'] = neighbor['lldp-remote-port-id']
                node['name'] = neighbor['lldp-remote-system-name']
                node['ports'] = port
                nodes.append(node)
                logging.info("On device {} found node: {} ".format(str(leaf_device_display_name), str(nodes)))

        all_nodes['nodes'] = nodes
        logging.info("Finished checking all devices, nodes that will be created: {} " + str(all_nodes))
        return all_nodes

    @staticmethod
    def create_os_node_filter(job_ctx, leaf_devices_command_output):
        """
        :param leaf_devices_command_output: Dictionary
            leaf_devices_command_output is a result from "show lldp neighbors detail" command,
            this param was gathered automatically in previous task, when above command was run on
            all leaf devices in fabric.
            example:
            {
                'msg': '...',
                'changed': False,
                'results': [
                    {
                    ...
                    }
                ]
            }
        :param job_ctx: Dictionary
            example:
            {
                'job_transaction_descr': 'Discover OS Computes',
                (...)
                'fabric_fq_name': 'default-global-system-config:ntf',
                (...)
            }
        :return: Dictionary
            if success, returns
            [
                <list: found OS nodes>
            ]
            if failure, returns
            {
                'status': 'failure',
                'error_msg': <string: error message>,
                'import_log': <string: import_log>
            }
        """
        errmsg = None
        try:
            job_input, vnc_api = FilterModule._validate_job_ctx(job_ctx)
            os_compute_nodes = FilterModule.create_os_node(vnc_api, leaf_devices_command_output, job_input)
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
