#!/usr/bin/python

import pprint
import argparse
import logging
import sys
import traceback
import netaddr
import subprocess
import itertools
from ironicclient import client
import requests
import json
import gevent
from gevent import Greenlet, monkey, pool, queue
#monkey.patch_all()
from plugin_ironic import *
from contrail_command import *

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

    def get_keystone_token(self,keystone_url, username, password, user_domain='default', project_name='admin', verify=False):
        print "GET TOKEN : openstack-url ", keystone_url
        print "GET TOKEN : username ", username
        print "GET TOKEN : password ", password
        print "GET TOKEN : user domain", user_domain
    
        print "GET TOKEN : ", keystone_url
        request_headers = { 'Content-Type': 'application/json',
                          }
        #pdb.set_trace()
        ks_data = { "auth": {
                       "identity": {
                         "methods": ["password"],
                         "password": {
                           "user": {
                             "name": username,
                             "domain": { "id": user_domain },
                             "password": password
                           }
                         }
                       },
                       "scope": {
                         "project": {
                           "name": project_name,
                           "domain": { "id": "default" }
                         }
                       }
                     }
                   }
    
    
        response = requests.post(keystone_url, headers= request_headers, data = json.dumps(ks_data), verify=verify)
        print "GET TOKEN : response ", response
        print "GET TOKEN : headers ", pprint.pprint(response.headers)
        print "GET TOKEN : body ", pprint.pprint(response.content)
        token = ""
        if response.status_code == 201:
            token = response.headers['X-Subject-Token']
            print "GET TOKEN : ", pprint.pprint(token)
        else:
             print  "GET TOKEN : status_code " , response.status_code
    
        return token
    
    def get_cc_token(self,cc_ip, username, password):
        keystone_url = "https://" + cc_ip + ":9091/keystone/v3/auth/tokens?nocatalog"
        print "GET CC TOKEN : openstack-url ", keystone_url
        token = self.get_keystone_token(keystone_url, username, password, verify=False)

        return token

    def check_nodes_with_cc(self, ipmi_nodes_detail, cc_node_details):
        # TODO: returning all nodes as of now, it must be the diff of nodes 
        # from CC and ipmi_nodes_details
        final_ipmi_detail = []
        print cc_node_details
        print type(cc_node_details)
        cc_node = CreateCCResource(cc_node_details)
        print  cc_node.auth_token
        request_headers = { 'X-Auth-Token': cc_node.auth_token,
                            'Content-Type': 'application/json',
                          }

        cc_url = "https://10.87.69.79:9091/nodes?detail=true"
        cc_resp = requests.get(cc_url, headers = request_headers, verify = False)
        #print cc_resp
        cc_nodes = json.loads( cc_resp.content)
        #pprint.pprint(cc_nodes)
        cc_node_data = []
        for node in cc_nodes['nodes']:
            #pprint.pprint(node)
            cc_node = {}
            if 'bms_info' in node and node['bms_info']  \
                and 'driver_info' in node['bms_info'] \
                and node['bms_info']['driver_info'] \
                and 'ipmi_address' in node['bms_info']['driver_info']  \
                and node['bms_info']['driver_info'] ['ipmi_address']:
               
                print node['bms_info']['driver_info']['ipmi_address']
                cc_node['ipmi_address'] = node['bms_info']['driver_info']['ipmi_address']
                if 'ipmi_port' in node['bms_info']['driver_info'] \
                    and node['bms_info']['driver_info']['ipmi_port']:
  
                    print node['bms_info']['driver_info']['ipmi_port']
                    cc_node['ipmi_port'] = node['bms_info']['driver_info']['opmi_port']
                else: 
                    cc_node['ipmi_port'] = '623'

                cc_node_data.append(cc_node)

        pprint.pprint(cc_node_data)
        for ipmi_node in ipmi_nodes_detail:
            node_found_in_cc = False
            for cc_node in cc_node_data:
                #print "NODE CC", cc_node, ipmi_node
                if ipmi_node['host'] == cc_node['ipmi_address'] \
                    and int(ipmi_node['port']) == int(cc_node['ipmi_port']):
                    # node is already there in contrail-command, skip for 
                    # introspection
                    print "NODE FOUND IN CC", cc_node
                    node_found_in_cc = True
                    break

            if node_found_in_cc == False :
                final_ipmi_detail.append(ipmi_node)
        
        return final_ipmi_detail


    def ping_check(self, retry_queue, result_queue ):
        if retry_queue.empty() :
          return True

        host_params = retry_queue.get_nowait()
        host = host_params['host']
        print host, len(host)
        try:
            ping_output = subprocess.Popen(
                ['ping', '-W', '1', '-c', '2', host], stdout=subprocess.PIPE)
            gevent.sleep(2)
            ping_output.wait()
            result_queue.put({'host':host,
                              'result': ping_output.returncode == 0})
            return ping_output.returncode == 0
        except Exception as ex:
            print host, "ERROR: SUBPROCESS.POPEN failed with error {}"
            return False
    # end _ping_check

    def ping_sweep(self,ipaddr_list):
      input_queue = queue.Queue()
      result_queue = queue.Queue()

      print ipaddr_list
      for address in ipaddr_list:
        print address
        host_map_dict = {
          'host': str(address)
        }
        input_queue.put(host_map_dict)

      self.call_gevent_func(input_queue, result_queue, self.ping_check)

      print ("RESULTS" , result_queue.qsize())
      ping_sweep_success_list = []
      while result_queue.qsize() != 0:
        result = result_queue.get()
        if result['result']:
          print "HOST PING PASSED", result['host']
          ping_sweep_success_list.append(result['host'])
        #print result

      print "PING SWEEP DONE"
      return ping_sweep_success_list

    def expand_subnets(self, ipmi_subnets):
        self._logger.info("Starting Server Discovery2")
        print (ipmi_subnets)
        ipmi_addresses = []
        #my_auth_args = {
        #'auth_url': 'http://10.87.82.34:5000/v3',
        #'username': "admin",
        #'password': "905bee05b9f8dda6c3f5eee0aed8056e214b77f6",
        #'user_domain_name': 'default',
        #'project_domain_name': 'default',
        #'project_name': 'admin',
        #'aaa_mode': None
        #}
        #cc_auth = {
        #'auth_host': "10.87.69.79",
        #'username': "admin",
        #'password': "contrail123"
        #}
        #introspection_flag = False 
        #ironic_node = ImportIronicNodes(auth_args=my_auth_args,
        #cc_auth_args=cc_auth )
        #print "CC-NODE:" , ironic_node.cc_node.auth_token
        self._logger.info("Starting Server Discovery3")

        for subnet in ipmi_subnets:
            print subnet
            for ipaddr in netaddr.IPNetwork(subnet):
               ipmi_addresses.append(str(ipaddr))
        print (ipmi_addresses)
        return list(set(ipmi_addresses))


    def ipmi_check_gevent(self, input_queue, result_queue):
        if input_queue.empty():
            return True
        ipmi_host = input_queue.get_nowait()
        status = self.ipmi_check_power_status(ipmi_host['host'], 
                                              ipmi_host['username'],
                                              ipmi_host['password'],
                                              ipmi_host['port'])
        ipmi_host['valid'] = status
        result_queue.put(ipmi_host)
        return

    def ipmi_check_power_status(self, address, ipmi_username, ipmi_password,
                               ipmi_port):
        print address , ipmi_port , ipmi_username, ipmi_password
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
            print address, "IPMI_OUTPUT_CODE", ipmi_output.returncode
            if ipmi_output.returncode == 0:
                print address, "IPMI CREDS ARE GOOD"
                return True
            else:
                print address, "IPMI CREDS ARE BAD"
                return False
        except Exception as ex:
            print address, "ERROR: SUBPROCESS.POPEN failed {} ".format(str(ex))
            return False

    def call_gevent_func(self, input_queue, result_queue, func_call):
        print "INPUT-QUEUE: ", input_queue.qsize()
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
            print "QUEUE EMPTY EXIT"
            break

        print ("RESULTS-QUEUE-SIZE" , result_queue.qsize())

    def ipmi_auth_check(self, ipaddress_list, ipmi_credentials, 
                        ipmi_ports =['623']):
        print ipmi_credentials, len(ipmi_credentials)
        print ipmi_ports
        valid_ipmi_details = []
        #expand ipmi_ports to port list
        port_list = []
        for ports in map(str, ipmi_ports):
            print ports
            if '-' in ports:
                # we need to expand it.
                port_range = ports.split('-')
                if not (port_range[0].isdigit() and port_range[1].isdigit()):
                    print "BAD RANGE", ports
                    continue
                print range(int(port_range[0]), int(port_range[1]))
                #validate correct range
                if int(port_range[1]) > int(port_range[0]):
                    # we are good
                    port_list.extend(range(int(port_range[0]),
                                           int(port_range[1])))
                else:
                    #bad range, ignore
                    print "IGNORING, BAD RANGE", ports
            elif ports.isdigit():
                # add it to list
                print ports
                port_list.append(int(ports))
            else:
                # ignore it, report it
                print "IGNORING " , ports

        print port_list, len(port_list)
        final_port_list = list(set(port_list))
        print final_port_list, len(final_port_list)

        validate_ipmi_details_queue = queue.Queue()
        result_queue = queue.Queue()
        for address in ipaddress_list:
            for item in itertools.product(final_port_list, ipmi_credentials):
                print item
                print item[0]
                ipmi_username, ipmi_password = item[1].split(":")
                ipmi_host_dict = {
                    'host'     : address,
                    'username' : ipmi_username,
                    'password' : ipmi_password,
                    'port'     : item[0],
                }
                validate_ipmi_details_queue.put(ipmi_host_dict)

        self.call_gevent_func(validate_ipmi_details_queue, 
                              result_queue,
                              self.ipmi_check_gevent)
        while result_queue.qsize() != 0:
            result = result_queue.get()
            #print result
            if result['valid']:
                print ("HOST IPMI PASSED", result['host'], result['port'],
                       result['username'],  result['password'])
                valid_ipmi_details.append(result)

        return valid_ipmi_details

    def register_nodes(self, ipmi_nodes_detail, ironic_node,
                       contrail_command_node):
        ironic_node_object = ImportIronicNodes(auth_args = ironic_node,
                                 cc_auth_args = contrail_command_node)
        registered_nodes = ironic_node_object.register_nodes(ipmi_nodes_detail)
        return registered_nodes

    def trigger_introspect(self, registered_nodes, ironic_node,
                       contrail_command_node):
        ironic_node_object = ImportIronicNodes(auth_args = ironic_node,
                                 cc_auth_args = contrail_command_node)
        introspected_nodes = ironic_node_object.trigger_introspection(registered_nodes)
        return introspected_nodes

    def import_ironic_nodes(self, added_nodes_list, ironic_node, 
                            contrail_command_node, introspection_flag):

        pprint.pprint(added_nodes_list)
        ironic_object = ImportIronicNodes(auth_args=ironic_node,
                                          cc_auth_args=contrail_command_node,
                                          added_nodes_list=added_nodes_list)
        ironic_object.read_nodes_from_db()
        if introspection_flag:
            print "CHECKING INTROSPECTION"
            ironic_object.check_introspection()
        else:
            print "IMPORTING WITHOUT INTROSPECT"
            for node in ironic_object.node_info_list:
                ironic_object.create_cc_node(node)

