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
from vnc_api.common import exceptions as vnc_exceptions
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
        print 'test ip allocation'

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

        'print Allocting an IP4 address for first VM'
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
        print 'got v4 IP Address for first instance', ip_addr1
        if ip_addr1 != '11.1.1.253':
            print 'Allocation failed, expected v4 IP Address 11.1.1.253'

        'print Allocting an IP6 address for first VM'
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)
        ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id2)
        ip_addr2 = ip_obj2.get_instance_ip_address()
        print 'got v6 IP Address for first instance', ip_addr2
        if ip_addr2 != 'fd14::fd':
            print 'Allocation failed, expected v6 IP Address fd14::fd'

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
        print 'test ip allocation'

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

        'print Allocting an IP4 address for first VM'
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
        print 'got v4 IP Address for first instance', ip_addr1
        if ip_addr1 != '11.1.1.30':
            print 'Allocation failed, expected v4 IP Address 11.1.1.30'
        
        'print Allocting an IP6 address for first VM'
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)
        ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id2)
        ip_addr2 = ip_obj2.get_instance_ip_address()
        print 'got v6 IP Address for first instance', ip_addr2
        if ip_addr2 != 'fd14::30':
            print 'Allocation failed, expected v4 IP Address fd14::30'

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


#end class TestIpAlloc

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    unittest.main()
