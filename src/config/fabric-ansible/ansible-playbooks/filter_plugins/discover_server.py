#!/usr/bin/python

from __future__ import print_function
from builtins import str
from builtins import range
from builtins import object
import logging
import sys
import traceback
import netaddr
import subprocess
import itertools
import requests
import gevent
from gevent import Greenlet, monkey, pool, queue
from pprint import pformat
sys.path.append('/opt/contrail/fabric_ansible_playbooks/filter_plugins')
sys.path.append('/opt/contrail/fabric_ansible_playbooks/common')
from plugin_ironic import ImportIronicNodes
from contrail_command import CreateCCNode, CreateCCNodeProfile
import json
import jsonschema
from job_manager.job_utils import JobVncApi


class DiscoveryLog(object):
    _instance = None

    @staticmethod
    def instance():
        if not DiscoveryLog._instance:
            DiscoveryLog._instance = DiscoveryLog()
        return DiscoveryLog._instance
    # end instance

    @staticmethod
    def _init_logging():
        """
        :return: type=<logging.Logger>
        """
        logger = logging.getLogger('ServerFilter')
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
        self._logger = DiscoveryLog._init_logging()
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
# end DiscoveryLog


class FilterModule(object):
    @staticmethod
    def _init_logging():
        logger = logging.getLogger('ServerDiscoveryFilter')
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.WARN)

        formatter = logging.Formatter('%(asctime)s %(levelname)-8s %(message)s',
                                      datefmt='%Y/%m/%d %H:%M:%S')
        console_handler.setFormatter(formatter)
        logger.addHandler(console_handler)

        return logger

    def __init__(self):
        self._logger = FilterModule._init_logging()
    # end __init__

    @staticmethod
    def _validate_job_ctx(job_ctx):
        vnc_api = JobVncApi.vnc_init(job_ctx)
        job_template_fqname = job_ctx.get('job_template_fqname')
        if not job_template_fqname:
            raise ValueError('Invalid job_ctx: missing job_template_fqname')

        job_input = job_ctx.get('input')
        if not job_input:
            raise ValueError('Invalid job_ctx: missing job_input')

        # retrieve job input schema from job template to validate the job input
        server_discovery_template = vnc_api.job_template_read(
            fq_name=job_template_fqname
        )
        input_schema = server_discovery_template.get_job_template_input_schema()
        input_schema = json.loads(input_schema)
        jsonschema.validate(job_input, input_schema)
        return job_input

    def filters(self):
        return {
            'expand_subnets': self.expand_subnets,
            'ping_sweep': self.ping_sweep,
            'ipmi_auth_check': self.ipmi_auth_check,
            'check_nodes_with_cc': self.check_nodes_with_cc,
            'register_nodes': self.register_nodes,
            'trigger_introspect': self.trigger_introspect,
            'import_ironic_nodes': self.import_ironic_nodes,
        }

    def check_nodes_with_cc(self, job_ctx, ipmi_nodes_detail):
        try:
            # TODO: returning all nodes as of now, it must be the diff of nodes
            # from CC and ipmi_nodes_details
            final_ipmi_details = []
            job_input = FilterModule._validate_job_ctx(job_ctx)
            cluster_id = job_ctx.get('contrail_cluster_id')
            cluster_token = job_ctx.get('auth_token')
            cc_host = job_input.get('contrail_command_host')
            cc_username = job_input.get('cc_username')
            cc_password = job_input.get('cc_password')
            cc_node = CreateCCNode(cc_host, cluster_id, cluster_token,
                                   cc_username, cc_password)
            cc_nodes = cc_node.get_cc_nodes()
            cc_node_data = []
            for node in cc_nodes['nodes']:
                cc_node = {}
                if node.get('bms_info') \
                        and node['bms_info'].get('driver_info') \
                        and node['bms_info']['driver_info'].get('ipmi_address'):

                    print(node['bms_info']['driver_info']['ipmi_address'])
                    cc_node['ipmi_address'] = node['bms_info']['driver_info'][
                        'ipmi_address']
                    if node['bms_info']['driver_info'].get('ipmi_port'):
                        print(node['bms_info']['driver_info']['ipmi_port'])
                        cc_node['ipmi_port'] = node['bms_info']['driver_info'][
                            'ipmi_port']
                    else:
                        cc_node['ipmi_port'] = '623'

                    cc_node_data.append(cc_node)

            for ipmi_node in ipmi_nodes_detail:
                node_found_in_cc = False
                for cc_node in cc_node_data:
                    if ipmi_node['address'] == cc_node['ipmi_address'] \
                            and int(ipmi_node['port']) == int(
                                cc_node['ipmi_port']):
                        # node is already there in contrail-command, skip for
                        # introspection
                        node_found_in_cc = True
                        break

                if node_found_in_cc == False:
                    final_ipmi_details.append(ipmi_node)
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            self._logger.error(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'discover_log': DiscoveryLog.instance().dump()
            }

        return {
            'status': 'success',
            'final_ipmi_details': final_ipmi_details,
            'discover_log': DiscoveryLog.instance().dump()
        }


    def ping_check(self, retry_queue, result_queue ):
        if retry_queue.empty() :
          return True

        host_params = retry_queue.get_nowait()
        host = host_params['address']
        try:
            ping_output = subprocess.Popen(
                ['ping', '-W', '1', '-c', '2', host], stdout=subprocess.PIPE)
            gevent.sleep(2)
            ping_output.wait()
            result_queue.put({'address':host,
                              'result': ping_output.returncode == 0})
            return ping_output.returncode == 0
        except Exception as ex:
            print(host, "ERROR: SUBPROCESS.POPEN failed with error {}")
            return False
    # end _ping_check

    def ping_sweep(self, job_ctx, ipaddr_list):
        try:
            input_queue = queue.Queue()
            result_queue = queue.Queue()
            for address in ipaddr_list:
                host_map_dict = {
                    'address': str(address)
                }
                input_queue.put(host_map_dict)

            self.call_gevent_func(input_queue, result_queue, self.ping_check)

            ping_sweep_success_list = []
            while result_queue.qsize() != 0:
                result = result_queue.get()
                if result['result']:
                    ping_sweep_success_list.append(result['address'])
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            self._logger.error(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'discover_log': DiscoveryLog.instance().dump()
            }
        return ping_sweep_success_list

    # ***************** expand_subnets filter *********************************

    def expand_subnets(self, job_ctx):
        """
        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "ipmi": {
                        "ipmi_subnet_list": [
                            "30.1.1.1/24"
                        ],
                        "ipmi_cred_list": [
                           "admin:admin",
                           "admin:password"
                        ],
                        "ipmi_port_ranges": [
                            {
                                "port_range_start": 623,
                                "port_range_end": 623
                            }
                        ]
                    },
                    "ironic": {
                        "auth_url": "",
                        "username": "",
                        "password": ""
                    },
                    "contrail_command_host": ""
                }
            }
        :return: Dictionary
            if success, returns
            [
                <list: valid_ipmi_list>
            ]
            if failure, returns
            {
                'status': 'failure',
                'error_msg': <string: error message>,
                'discover_log': <string: discover_log>
            }
            """
        try:
            job_input = FilterModule._validate_job_ctx(job_ctx)
            self._logger.info("Job INPUT:\n" + str(job_input))
            ipmi_config = job_input.get('ipmi')
            ipmi_subnets = ipmi_config.get('ipmi_subnet_list')
            self._logger.info("Starting Server Discovery2")
            ipmi_addresses = []

            for subnet in ipmi_subnets:
                for ipaddr in netaddr.IPNetwork(subnet):
                    ipmi_addresses.append(str(ipaddr))
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            self._logger.error(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'discover_log': DiscoveryLog.instance().dump()
            }

        return list(set(ipmi_addresses))


    def ipmi_check_gevent(self, input_queue, result_queue):
        if input_queue.empty():
            return True
        ipmi_host = input_queue.get_nowait()
        status = self.ipmi_check_power_status(ipmi_host['address'],
                                              ipmi_host['username'],
                                              ipmi_host['password'],
                                              ipmi_host['port'])
        ipmi_host['valid'] = status
        result_queue.put(ipmi_host)
        return

    def ipmi_check_power_status(self, address, ipmi_username, ipmi_password,
                               ipmi_port):
        try:
            ipmi_output = subprocess.Popen(['/usr/bin/ipmitool', '-R', '2',
                                              '-I','lanplus',
                                              '-U', ipmi_username,
                                              '-P', ipmi_password,
                                              '-p', str(ipmi_port),
                                              '-H', address,
                                              'power' ,'status'],
                                          stdout=subprocess.PIPE,
                                          stderr=subprocess.PIPE)
            gevent.sleep(10)
            ipmi_output.wait()
            if ipmi_output.returncode == 0:
                return True
            else:
                return False
        except Exception as ex:
            print(address, "ERROR: SUBPROCESS.POPEN failed {} ".format(str(ex)))
            return False

    def call_gevent_func(self, input_queue, result_queue, func_call):
        concurrent = 500
        if input_queue.qsize() < concurrent:
            concurrent = input_queue.qsize()

        threadpool = pool.Pool(concurrent)
        while True:
          try:
            if input_queue.qsize() == 0:
                break

            for each_thread in range(concurrent):
                threadpool.start(
                    Greenlet(
                        func_call,
                        input_queue, result_queue ))
            threadpool.join()

          except queue.Empty:
            print("QUEUE EMPTY EXIT")
            break

        print(("RESULTS-QUEUE-SIZE" , result_queue.qsize()))

    def ipmi_auth_check(self, job_ctx, ipaddress_list):
        """
        :param job_ctx: Dictionary
            example:
            {
                "auth_token": "EB9ABC546F98",
                "job_input": {
                    "ipmi": {
                        "ipmi_subnet_list": [
                            "30.1.1.1/24"
                        ],
                        "ipmi_cred_list": [
                           "admin:admin",
                           "admin:password"
                        ],
                        "ipmi_port_ranges": [
                            {
                                "port_range_start": 623,
                                "port_range_end": 623
                            }
                        ]
                    },
                    "ironic": {
                        "auth_url": "",
                        "username": "",
                        "password": ""
                    },
                    "contrail_command_host": ""
                }
            }
        :return: Dictionary
            if success, returns
            [
                <list: valid_ipmi_details>
            ]
            if failure, returns
            {
                'status': 'failure',
                'error_msg': <string: error message>,
                'discover_log': <string: discover_log>
            }
            """
        try:
            job_input = FilterModule._validate_job_ctx(job_ctx)
            ipmi_config = job_input.get('ipmi')
            ipmi_credentials = ipmi_config.get('ipmi_credentials')
            ipmi_port_ranges = ipmi_config.get('ipmi_port_ranges')
            valid_ipmi_details = []
            # expand ipmi_ports to port list
            port_list = []
            for ipmi_port_range in ipmi_port_ranges:
                port_range_start = ipmi_port_range["port_range_start"]
                port_range_end = ipmi_port_range["port_range_end"]
                if port_range_end >= port_range_start:
                    port_list.extend(list(range(port_range_start,port_range_end+1)))
                else:
                    print("IGNORING, BAD RANGE ", ipmi_port_range)

            final_port_list = list(set(port_list))

            validate_ipmi_details_queue = queue.Queue()
            result_queue = queue.Queue()
            for address in ipaddress_list:
                for item in itertools.product(final_port_list,
                                              ipmi_credentials):
                    ipmi_username, ipmi_password = item[1].split(":")
                    ipmi_host_dict = {
                        'address': address,
                        'username': ipmi_username,
                        'password': ipmi_password,
                        'port': item[0],
                    }
                    validate_ipmi_details_queue.put(ipmi_host_dict)

            validate_ipmi_details_queue.qsize()

            self.call_gevent_func(validate_ipmi_details_queue,
                                  result_queue,
                                  self.ipmi_check_gevent)
            while result_queue.qsize() != 0:
                result = result_queue.get()
                if result['valid']:
                    valid_ipmi_details.append(result)
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            self._logger.error(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'discover_log': DiscoveryLog.instance().dump()
            }

        return valid_ipmi_details

    def register_nodes(self, job_ctx, ipmi_nodes_detail):
        try:
            job_input = FilterModule._validate_job_ctx(job_ctx)
            ironic_auth_args = job_input.get('ironic')
            cluster_id = job_ctx.get('contrail_cluster_id')
            cluster_token = job_ctx.get('auth_token')
            cc_host = job_input.get('contrail_command_host')
            cc_username = job_input.get('cc_username')
            cc_password = job_input.get('cc_password')
            ironic_node_object = ImportIronicNodes(auth_args=ironic_auth_args,
                                                   cluster_id=cluster_id,
                                                   cluster_token=cluster_token,
                                                   cc_username=cc_username,
                                                   cc_password=cc_password,
                                                   cc_host=cc_host)

            registered_nodes = ironic_node_object.register_nodes(
                ipmi_nodes_detail)
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            self._logger.error(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'discover_log': DiscoveryLog.instance().dump(),
                'nodes': ""
            }

        return {
            'status': 'success',
            'discover_log': DiscoveryLog.instance().dump(),
            'nodes': registered_nodes
        }

    def trigger_introspect(self, job_ctx, registered_nodes):
        try:
            job_input = FilterModule._validate_job_ctx(job_ctx)
            ironic_auth_args = job_input.get('ironic')
            cluster_id = job_ctx.get('contrail_cluster_id')
            cluster_token = job_ctx.get('auth_token')
            cc_host = job_input.get('contrail_command_host')
            cc_username = job_input.get('cc_username')
            cc_password = job_input.get('cc_password')
            ironic_node_object = ImportIronicNodes(auth_args=ironic_auth_args,
                                              cluster_id=cluster_id,
                                              cluster_token=cluster_token,
                                              cc_username=cc_username,
                                              cc_password=cc_password,
                                              cc_host=cc_host)
            introspected_nodes = ironic_node_object.trigger_introspection(
                registered_nodes)
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            self._logger.error(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'discover_log': DiscoveryLog.instance().dump()
            }

        return {
            'status': 'success',
            'discover_log': DiscoveryLog.instance().dump(),
            'nodes': introspected_nodes
        }

    def import_ironic_nodes(self, job_ctx, added_nodes_list):
        """
                :param job_ctx: Dictionary
                    example:
                    {
                        "auth_token": "EB9ABC546F98",
                        "job_input": {
                            "ipmi": {
                                "ipmi_subnet_list": [
                                    "30.1.1.1/24"
                                ],
                                "ipmi_cred_list": [
                                   "admin:admin",
                                   "admin:password"
                                ],
                                "ipmi_port_ranges": [
                                    {
                                        "port_range_start": 623,
                                        "port_range_end": 623
                                    }
                                ]
                            },
                            "ironic": {
                                "auth_url": "",
                                "username": "",
                                "password": ""
                            },
                            "contrail_command_host": ""
                        }
                    }
                :return: Dictionary
                    if success, returns
                    [
                        'status': 'success',
                        'success_nodes': <list: successful introspected nodes>,
                        'failed_nodes': <list: failed introspected nodes>,
                        'discover_log': <list: discover_log>
                    ]
                    if failure, returns
                    {
                        'status': 'failure',
                        'error_msg': <string: error message>,
                        'discover_log': <string: discover_log>
                    }
                    """
        try:
            job_input = FilterModule._validate_job_ctx(job_ctx)
            ironic_auth_args = job_input.get('ironic')
            cluster_id = job_ctx.get('contrail_cluster_id')
            cluster_token = job_ctx.get('auth_token')
            cc_host = job_input.get('contrail_command_host')
            cc_username = job_input.get('cc_username')
            cc_password = job_input.get('cc_password')

            ironic_object = ImportIronicNodes(auth_args=ironic_auth_args,
                                              cluster_id=cluster_id,
                                              cluster_token=cluster_token,
                                              cc_host=cc_host,
                                              cc_username=cc_username,
                                              cc_password=cc_password,
                                              added_nodes_list=added_nodes_list)
            ironic_object.read_nodes_from_db()
            success_nodes = []
            failed_nodes = []
            introspection_flag = ironic_auth_args.get('introspection_flag',True)
            if introspection_flag:
                print("CHECKING INTROSPECTION")
                success_nodes, failed_nodes = \
                    ironic_object.check_introspection()
            else:
                print("IMPORTING WITHOUT INTROSPECT")
                for node in ironic_object.node_info_list:
                    ironic_object.create_cc_node(node)
        except Exception as e:
            errmsg = "Unexpected error: %s\n%s" % (
                str(e), traceback.format_exc()
            )
            self._logger.error(errmsg)
            return {
                'status': 'failure',
                'error_msg': errmsg,
                'discover_log': DiscoveryLog.instance().dump()
            }

        return {
            'status': 'success',
            'success_nodes': success_nodes,
            'failed_nodes': failed_nodes,
            'discover_log': DiscoveryLog.instance().dump()
        }
