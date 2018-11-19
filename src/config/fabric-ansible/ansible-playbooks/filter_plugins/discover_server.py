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
monkey.patch_all()


class FilterModule(object):
    @staticmethod
    def _init_logging():
        logger = logging.getLogger('ServerDiscoveryFilter')
        console_handler = logging.StreamHandler()
        console_handler.setLevel(logging.INFO)

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
            'discover_server': self.discover_server,
            'expand_subnets': self.expand_subnets,
            'ping_sweep': self.ping_sweep,
            'ipmi_auth_check': self.ipmi_auth_check,
            'check_nodes_with_cc': self.check_nodes_with_cc,
            'register_nodes': self.register_nodes,
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
        cc_token = self.get_cc_token("10.87.69.79", "admin", "contrail123")
        print cc_token
        request_headers = { 'X-Auth-Token': cc_token,
                            'Content-Type': 'application/json',
                          }

        cc_url = "https://10.87.69.79:9091/nodes?detail=true"
        cc_resp = requests.get(cc_url, headers = request_headers, verify = False)
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

    def register_nodes(self, node_ipmi_details, ironic_node_details):
        registered_nodes_list = []
        kwargs = {
            'os_username': ironic_node_details['username'],
            'os_password': ironic_node_details['password'],
            'os_auth_url' : ironic_node_details['auth_url'],
            'os_project_name': ironic_node_details['project_name'],
            'os_user_domain_name': ironic_node_details['user_domain_name'],
            'os_project_domain_name': ironic_node_details['project_domain_name'],
            'os_ironic_api_version': '1.31'
        }

        try:
            ironic_client = client.get_client(1, **kwargs)
        except Exception as e:
             print "ERROR:  ", e
             return registered_nodes_list

        for node in node_ipmi_details:
            ironic_node = {}
            ironic_node['driver'] = 'pxe_ipmitool'
            ironic_node['driver_info'] = {}
            ironic_node['driver_info']['ipmi_address'] = node['host']
            ironic_node['driver_info']['ipmi_port'] = node['port']
            ironic_node['driver_info']['ipmi_username'] = node['username']
            ironic_node['driver_info']['ipmi_password'] = node['password']
            resp = ironic_client.node.create(**ironic_node)
            print resp.uuid
            node['uuid'] = resp.uuid
            registered_nodes_list.append(node)

        #current_nodes = ironic_client.node.list()
        #pprint.pprint(current_nodes)


        return registered_nodes_list

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
        print (ipmi_subnets)
        ipmi_addresses = []
        for subnet in ipmi_subnets:
            print subnet
            for ipaddr in netaddr.IPNetwork(subnet):
               ipmi_addresses.append(str(ipaddr))
        print (ipmi_addresses)
        return list(set(ipmi_addresses))


    def discover_server(self,  ipmi_address_list, ipmi_credentials, 
                        discovery_host_details, discover_module='ironic', 
                        ipmi_port_range = ['623']):
        self._logger.info("Starting Server Discovery")
        print (discovery_host_details)
        print (ipmi_address_list)
        print (ipmi_port_range)
        print (ipmi_credentials)

        return discovery_host_details

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
        for ports in ipmi_ports:
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

def _parse_args():
    parser = argparse.ArgumentParser(description='fabric filters tests')
    parser.add_argument('-d', '--discover-server',
                        action='store_true',
                        help='discover role for physical routers')
    parser.add_argument('-p', '--ping-sweep',
                        action='store_true',
                        help='discover role for physical routers')
    return parser.parse_args()
# end _parse_args


if __name__ == '__main__':

    results = None
    fabric_filter = FilterModule()
    parser = _parse_args()
    if parser.ping_sweep:
        thirdparty_node_details = { 'plugin': 'ironic',
                                    'auth_url': 'http://10.87.82.34:5000/v3',
                                    'username': 'admin',
                                    'password': '905bee05b9f8dda6c3f5eee0aed8056e214b77f6',
                                    'user_domain_name': 'Default',
                                    'project_domain_name': 'Default',
                                    'project_name': 'admin',
                                  }
        #results_expand_subnets = fabric_filter.expand_subnets( ['10.87.82.5', '10.87.82.6', '10.87.82.7', '10.87.82.10', '10.87.82.11', '10.87.82.0/24',"10.84.22.30", "10.])
        #results_expand_subnets = fabric_filter.expand_subnets( ['10.87.82.5', '10.87.82.6', '10.87.82.7', '10.87.82.10', '10.87.82.11', '10.84.22.30', '10.87.69.53'])
        #result_ping_sweep = fabric_filter.ping_sweep(results_expand_subnets)
        #result_ping_sweep = ['10.87.82.5', '10.87.82.6', '10.87.82.7']
        #result_ipmi_auth_check = fabric_filter.ipmi_auth_check(result_ping_sweep, ["admin:admin", "ADMIN:ADMIN", "admin:password"], ["600", "623", "1020-1025", "620-630", "1021-1024", "100-90", "abd", "abc-333", "6200-6210", "admin:password"])
        result_ipmi_auth_check = [{'host': '10.87.82.7', 'password': 'ADMIN', 'port': 623, 'username': 'ADMIN', 'valid': True}, {'host': '10.87.69.53', 'password': 'password', 'port': 6206, 'username': 'admin', 'valid': True}, {'host': '10.87.69.53', 'password': 'password', 'port': 6205, 'username': 'admin', 'valid': True}, {'host': '10.87.69.53', 'password': 'password', 'port': 6204, 'username': 'admin', 'valid': True}, {'host': '10.87.69.53', 'password': 'password', 'port': 6203, 'username': 'admin', 'valid': True}, {'host': '10.87.69.53', 'password': 'password', 'port': 6202, 'username': 'admin', 'valid': True}, {'host': '10.87.69.53', 'password': 'password', 'port': 6200, 'username': 'admin', 'valid': True}, {'host': '10.87.82.5', 'password': 'ADMIN', 'port': 623, 'username': 'ADMIN', 'valid': True}, {'host': '10.87.82.6', 'password': 'ADMIN', 'port': 623, 'username': 'ADMIN', 'valid': True}]
        results_check_nodes_with_cc = fabric_filter.check_nodes_with_cc(result_ipmi_auth_check, {})
        #new_node_uuids = fabric_filter.register_nodes(
                           #results_check_nodes_with_cc,
                           #thirdparty_node_details)
        #print (result_ipmi_auth_check)
        pprint.pprint ( results_check_nodes_with_cc)
        


