#
# Copyright (c) 2013,2014 Juniper Networks, Inc. All rights reserved.
#
import gevent
import os
import sys
import socket
import errno
import uuid
import logging
import coverage

import cgitb
cgitb.enable(format='text')

import testtools
from testtools.matchers import Equals, MismatchError, Not, Contains
from testtools import content, content_type, ExpectedException
import unittest
import re
import json
import copy
import inspect
import pycassa
import kombu
import requests
import bottle
import netaddr

from vnc_api.vnc_api import *
from vnc_api.common import exceptions as vnc_exceptions
import vnc_api.gen.vnc_api_test_gen
from vnc_api.gen.resource_test import *
import cfgm_common
from netaddr import *

sys.path.append('../common/tests')
from test_utils import *
import test_common
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestIpAlloc(test_case.ApiServerTestCase):

    def test_ip_alloction(self):
        # Create Domain
        domain = Domain('my-v4-v6-domain')
        self._vnc_lib.domain_create(domain)
        print 'Created domain '

        # Create Project
        project = Project('my-v4-v6-proj', domain)
        self._vnc_lib.project_create(project)
        print 'Created Project'

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        print 'Created network ipam'

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-v4-v6-domain', 'my-v4-v6-proj',
                                                        'default-network-ipam'])
        print 'Read network ipam'

        # Create subnets
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        ipam_sn_v6 = IpamSubnetType(subnet=SubnetType('fd14::', 120))

        # Create VN
        vn = VirtualNetwork('my-v4-v6-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4, ipam_sn_v6]))
        self._vnc_lib.virtual_network_create(vn)
        print 'Created Virtual Network object', vn.uuid
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Create v4 Ip object
        ip_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ip_obj1.uuid = ip_obj1.name
        print 'Created Instance IP object 1 ', ip_obj1.uuid

        # Create v6 Ip object
        ip_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v6')
        ip_obj2.uuid = ip_obj2.name
        print 'Created Instance IP object 2', ip_obj2.uuid

        # Create VM
        vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj1.uuid = vm_inst_obj1.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj1)

        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj1, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn)
        ip_obj1.set_virtual_machine_interface(port_obj1)
        ip_obj1.set_virtual_network(net_obj)
        ip_obj2.set_virtual_machine_interface(port_obj1)
        ip_obj2.set_virtual_network(net_obj)
        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)

        print 'Allocating an IP4 address for first VM'
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
        print '  got v4 IP Address for first instance', ip_addr1
        if ip_addr1 != '11.1.1.253':
            print 'Allocation failed, expected v4 IP Address 11.1.1.253'

        print 'Allocating an IP6 address for first VM'
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)
        ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id2)
        ip_addr2 = ip_obj2.get_instance_ip_address()
        print '  got v6 IP Address for first instance', ip_addr2
        if ip_addr2 != 'fd14::fd':
            print 'Allocation failed, expected v6 IP Address fd14::fd'

        # Read gateway ip address 
        print 'Read default gateway ip address' 
        ipam_refs = net_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                print 'Gateway for subnet (%s/%s) is (%s)' %(subnet.subnet.get_ip_prefix(), 
                        subnet.subnet.get_ip_prefix_len(),
                        subnet.get_default_gateway())


        #cleanup
        print 'Cleaning up'
        self._vnc_lib.instance_ip_delete(id=ip_id1)
        self._vnc_lib.instance_ip_delete(id=ip_id2)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_ip_alloction_pools(self):
        # Create Domain
        domain = Domain('my-v4-v6-domain')
        self._vnc_lib.domain_create(domain)
        print 'Created domain '

        # Create Project
        project = Project('my-v4-v6-proj', domain)
        self._vnc_lib.project_create(project)
        print 'Created Project'

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        print 'Created network ipam'

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-v4-v6-domain', 'my-v4-v6-proj',
                                                        'default-network-ipam'])
        print 'Read network ipam'

        # Create subnets
        alloc_pool_list = []
        # Create wrong allocation_pool and try creating network
        # 1. addressformat wrong
        # 2. start or end are not in subnet range
        # different exception should occur in creating Virtual networks
        alloc_pool_list.append(AllocationPoolType(start='str1', end='str2'))
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24),
                                    allocation_pools=alloc_pool_list,
                                    addr_from_start=True)
        alloc_pool_list_v6 = []
        alloc_pool_list_v6.append(AllocationPoolType(start='fd14::30', end='fd14::40'))
        ipam_sn_v6 = IpamSubnetType(subnet=SubnetType('fd14::', 120),
                                    allocation_pools=alloc_pool_list_v6,
                                    addr_from_start=True)

        # Create VN
        vn = VirtualNetwork('my-v4-v6-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4, ipam_sn_v6]))
        try:
            self._vnc_lib.virtual_network_create(vn)
        except Exception as e:
            self.assertThat(e.message,
                            Equals('Invalid allocation Pool start:str1, end:str2 in subnet:11.1.1.0/24'))

        alloc_pool_list[:] = []
        alloc_pool_list.append(AllocationPoolType(start='11.1.1.20', end='11.2.1.25')) 
        ipam_sn_v4.set_allocation_pools(alloc_pool_list)
        try:
            self._vnc_lib.virtual_network_create(vn)
        except Exception as e:
            self.assertThat(e.message,
                            Equals('allocation pool start:11.1.1.20, end:11.2.1.25 out of cidr:11.1.1.0/24'))

        alloc_pool_list[:] = []
        alloc_pool_list.append(AllocationPoolType(start='11.1.1.20', end='11.1.1.25')) 
        ipam_sn_v4.set_allocation_pools(alloc_pool_list)

        gwip = "wrong_gw"
        ipam_sn_v4.set_default_gateway(gwip)
        try:
            self._vnc_lib.virtual_network_create(vn)
        except Exception as e:
            self.assertThat(e.message,
                            Equals('Invalid gateway Ip address:wrong_gw'))
        gwip = None
        ipam_sn_v4.set_default_gateway(gwip)        
        self._vnc_lib.virtual_network_create(vn)
        print 'Created Virtual Network object', vn.uuid
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Create v4 Ip object
        ip_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ip_obj1.uuid = ip_obj1.name
        print 'Created Instance IP object 1 ', ip_obj1.uuid

        # Create v6 Ip object
        ip_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v6')
        ip_obj2.uuid = ip_obj2.name
        print 'Created Instance IP object 2', ip_obj2.uuid

        # Create VM
        vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj1.uuid = vm_inst_obj1.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj1)

        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj1, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn)
        ip_obj1.set_virtual_machine_interface(port_obj1)
        ip_obj1.set_virtual_network(net_obj)
        ip_obj2.set_virtual_machine_interface(port_obj1)
        ip_obj2.set_virtual_network(net_obj)
        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)

        print 'Allocating an IP4 address for first VM'
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
        print 'got v4 IP Address for first instance', ip_addr1
        if ip_addr1 != '11.1.1.20':
            print 'Allocation failed, expected v4 IP Address 11.1.1.20'
        
        print 'Allocating an IP6 address for first VM'
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)
        ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id2)
        ip_addr2 = ip_obj2.get_instance_ip_address()
        print 'got v6 IP Address for first instance', ip_addr2
        if ip_addr2 != 'fd14::30':
            print 'Allocation failed, expected v6 IP Address fd14::30'

        # Read gateway ip address 
        print 'Read default gateway ip address' 
        ipam_refs = net_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                print 'Gateway for subnet (%s/%s) is (%s)' %(subnet.subnet.get_ip_prefix(), 
                        subnet.subnet.get_ip_prefix_len(),
                        subnet.get_default_gateway())


        #cleanup
        print 'Cleaning up'
        #cleanup subnet and allocation pools 
        self._vnc_lib.instance_ip_delete(id=ip_id1)
        self._vnc_lib.instance_ip_delete(id=ip_id2)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_subnet_network_update(self):
        domain = Domain('my-domain')
        self._vnc_lib.domain_create(domain)

        project = Project('my-proj', domain)
        self._vnc_lib.project_create(project)

        api_server = self._api_server
        addr_mgmt = api_server._addr_mgmt

        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-domain', 'my-proj',
                                                        'default-network-ipam'])
        pool_list = []
        pool_list.append(AllocationPoolType(start='11.1.1.20', end='11.1.1.25'))
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24),
                                    allocation_pools=pool_list,
                                    addr_from_start=True)
        # Create VN
        vn = VirtualNetwork('my-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        print 'Created Virtual Network object', vn.uuid

        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        vnc_lib = self._vnc_lib
        gsn_obj = vnc_lib.global_system_config_read(fq_name=['default-global-system-config'])
        g_asn = gsn_obj.get_autonomous_system()
        rt_target_num1 = 7999999
        rtgt_val1 = "target:%s:%s" % (g_asn, rt_target_num1)
        rt_target_num2 = 8999999
        rtgt_val2 = "target:%s:%s" % (g_asn, rt_target_num2)
        rtgt_val3 = "notarget:%s:%s" % (g_asn, rt_target_num1)
        rtgt_val4 = "target:%s:%s" % ('noip_addr', rt_target_num1)

        #Positive test of adding route_target_list
        route_targets = net_obj.get_route_target_list()
        if route_targets:
            route_targets.add_route_target(rtgt_val1)
        else:
            route_targets = RouteTargetList([rtgt_val1])
        net_obj.set_route_target_list(route_targets)
        self._vnc_lib.virtual_network_update(net_obj)

        # add a route_target with number larger than permissible
        route_targets = net_obj.get_route_target_list()
        if route_targets:
            route_targets.add_route_target(rtgt_val2)
        else:
            route_targets = RouteTargetList([rtgt_val2])
        net_obj.set_route_target_list(route_targets)
        try:
            self._vnc_lib.virtual_network_update(net_obj)
        except Exception as e:
            self.assertEquals(str(e),
                              'HTTP Status: 400 Content: Configured route target must use ASN that is different from global ASN or route target value must be less than 8000000')

        #delete all route_targets and add a new one with wrong prefix
        route_targets = net_obj.get_route_target_list()
        route_targets.delete_route_target(rtgt_val1)
        route_targets.delete_route_target(rtgt_val2)
        self._vnc_lib.virtual_network_update(net_obj)
        
        # add a route_target with suffix different that target
        route_targets = net_obj.get_route_target_list()
        if route_targets:
            route_targets.add_route_target(rtgt_val3)
        else:
            route_targets = RouteTargetList([rtgt_val3])
        net_obj.set_route_target_list(route_targets)
        try:
            self._vnc_lib.virtual_network_update(net_obj)
        except Exception as e:
            self.assertEquals(str(e),
                "HTTP Status: 400 Content: Route target must be of the format 'target:<asn>:<number>' or 'target:<ip>:number'") 

        route_targets = net_obj.get_route_target_list()
        route_targets.delete_route_target(rtgt_val3)
        self._vnc_lib.virtual_network_update(net_obj)
        route_targets = net_obj.get_route_target_list()
        route_targets.add_route_target(rtgt_val4)
        net_obj.set_route_target_list(route_targets)

        try:
            self._vnc_lib.virtual_network_update(net_obj)
        except Exception as e:
            self.assertEquals(str(e),
                "HTTP Status: 400 Content: Route target must be of the format 'target:<asn>:<number>' or 'target:<ip>:number'") 

        route_targets.delete_route_target(rtgt_val4)
        net_obj.set_route_target_list(route_targets)
        self._vnc_lib.virtual_network_update(net_obj)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        subnet_obj = addr_mgmt._subnet_objs['my-domain:my-proj:my-vn']['11.1.1.0/24']
        subnet_name = subnet_obj.get_name()
        self.assertEquals(subnet_name, 'my-domain:my-proj:my-vn:11.1.1.0/24')

        exclude_list = subnet_obj.get_exclude()
        self.assertEqual(len(exclude_list), 2)
        self.assertEquals(exclude_list[0], netaddr.ip.IPAddress('11.1.1.0'))
        self.assertEquals(exclude_list[1], netaddr.ip.IPAddress('11.1.1.255'))

        ip_ver = subnet_obj.get_version()
        self.assertEqual(ip_ver, 4)
        ip_ver = 6
        subnet_obj.set_version(ip_ver)
        ip_ver = subnet_obj.get_version()
        self.assertEqual(ip_ver, 6)
        ip_ver = 4
        subnet_obj.set_version(ip_ver)
   
        # test ip_alloc and ip_free with given ip_addr
        # first force ip_alloc to return No address.
        vnc_db_client = self._api_server.get_db_connection()
        #fake subnet_reserve_req not to return any ip addr
        # already on the project
        def fake_subnet_reserve_req(*args, **kwargs):
            return False

        def fake_subnet_alloc_req(*args, **kwargs):
            return False

        with test_common.patch(
            vnc_db_client, 'subnet_reserve_req', fake_subnet_reserve_req):
            assign_ip = subnet_obj.ip_alloc(ipaddr='11.1.1.20')
            self.assertEqual(assign_ip, None)

        with test_common.patch(
            vnc_db_client, 'subnet_alloc_req', fake_subnet_alloc_req):
            assign_ip = subnet_obj.ip_alloc()
            self.assertEqual(assign_ip, None)

        assign_ip = subnet_obj.ip_alloc(ipaddr='11.1.1.25')
        self.assertEqual(assign_ip, '11.1.1.25')
        assign_ip_obj = IPAddress(assign_ip)
        subnet_obj.ip_free(int(assign_ip_obj))
        # Now test ip_alloc without specificy ip_addr
        assign_ip = subnet_obj.ip_alloc()
        self.assertEqual(assign_ip, '11.1.1.20')
        # Try allocating same ip address without free
        new_ip = subnet_obj.ip_alloc(ipaddr=assign_ip)
        self.assertEqual(new_ip, None)

        new_ip_obj = IPAddress(assign_ip) 
        subnet_obj.ip_free(int(new_ip_obj))

        # Update virtual network keeping same subnet but with different
        # start and end of alloc_pool_list
        pool_list[:] = []
        pool_list.append(AllocationPoolType(start='11.1.1.25', end='11.1.1.30'))
        ipam_sn_v4.set_allocation_pools(pool_list)
        gw = '11.1.1.1'
        ipam_sn_v4.set_default_gateway(gw)
        try:
            self._vnc_lib.virtual_network_update(vn)
        except Exception as e:
            self.assertEquals(str(e),
                              'HTTP Status: 500 Content: Virtual-Network(my-domain:my-proj:my-vn) has invalid subnet(11.1.1.0/24)')

        # Update virtual network keeping same subnet but with more alloc_pools
        pool_list[:] = []
        pool_list.append(AllocationPoolType(start='11.1.1.25', end='11.1.1.30'))
        pool_list.append(AllocationPoolType(start='11.1.1.35', end='11.1.1.39'))
        ipam_sn_v4.set_allocation_pools(pool_list)
        gw = '11.1.1.1'
        ipam_sn_v4.set_default_gateway(gw)
        try:
            self._vnc_lib.virtual_network_update(vn)
        except Exception as e:
            self.assertEquals(str(e),
                              'HTTP Status: 500 Content: Virtual-Network(my-domain:my-proj:my-vn) has invalid subnet(11.1.1.0/24)')

        # Update virtual network keeping same subnet but different gw
        gw = '11.1.1.14'
        ipam_sn_v4.set_default_gateway(gw)
        try:
            self._vnc_lib.virtual_network_update(vn)
        except Exception as e:
            self.assertEquals(str(e),
                              'HTTP Status: 500 Content: Virtual-Network(my-domain:my-proj:my-vn) has invalid subnet(11.1.1.0/24)')

        # Add quota for subnet and create another subnet in network
        # virtual network update should assert due to Quota Limit
        project = self._vnc_lib.project_read(fq_name=['my-domain', 'my-proj'])
        quota=QuotaType()
        quota.subnet=1
        project.set_quota(quota)
        self._vnc_lib.project_update(project)

        ipam_sn2_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 24),
                                     addr_from_start=True)
        vn.set_network_ipam(ipam, VnSubnetsType([ipam_sn_v4, ipam_sn2_v4]))
        try:
            self._vnc_lib.virtual_network_update(vn)
        except Exception as e:
            self.assertEquals(str(e),
                              "['my-domain', 'my-proj', 'my-vn'] : quota limit (1) exceeded for resource subnet")

        # add quota for virtual network and create another network in the project,
        # network add should fail due to quota limit
        quota.subnet= None
        quota.virtual_network = 1 
        project.set_quota(quota)
        self._vnc_lib.project_update(project)

        #fake dbe_count_childern method to return 1 virtual network
        # already on the project
        def fake_dbe_count_children(*args, **kwargs):
            return (True, 1)

        with test_common.patch(
            vnc_db_client, 'dbe_count_children', fake_dbe_count_children):

            # Create VN
            vn2 = VirtualNetwork('my-vn2', project)
            vn2.add_network_ipam(ipam, VnSubnetsType([ipam_sn2_v4]))
            try:
                self._vnc_lib.virtual_network_create(vn2)
            except Exception as e:
                self.assertEquals(str(e),
                    "['my-domain', 'my-proj', 'my-vn2'] : quota limit (1) exceeded for resource virtual-network")

        # Increase subnet quota to 3, add 2 subnets, assign instance ip
        # from one subnet, use network_update to modify subnet
        # remove subnet from where ip has been allocated  
        quota.subnet = 3 
        project.set_quota(quota)
        self._vnc_lib.project_update(project)
        ipam_sn3_v4 = IpamSubnetType(subnet=SubnetType('13.1.1.0', 24),
                                     addr_from_start=True)

        # restore original alloc_pool to match with db_subnet
        pool_list[:] = []
        pool_list.append(AllocationPoolType(start='11.1.1.20', end='11.1.1.25'))
        ipam_sn_v4.set_allocation_pools(pool_list)
        # restore original gw_ip to match with db_subnet
        gw = '11.1.1.1'
        ipam_sn_v4.set_default_gateway(gw)
        vn.set_network_ipam(ipam, VnSubnetsType([ipam_sn_v4, ipam_sn2_v4]))
        self._vnc_lib.virtual_network_update(vn)

        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        subnet_obj = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[0]
        sn_uuid = subnet_obj.subnet_uuid
        # Create v4 Ip object, with v4 requested ip
        ip_obj1 =  InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4',
                              subnet_uuid = sn_uuid)
        ip_obj1.uuid = ip_obj1.name
        ip_obj1.set_virtual_network(net_obj)

        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
        self.assertEqual(ip_addr1, '11.1.1.20')

        # Now update network ip and delete one subnet 
        # from where instace_ip allotted
        vn.set_network_ipam(ipam, VnSubnetsType([ipam_sn2_v4, ipam_sn3_v4]))
        try:
            self._vnc_lib.virtual_network_update(vn)
        except Exception as e:
             self.assertEquals(str(e),
                 'Cannot Delete IP Block, IP(11.1.1.20) is in use')

        self._vnc_lib.instance_ip_delete(id=ip_id1)

        gw = '12.1.1.1'
        ipam_sn2_v4.set_default_gateway(gw)
        # Now network update should go through fine, with change in subnets 
        self._vnc_lib.virtual_network_update(vn)
        # Redo update without changing any attribute,
        self._vnc_lib.virtual_network_update(vn)
        # update network without any subnet
        vn.set_network_ipam(ipam, VnSubnetsType([]))
        self._vnc_lib.virtual_network_update(vn)

        vn.set_network_ipam(ipam, VnSubnetsType([ipam_sn2_v4, ipam_sn3_v4]))
        self._vnc_lib.virtual_network_update(vn)
      
        f_ip_pool = FloatingIpPool('fip_pool1', vn)
        f_ip_pool_id = self._vnc_lib.floating_ip_pool_create(f_ip_pool)
        f_ip_pool_obj = self._vnc_lib.floating_ip_pool_read(id=f_ip_pool_id)

        f_ip = FloatingIp('fl_ip1', f_ip_pool_obj)          
        f_ip.add_project(project)

        api_server = self._api_server
        addr_mgmt = api_server._addr_mgmt

        # Try creating floating ip with fake ip_alloc_req in addr_mgmt
        def fake_ip_alloc_req(*args, **kwargs):
            raise HttpError(500, " ip_alloc_req returns exception")

        with test_common.patch(
            addr_mgmt, 'ip_alloc_req', fake_ip_alloc_req):
            try:
                f_ip_id = self._vnc_lib.floating_ip_create(f_ip)
            except Exception as e:
                self.assertEquals(str(e),
                    'HTTP Status: 500 Content: HTTP Status: 500 Content:  ip_alloc_req returns exception') 

        f_ip_id = self._vnc_lib.floating_ip_create(f_ip)
        f_ip_obj = self._vnc_lib.floating_ip_read(id=f_ip_id)
        f_ip_addr = f_ip_obj.get_floating_ip_address() 
        self.assertEqual(f_ip_addr, '12.1.1.3')

        # Request to get already assigned floating-ip
        f_ip2 = FloatingIp('fl_ip2', f_ip_pool_obj, floating_ip_address='12.1.1.3')          
        f_ip2.add_project(project)
        try:
            f_ip2_id = self._vnc_lib.floating_ip_create(f_ip2)
        except Exception as e:
            self.assertEquals(str(e), 'Ip address already in use')

        #Update ipam to delete subnet that assigned floating_ip, add a
        # a new subnet, network update should fail 
        gw = '13.1.1.1'
        ipam_sn3_v4.set_default_gateway(gw)
        vn.set_network_ipam(ipam, VnSubnetsType([ipam_sn_v4, ipam_sn3_v4]))
        try:
            self._vnc_lib.virtual_network_update(vn)
        except Exception as e:
             self.assertEquals(str(e),
                 'Cannot Delete IP Block, Floating IP(12.1.1.3) is in use')

        self._vnc_lib.floating_ip_delete(id=f_ip_id)
        # Now update virtual-network will go through
        self._vnc_lib.virtual_network_update(vn)

        self._vnc_lib.floating_ip_pool_delete(id=f_ip_pool_id)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)

    def test_subnet_gateway_ip_alloc(self):
        # Create Domain
        domain = Domain('my-v4-v6-domain')
        self._vnc_lib.domain_create(domain)
        print 'Created domain '

        # Create Project
        project = Project('my-v4-v6-proj', domain)
        self._vnc_lib.project_create(project)
        print 'Created Project'

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        print 'Created network ipam'

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-v4-v6-domain', 'my-v4-v6-proj',
                                                        'default-network-ipam'])
        print 'Read network ipam'

        # Create subnets
        alloc_pool_list = []
        alloc_pool_list.append(AllocationPoolType(start='11.1.1.20', end='11.1.1.25'))
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24),
                                    allocation_pools=alloc_pool_list,
                                    addr_from_start=True)
        alloc_pool_list_v6 = []
        alloc_pool_list_v6.append(AllocationPoolType(start='fd14::30', end='fd14::40'))
        ipam_sn_v6 = IpamSubnetType(subnet=SubnetType('fd14::', 120),
                                    allocation_pools=alloc_pool_list_v6,
                                    addr_from_start=True)

        # Create VN
        vn = VirtualNetwork('my-v4-v6-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4, ipam_sn_v6]))
        self._vnc_lib.virtual_network_create(vn)
        print 'Created Virtual Network object', vn.uuid
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Read gateway ip address 
        print 'Read default gateway ip address' 
        ipam_refs = net_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                print 'Gateway for subnet (%s/%s) is (%s)' %(subnet.subnet.get_ip_prefix(), 
                        subnet.subnet.get_ip_prefix_len(),
                        subnet.get_default_gateway())
                if subnet.subnet.get_ip_prefix() == '11.1.1.0':
                    if subnet.get_default_gateway() != '11.1.1.1':
                        print ' Failure, expected gateway ip address 11.1.1.1'
                if subnet.subnet.get_ip_prefix() == 'fd14::':
                    if subnet.get_default_gateway() != 'fd14::1':
                        print ' Failure, expected gateway ip address fd14::1'


        #cleanup
        print 'Cleaning up'
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_bulk_ip_alloc_free(self):
        # Create Domain
        domain = Domain('v4-domain')
        self._vnc_lib.domain_create(domain)
        print 'Created domain '

        # Create Project
        project = Project('v4-proj', domain)
        self._vnc_lib.project_create(project)
        print 'Created Project'

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        print 'Created network ipam'
        ipam = self._vnc_lib.network_ipam_read(fq_name=['v4-domain', 'v4-proj',
                                                        'default-network-ipam'])
        print 'Read network ipam'

        # Create subnets
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create VN
        vn = VirtualNetwork('v4-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        print 'Created Virtual Network object', vn.uuid
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # request to allocate 10 ip address using bulk allocation api
        data = {"subnet" : "11.1.1.0/24", "count" : 10}
        url = '/virtual-network/%s/ip-alloc' %(vn.uuid)
        rv_json = self._vnc_lib._request_server(rest.OP_POST, url,
                                                json.dumps(data))
        ret_data = json.loads(rv_json)
        ret_ip_addr = ret_data['ip_addr']
        expected_ip_addr = ['11.1.1.252', '11.1.1.251', '11.1.1.250',
                            '11.1.1.249', '11.1.1.248', '11.1.1.247',
                            '11.1.1.246', '11.1.1.245', '11.1.1.244',
                            '11.1.1.243']

        self.assertEqual(len(expected_ip_addr), len(ret_ip_addr))
        for idx in range(len(expected_ip_addr)):
            self.assertEqual(expected_ip_addr[idx], ret_ip_addr[idx])
        
        print 'Verify bulk ip address allocation'
        # Find out number of allocated ips from given VN/subnet
        # We should not get 13 ip allocated from this subnet
        # 10 user request + 3 reserved ips (first, last and gw).
        data = {"subnet_list" : ["11.1.1.0/24"]}
        url = '/virtual-network/%s/subnet-ip-count' %(vn.uuid)
        rv_json = self._vnc_lib._request_server(rest.OP_POST, url,
                                                json.dumps(data))
        ret_ip_count = json.loads(rv_json)['ip_count_list'][0]
        allocated_ip = ret_ip_count - 3
        self.assertEqual(allocated_ip, 10)

        #free 5 allocated ip addresses from vn
        data = {"subnet" : "11.1.1.0/24",
                "ip_addr" : ['11.1.1.252', '11.1.1.251', '11.1.1.250',
                             '11.1.1.249', '11.1.1.248']}
        url = '/virtual-network/%s/ip-free' %(vn.uuid)
        self._vnc_lib._request_server(rest.OP_POST, url, json.dumps(data))

        # Find out number of allocated ips from given VN/subnet
        # We should  get 5+3 ip allocated from this subnet
        data = {"subnet_list" : ["11.1.1.0/24"]}
        url = '/virtual-network/%s/subnet-ip-count' %(vn.uuid)
        rv_json = self._vnc_lib._request_server(rest.OP_POST, url,
                                                json.dumps(data))
        ret_ip_count = json.loads(rv_json)['ip_count_list'][0]
        allocated_ip = ret_ip_count - 3
        self.assertEqual(allocated_ip, 5)

        #free remaining 5 allocated ip addresses from vn
        data = {"subnet" : "11.1.1.0/24",
                "ip_addr": ['11.1.1.247', '11.1.1.246', '11.1.1.245',
                            '11.1.1.244', '11.1.1.243']}
        url = '/virtual-network/%s/ip-free' %(vn.uuid)
        self._vnc_lib._request_server(rest.OP_POST, url, json.dumps(data))

        data = {"subnet_list" : ["11.1.1.0/24"]}
        url = '/virtual-network/%s/subnet-ip-count' %(vn.uuid)
        rv_json = self._vnc_lib._request_server(rest.OP_POST, url,
                                                json.dumps(data))
        ret_ip_count = json.loads(rv_json)['ip_count_list'][0]
        allocated_ip = ret_ip_count - 3
        self.assertEqual(allocated_ip, 0)
        print 'Verified bulk ip free'

        # cleanup
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_network_ipam_crud(self):
        # Create Domain
        domain = Domain('v4-domain')
        self._vnc_lib.domain_create(domain)
        print 'Created domain '

        # Create Project
        project = Project('v4-proj', domain)
        self._vnc_lib.project_create(project)
        print 'Created Project'

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        print 'Created network ipam'

        vnc_lib = self._vnc_lib
        gsn_obj = vnc_lib.global_system_config_read(fq_name=['default-global-system-config'])
        g_asn = gsn_obj.get_autonomous_system()
        print 'Read network ipam'
        ipam = self._vnc_lib.network_ipam_read(fq_name=['v4-domain', 'v4-proj',
                                                        'default-network-ipam'])

        # create VM and try to change DNS method to new_dns_method, 
        # we should get an exception, delete VM and then change dsn-method
        # change should be successful 
        # Create subnets
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create VN
        vn = VirtualNetwork('v4-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        subnet_obj = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[0]
        sn_uuid = subnet_obj.subnet_uuid

        # Create v4 Ip object, with v4 requested ip
        ip_obj1 =  InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4',
                              subnet_uuid = sn_uuid)
        ip_obj1.uuid = ip_obj1.name
        ip_obj1.set_virtual_network(net_obj)
        
        # Create VM
        vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj1.uuid = vm_inst_obj1.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj1)

        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj1, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn)
        ip_obj1.set_virtual_machine_interface(port_obj1)
        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)

        # We have domain, project, network, ipam, subnet
        # change ipam_dns_method in ipam_mgmt, update ipam
        ipam_mgmt_obj=ipam.get_network_ipam_mgmt()
        orig_dns_method = ipam_mgmt_obj.get_ipam_dns_method()
        self.assertEqual(orig_dns_method, None)
        new_dns_method = "default-dns-server"
        ipam_mgmt_obj.set_ipam_dns_method(new_dns_method)
        ipam.set_network_ipam_mgmt(ipam_mgmt_obj)
        try:
            self._vnc_lib.network_ipam_update(ipam)
        except cfgm_common.exceptions.RefsExistError as e:
            self.assertThat(e.message,
                            Equals('Cannot change DNS Method  with active VMs referring to the IPAM'))

        # delete machine_interface, Virtual_machine and then update dns_method
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)

        print 'Read network ipam'
        ipam = self._vnc_lib.network_ipam_read(fq_name=['v4-domain', 'v4-proj',
                                                        'default-network-ipam'])

        ipam_mgmt_obj=ipam.get_network_ipam_mgmt()
        orig_dns_method = ipam_mgmt_obj.get_ipam_dns_method()
        ipam_mgmt_obj.set_ipam_dns_method(new_dns_method)
        ipam.set_network_ipam_mgmt(ipam_mgmt_obj)
        self._vnc_lib.network_ipam_update(ipam)

        # read ipam and verify the change
        ipam = self._vnc_lib.network_ipam_read(fq_name=['v4-domain', 'v4-proj',
                                                        'default-network-ipam'])
        ipam_mgmt_obj=ipam.get_network_ipam_mgmt()
        modified_dns_method = ipam_mgmt_obj.get_ipam_dns_method()
        self.assertEqual(modified_dns_method, new_dns_method)
        # create VM with "default-dns-server" as dns_method in ipam

        # Now change dns_method to tenant-dns-server
        # ipam update should fail
        new_dns_method = "tenant-dns-server"
        ipam_mgmt_obj.set_ipam_dns_method(new_dns_method)
        ipam.set_network_ipam_mgmt(ipam_mgmt_obj)
        modified_dns_method = ipam_mgmt_obj.get_ipam_dns_method()
        self.assertEqual(modified_dns_method, new_dns_method)

        # Create VM
        vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj1.uuid = vm_inst_obj1.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj1)

        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj1, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn)
        ip_obj1.set_virtual_machine_interface(port_obj1)
        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)

        try:
            self._vnc_lib.network_ipam_update(ipam)
        except cfgm_common.exceptions.RefsExistError as e:
            self.assertThat(e.message,
                            Equals('Cannot change DNS Method  with active VMs referring to the IPAM'))

        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)

        self._vnc_lib.network_ipam_update(ipam)
        # restore old dns_method and update ipam
        ipam_mgmt_obj.set_ipam_dns_method(orig_dns_method)
        ipam.set_network_ipam_mgmt(ipam_mgmt_obj)
        self._vnc_lib.network_ipam_update(ipam)

        # read ipam and verify the change
        ipam = self._vnc_lib.network_ipam_read(fq_name=['v4-domain', 'v4-proj',
                                                        'default-network-ipam'])

        ipam_mgmt_obj=ipam.get_network_ipam_mgmt()
        modified_dns_method = ipam_mgmt_obj.get_ipam_dns_method()
        self.assertEqual(modified_dns_method, orig_dns_method)

        # change display_name in ipam and update ipam
        orig_display_name = ipam.get_display_name()
        new_display_name = "modified-network-ipam"
        ipam.set_display_name(new_display_name)
        self._vnc_lib.network_ipam_update(ipam)

        # read ipam and verify the change
        ipam = self._vnc_lib.network_ipam_read(fq_name=['v4-domain', 'v4-proj',
                                                        'default-network-ipam'])

        modified_display_name = ipam.get_display_name()
        self.assertEqual(modified_display_name, new_display_name)

        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_resource_delete_fail(self):
        # Create Domain
        domain = Domain('v4-domain')
        self._vnc_lib.domain_create(domain)

        # Create Project 
        project = Project('v4-proj', domain)
        self._vnc_lib.project_create(project)

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

        print 'Read network ipam'
        ipam = self._vnc_lib.network_ipam_read(fq_name=['v4-domain', 'v4-proj',
                                                        'default-network-ipam'])
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create VN
        vn = VirtualNetwork('v4-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        subnet_obj = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[0]
        sn_uuid = subnet_obj.subnet_uuid

        # Create v4 Ip object, with v4 requested ip
        ip_obj1 =  InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4',
                              subnet_uuid = sn_uuid)
        ip_obj1.uuid = ip_obj1.name
        ip_obj1.set_virtual_network(net_obj)

        # Create VM
        vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj1.uuid = vm_inst_obj1.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj1)

        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj1, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn)
        ip_obj1.set_virtual_machine_interface(port_obj1)
        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)

        print 'Allocating an IP4 address for first VM'
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
        vnc_db_client = self._api_server.get_db_connection()

        #instance-ip delete-fail
        def fake_dbe_delete(*args, **kwargs):
            raise HttpError(500, "dbe_delete returns exception")

        with test_common.patch(
            vnc_db_client, 'dbe_delete', fake_dbe_delete):
            try:
                self._vnc_lib.instance_ip_delete(id=ip_id1)
            except Exception as e:
                self.assertEquals(e.message, 'HTTP Status: 500 Content: dbe_delete returns exception')

        # Read instance_ip with a same id,
        # instance_ip object should be present with ip_addr1
        ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id1)
        self.assertEqual(ip_addr1, ip_obj2.get_instance_ip_address())

        # Delete instance ip and read again,
        # we should not get ip_obj for given ip_id           
        self._vnc_lib.instance_ip_delete(id=ip_id1)
        try:
            ip_obj2 = None
            ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id1)
        except NoIdError:
            pass
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)

        # Floating-ip delete-fail 
        f_ip_pool = FloatingIpPool('fip_pool1', vn)
        f_ip_pool_id = self._vnc_lib.floating_ip_pool_create(f_ip_pool)
        f_ip_pool_obj = self._vnc_lib.floating_ip_pool_read(id=f_ip_pool_id)
        f_ip = FloatingIp('fl_ip1', f_ip_pool_obj)
        f_ip.add_project(project)
        f_ip_id = self._vnc_lib.floating_ip_create(f_ip)
        f_ip_obj = self._vnc_lib.floating_ip_read(id=f_ip_id)
        f_ip_addr = f_ip_obj.get_floating_ip_address()

        with test_common.patch(
            vnc_db_client, 'dbe_delete', fake_dbe_delete):
            try:
                self._vnc_lib.floating_ip_delete(id=f_ip_id)
            except Exception as e:
                self.assertEquals(e.message, 'HTTP Status: 500 Content: dbe_delete returns exception')

        f_ip_obj2 = self._vnc_lib.floating_ip_read(id=f_ip_id)
        self.assertEqual(f_ip_addr, f_ip_obj2.get_floating_ip_address())
        # Delete floating ip and read again
        self._vnc_lib.floating_ip_delete(id=f_ip_id)
        try:
            f_ip_obj2 = None
            f_ip_obj2 = self._vnc_lib.floating_ip_read(id=f_ip_id)
        except NoIdError:
            pass

        self._vnc_lib.floating_ip_pool_delete(id=f_ip_pool_id)
        #virtual-network delete-fail
        with test_common.patch(
            vnc_db_client, 'dbe_delete', fake_dbe_delete):
            try:
                self._vnc_lib.virtual_network_delete(id=vn.uuid)
            except Exception as e:
                self.assertEquals(e.message, 'HTTP Status: 500 Content: dbe_delete returns exception')

        #Read virtual network with vn.uuid and it should have same uuid     
        net_obj2 = self._vnc_lib.virtual_network_read(id = vn.uuid)
        self.assertEqual(net_obj.get_uuid(), net_obj2.get_uuid())

        # Delete virtual network and read again, we should not get vn for given uuid      
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        try:
            net_obj2 = None
            net_obj2 = self._vnc_lib.virtual_network_read(id=vn.uuid)
        except NoIdError:
            pass

        #clean up
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_instance_http_post_fail(self):
        # Create Domain
        domain = Domain('v4-domain')
        self._vnc_lib.domain_create(domain)

        # Create Project
        project = Project('v4-proj', domain)
        self._vnc_lib.project_create(project)

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

        print 'Read network ipam'
        ipam = self._vnc_lib.network_ipam_read(fq_name=['v4-domain', 'v4-proj',
                                                        'default-network-ipam'])
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create VN
        vn = VirtualNetwork('v4-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        subnet_obj = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[0]
        sn_uuid = subnet_obj.subnet_uuid

        # Create v4 Ip object, with v4 requested ip
        ip_obj1 =  InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4',
                              subnet_uuid = sn_uuid)
        ip_obj1.uuid = ip_obj1.name

        # Create VM
        vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj1.uuid = vm_inst_obj1.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj1)

        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj1, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn)
        ip_obj1.set_virtual_machine_interface(port_obj1)
        ip_obj1.set_virtual_network(net_obj)
        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)

        vnc_db_client = self._api_server.get_db_connection()
        print 'Allocating an IP4 address for first VM'
        def fake_bottle_abort(*args, **kwargs):
            return
        def fake_dbe_create(*args, **kwargs):
            return (False, "dbe_create returns exception")

        def fake_dbe_read(*args, **kwargs):
            return (False, "dbe_read returns exception")

        def fake_dbe_read_404(*args, **kwargs):
            raise cfgm_common.exceptions.NoIdError('dbe_read returns exception')

        with test_common.patch(
            bottle, 'abort', fake_bottle_abort):
            with test_common.patch(
                vnc_db_client, 'dbe_create', fake_dbe_create):
                ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)

        try:
            ip_obj2 = None
            ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id1)
            self.assertEqual(ip_obj2, None)
        # Expected to get exception as instance_ip should not be created
        except NoIdError:
            pass

        # Now fail in http_post_collection for dbe_read for reading virtual network
        with test_common.patch(
            vnc_db_client, 'dbe_read', fake_dbe_read):
            try:
                ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
            except Exception as e:
                self.assertEqual(e.status_code, 500)
                pass

        try:
            ip_obj2 = None
            ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id1)
            self.assertEqual(ip_obj2, None)
        # Expected to get exception as instance_ip should not be created
        except NoIdError:
            pass

        # Now fail in http_post_collection for dbe_read to raise
        # cfgm_common.exceptions.NoIdErro
        with test_common.patch(
            vnc_db_client, 'dbe_read', fake_dbe_read_404):
            try:
                ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
            except NoIdError:
                pass

        try:
            ip_obj2 = None
            ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id1)
            self.assertEqual(ip_obj2, None)
        # Expected to get exception as instance_ip should not be created
        except NoIdError:
            pass

        #clean up
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_v4_ip_allocation_exhaust(self):
        # Create Domain
        domain = Domain('v4-domain')
        self._vnc_lib.domain_create(domain)
        print 'Created domain '

        # Create Project
        project = Project('v4-proj', domain)
        self._vnc_lib.project_create(project)
        print 'Created Project'

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        print 'Created network ipam'

        ipam = self._vnc_lib.network_ipam_read(fq_name=['v4-domain', 'v4-proj',
                                                        'default-network-ipam'])
        print 'Read network ipam'

        ip_alloc_from_start = [True, False]
        for from_start in ip_alloc_from_start:
            # Create subnets
            alloc_pool_list = []
            alloc_pool_list.append(
                AllocationPoolType(start='11.1.1.21', end='11.1.1.24'))
            alloc_pool_list.append(
                AllocationPoolType(start='11.1.1.31', end='11.1.1.34'))
            ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24),
                                        allocation_pools=alloc_pool_list,
                                        addr_from_start=from_start)

            ip_addr_list = []
            for alloc_pool in alloc_pool_list:
                start_ip = alloc_pool.start
                end_ip = alloc_pool.end

                start = list(map(int, start_ip.split(".")))
                end = list(map(int, end_ip.split(".")))
                temp = start
                ip_addr_list.append(start_ip)
                while temp != end:
                    start[3] += 1
                    for i in (3, 2, 1):
                        if temp[i] == 256:
                            temp[i] = 0
                            temp[i-1] += 1
                    ip_addr_list.append(".".join(map(str, temp)))

            if from_start is False:
                ip_addr_list.reverse()

            total_addr = len(ip_addr_list)
            print 'ip address alloc list:', ip_addr_list[0:total_addr]
            # Create VN
            vn = VirtualNetwork('v4-vn', project)
            vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4]))
            self._vnc_lib.virtual_network_create(vn)
            print 'Created Virtual Network object', vn.uuid
            net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

            subnet_obj = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[0]
            sn_uuid = subnet_obj.subnet_uuid
            # Create v4 Ip object for all possible addresses in alloc_pool
            v4_ip_obj_list = []

            for idx, val in enumerate(ip_addr_list):
                v4_ip_obj_list.append(
                    InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4',
                               subnet_uuid = sn_uuid))
                v4_ip_obj_list[idx].uuid = v4_ip_obj_list[idx].name
                print 'Created Instance IP object',idx, v4_ip_obj_list[idx].uuid

            # Create number of VMs to assign ip addresses
            # to use all addresses in alloc_pool
            vm_list_v4 = []
            for idx, val in enumerate(ip_addr_list):
                vm_list_v4.append(VirtualMachine(str(uuid.uuid4())))
                vm_list_v4[idx].uuid = vm_list_v4[idx].name
                self._vnc_lib.virtual_machine_create(vm_list_v4[idx])

            port_list = []
            port_id_list = []
            for idx, val in enumerate(ip_addr_list):
                id_perms = IdPermsType(enable=True)
                port_list.append(
                    VirtualMachineInterface(str(uuid.uuid4()), vm_list_v4[idx],
                                            id_perms=id_perms))
                port_list[idx].uuid = port_list[idx].name
                port_list[idx].set_virtual_network(vn)
                v4_ip_obj_list[idx].set_virtual_machine_interface(port_list[idx])
                v4_ip_obj_list[idx].set_virtual_network(net_obj)
                port_id_list.append(
                    self._vnc_lib.virtual_machine_interface_create(port_list[idx]))

            ip_ids = []
            print 'Allocating an IP4 address for VMs'
            for idx, val in enumerate(ip_addr_list):
                ip_ids.append(
                    self._vnc_lib.instance_ip_create(v4_ip_obj_list[idx]))
                v4_ip_obj_list[idx] = self._vnc_lib.instance_ip_read(
                                          id=ip_ids[idx])
                ip_addr = v4_ip_obj_list[idx].get_instance_ip_address()
                print 'got v4 IP Address for instance',idx,':', ip_addr
                if ip_addr != ip_addr_list[idx]:
                    print 'Allocation failed, expected v4 IP Address:', ip_addr_list[idx]

            # Find out number of allocated ips from given VN/subnet to test
            # vn_subnet_ip_count_http_post()
            data = {"subnet_list" : ["11.1.1.0/24"]}
            url = '/virtual-network/%s/subnet-ip-count' %(vn.uuid)
            rv_json = self._vnc_lib._request_server(rest.OP_POST, url, json.dumps(data))
            ret_ip_count = json.loads(rv_json)['ip_count_list'][0]
            total_ip_addr = len(ip_addr_list)
            self.assertEqual(ret_ip_count, total_ip_addr)

            # Delete 2 VMs (With First and Last IP), associated Ports
            # and instanace IPs,
            # recreate them to make sure that we get same ips again.
            # Repeat this for 2 VMs from middle of the alloc_pool
            total_ip_addr = len(ip_addr_list)
            to_modifies = [[0, total_ip_addr-1],
                           [total_ip_addr/2 -1, total_ip_addr/2]]
            for to_modify in to_modifies:
                print 'Delete Instances', to_modify[0], to_modify[1]
                for idx, val in enumerate(to_modify):
                    self._vnc_lib.instance_ip_delete(id=ip_ids[val])
                    ip_ids[val] = None
                    self._vnc_lib.virtual_machine_interface_delete(
                        id=port_list[val].uuid)
                    port_list[val] = None
                    port_id_list[val] = None
                    self._vnc_lib.virtual_machine_delete(
                        id=vm_list_v4[val].uuid)
                    vm_list_v4[val] = None
                    v4_ip_obj_list[val] = None
                    ip_ids[val] = None
                    print 'Deleted instance',val

                # Re-create two VMs and assign IP addresses
                # these should get first and last ip.
                for idx, val in enumerate(to_modify):
                    v4_ip_obj_list[val] = InstanceIp(
                        name=str(uuid.uuid4()), instance_ip_family='v4')
                    v4_ip_obj_list[val].uuid = v4_ip_obj_list[val].name
                    vm_list_v4[val] = VirtualMachine(str(uuid.uuid4()))
                    vm_list_v4[val].uuid = vm_list_v4[val].name
                    self._vnc_lib.virtual_machine_create(vm_list_v4[val])
                    id_perms = IdPermsType(enable=True)
                    port_list[val] = VirtualMachineInterface(
                        str(uuid.uuid4()), vm_list_v4[val], id_perms=id_perms)

                    port_list[val].uuid = port_list[val].name
                    port_list[val].set_virtual_network(vn)
                    v4_ip_obj_list[val].set_virtual_machine_interface(
                        port_list[val])
                    v4_ip_obj_list[val].set_virtual_network(net_obj)
                    port_id_list[val] = self._vnc_lib.virtual_machine_interface_create(port_list[val])
                    print 'Created instance',val

                # Allocate IPs to modified VMs
                for idx, val in enumerate(to_modify):
                    ip_ids[val] = self._vnc_lib.instance_ip_create(v4_ip_obj_list[val])
                    v4_ip_obj_list[val] = self._vnc_lib.instance_ip_read(
                        id=ip_ids[val])
                    ip_addr = v4_ip_obj_list[val].get_instance_ip_address()
                    print 'got v4 IP Address for instance',val,':', ip_addr
                    if ip_addr != ip_addr_list[val]:
                        print 'Allocation failed, expected v4 IP Address:', ip_addr_list[val]

            # negative test.
            # Create a new VM and try getting a new instance_ip
            # we should get an exception as alloc_pool is fully exhausted.

            print 'Negative Test to create extra instance and try assigning IP address'
            # Create v4 Ip object
            ip_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
            ip_obj1.uuid = ip_obj1.name
            print 'Created new Instance IP object', ip_obj1.uuid

            # Create VM
            vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
            vm_inst_obj1.uuid = vm_inst_obj1.name
            self._vnc_lib.virtual_machine_create(vm_inst_obj1)

            id_perms = IdPermsType(enable=True)
            port_obj1 = VirtualMachineInterface(
                str(uuid.uuid4()), vm_inst_obj1, id_perms=id_perms)
            port_obj1.uuid = port_obj1.name
            port_obj1.set_virtual_network(vn)
            ip_obj1.set_virtual_machine_interface(port_obj1)
            ip_obj1.set_virtual_network(net_obj)
            port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)
            print 'Created extra instance'

            print 'Allocating an IP4 address for extra instance'
            try:
                ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
            except HttpError:
                print 'alloc pool is exhausted'
                pass

            # cleanup for negative test
            self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
            self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)

            # user requested instance_ip, if VM is getting created
            # with user requested ip and ip is already allocated,
            # system allows VM creation with same ip
            # Test is with start from begining allocation scheme
            if from_start is True:
                # Create a v4 Ip object
                ip_obj2 = InstanceIp(name=str(uuid.uuid4()),
                                     instance_ip_address='11.1.1.1',
                                     instance_ip_family='v4')
                ip_obj2.uuid = ip_obj2.name
                print 'Created new Instance IP object', ip_obj2.uuid

                # Create VM
                vm_inst_obj2 = VirtualMachine(str(uuid.uuid4()))
                vm_inst_obj2.uuid = vm_inst_obj2.name
                self._vnc_lib.virtual_machine_create(vm_inst_obj2)

                id_perms = IdPermsType(enable=True)
                port_obj2 = VirtualMachineInterface(
                    str(uuid.uuid4()), vm_inst_obj2, id_perms=id_perms)
                port_obj2.uuid = port_obj2.name
                port_obj2.set_virtual_network(vn)
                ip_obj2.set_virtual_machine_interface(port_obj2)
                ip_obj2.set_virtual_network(net_obj)
                port_id2 = self._vnc_lib.virtual_machine_interface_create(
                    port_obj2)
                ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)

                #cleanup for user requested IP, VM, port
                self._vnc_lib.instance_ip_delete(id=ip_id2)
                self._vnc_lib.virtual_machine_interface_delete(
                    id=port_obj2.uuid)
                self._vnc_lib.virtual_machine_delete(id=vm_inst_obj2.uuid)

            #cleanup subnet and allocation pools
            for idx, val in enumerate(ip_addr_list):
                self._vnc_lib.instance_ip_delete(id=ip_ids[idx])
                self._vnc_lib.virtual_machine_interface_delete(
                    id=port_list[idx].uuid)
                self._vnc_lib.virtual_machine_delete(id=vm_list_v4[idx].uuid)
            self._vnc_lib.virtual_network_delete(id=vn.uuid)

        # end of from_start
        print 'Cleaning up'
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_req_ip_allocation(self):
        # Create Domain
        domain = Domain('my-v4-v6-req-ip-domain')
        self._vnc_lib.domain_create(domain)
        print 'Created domain '

        # Create Project
        project = Project('my-v4-v6-req-ip-proj', domain)
        self._vnc_lib.project_create(project)
        print 'Created Project'

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        print 'Created network ipam'

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-v4-v6-req-ip-domain',
                                                        'my-v4-v6-req-ip-proj',
                                                        'default-network-ipam'])
        print 'Read network ipam'

        # Create subnets
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        ipam_sn_v6 = IpamSubnetType(subnet=SubnetType('fd14::', 120))

        # Create VN
        vn = VirtualNetwork('my-v4-v6-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4, ipam_sn_v6]))
        self._vnc_lib.virtual_network_create(vn)
        print 'Created Virtual Network object', vn.uuid
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Create v4 Ip object, with v4 requested ip
        ip_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_address='11.1.1.4',
                             instance_ip_family='v4')
        ip_obj1.uuid = ip_obj1.name
        print 'Created Instance IP object 1 ', ip_obj1.uuid

        # Create v6 Ip object with v6 requested ip
        ip_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_address='fd14::4',
                             instance_ip_family='v6')
        ip_obj2.uuid = ip_obj2.name
        print 'Created Instance IP object 2', ip_obj2.uuid

        # Create VM
        vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj1.uuid = vm_inst_obj1.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj1)

        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj1, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn)
        ip_obj1.set_virtual_machine_interface(port_obj1)
        ip_obj1.set_virtual_network(net_obj)
        ip_obj2.set_virtual_machine_interface(port_obj1)
        ip_obj2.set_virtual_network(net_obj)
        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)

        print 'Allocating an IP4 address for first VM'
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
        print '  got v4 IP Address for first instance', ip_addr1
        if ip_addr1 != '11.1.1.4':
            print 'Allocation failed, expected v4 IP Address 11.1.1.4'

        print 'Allocating an IP6 address for first VM'
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)
        ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id2)
        ip_addr2 = ip_obj2.get_instance_ip_address()
        print '  got v6 IP Address for first instance', ip_addr2
        if ip_addr2 != 'fd14::4':
            print 'Allocation failed, expected v6 IP Address fd14::4'

        # Read gateway ip address
        print 'Read default gateway ip address'
        ipam_refs = net_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                print 'Gateway for subnet (%s/%s) is (%s)' %(subnet.subnet.get_ip_prefix(),
                        subnet.subnet.get_ip_prefix_len(),
                        subnet.get_default_gateway())


        #cleanup
        print 'Cleaning up'
        self._vnc_lib.instance_ip_delete(id=ip_id1)
        self._vnc_lib.instance_ip_delete(id=ip_id2)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_notify_doesnt_persist(self):
        # net/ip notify context shouldn't persist to db, should only
        # update in-memory book-keeping
        def_ipam = NetworkIpam()
        ipam_obj = self._vnc_lib.network_ipam_read(
                      fq_name=def_ipam.get_fq_name())
        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn_obj)

        iip_obj = InstanceIp('iip-%s' %(self.id()))
        iip_obj.add_virtual_network(vn_obj)

        class SpyCreateNode(object):
            def __init__(self, orig_object, method_name):
                self._orig_method = getattr(orig_object, method_name)
                self._invoked = 0
            # end __init__

            def __call__(self, *args, **kwargs):
                if self._invoked >= 1:
                    raise Exception(
                        "Instance IP was persisted more than once")

                if args[1].startswith('/api-server/subnets'):
                    self._invoked += 1
                return self._orig_method(args, kwargs)
        # end SpyCreateNode

        orig_object = self._api_server._db_conn._zk_db._zk_client
        method_name = 'create_node'
        with test_common.patch(orig_object, method_name,
                               SpyCreateNode(orig_object, method_name)):
            self._vnc_lib.instance_ip_create(iip_obj)
            self.assertTill(self.ifmap_has_ident, obj=iip_obj)

    #end test_notify_doesnt_persist
 
#end class TestIpAlloc

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    unittest.main()
