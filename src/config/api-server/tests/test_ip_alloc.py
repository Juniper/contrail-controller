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
    def __init__(self, *args, **kwargs):
        ch = logging.StreamHandler()
        ch.setLevel(logging.DEBUG)
        logger.addHandler(ch)
        super(TestIpAlloc, self).__init__(*args, **kwargs)

    def test_flat_subnet_ipam_crud(self):
        # Create Domain
        domain = Domain('flat-subnet-domain')
        self._vnc_lib.domain_create(domain)
        logger.debug('Created domain ')

        # Create Project
        project = Project('flat-subnet-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        # create ipam without any subnets or subnet_method
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        ipam = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'default-network-ipam'])
        self.assertEqual(ipam.get_ipam_subnet_method(), None)

        # try changing subnet-method from None to flat-subnet, vnc_lib should reject
        ipam.set_ipam_subnet_method('flat-subnet')
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'ipam_subnet_method can not be changed') as e:
            self._vnc_lib.network_ipam_update(ipam)

        ipam.set_ipam_subnet_method('user-defined-subnet')
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'ipam_subnet_method can not be changed') as e:
            self._vnc_lib.network_ipam_update(ipam)

        ipam1_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 28),
                                     alloc_unit=4)
        ipam2_sn_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 28),
                                     alloc_unit=4)

        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="flat-subnet")
        self._vnc_lib.network_ipam_create(ipam)
        ipam.set_ipam_subnets(IpamSubnets([ipam1_sn_v4, ipam2_sn_v4]))
        #update ipam to add subnets
        self._vnc_lib.network_ipam_update(ipam)
        #read ipam back and inspect ipam_subnets
        ipam = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'default-network-ipam'])
        # delete ipam before creating one with flat-subnet
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="flat-subnet",
                           ipam_subnets=IpamSubnets([ipam1_sn_v4]))
        self._vnc_lib.network_ipam_create(ipam)

        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end test_flat_subnet_ipam_crud

    def test_flat_subnet_ipam_user_defined_network(self):
        #retest failing
        domain = Domain('flat-subnet-domain')
        self._vnc_lib.domain_create(domain)
        project = Project('flat-subnet-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        ipam1_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 28),
                                     alloc_unit=4)
        ipam2_sn_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 28),
                                     alloc_unit=4)

        ipam3_sn_v4 = IpamSubnetType(subnet=SubnetType('13.1.1.0', 28),
                                     alloc_unit=4)

        ipam4_sn_v4 = IpamSubnetType(subnet=SubnetType('14.1.1.0', 28),
                                     alloc_unit=4)

        # Create a NetworkIpam without specifying ipam_subnet_method
        # ipam_create should fail if any ipam_subnets attached to ipam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"),
                           ipam_subnets=IpamSubnets([ipam1_sn_v4, ipam2_sn_v4]))
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'ipam-subnets are allowed only with flat-subnet') as e:
            self._vnc_lib.network_ipam_create(ipam)

        # Create NetworkIpam specifying subnet_method as flat-subnet
        # use only ipam1_sn_v4, ipam2_sn_v4 in ipam subnet,
        # ipam3_sn_v4 will be added on later

        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="flat-subnet",
                           ipam_subnets=IpamSubnets([ipam1_sn_v4, ipam2_sn_v4]))
        self._vnc_lib.network_ipam_create(ipam)
        logger.debug('Created network ipam')

        ipam = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'default-network-ipam'])
        logger.debug('create default virtual network')
        vn = VirtualNetwork('my-v4-v6-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam3_sn_v4]))
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'with flat-subnet, netowrk can not have user-defined subnet') as e:
            self._vnc_lib.virtual_network_create(vn)

        #add another ipam in VnSubnetType, should fail again
        vn.set_network_ipam(ipam, VnSubnetsType([ipam3_sn_v4, ipam4_sn_v4]))
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'with flat-subnet, netowrk can not have user-defined subnet') as e:
            self._vnc_lib.virtual_network_create(vn)

        logger.debug('create l2 virtual network')
        vn = VirtualNetwork('my-v4-v6-vn', project,
                            virtual_network_properties=VirtualNetworkType(forwarding_mode='l2'))
        vn.add_network_ipam(ipam, VnSubnetsType([]))
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'non l3 only network not allowed with flat-subnet') as e:
            self._vnc_lib.virtual_network_create(vn)

        logger.debug('create l3 virtual network')
        vn = VirtualNetwork('my-v4-v6-vn', project,
                            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
                            address_allocation_mode='user-defined-subnet-preferred')

        vn.add_network_ipam(ipam, VnSubnetsType([]))
        # put a add method to add flag for ip allocation user-only
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        self.assertEqual(net_obj.get_address_allocation_mode(), 'user-defined-subnet-preferred')

        #update network by adding cidr in network_ipam_link, network update should fail
        # as flat-subnet ipam can not have cidrs in the network-ipam link
        vn.set_network_ipam(ipam, VnSubnetsType([ipam3_sn_v4, ipam4_sn_v4]))
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'with flat-subnet, netowrk can not have user-defined subnet') as e:
            self._vnc_lib.virtual_network_update(vn)

        vn.set_network_ipam(ipam, VnSubnetsType([]))
        vn.set_virtual_network_properties(VirtualNetworkType(forwarding_mode='l2'))
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'non l3 only network not allowed with flat-subnet') as e:
            self._vnc_lib.virtual_network_update(vn)

        vn.set_address_allocation_mode('user-defined-subnet-only')
        vn.set_virtual_network_properties(VirtualNetworkType(forwarding_mode='l3'))
        self._vnc_lib.virtual_network_update(vn)

        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        self.assertEqual(net_obj.get_address_allocation_mode(), 'user-defined-subnet-only')

        #allocate ip address to see get only from the link, after exhaustion, resource exhausted
        # exception should come even if address are available in flat-subnet

        # Create v4 Ip objects
        ipv4_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj1.uuid = ipv4_obj1.name
        logger.debug('Created Instance IPv4 object 1 %s', ipv4_obj1.uuid)

        ipv4_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj2.uuid = ipv4_obj2.name
        logger.debug('Created Instance IPv4 object 2 %s', ipv4_obj2.uuid)

        ipv4_obj3 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj3.uuid = ipv4_obj3.name
        logger.debug('Created Instance IPv4 object 3 %s', ipv4_obj3.uuid)

        ipv4_obj4 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj4.uuid = ipv4_obj4.name
        logger.debug('Created Instance IPv4 object 4 %s', ipv4_obj4.uuid)

        ipv4_obj5 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj5.uuid = ipv4_obj5.name
        logger.debug('Created Instance IPv4 object 5 %s', ipv4_obj5.uuid)

        # Create VM
        vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj1.uuid = vm_inst_obj1.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj1)

        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj1, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn)
        ipv4_obj1.set_virtual_machine_interface(port_obj1)
        ipv4_obj1.set_virtual_network(net_obj)

        ipv4_obj2.set_virtual_machine_interface(port_obj1)
        ipv4_obj2.set_virtual_network(net_obj)

        ipv4_obj3.set_virtual_machine_interface(port_obj1)
        ipv4_obj3.set_virtual_network(net_obj)

        ipv4_obj4.set_virtual_machine_interface(port_obj1)
        ipv4_obj4.set_virtual_network(net_obj)

        ipv4_obj5.set_virtual_machine_interface(port_obj1)
        ipv4_obj5.set_virtual_network(net_obj)

        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)

        ipv4_obj1.set_instance_ip_address(None)
        try:
            ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)
        except HttpError:
            pass

        #with ExpectedException(cfgm_common.exceptions.HttpError,
                               #'/Virtual-Network(flat-subnet-domain:flat-subnet-proj:my-v4-v6-vn) has no defined subnet(s)/') as e:
            #ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)

        # update allocation_mode to get ip_addresses from ipam_subnets
        # if possible, but prefer from user-defined-subnet if any avaialble
        # in this case None
        vn.set_address_allocation_mode('user-defined-subnet-preferred')
        #change the allocation-mode to 'user-defined-subnet-preferred' and allocate ip address
        self._vnc_lib.virtual_network_update(vn)

        logger.debug('Allocating an IPV4 address for first VM')
        ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)
        ipv4_obj1 = self._vnc_lib.instance_ip_read(id=ipv4_id1)
        ipv4_addr1 = ipv4_obj1.get_instance_ip_address()
        if ipv4_addr1 != '11.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.8')

        logger.debug('Allocating an IPV4 address for second VM')
        ipv4_id2 = self._vnc_lib.instance_ip_create(ipv4_obj2)
        ipv4_obj2 = self._vnc_lib.instance_ip_read(id=ipv4_id2)
        ipv4_addr2 = ipv4_obj2.get_instance_ip_address()
        if ipv4_addr2 != '11.1.1.4':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.4')

        # try updating ip address vnc_lib should reject ip address update
        ipv4_obj2.set_instance_ip_address('11.1.1.240')

        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Instance IP Address can not be changed') as e:
            self._vnc_lib.instance_ip_update(ipv4_obj2)

        ipv4_obj2.set_instance_ip_address('11.1.1.4')
        ipv4_obj2 = self._vnc_lib.instance_ip_read(id=ipv4_id2)
        ipv4_addr2 = ipv4_obj2.get_instance_ip_address()

        logger.debug('Allocating an IPV4 address for Third VM')
        ipv4_id3 = self._vnc_lib.instance_ip_create(ipv4_obj3)
        ipv4_obj3 = self._vnc_lib.instance_ip_read(id=ipv4_id3)
        ipv4_addr3 = ipv4_obj3.get_instance_ip_address()
        if ipv4_addr3 != '12.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 12.1.1.8')

        logger.debug('Allocating an IPV4 address for Fourth VM')
        ipv4_id4 = self._vnc_lib.instance_ip_create(ipv4_obj4)
        ipv4_obj4 = self._vnc_lib.instance_ip_read(id=ipv4_id4)
        ipv4_addr4 = ipv4_obj4.get_instance_ip_address()
        if ipv4_addr4 != '12.1.1.4':
            logger.debug('Allocation failed, expected v4 IP Address 12.1.1.4')

        logger.debug('Allocating an IPV4 address for Fifth VM')
        try:
            ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)
        except HttpError:
            pass

        #cleanup
        self._vnc_lib.instance_ip_delete(id=ipv4_id1)
        self._vnc_lib.instance_ip_delete(id=ipv4_id2)
        self._vnc_lib.instance_ip_delete(id=ipv4_id3)
        self._vnc_lib.instance_ip_delete(id=ipv4_id4)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)

        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end test_flat_subnet_ipam_user_defined_network

    def test_flat_subnet_ipam_flat_subnet_network(self):
        domain = Domain('flat-subnet-domain')
        self._vnc_lib.domain_create(domain)
        project = Project('flat-subnet-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        ipam1_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 28),
                                     alloc_unit=4)
        ipam2_sn_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 28),
                                     alloc_unit=4)

        # Create NetworkIpam specifying subnet_method as flat-subnet
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="flat-subnet",
                           ipam_subnets=IpamSubnets([ipam1_sn_v4]))
        self._vnc_lib.network_ipam_create(ipam)
        logger.debug('Created network ipam')

        ipam = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

        # try updating ipam_subnet_method from flat-subnet to
        # user-defined-subnet, vnc_lib should reject
        ipam.set_ipam_subnet_method('user-defined-subnet')
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'ipam_subnet_method can not be changed') as e:
            self._vnc_lib.network_ipam_update(ipam)

        # restore flat-subnet as a subnet_method in ipam
        logger.debug('Read network ipam')
        ipam = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'default-network-ipam'])
        logger.debug('create l3 virtual network')
        vn = VirtualNetwork('my-v4-v6-vn', project,
                            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
                            address_allocation_mode='flat-subnet-preferred')
        vn.add_network_ipam(ipam, VnSubnetsType([]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Create v4 Ip objects
        ipv4_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj1.uuid = ipv4_obj1.name
        logger.debug('Created Instance IPv4 object 1 %s', ipv4_obj1.uuid)

        ipv4_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj2.uuid = ipv4_obj2.name
        logger.debug('Created Instance IPv4 object 2 %s', ipv4_obj2.uuid)

        ipv4_obj3 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj3.uuid = ipv4_obj3.name
        logger.debug('Created Instance IPv4 object 3 %s', ipv4_obj3.uuid)

        # Create VM
        vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj1.uuid = vm_inst_obj1.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj1)

        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj1, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn)
        ipv4_obj1.set_virtual_machine_interface(port_obj1)
        ipv4_obj1.set_virtual_network(net_obj)

        ipv4_obj2.set_virtual_machine_interface(port_obj1)
        ipv4_obj2.set_virtual_network(net_obj)

        ipv4_obj3.set_virtual_machine_interface(port_obj1)
        ipv4_obj3.set_virtual_network(net_obj)

        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)

        ipv4_obj1.set_instance_ip_address(None)
        logger.debug('Allocating an IP4 address for first VM')
        ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)
        ipv4_obj1 = self._vnc_lib.instance_ip_read(id=ipv4_id1)
        ipv4_addr1 = ipv4_obj1.get_instance_ip_address()
        logger.debug('  got v4 IP Address for first instance %s', ipv4_addr1)
        if ipv4_addr1 != '11.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.8')

        logger.debug('Allocating an IPV4 address for second VM')
        ipv4_id2 = self._vnc_lib.instance_ip_create(ipv4_obj2)
        ipv4_obj2 = self._vnc_lib.instance_ip_read(id=ipv4_id2)
        ipv4_addr2 = ipv4_obj2.get_instance_ip_address()
        logger.debug('  got v4 IP Address for first instance %s', ipv4_addr2)
        if ipv4_addr2 != '11.1.1.4':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.4')

        #next instance_ip allocation should fail due to ip address exhaustion
        logger.debug('Allocating an IPV4 address for Third VM')
        with ExpectedException(cfgm_common.exceptions.HttpError) as e:
            ipv4_id3 = self._vnc_lib.instance_ip_create(ipv4_obj3)

        # update ipam with additional subnet
        ipam.set_ipam_subnets(IpamSubnets([ipam1_sn_v4, ipam2_sn_v4]))
        self._vnc_lib.network_ipam_update(ipam)
        #read ipam from the vnc_lib
        ipam = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'default-network-ipam'])

        #change allocation_mode to
        vn.set_address_allocation_mode('user-defined-subnet-only')
        self._vnc_lib.virtual_network_update(vn)

        #allocation will still fail as allocation_mode is user-defined-subnet
        try:
            ipv4_id3 = self._vnc_lib.instance_ip_create(ipv4_obj3)
        except HttpError:
            pass

        #restore allocation_mode to flat-subnet-preferred
        vn.set_address_allocation_mode('flat-subnet-preferred')
        self._vnc_lib.virtual_network_update(vn)

        ipv4_id3 = self._vnc_lib.instance_ip_create(ipv4_obj3)
        ipv4_obj3 = self._vnc_lib.instance_ip_read(id=ipv4_id3)
        ipv4_addr3 = ipv4_obj3.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Third instance %s', ipv4_addr3)
        if ipv4_addr3 != '12.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 12.1.1.8')

        #change allocation_mode to "flat-subnet-only"
        vn.set_address_allocation_mode('flat-subnet-preferred')
        self._vnc_lib.virtual_network_update(vn)
 
        logger.debug('Created Instance IPv4 object 2 %s', ipv4_obj2.uuid)
        #delete ipv4_id3 and reassign to see that we should
        # get same ip again. changes in allocation mode should
        # get from flat-subnet only and ip free and realloc should fetch
        # same ip.
        self._vnc_lib.instance_ip_delete(id=ipv4_id3)
        ipv4_obj3 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj3.uuid = ipv4_obj3.name
        logger.debug('Created Instance IPv4 object 3 %s', ipv4_obj3.uuid)
        ipv4_obj3.set_virtual_network(net_obj)
        ipv4_id3 = self._vnc_lib.instance_ip_create(ipv4_obj3)
        ipv4_obj3 = self._vnc_lib.instance_ip_read(id=ipv4_id3)
        ipv4_addr3 = ipv4_obj3.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Third instance %s', ipv4_addr3)
        if ipv4_addr3 != '12.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 12.1.1.8')

        #delete ipv4_id2 and request a specific ip which is already used
        self._vnc_lib.instance_ip_delete(id=ipv4_id2)
        ipv4_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj2.set_instance_ip_address('12.1.1.8')
        ipv4_obj2.uuid = ipv4_obj2.name
        ipv4_obj2.set_virtual_network(net_obj)
        logger.debug('Created Instance IPv4 object 2 %s', ipv4_obj2.uuid)
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            ipv4_id2 = self._vnc_lib.instance_ip_create(ipv4_obj2)

        #now ask for unused ip and allcoation should go through
        ipv4_obj2.set_instance_ip_address('12.1.1.4')
        ipv4_id2 = self._vnc_lib.instance_ip_create(ipv4_obj2)
        ipv4_obj2 = self._vnc_lib.instance_ip_read(id=ipv4_id2)
        ipv4_addr2 = ipv4_obj2.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Third instance %s', ipv4_addr2)
        if ipv4_addr2 != '12.1.1.4':
            logger.debug('Allocation failed, expected v4 IP Address 12.1.1.4')

        #cleanup
        logger.debug('Cleaning up')
        self._vnc_lib.instance_ip_delete(id=ipv4_id1)
        self._vnc_lib.instance_ip_delete(id=ipv4_id2)
        self._vnc_lib.instance_ip_delete(id=ipv4_id3)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)

        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end test_flat_subnet_ipam_flat_subnet_network

    def test_hybrid_subnet_ipam_flat_subnet_network(self):
        domain = Domain('flat-subnet-domain')
        self._vnc_lib.domain_create(domain)
        project = Project('flat-subnet-proj', domain)
        self._vnc_lib.project_create(project)
        ipam1_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 28),
                                     alloc_unit=4)
        ipam2_sn_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 28),
                                     alloc_unit=4)
        ipam3_sn_v4 = IpamSubnetType(subnet=SubnetType('13.1.1.0', 28),
                                     alloc_unit=4)
        ipam4_sn_v4 = IpamSubnetType(subnet=SubnetType('14.1.1.0', 28),
                                     alloc_unit=4)

        # Create NetworkIpams specifying subnet_method as flat-subnet
        # and user-defined-subnet
        ipam1 = NetworkIpam('flat-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="flat-subnet",
                           ipam_subnets=IpamSubnets([ipam1_sn_v4, ipam2_sn_v4]))
        self._vnc_lib.network_ipam_create(ipam1)
        logger.debug('Created network ipam')
        ipam1 = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'flat-ipam'])
        logger.debug('Read network ipam1')

        ipam2 = NetworkIpam('user-defined-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="user-defined-subnet")
        self._vnc_lib.network_ipam_create(ipam2)
        logger.debug('Created network ipam')
        ipam2 = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'user-defined-ipam'])
        logger.debug('Read network ipam2')

        vn = VirtualNetwork('my-v4-v6-vn', project,
                            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
                            address_allocation_mode='flat-subnet-only')

        vn.add_network_ipam(ipam1, VnSubnetsType([]))
        vn.add_network_ipam(ipam2, VnSubnetsType([ipam3_sn_v4, ipam4_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        #delete both network-ipam and add it again and update virtual_network
        vn.del_network_ipam(ipam1)
        vn.del_network_ipam(ipam2)
        self._vnc_lib.virtual_network_update(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # add both ipams again to make a hybird network with
        # one flat-subnet ipam and one user-defined-subnet ipam
        vn.add_network_ipam(ipam1, VnSubnetsType([]))
        vn.add_network_ipam(ipam2, VnSubnetsType([ipam3_sn_v4, ipam4_sn_v4]))
        self._vnc_lib.virtual_network_update(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        #network is hybrid but with allocation-mode is flat-subnet-only
        # ip allcation will happen only from flat-subnet ipams and will get 
        # exhaustion errors once all ip gets exhaused in flat-subnet ipam

        # Create v4 Ip objects
        ipv4_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj1.uuid = ipv4_obj1.name

        ipv4_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj2.uuid = ipv4_obj2.name

        ipv4_obj3 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj3.uuid = ipv4_obj3.name

        ipv4_obj4 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj4.uuid = ipv4_obj4.name

        ipv4_obj5 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj5.uuid = ipv4_obj5.name

        logger.debug('Allocating an IP4 address for first VM')
        ipv4_obj1.set_virtual_network(net_obj)
        ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)
        ipv4_obj1 = self._vnc_lib.instance_ip_read(id=ipv4_id1)
        ipv4_addr1 = ipv4_obj1.get_instance_ip_address()
        logger.debug('  got v4 IP Address for first instance %s', ipv4_addr1)
        if ipv4_addr1 != '11.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.8')

        logger.debug('Allocating an IP4 address for Next VM')
        ipv4_obj2.set_virtual_network(net_obj)
        ipv4_id2 = self._vnc_lib.instance_ip_create(ipv4_obj2)
        ipv4_obj2 = self._vnc_lib.instance_ip_read(id=ipv4_id2)
        ipv4_addr2 = ipv4_obj2.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr2)
        if ipv4_addr2 != '11.1.1.4':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.4')


        logger.debug('Allocating an IP4 address for Next VM')
        ipv4_obj3.set_virtual_network(net_obj)
        ipv4_id3 = self._vnc_lib.instance_ip_create(ipv4_obj3)
        ipv4_obj3 = self._vnc_lib.instance_ip_read(id=ipv4_id3)
        ipv4_addr3 = ipv4_obj3.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr3)
        if ipv4_addr3 != '12.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.8')


        logger.debug('Allocating an IP4 address for Next VM')
        ipv4_obj4.set_virtual_network(net_obj)
        ipv4_id4 = self._vnc_lib.instance_ip_create(ipv4_obj4)
        ipv4_obj4 = self._vnc_lib.instance_ip_read(id=ipv4_id4)
        ipv4_addr4 = ipv4_obj4.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr4)
        if ipv4_addr4 != '12.1.1.4':
            logger.debug('Allocation failed, expected v4 IP Address 12.1.1.4')

        #for a given allocation_mode, all subnet exhausted.
        # we should get exception to ip_alloc_req
        logger.debug('Allocating an IP4 address for Next VM')
        ipv4_obj5.set_virtual_network(net_obj)
        with ExpectedException(cfgm_common.exceptions.HttpError) as e:
            ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)

        #try allocating specific ip, which has been assigned already
	ipv4_obj5.set_instance_ip_address('12.1.1.4')
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)

        # change the allocation_mode to flat-perferred and then
        # we should have ip address allocated from the network-ipam link
        #if such ipam is available
        vn.set_address_allocation_mode('flat-subnet-preferred')
        self._vnc_lib.virtual_network_update(vn)
        ipv4_obj5.set_instance_ip_address(None)
        ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)
        ipv4_obj5 = self._vnc_lib.instance_ip_read(id=ipv4_id5)
        ipv4_addr5 = ipv4_obj5.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr5)
        if ipv4_addr5 != '13.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 13.1.1.8')

        #change the allocation_mode again to 'flat-subnet-only'
        # request a specific ip from ipam-network link
        # ip allocation should go through
        self._vnc_lib.instance_ip_delete(id=ipv4_id5)
        vn.set_address_allocation_mode('flat-subnet-only')
        self._vnc_lib.virtual_network_update(vn)

        ipv4_obj5 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj5.uuid = ipv4_obj5.name
        ipv4_obj5.set_virtual_network(net_obj)
	ipv4_obj5.set_instance_ip_address('13.1.1.8')
        ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)
        ipv4_obj5 = self._vnc_lib.instance_ip_read(id=ipv4_id5)
        ipv4_addr5 = ipv4_obj5.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr5)
        if ipv4_addr5 != '13.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 13.1.1.8')


        #clean up
        self._vnc_lib.instance_ip_delete(id=ipv4_id1)
        self._vnc_lib.instance_ip_delete(id=ipv4_id2)
        self._vnc_lib.instance_ip_delete(id=ipv4_id3)
        self._vnc_lib.instance_ip_delete(id=ipv4_id4)
        self._vnc_lib.instance_ip_delete(id=ipv4_id5)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam1.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam2.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
        #failing in 2-3 attempts
    #end test_hybrid_subnet_ipam_flat_subnet_network

    def test_hybrid_subnet_ipam_user_subnet_network(self):
        domain = Domain('flat-subnet-domain')
        self._vnc_lib.domain_create(domain)
        project = Project('flat-subnet-proj', domain)
        self._vnc_lib.project_create(project)
        ipam1_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 28),
                                     alloc_unit=4)
        ipam2_sn_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 28),
                                     alloc_unit=4)
        ipam3_sn_v4 = IpamSubnetType(subnet=SubnetType('13.1.1.0', 28),
                                     alloc_unit=4)
        ipam4_sn_v4 = IpamSubnetType(subnet=SubnetType('14.1.1.0', 28),
                                     alloc_unit=4)

        # Create NetworkIpams specifying subnet_method as flat-subnet
        # and user-defined-subnet
        ipam1 = NetworkIpam('flat-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="flat-subnet",
                           ipam_subnets=IpamSubnets([ipam1_sn_v4, ipam2_sn_v4]))
        self._vnc_lib.network_ipam_create(ipam1)
        logger.debug('Created network ipam')
        ipam1 = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'flat-ipam'])
        logger.debug('Read network ipam1')

        ipam2 = NetworkIpam('user-defined-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="user-defined-subnet")
        self._vnc_lib.network_ipam_create(ipam2)
        logger.debug('Created network ipam')
        ipam2 = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'user-defined-ipam'])
        logger.debug('Read network ipam2')

        vn = VirtualNetwork('my-v4-v6-vn', project,
                            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
                            address_allocation_mode='user-defined-subnet-only')
        vn.add_network_ipam(ipam1, VnSubnetsType([]))
        vn.add_network_ipam(ipam2, VnSubnetsType([ipam3_sn_v4, ipam4_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #network is hybrid but with allocation-mode is flat-subnet-only
        # ip allcation will happen only from flat-subnet ipams and will get
        # exhaustion errors once all ip gets exhaused in flat-subnet ipam

        # Create v4 Ip objects
        ipv4_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj1.uuid = ipv4_obj1.name

        ipv4_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj2.uuid = ipv4_obj2.name

        ipv4_obj3 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj3.uuid = ipv4_obj3.name

        ipv4_obj4 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj4.uuid = ipv4_obj4.name

        ipv4_obj5 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj5.uuid = ipv4_obj5.name

        logger.debug('Allocating an IP4 address for first VM')
        ipv4_obj1.set_virtual_network(net_obj)
        ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)
        ipv4_obj1 = self._vnc_lib.instance_ip_read(id=ipv4_id1)
        ipv4_addr1 = ipv4_obj1.get_instance_ip_address()
        logger.debug('  got v4 IP Address for first instance %s', ipv4_addr1)
        if ipv4_addr1 != '13.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 13.1.1.8')

        logger.debug('Allocating an IP4 address for Next VM')
        ipv4_obj2.set_virtual_network(net_obj)
        ipv4_id2 = self._vnc_lib.instance_ip_create(ipv4_obj2)
        ipv4_obj2 = self._vnc_lib.instance_ip_read(id=ipv4_id2)
        ipv4_addr2 = ipv4_obj2.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr2)
        if ipv4_addr2 != '13.1.1.4':
            logger.debug('Allocation failed, expected v4 IP Address 13.1.1.4')


        logger.debug('Allocating an IP4 address for Next VM')
        ipv4_obj3.set_virtual_network(net_obj)
        ipv4_id3 = self._vnc_lib.instance_ip_create(ipv4_obj3)
        ipv4_obj3 = self._vnc_lib.instance_ip_read(id=ipv4_id3)
        ipv4_addr3 = ipv4_obj3.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr3)
        if ipv4_addr3 != '14.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 14.1.1.8')


        logger.debug('Allocating an IP4 address for Next VM')
        ipv4_obj4.set_virtual_network(net_obj)
        ipv4_id4 = self._vnc_lib.instance_ip_create(ipv4_obj4)
        ipv4_obj4 = self._vnc_lib.instance_ip_read(id=ipv4_id4)
        ipv4_addr4 = ipv4_obj4.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr4)
        if ipv4_addr4 != '14.1.1.4':
            logger.debug('Allocation failed, expected v4 IP Address 14.1.1.4')

        # for a given allocation_mode, all subnet exhausted.
        # we should get exception to ip_alloc_req
        logger.debug('Allocating an IP4 address for Next VM')
        ipv4_obj5.set_virtual_network(net_obj)
        with ExpectedException(cfgm_common.exceptions.HttpError) as e:
            ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)

        #try allocating specific ip, which has been assigned already
	ipv4_obj5.set_instance_ip_address('14.1.1.8')
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)

        # change the allocation_mode to flat-perferred and then
        # we should have ip address allocated from the network-ipam link
        # if such ipam is available
        vn.set_address_allocation_mode('user-defined-subnet-preferred')
        self._vnc_lib.virtual_network_update(vn)
        ipv4_obj5.set_instance_ip_address(None)
        ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)
        ipv4_obj5 = self._vnc_lib.instance_ip_read(id=ipv4_id5)
        ipv4_addr5 = ipv4_obj5.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr5)
        if ipv4_addr5 != '11.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.8')

        #change the allocation_mode again to 'flat-subnet-only'
        # request a specific ip from ipam-network link
        # ip allocation should go through
        self._vnc_lib.instance_ip_delete(id=ipv4_id5)
        vn.set_address_allocation_mode('user-defined-subnet-only')
        self._vnc_lib.virtual_network_update(vn)

        ipv4_obj5 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj5.uuid = ipv4_obj5.name
        ipv4_obj5.set_virtual_network(net_obj)
	ipv4_obj5.set_instance_ip_address('11.1.1.4')
        ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)
        ipv4_obj5 = self._vnc_lib.instance_ip_read(id=ipv4_id5)
        ipv4_addr5 = ipv4_obj5.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr5)
        if ipv4_addr5 != '11.1.1.4':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.4')


        #clean up
        self._vnc_lib.instance_ip_delete(id=ipv4_id1)
        self._vnc_lib.instance_ip_delete(id=ipv4_id2)
        self._vnc_lib.instance_ip_delete(id=ipv4_id3)
        self._vnc_lib.instance_ip_delete(id=ipv4_id4)
        self._vnc_lib.instance_ip_delete(id=ipv4_id5)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam1.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam2.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end test_hybrid_subnet_ipam_user_subnet_network

    def test_hybrid_subnet_ipam_ask_ip(self):
        domain = Domain('flat-subnet-domain')
        self._vnc_lib.domain_create(domain)
        project = Project('flat-subnet-proj', domain)
        self._vnc_lib.project_create(project)
        ipam1_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 28),
                                     alloc_unit=4)
        ipam2_sn_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 28),
                                     alloc_unit=4)
        ipam3_sn_v4 = IpamSubnetType(subnet=SubnetType('13.1.1.0', 28),
                                     alloc_unit=4)
        ipam4_sn_v4 = IpamSubnetType(subnet=SubnetType('14.1.1.0', 28),
                                     alloc_unit=4)

        # Create NetworkIpams specifying subnet_method as flat-subnet
        # and user-defined-subnet
        ipam1 = NetworkIpam('flat-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="flat-subnet",
                           ipam_subnets=IpamSubnets([ipam1_sn_v4, ipam2_sn_v4]))
        self._vnc_lib.network_ipam_create(ipam1)
        logger.debug('Created network ipam')
        ipam1 = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'flat-ipam'])

        ipam2 = NetworkIpam('user-defined-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="user-defined-subnet")
        self._vnc_lib.network_ipam_create(ipam2)
        logger.debug('Created network ipam')
        ipam2 = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'user-defined-ipam'])
        vn = VirtualNetwork('my-v4-v6-vn', project,
                            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
                            address_allocation_mode='flat-subnet-only')
        vn.add_network_ipam(ipam1, VnSubnetsType([]))
        vn.add_network_ipam(ipam2, VnSubnetsType([ipam3_sn_v4, ipam4_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Create v4 Ip objects
        ipv4_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj1.uuid = ipv4_obj1.name

        ipv4_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj2.uuid = ipv4_obj2.name

        ipv4_obj3 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj3.uuid = ipv4_obj3.name

        ipv4_obj4 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj4.uuid = ipv4_obj4.name

        ipv4_obj5 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj5.uuid = ipv4_obj5.name
        #there are two ipams with different subnet-method and virttual-network 
        #configuration is flat-subnet-only, if instance-ip is created with a
        # specific ip address from subnets defined at ipam, allocation should
        # be sucessful

        logger.debug('Allocating an IP4 address for first VM')
        ipv4_obj1.set_virtual_network(net_obj)
	ipv4_obj1.set_instance_ip_address('13.1.1.8')
        ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)
        ipv4_obj1 = self._vnc_lib.instance_ip_read(id=ipv4_id1)
        ipv4_addr1 = ipv4_obj1.get_instance_ip_address()
        logger.debug('  got v4 IP Address for first instance %s', ipv4_addr1)
        if ipv4_addr1 != '13.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 13.1.1.8')

        #asking for same ip again, without delete, we should get an exception
        # of ip in use
        ipv4_obj2.set_virtual_network(net_obj)
	ipv4_obj2.set_instance_ip_address('13.1.1.8')
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            ipv4_id2 = self._vnc_lib.instance_ip_create(ipv4_obj2)

        #change allocation-mode to flat-subnet-preferred and ask ip addr
        # from cidr with user-defined subnet
        vn.set_address_allocation_mode('flat-subnet-preferred')
        self._vnc_lib.virtual_network_update(vn)

        # free 13.1.1.8 from obj1 and assign it to obj2
        self._vnc_lib.instance_ip_delete(id=ipv4_id1)
        ipv4_id2 = self._vnc_lib.instance_ip_create(ipv4_obj2)
        ipv4_obj2 = self._vnc_lib.instance_ip_read(id=ipv4_id2)
        ipv4_addr2 = ipv4_obj2.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr2)
        if ipv4_addr2 != '13.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 13.1.1.8')

        #change network allocation mode to user-defined
        # and test alloc and delete for specific ip from flat-subnet ipam
        vn.set_address_allocation_mode('user-defined-subnet-only')
        self._vnc_lib.virtual_network_update(vn)

        logger.debug('Allocating an IP4 address for Next VM')
        ipv4_obj3.set_virtual_network(net_obj)
	ipv4_obj3.set_instance_ip_address('11.1.1.8')
        ipv4_id3 = self._vnc_lib.instance_ip_create(ipv4_obj3)
        ipv4_obj3 = self._vnc_lib.instance_ip_read(id=ipv4_id3)
        ipv4_addr3 = ipv4_obj3.get_instance_ip_address()
        logger.debug('  got v4 IP Address for first instance %s', ipv4_addr3)
        if ipv4_addr3 != '11.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.8')

        #asking for same ip again, without delete, we should get an exception
        # of ip in use.
        ipv4_obj4.set_virtual_network(net_obj)
	ipv4_obj4.set_instance_ip_address('11.1.1.8')
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            ipv4_id4 = self._vnc_lib.instance_ip_create(ipv4_obj4)

        #change allocation-mode to user-defined-subnet-preferred and ask ip addr
        # from cidr with user-defined subnet
        vn.set_address_allocation_mode('user-defined--subnet-preferred')
        self._vnc_lib.virtual_network_update(vn)

        # free 11.1.1.8 from obj1 and assign it to obj2
        self._vnc_lib.instance_ip_delete(id=ipv4_id3)
        ipv4_id4 = self._vnc_lib.instance_ip_create(ipv4_obj4)
        ipv4_obj4 = self._vnc_lib.instance_ip_read(id=ipv4_id4)
        ipv4_addr4 = ipv4_obj4.get_instance_ip_address()
        logger.debug('  got v4 IP Address for Next instance %s', ipv4_addr4)
        if ipv4_addr4 != '11.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.8')


        self._vnc_lib.instance_ip_delete(id=ipv4_id2)
        self._vnc_lib.instance_ip_delete(id=ipv4_id4)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam1.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam2.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end test_hybrid_subnet_ipam_ask_ip

    def test_hybrid_subnet_ipam_ip_alloc_from_subnet_uuid(self):
        domain = Domain('flat-subnet-domain')
        self._vnc_lib.domain_create(domain)
        project = Project('flat-subnet-proj', domain)
        self._vnc_lib.project_create(project)
        ipam1_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 28),
                                     alloc_unit=4)
        ipam2_sn_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 28),
                                     alloc_unit=4)
        ipam3_sn_v4 = IpamSubnetType(subnet=SubnetType('13.1.1.0', 28),
                                     alloc_unit=4)
        ipam4_sn_v4 = IpamSubnetType(subnet=SubnetType('14.1.1.0', 28),
                                     alloc_unit=4)

        # Create NetworkIpams specifying subnet_method as flat-subnet
        # and user-defined-subnet 
        ipam1 = NetworkIpam('flat-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="flat-subnet",
                           ipam_subnets=IpamSubnets([ipam1_sn_v4, ipam2_sn_v4]))
        self._vnc_lib.network_ipam_create(ipam1)
        logger.debug('Created network ipam')
        ipam1 = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'flat-ipam'])

        ipam2 = NetworkIpam('user-defined-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="user-defined-subnet")
        self._vnc_lib.network_ipam_create(ipam2)
        logger.debug('Created network ipam')
        ipam2 = self._vnc_lib.network_ipam_read(fq_name=['flat-subnet-domain',
                                                        'flat-subnet-proj',
                                                        'user-defined-ipam'])
        vn = VirtualNetwork('my-v4-v6-vn', project,
                            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'),
                            address_allocation_mode='flat-subnet-only')
        vn.add_network_ipam(ipam1, VnSubnetsType([]))
        vn.add_network_ipam(ipam2, VnSubnetsType([ipam3_sn_v4, ipam4_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Create v4 Ip objects
        ipv4_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj1.uuid = ipv4_obj1.name

        ipv4_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj2.uuid = ipv4_obj2.name

        ipv4_obj3 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj3.uuid = ipv4_obj3.name

        #there are two ipams with different subnet-method and virttual-network
        #configuration is flat-subnet-only, if instance-ip is created with a
        # subnet_uuid from subnets defined for flat-ipam and for
        # user-defined-ipam allocation should  be sucessful
        if (net_obj.network_ipam_refs[0]['to'] == ipam1.fq_name):
            #this is a flat-subnet ipam, take a subnet-uuid from the link
            # and use it for ip_alloc
            flat_subnet_uuid = \
                net_obj.network_ipam_refs[0]['attr'].ipam_subnets[0].subnet_uuid
            subnet_uuid1 = net_obj.network_ipam_refs[1]['attr'].ipam_subnets[0].subnet_uuid
            subnet_uuid2 = net_obj.network_ipam_refs[1]['attr'].ipam_subnets[1].subnet_uuid
        else:
            flat_subnet_uuid = \
                net_obj.network_ipam_refs[1]['attr'].ipam_subnets[0].subnet_uuid
            subnet_uuid1 = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[0].subnet_uuid
            subnet_uuid2 = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[1].subnet_uuid

        # user-define-subnet uuid based allocation
        ipv4_obj1.set_virtual_network(net_obj)
	ipv4_obj1.set_subnet_uuid(str(subnet_uuid1))
        ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)
        ipv4_obj1 = self._vnc_lib.instance_ip_read(id=ipv4_id1)
        ipv4_addr1 = ipv4_obj1.get_instance_ip_address()
        logger.debug('  got v4 IP Address for first instance %s', ipv4_addr1)
        if ipv4_addr1 != '13.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 13.1.1.8')

        ipv4_obj2.set_virtual_network(net_obj)
	ipv4_obj2.set_subnet_uuid(str(subnet_uuid2))
        ipv4_id2 = self._vnc_lib.instance_ip_create(ipv4_obj2)
        ipv4_obj2 = self._vnc_lib.instance_ip_read(id=ipv4_id2)
        ipv4_addr2 = ipv4_obj2.get_instance_ip_address()
        logger.debug('  got v4 IP Address for next instance %s', ipv4_addr2)
        if ipv4_addr2 != '14.1.1.8':
            logger.debug('Allocation failed, expected v4 IP Address 14.1.1.8')

        #flat-subnet uuid based allocation
        ipv4_obj3.set_virtual_network(net_obj)
	ipv4_obj3.set_subnet_uuid(str(flat_subnet_uuid))
        ipv4_id3 = self._vnc_lib.instance_ip_create(ipv4_obj3)
        ipv4_obj3 = self._vnc_lib.instance_ip_read(id=ipv4_id3)
        ipv4_addr3 = ipv4_obj3.get_instance_ip_address()
        #clean up
        self._vnc_lib.instance_ip_delete(id=ipv4_id1)
        self._vnc_lib.instance_ip_delete(id=ipv4_id2)
        self._vnc_lib.instance_ip_delete(id=ipv4_id3)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam1.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam2.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end test_hybrid_subnet_ipam_ip_alloc_from_subnet_uuid

    def test_subnet_alloc_unit(self):
        # Create Domain
        domain = Domain('my-v4-v6-domain')
        self._vnc_lib.domain_create(domain)
        logger.debug('Created domain ')

        # Create Project
        project = Project('my-v4-v6-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        logger.debug('Created network ipam')

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-v4-v6-domain',
                                                        'my-v4-v6-proj',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

        # create ipv4 subnet with alloc_unit not power of 2
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24),
                                    alloc_unit=3)
        vn = VirtualNetwork('my-v4-v6-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4]))
        try:
            self._vnc_lib.virtual_network_create(vn)
        except HttpError:
            logger.debug('alloc-unit is not power of 2')
            pass

        vn.del_network_ipam(ipam)
        # create ipv6 subnet with alloc_unit not power of 2
        ipam_sn_v6 = IpamSubnetType(subnet=SubnetType('fd14::', 120),
                                    alloc_unit=3)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v6]))

        try:
            self._vnc_lib.virtual_network_create(vn)
        except HttpError:
            logger.debug('alloc-unit is not power of 2')
            pass

        vn.del_network_ipam(ipam)
        # Create subnets
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24),
                                    alloc_unit=4)
        ipam_sn_v6 = IpamSubnetType(subnet=SubnetType('fd14::', 120),
                                    alloc_unit=4)

        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4, ipam_sn_v6]))
        self._vnc_lib.virtual_network_create(vn)
        logger.debug('Created Virtual Network object %s', vn.uuid)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Create v4 Ip objects
        ipv4_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj1.uuid = ipv4_obj1.name
        logger.debug('Created Instance IPv4 object 1 %s', ipv4_obj1.uuid)

        ipv4_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ipv4_obj2.uuid = ipv4_obj2.name
        logger.debug('Created Instance IPv4 object 2 %s', ipv4_obj2.uuid)

        # Create v6 Ip object
        ipv6_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v6')
        ipv6_obj1.uuid = ipv6_obj1.name
        logger.debug('Created Instance IPv6 object 2 %s', ipv6_obj1.uuid)

        ipv6_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v6')
        ipv6_obj2.uuid = ipv6_obj2.name
        logger.debug('Created Instance IPv6 object 2 %s', ipv6_obj2.uuid)

        # Create VM
        vm_inst_obj1 = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj1.uuid = vm_inst_obj1.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj1)

        id_perms = IdPermsType(enable=True)
        port_obj1 = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj1, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name
        port_obj1.set_virtual_network(vn)
        ipv4_obj1.set_virtual_machine_interface(port_obj1)
        ipv4_obj1.set_virtual_network(net_obj)
        ipv4_obj2.set_virtual_machine_interface(port_obj1)
        ipv4_obj2.set_virtual_network(net_obj)

        ipv6_obj1.set_virtual_machine_interface(port_obj1)
        ipv6_obj1.set_virtual_network(net_obj)
        ipv6_obj2.set_virtual_machine_interface(port_obj1)
        ipv6_obj2.set_virtual_network(net_obj)

        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)

        logger.debug('Wrong ip address request,not aligned with alloc-unit')
        ipv4_obj1.set_instance_ip_address('11.1.1.249') 
        try:
            ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)
        except HttpError:
            logger.debug('requested ipaddr is not aligned with alloc-unit')
            pass
         
        ipv4_obj1.set_instance_ip_address(None) 
        logger.debug('Allocating an IP4 address for first VM')
        ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)
        ipv4_obj1 = self._vnc_lib.instance_ip_read(id=ipv4_id1)
        ipv4_addr1 = ipv4_obj1.get_instance_ip_address()
        logger.debug('  got v4 IP Address for first instance %s', ipv4_addr1)
        if ipv4_addr1 != '11.1.1.248':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.248')

        logger.debug('Allocating an IPV4 address for second VM')
        ipv4_id2 = self._vnc_lib.instance_ip_create(ipv4_obj2)
        ipv4_obj2 = self._vnc_lib.instance_ip_read(id=ipv4_id2)
        ipv4_addr2 = ipv4_obj2.get_instance_ip_address()
        logger.debug('  got v6 IP Address for first instance %s', ipv4_addr2)
        if ipv4_addr2 != '11.1.1.244':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.244')

        logger.debug('Allocating an IP6 address for first VM')
        ipv6_id1 = self._vnc_lib.instance_ip_create(ipv6_obj1)
        ipv6_obj1 = self._vnc_lib.instance_ip_read(id=ipv6_id1)
        ipv6_addr1 = ipv6_obj1.get_instance_ip_address()
        logger.debug('  got v6 IP Address for first instance %s', ipv6_addr1)
        if ipv6_addr1 != 'fd14::f8':
            logger.debug('Allocation failed, expected v6 IP Address fd14::f8')

        logger.debug('Allocating an IP6 address for second VM')
        ipv6_id2 = self._vnc_lib.instance_ip_create(ipv6_obj2)
        ipv6_obj2 = self._vnc_lib.instance_ip_read(id=ipv6_id2)
        ipv6_addr2 = ipv6_obj2.get_instance_ip_address()
        logger.debug('  got v6 IP Address for first instance %s', ipv6_addr2)
        if ipv6_addr2 != 'fd14::f4':
            logger.debug('Allocation failed, expected v6 IP Address fd14::f4')

        #cleanup
        logger.debug('Cleaning up')
        self._vnc_lib.instance_ip_delete(id=ipv4_id1)
        self._vnc_lib.instance_ip_delete(id=ipv4_id2)
        self._vnc_lib.instance_ip_delete(id=ipv6_id1)
        self._vnc_lib.instance_ip_delete(id=ipv6_id2)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_ip_alloction(self):
        # Create Domain
        domain = Domain('my-v4-v6-domain')
        self._vnc_lib.domain_create(domain)
        logger.debug('Created domain ')

        # Create Project
        project = Project('my-v4-v6-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        logger.debug('Created network ipam')

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-v4-v6-domain', 'my-v4-v6-proj',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

        # Create subnets
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        ipam_sn_v6 = IpamSubnetType(subnet=SubnetType('fd14::', 120))

        # Create VN
        vn = VirtualNetwork('my-v4-v6-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4, ipam_sn_v6]))
        self._vnc_lib.virtual_network_create(vn)
        logger.debug('Created Virtual Network object %s', vn.uuid)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Create v4 Ip object
        ip_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ip_obj1.uuid = ip_obj1.name
        logger.debug('Created Instance IP object 1 %s', ip_obj1.uuid)

        # Create v6 Ip object
        ip_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v6')
        ip_obj2.uuid = ip_obj2.name
        logger.debug('Created Instance IP object 2 %s', ip_obj2.uuid)

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

        logger.debug('Allocating an IP4 address for first VM')
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
        logger.debug('  got v4 IP Address for first instance %s', ip_addr1)
        if ip_addr1 != '11.1.1.253':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.253')

        logger.debug('Allocating an IP6 address for first VM')
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)
        ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id2)
        ip_addr2 = ip_obj2.get_instance_ip_address()
        logger.debug('  got v6 IP Address for first instance %s', ip_addr2)
        if ip_addr2 != 'fd14::fd':
            logger.debug('Allocation failed, expected v6 IP Address fd14::fd')

        # Read gateway ip address 
        logger.debug('Read default gateway ip address' )
        ipam_refs = net_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                logger.debug('Gateway for subnet (%s/%s) is (%s)' %(subnet.subnet.get_ip_prefix(), 
                        subnet.subnet.get_ip_prefix_len(),
                        subnet.get_default_gateway()))


        #cleanup
        logger.debug('Cleaning up')
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
        logger.debug('Created domain ')

        # Create Project
        project = Project('my-v4-v6-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        logger.debug('Created network ipam')

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-v4-v6-domain', 'my-v4-v6-proj',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

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
        logger.debug('Created Virtual Network object %s', vn.uuid)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Create v4 Ip object
        ip_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ip_obj1.uuid = ip_obj1.name
        logger.debug('Created Instance IP object 1 %s', ip_obj1.uuid)

        # Create v6 Ip object
        ip_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v6')
        ip_obj2.uuid = ip_obj2.name
        logger.debug('Created Instance IP object 2 %s', ip_obj2.uuid)

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

        logger.debug('Allocating an IP4 address for first VM')
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
        logger.debug('got v4 IP Address for first instance %s', ip_addr1)
        if ip_addr1 != '11.1.1.20':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.20')
        
        logger.debug('Allocating an IP6 address for first VM')
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)
        ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id2)
        ip_addr2 = ip_obj2.get_instance_ip_address()
        logger.debug('got v6 IP Address for first instance %s', ip_addr2)
        if ip_addr2 != 'fd14::30':
            logger.debug('Allocation failed, expected v6 IP Address fd14::30')

        # Read gateway ip address 
        logger.debug('Read default gateway ip address')
        ipam_refs = net_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                logger.debug('Gateway for subnet (%s/%s) is (%s)' %(subnet.subnet.get_ip_prefix(), 
                        subnet.subnet.get_ip_prefix_len(),
                        subnet.get_default_gateway()))


        #cleanup
        logger.debug('Cleaning up')
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
        logger.debug('Created domain ')

        # Create Project
        project = Project('my-v4-v6-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        logger.debug('Created network ipam')

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-v4-v6-domain', 'my-v4-v6-proj',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

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
        logger.debug('Created Virtual Network object %s', vn.uuid)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Read gateway ip address 
        logger.debug('Read default gateway ip address')
        ipam_refs = net_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                logger.debug('Gateway for subnet (%s/%s) is (%s)' %(subnet.subnet.get_ip_prefix(), 
                        subnet.subnet.get_ip_prefix_len(),
                        subnet.get_default_gateway()))
                if subnet.subnet.get_ip_prefix() == '11.1.1.0':
                    if subnet.get_default_gateway() != '11.1.1.1':
                        logger.debug(' Failure, expected gateway ip address 11.1.1.1')
                if subnet.subnet.get_ip_prefix() == 'fd14::':
                    if subnet.get_default_gateway() != 'fd14::1':
                        logger.debug(' Failure, expected gateway ip address fd14::1')


        #cleanup
        logger.debug('Cleaning up')
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_bulk_ip_alloc_free(self):
        # Create Domain
        domain = Domain('v4-domain')
        self._vnc_lib.domain_create(domain)
        logger.debug('Created domain ')

        # Create Project
        project = Project('v4-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        logger.debug('Created network ipam')
        ipam = self._vnc_lib.network_ipam_read(fq_name=['v4-domain', 'v4-proj',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

        # Create subnets
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create VN
        vn = VirtualNetwork('v4-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        logger.debug('Created Virtual Network object %s', vn.uuid)
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
        
        logger.debug('Verify bulk ip address allocation')
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
        logger.debug('Verified bulk ip free')

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
        logger.debug('Created domain ')

        # Create Project
        project = Project('v4-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        logger.debug('Created network ipam')

        ipam = self._vnc_lib.network_ipam_read(fq_name=['v4-domain', 'v4-proj',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

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
            logger.debug('ip address alloc list: %s', ip_addr_list[0:total_addr])
            # Create VN
            vn = VirtualNetwork('v4-vn', project)
            vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4]))
            self._vnc_lib.virtual_network_create(vn)
            logger.debug('Created Virtual Network object %s', vn.uuid)
            net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

            # Create v4 Ip object for all possible addresses in alloc_pool
            v4_ip_obj_list = []

            for idx, val in enumerate(ip_addr_list):
                v4_ip_obj_list.append(
                    InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4'))
                v4_ip_obj_list[idx].uuid = v4_ip_obj_list[idx].name
                logger.debug('Created Instance IP object %s %s',idx, v4_ip_obj_list[idx].uuid)

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
            logger.debug('Allocating an IP4 address for VMs')
            for idx, val in enumerate(ip_addr_list):
                ip_ids.append(
                    self._vnc_lib.instance_ip_create(v4_ip_obj_list[idx]))
                v4_ip_obj_list[idx] = self._vnc_lib.instance_ip_read(
                                          id=ip_ids[idx])
                ip_addr = v4_ip_obj_list[idx].get_instance_ip_address()
                logger.debug('got v4 IP Address for instance %s:%s', idx, ip_addr)
                if ip_addr != ip_addr_list[idx]:
                    logger.debug('Allocation failed, expected v4 IP Address: %s', ip_addr_list[idx])

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
                logger.debug('Delete Instances %s %s', to_modify[0], to_modify[1])
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
                    logger.debug('Deleted instance %s', val)

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
                    logger.debug('Created instance %s',val)

                # Allocate IPs to modified VMs
                for idx, val in enumerate(to_modify):
                    ip_ids[val] = self._vnc_lib.instance_ip_create(v4_ip_obj_list[val])
                    v4_ip_obj_list[val] = self._vnc_lib.instance_ip_read(
                        id=ip_ids[val])
                    ip_addr = v4_ip_obj_list[val].get_instance_ip_address()
                    logger.debug('got v4 IP Address for instance %s:%s', val, ip_addr)
                    if ip_addr != ip_addr_list[val]:
                        logger.debug('Allocation failed, expected v4 IP Address: %s', ip_addr_list[val])

            # negative test.
            # Create a new VM and try getting a new instance_ip
            # we should get an exception as alloc_pool is fully exhausted.

            logger.debug('Negative Test to create extra instance and try assigning IP address')
            # Create v4 Ip object
            ip_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
            ip_obj1.uuid = ip_obj1.name
            logger.debug('Created new Instance IP object %s', ip_obj1.uuid)

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
            logger.debug('Created extra instance')

            logger.debug('Allocating an IP4 address for extra instance')
            try:
                ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
            except HttpError:
                logger.debug('alloc pool is exhausted')
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
                logger.debug('Created new Instance IP object %s', ip_obj2.uuid)

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
        logger.debug('Cleaning up')
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_req_ip_allocation(self):
        # Create Domain
        domain = Domain('my-v4-v6-req-ip-domain')
        self._vnc_lib.domain_create(domain)
        logger.debug('Created domain ')

        # Create Project
        project = Project('my-v4-v6-req-ip-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        logger.debug('Created network ipam')

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-v4-v6-req-ip-domain',
                                                        'my-v4-v6-req-ip-proj',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

        # Create subnets
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        ipam_sn_v6 = IpamSubnetType(subnet=SubnetType('fd14::', 120))

        # Create VN
        vn = VirtualNetwork('my-v4-v6-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4, ipam_sn_v6]))
        self._vnc_lib.virtual_network_create(vn)
        logger.debug('Created Virtual Network object %s', vn.uuid)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Create v4 Ip object, with v4 requested ip
        ip_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_address='11.1.1.4',
                             instance_ip_family='v4')
        ip_obj1.uuid = ip_obj1.name
        logger.debug('Created Instance IP object 1 %s', ip_obj1.uuid)

        # Create v6 Ip object with v6 requested ip
        ip_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_address='fd14::4',
                             instance_ip_family='v6')
        ip_obj2.uuid = ip_obj2.name
        logger.debug('Created Instance IP object 2 %s', ip_obj2.uuid)

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

        logger.debug('Allocating an IP4 address for first VM')
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
        logger.debug('  got v4 IP Address for first instance %s', ip_addr1)
        if ip_addr1 != '11.1.1.4':
            logger.debug('Allocation failed, expected v4 IP Address 11.1.1.4')

        logger.debug('Allocating an IP6 address for first VM')
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)
        ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id2)
        ip_addr2 = ip_obj2.get_instance_ip_address()
        logger.debug('  got v6 IP Address for first instance %s', ip_addr2)
        if ip_addr2 != 'fd14::4':
            logger.debug('Allocation failed, expected v6 IP Address fd14::4')

        # Read gateway ip address
        logger.debug('Read default gateway ip address')
        ipam_refs = net_obj.get_network_ipam_refs()
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets()
            for subnet in subnets:
                logger.debug('Gateway for subnet (%s/%s) is (%s)' %(subnet.subnet.get_ip_prefix(),
                        subnet.subnet.get_ip_prefix_len(),
                        subnet.get_default_gateway()))


        #cleanup
        logger.debug('Cleaning up')
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
 
    def test_ip_alloc_clash(self):
        # prep objects for testing
        proj_obj = Project('proj-%s' %(self.id()), parent_obj=Domain())
        self._vnc_lib.project_create(proj_obj)

        ipam_obj = NetworkIpam('ipam-%s' %(self.id()), proj_obj)
        self._vnc_lib.network_ipam_create(ipam_obj)

        vn_obj = VirtualNetwork('vn-%s' %(self.id()), proj_obj)
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn_obj)

        fip_pool_obj = FloatingIpPool(
            'fip-pool-%s' %(self.id()), parent_obj=vn_obj)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)

        aip_pool_obj = AliasIpPool(
            'aip-pool-%s' %(self.id()), parent_obj=vn_obj)
        self._vnc_lib.alias_ip_pool_create(aip_pool_obj)

        iip_obj = InstanceIp('existing-iip-%s' %(self.id()))
        iip_obj.add_virtual_network(vn_obj)
        self._vnc_lib.instance_ip_create(iip_obj)
        # read-in to find allocated address
        iip_obj = self._vnc_lib.instance_ip_read(id=iip_obj.uuid)

        fip_obj = FloatingIp('existing-fip-%s' %(self.id()), fip_pool_obj)
        fip_obj.add_project(proj_obj)
        self._vnc_lib.floating_ip_create(fip_obj)
        # read-in to find allocated address
        fip_obj = self._vnc_lib.floating_ip_read(id=fip_obj.uuid)

        aip_obj = AliasIp('existing-aip-%s' %(self.id()), aip_pool_obj)
        aip_obj.add_project(proj_obj)
        self._vnc_lib.alias_ip_create(aip_obj)
        # read-in to find allocated address
        aip_obj = self._vnc_lib.alias_ip_read(id=aip_obj.uuid)

        vm_obj = VirtualMachine('vm-%s' %(self.id()))
        self._vnc_lib.virtual_machine_create(vm_obj)

        vm_vmi_obj = VirtualMachineInterface('vm-vmi-%s' %(self.id()),
                                              proj_obj)
        vm_vmi_obj.add_virtual_network(vn_obj)
        vm_vmi_obj.add_virtual_machine(vm_obj)
        self._vnc_lib.virtual_machine_interface_create(vm_vmi_obj)

        rtr_vmi_obj = VirtualMachineInterface('rtr-vmi-%s' %(self.id()),
                                              proj_obj)
        rtr_vmi_obj.add_virtual_network(vn_obj)
        self._vnc_lib.virtual_machine_interface_create(rtr_vmi_obj)
        lr_obj = LogicalRouter('rtr-%s' %(self.id()), proj_obj)
        lr_obj.add_virtual_machine_interface(rtr_vmi_obj)
        self._vnc_lib.logical_router_create(lr_obj)

        isolated_vmi_obj = VirtualMachineInterface('iso-vmi-%s' %(self.id()),
                                              proj_obj)
        isolated_vmi_obj.add_virtual_network(vn_obj)
        self._vnc_lib.virtual_machine_interface_create(isolated_vmi_obj)

        # allocate instance-ip clashing with existing instance-ip
        iip2_obj = InstanceIp('clashing-iip-%s' %(self.id()),
                              instance_ip_address=iip_obj.instance_ip_address)
        iip2_obj.add_virtual_network(vn_obj)
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            self._vnc_lib.instance_ip_create(iip2_obj)
        
        # allocate instance-ip clashing with existing floating-ip
        iip2_obj.set_instance_ip_address(fip_obj.floating_ip_address)
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            self._vnc_lib.instance_ip_create(iip2_obj)

        # allocate floating-ip clashing with existing floating-ip
        fip2_obj = FloatingIp('clashing-fip-%s' %(self.id()), fip_pool_obj,
                              floating_ip_address=fip_obj.floating_ip_address)
        fip2_obj.add_project(proj_obj)
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            self._vnc_lib.floating_ip_create(fip2_obj)

        # allocate alias-ip clashing with existing alias-ip
        aip2_obj = AliasIp('clashing-aip-%s' %(self.id()), aip_pool_obj,
                           alias_ip_address=aip_obj.alias_ip_address)
        aip2_obj.add_project(proj_obj)
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            self._vnc_lib.alias_ip_create(aip2_obj)

        # allocate floating-ip clashing with existing instance-ip
        fip2_obj.set_floating_ip_address(iip_obj.instance_ip_address)
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            self._vnc_lib.floating_ip_create(fip2_obj)

        # allocate alias-ip clashing with existing instance-ip
        aip2_obj.set_alias_ip_address(iip_obj.instance_ip_address)
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            self._vnc_lib.alias_ip_create(aip2_obj)

        # allocate alias-ip clashing with existing floating-ip
        aip2_obj.set_alias_ip_address(fip_obj.floating_ip_address)
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            self._vnc_lib.alias_ip_create(aip2_obj)

        # allocate floating-ip with gateway ip and verify failure
        fip2_obj.set_floating_ip_address('11.1.1.254')
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            self._vnc_lib.floating_ip_create(fip2_obj)

        # allocate alias-ip with gateway ip and verify failure
        aip2_obj.set_alias_ip_address('11.1.1.254')
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                               'Ip address already in use') as e:
            self._vnc_lib.alias_ip_create(aip2_obj)

        # allocate 2 instance-ip with gateway ip - should work
        # then verify iip cannot # ref to vm port (during iip-update
        # or vmi-update)
        iip_gw_ip = InstanceIp('gw-ip-iip-%s' %(self.id()),
                               instance_ip_address='11.1.1.254')
        iip_gw_ip.add_virtual_network(vn_obj)
        self._vnc_lib.instance_ip_create(iip_gw_ip)
        iip2_gw_ip = InstanceIp('gw-ip-iip2-%s' %(self.id()),
                               instance_ip_address='11.1.1.254')
        iip2_gw_ip.add_virtual_network(vn_obj)
        iip2_gw_ip.add_virtual_machine_interface(rtr_vmi_obj)
        self._vnc_lib.instance_ip_create(iip2_gw_ip)

        iip_gw_ip.add_virtual_machine_interface(vm_vmi_obj)
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                 'Gateway IP cannot be used by VM port') as e:
            self._vnc_lib.instance_ip_update(iip_gw_ip)

        iip_gw_ip.del_virtual_machine_interface(vm_vmi_obj)
        iip_gw_ip.add_virtual_machine_interface(rtr_vmi_obj)
        self._vnc_lib.instance_ip_update(iip_gw_ip)

        iip2_gw_ip.add_virtual_machine_interface(isolated_vmi_obj)
        self._vnc_lib.instance_ip_update(iip2_gw_ip)

        isolated_vmi_obj.add_virtual_machine(vm_obj)
        with ExpectedException(cfgm_common.exceptions.PermissionDenied,
                 'Gateway IP cannot be used by VM port') as e:
            self._vnc_lib.virtual_machine_interface_update(
                isolated_vmi_obj)
    # end test_ip_alloc_clash

#end class TestIpAlloc

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    unittest.main()
