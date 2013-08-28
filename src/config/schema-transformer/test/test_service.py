#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import gevent
import os
import sys
import socket
import errno
import uuid
import subprocess

import unittest
import re
import json
import copy
from lxml import etree
#import inspect
#from mock import patch
from flexmock import flexmock, Mock
from collections import OrderedDict
from cfgm_common.discovery import DiscoveryService

import pycassa

import cfgm_common.ifmap
from vnc_api.vnc_api import *
from cfgm_common import exceptions as vnc_exceptions
sys.path.insert(0, os.path.realpath('lib/python2.7/site-packages/vnc_cfg_api_server'))
import vnc_cfg_api_server
sys.path.insert(1, os.path.realpath('lib/python2.7/site-packages/svc_monitor'))
import svc_monitor
from schema_transformer import to_bgp
from cfgm_common.test_utils import *

from cfgm_common.ifmap import client as ifmap_client
from cfgm_common.ifmap import response as ifmap_response
import novaclient
import inspect

def lineno():
    """Returns the current line number in our program."""
    return inspect.currentframe().f_back.f_lineno
#end lineno

vnc_lib = None
ifmap_server = None
def stub(*args, **kwargs):
    pass

def launch_api_server(listen_ip, listen_port):
    args_str = ""
    args_str = args_str + "--listen_ip_addr %s " %(listen_ip)
    args_str = args_str + "--listen_port %s " %(listen_port)
    args_str = args_str + "--ifmap_username api-server "
    args_str = args_str + "--ifmap_password api-server "
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160"

    vnc_cfg_api_server.main(args_str)
#end launch_api_server

def launch_svc_monitor(api_server_ip, api_server_port):
    args_str = ""
    args_str = args_str + "--api_server_ip %s " %(api_server_ip)
    args_str = args_str + "--api_server_port %s " %(api_server_port)
    args_str = args_str + "--ifmap_username api-server "
    args_str = args_str + "--ifmap_password api-server "
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160"

    svc_monitor.main(args_str)
#end launch_svc_monitor

def launch_ifmap_server(ip, port):
    global ifmap_server
    IFMAP_SVR_LOC='/home/sachin/ifmap-server'
    running=False
    try:
        s = socket.create_connection((ip, port))
        s.close()
        running = True
    except Exception as err:
        pass
    if running:
        print "ifmap server already running. Close it and retry"
        os._exit(0)
    logf_out = open('ifmap-server.out', 'w')
    logf_err = open('ifmap-server.err', 'w')
    ifmap_server = subprocess.Popen(['java', '-jar', 'build/irond.jar'],
               cwd=IFMAP_SVR_LOC, stdout = logf_out, stderr = logf_err)
#end launch_ifmap_server

def launch_schema_transformer(api_server_ip, api_server_port):
    args_str = ""
    args_str = args_str + "--api_server_ip %s " %(api_server_ip)
    args_str = args_str + "--api_server_port %s " %(api_server_port)
    args_str = args_str + "--cassandra_server_list 0.0.0.0:9160"
    to_bgp.main(args_str) 
#end launch_schema_transformer

class DiscoveryServiceMock(object):
    def __init__(self, *args, **kwargs):
        self._count = 0
        self._values = {}
    #end __init__
    def alloc_from(self, path, max_id):
        self._count = self._count + 1
        return self._count
    #end alloc_from
    def alloc_from_str(self, path, value = ''):
        self._count = self._count + 1
        zk_val = "%(#)010d"%{'#':self._count}
        self._values[path + zk_val] = value
        return zk_val
    #end alloc_from_str
    def delete(self, path):
        del self._values[path]
    #end delete
    def read(self, path):
        try:
            return self._values[path]
        except Exception as err:
            raise pycassa.NotFoundException
    #end read
#end Class DiscoveryServiceMock

def setup_flexmock():
    FakeNovaClient.vnc_lib = vnc_lib
    flexmock(novaclient.client, Client = FakeNovaClient.initialize)
    flexmock(ifmap_client.client, __init__ = FakeIfmapClient.initialize,
            call = FakeIfmapClient.call)
    flexmock(ifmap_response.newSessionResult, get_publisher_id = stub)

    flexmock(pycassa.system_manager.Connection, __init__ = stub)
    flexmock(pycassa.system_manager.SystemManager, create_keyspace = stub,
                                                   create_column_family = stub)
    flexmock(pycassa.ConnectionPool, __init__ = stub)
    flexmock(pycassa.ColumnFamily, __new__ = FakeCF)

    flexmock(DiscoveryService, __new__ = DiscoveryServiceMock)

#end setup_flexmock

def create_virtual_network(vn_name, vn_subnet):
    vn_obj = VirtualNetwork(name=vn_name)
    ipam_fq_name = ['default-domain', 'default-project', 'default-network-ipam']
    ipam_obj = vnc_lib.network_ipam_read(fq_name=ipam_fq_name)
    cidr = vn_subnet.split('/')
    pfx = cidr[0]
    pfx_len = int(cidr[1])
    subnet_info = IpamSubnetType(subnet=SubnetType(pfx, pfx_len))
    subnet_data = VnSubnetsType([subnet_info])                                                      
    vn_obj.add_network_ipam(ipam_obj, subnet_data)    
    vnc_lib.virtual_network_create(vn_obj)

    return vn_obj
#end create_virtual_network

def create_network_policy(vn1, vn2, service_list=None, service_mode=None):
    addr1 = AddressType(virtual_network=vn1.get_fq_name_str())
    addr2 = AddressType(virtual_network=vn2.get_fq_name_str())
    port = PortType(-1, 0)
    action_list = None
    action = "pass"
    if service_list:
        service_name_list = []
        for service in service_list:
            sti = [ServiceTemplateInterfaceType('left'), ServiceTemplateInterfaceType('right')]
            st_prop = ServiceTemplateType(image_name='junk', service_mode=service_mode, interface_type=sti)
            service_template = ServiceTemplate(name=service+'template',
                                               service_template_properties=st_prop)
            vnc_lib.service_template_create(service_template)
            scale_out = ServiceScaleOutType()
            if service_mode == 'in-network':
                si_props = ServiceInstanceType(auto_policy=True, left_virtual_network=vn1.name,
                                          right_virtual_network=vn2.name, scale_out=scale_out)
            else:
                si_props = ServiceInstanceType(scale_out=scale_out)
            service_instance = ServiceInstance(name=service, service_instance_properties=si_props)
            vnc_lib.service_instance_create(service_instance)
            service_instance.add_service_template(service_template)
            vnc_lib.service_instance_update(service_instance)
            service_name_list.append(service_instance.get_fq_name_str())

        action_list = ActionListType(apply_service=service_name_list)
        action = None
    prule = PolicyRuleType(direction="<>", simple_action=action,
                       protocol="any", src_addresses=[addr1],
                       dst_addresses=[addr2], src_ports=[port],
                       dst_ports=[port], action_list=action_list)
    pentry = PolicyEntriesType([prule])
    np = NetworkPolicy("policy1", network_policy_entries = pentry)
    vnc_lib.network_policy_create(np)
    return np
#end create_network_policy

def delete_network_policy(policy):
    action_list = policy.network_policy_entries.policy_rule[0].action_list
    if action_list:
        for service in action_list.apply_service or []:
            si = vnc_lib.service_instance_read(fq_name_str=service)
            st_ref = si.get_service_template_refs()
            st = vnc_lib.service_template_read(id=st_ref[0]['uuid'])
            vnc_lib.service_instance_delete(id=si.uuid)
            vnc_lib.service_template_delete(id=st.uuid)
        #end for service
    #if action_list
    vnc_lib.network_policy_delete(id=policy.uuid)
#end delete_network_policy(policy)

class TestPolicy(object):
#class TestPolicy(unittest.TestCase):
    def setUp(self):
        setup_flexmock()
    #end setUp

    def tearDown(self):
        pass
    #end tearDown

    @classmethod
    def setUpClass(cls):
       pass 
    #end setUpClass
   
    @classmethod 
    def tearDownClass(cls):    
        #ifmap_server.kill()
        pass
    #end tearDownClass

    def test_basic_policy(self):
        vn1_obj = VirtualNetwork('vn1')
        vn2_obj = VirtualNetwork('vn2')

        np = create_network_policy(vn1_obj, vn2_obj)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        vn1_uuid = vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = vnc_lib.virtual_network_create(vn2_obj)
        while True:
            gevent.sleep(2)
            try:
               ri = vnc_lib.routing_instance_read(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'])
            except NoIdError:
                print "retrying ... ", lineno()
                continue

            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(ri_refs[0]['to'], [u'default-domain', u'default-project', u'vn2', u'vn2'])
                break
            print "retrying ... ", lineno()
        #end while True
        while True:
            try:
                ri = vnc_lib.routing_instance_read(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])
            except NoIdError:
                gevent.sleep(2)
                print "retrying ... ", lineno()
                continue

            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(ri_refs[0]['to'], [u'default-domain', u'default-project', u'vn1', u'vn1'])
                break
            print "retrying ... ", lineno()
            gevent.sleep(2)
        #end while True
        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        vnc_lib.virtual_network_update(vn1_obj)
        vnc_lib.virtual_network_update(vn2_obj)
        while True:
            ri = vnc_lib.routing_instance_read(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                gevent.sleep(2)
            else:
                break
            print "retrying ... ", lineno()
        #end while True
        delete_network_policy(np)
        vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        while True:
            try:
                vnc_lib.virtual_network_read(id=vn1_obj.uuid)
                print "retrying ... ", lineno()
                gevent.sleep(2)
                continue
            except NoIdError:
                print 'vn1 deleted'
            try:
                vnc_lib.routing_instance_read(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])
                print "retrying ... ", lineno()
                gevent.sleep(2)
                continue
            except NoIdError:
                print 'ri2 deleted'
            break
    #end test_basic_policy

    def test_service_policy(self):

        # create  vn1
        vn1_obj = VirtualNetwork('vn1')
        ipam_obj = NetworkIpam('ipam1')
        vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType([IpamSubnetType(SubnetType("10.0.0.1", 24))]))
        vnc_lib.virtual_network_create(vn1_obj)

        # create vn2
        vn2_obj = VirtualNetwork('vn2')
        ipam_obj = NetworkIpam('ipam2')
        vnc_lib.network_ipam_create(ipam_obj)
        vn2_obj.add_network_ipam(ipam_obj, VnSubnetsType([IpamSubnetType(SubnetType("20.0.0.1", 24))]))
        vnc_lib.virtual_network_create(vn2_obj)

        np = create_network_policy(vn1_obj, vn2_obj, ["s1"])
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        vnc_lib.virtual_network_update(vn1_obj)
        vnc_lib.virtual_network_update(vn2_obj)
        while True:
            gevent.sleep(2)
            try:
                ri = vnc_lib.routing_instance_read(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'])
            except NoIdError:
                print "retrying ... ", lineno()
                continue
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(ri_refs[0]['to'], [u'default-domain', u'default-project', u'vn1', u'service-default-domain_default-project_vn1-default-domain_default-project_vn2-default-domain_default-project_s1'])
                break
            print "retrying ... ", lineno()
        #end while True

        while True:
            try:
                ri = vnc_lib.routing_instance_read(fq_name=[u'default-domain', u'default-project', u'vn2', u'service-default-domain_default-project_vn2-default-domain_default-project_vn1-default-domain_default-project_s1'])
            except NoIdError:
                gevent.sleep(2)
                print "retrying ... ", lineno()
                continue
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(ri_refs[0]['to'], [u'default-domain', u'default-project', u'vn2', u'vn2'])
                sci  = ri.get_service_chain_information()
                if sci is None:
                    print "retrying ... ", lineno()
                    gevent.sleep(2)
                    continue
                self.assertEqual(sci.prefix[0], '10.0.0.1/24')
                break
            print "retrying ... ", lineno()
            gevent.sleep(2)
        #end while True

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        vnc_lib.virtual_network_update(vn1_obj)
        vnc_lib.virtual_network_update(vn2_obj)
        while True:
            gevent.sleep(2)
            try:
                ri = vnc_lib.routing_instance_read(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'])
            except NoIdError:
                print "retrying ... ", lineno()
                continue
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs is None:
                break
            print "retrying ... ", lineno()
        #end while True
        delete_network_policy(np)
        vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        while True:
            try:
                vnc_lib.virtual_network_read(id=vn1_obj.uuid)
                gevent.sleep(2)
                print "retrying ... ", lineno()
                continue
            except NoIdError:
                print 'vn1 deleted'
            try:
                vnc_lib.routing_instance_read(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])
                print "retrying ... ", lineno()
                gevent.sleep(2)
                continue
            except NoIdError:
                print 'ri2 deleted'
            break

    #end test_service_policy
#end class TestPolicy

#class TestRouteTable(object):
class TestRouteTable(unittest.TestCase):
    def setUp(self):
        setup_flexmock()
    #end setUp

    def tearDown(self):
        pass
    #end tearDown

    @classmethod
    def setUpClass(cls):
        pass 
    #end setUpClass
   
    @classmethod 
    def tearDownClass(cls):    
        #ifmap_server.kill()
        pass
    #end tearDownClass

    def test_add_delete_route(self):
        lvn = create_virtual_network("lvn", "10.0.0.0/24")
        rvn = create_virtual_network("rvn", "20.0.0.0/24")
        np = create_network_policy(lvn, rvn, ["s1"], "in-network")

        vn = create_virtual_network("vn100", "1.0.0.0/24")
        rt = RouteTable("rt1")
        vnc_lib.route_table_create(rt)
        vn.add_route_table(rt)
        vnc_lib.virtual_network_update(vn)
        routes = RouteTableType()
        route = RouteType(prefix = "0.0.0.0/0", next_hop="default-domain:default-project:s1")
        routes.add_route(route)
        rt.set_routes(routes)
        vnc_lib.route_table_update(rt)

        while 1:
            gevent.sleep(2)
            lvn = vnc_lib.virtual_network_read(id=lvn.uuid)
            try:
                lri = vnc_lib.routing_instance_read(fq_name=['default-domain', 'default-project', 'lvn', 'service-default-domain_default-project_lvn-default-domain_default-project_rvn-default-domain_default-project_s1'])
                sr = lri.get_static_route_entries()
                if sr is None:
                    print "retrying ... ", lineno()
                    continue
                route = sr.route[0]
                self.assertEqual(route.prefix, "0.0.0.0/0")
                self.assertEqual(route.next_hop, "10.0.0.253")
            except NoIdError:
                print "retrying ... ", lineno()
                continue

            try:
                ri100 = vnc_lib.routing_instance_read(fq_name=['default-domain', 'default-project', 'vn100', 'vn100'])
                rt100 = ri100.get_route_target_refs()[0]['to']
                ri = vnc_lib.routing_instance_read(fq_name=['default-domain', 'default-project', 'lvn', 'lvn'])
                rt_refs = ri.get_route_target_refs()
                found = False
                for rt_ref in ri.get_route_target_refs() or []:
                    if rt100 == rt_ref['to']:
                        found = True
                        break
                self.assertEqual(found, True) 
            except NoIdError:
                print "retrying ... ", lineno()
                continue
            break
        #end while

        routes.set_route([])
        rt.set_routes(route)
        vnc_lib.route_table_update(rt)

        while 1:
            lri = vnc_lib.routing_instance_read(fq_name=['default-domain', 'default-project', 'lvn', 'service-default-domain_default-project_lvn-default-domain_default-project_rvn-default-domain_default-project_s1'])
            sr = lri.get_static_route_entries()
            if sr and sr.route:
                gevent.sleep(2)
                print "retrying ... ", lineno()
                continue
            ri = vnc_lib.routing_instance_read(fq_name=['default-domain', 'default-project', 'lvn', 'lvn'])
            rt_refs = ri.get_route_target_refs()
            for rt_ref in ri.get_route_target_refs() or []:
                if rt100 == rt_ref['to']:
                    print "retrying ... ", lineno()
                    continue
            break
        #end while
      
        vnc_lib.virtual_network_delete(fq_name=['default-domain', 'default-project', 'vn100'])
        delete_network_policy(np)
        gevent.sleep(2)
        vnc_lib.virtual_network_delete(fq_name=['default-domain', 'default-project', 'lvn'])
        vnc_lib.virtual_network_delete(fq_name=['default-domain', 'default-project', 'rvn'])
    #test_add_delete_route 
#end class TestRouteTable

if __name__ == '__main__':
    global vnc_lib
    setup_flexmock()
    ifmap_server_ip = '127.0.0.1'
    ifmap_server_port = '8443'
    #gevent.spawn(launch_ifmap_server, ifmap_server_ip, ifmap_server_port)
    #block_till_port_listened(ifmap_server_ip, ifmap_server_port)
    api_server_ip = '127.0.0.1'
    api_server_port = get_free_port()
    gevent.spawn(launch_api_server, api_server_ip, api_server_port)
    block_till_port_listened(api_server_ip, api_server_port)
    gevent.spawn(launch_svc_monitor, api_server_ip, api_server_port)
    vnc_lib = VncApi('u', 'p', api_server_host = api_server_ip,
                             api_server_port = api_server_port)
    gevent.spawn(launch_schema_transformer, api_server_ip, api_server_port)
    unittest.main()
