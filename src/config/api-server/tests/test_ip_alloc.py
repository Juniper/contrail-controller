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

from vnc_api.vnc_api import *
import vnc_api.gen.vnc_api_test_gen
from vnc_api.gen.resource_test import *
import cfgm_common

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

            # Create v4 Ip object for all possible addresses in alloc_pool
            v4_ip_obj_list = []

            for idx, val in enumerate(ip_addr_list):
                v4_ip_obj_list.append(
                    InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4'))
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
