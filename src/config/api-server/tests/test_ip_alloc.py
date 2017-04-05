#
# Copyright (c) 2013,2014 Juniper Networks, Inc. All rights reserved.
#
import gevent
import gevent.monkey
gevent.monkey.patch_all()
import os
import sys
import socket
import errno
import uuid
import logging
import coverage
import ipaddress

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

from vnc_api.vnc_api import *
import vnc_api.gen.vnc_api_test_gen
from vnc_api.gen.resource_test import *
import cfgm_common
from cfgm_common import vnc_cgitb
vnc_cgitb.enable(format='text')

sys.path.append('../common/tests')
from test_utils import *
import test_common
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestIpAlloc(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestIpAlloc, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestIpAlloc, cls).tearDownClass(*args, **kwargs)

    def test_subnet_overlap(self):
        project = Project('v4-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)

        ipam0_v4 = IpamSubnetType(subnet=SubnetType('10.1.2.0', 23))
        ipam0_v4_overlap = IpamSubnetType(subnet=SubnetType('10.1.3.248', 28))

        ipam1_v4 = IpamSubnetType(subnet=SubnetType('11.1.2.0', 23))
        ipam1_v4_overlap = IpamSubnetType(subnet=SubnetType('11.1.3.248', 28))

        ipam2_v4 = IpamSubnetType(subnet=SubnetType('12.1.2.0', 23))
        ipam2_v4_overlap = IpamSubnetType(subnet=SubnetType('12.1.3.248', 28))

        ipam3_v4 = IpamSubnetType(subnet=SubnetType('13.1.2.0', 23))
        ipam3_v4_overlap = IpamSubnetType(subnet=SubnetType('13.1.3.248', 28))

        ipam4_v4 = IpamSubnetType(subnet=SubnetType('14.1.2.0', 23))
        ipam4_v4_overlap = IpamSubnetType(subnet=SubnetType('14.1.3.248', 28))

        ipam5_v4 = IpamSubnetType(subnet=SubnetType('15.1.2.0', 23))
        ipam5_v4_overlap = IpamSubnetType(subnet=SubnetType('15.1.3.248', 28))

        #create four ipams with different subnet methods
        #(none, user-defined and two flat)
        #ipam0 without subnet-method (default is user-defined-subnet)
        ipam0 = NetworkIpam('ipam0', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam0)
        ipam0_uuid=ipam0.uuid

        #ipam1 with user-defined-subnet
        ipam1 = NetworkIpam('ipam1', project, IpamType("dhcp"),
                            ipam_subnet_method="user-defined-subnet")
        self._vnc_lib.network_ipam_create(ipam1)
        ipam1_uuid=ipam1.uuid

        #ipam2 with flat-subnet
        ipam2 = NetworkIpam('ipam2', project, IpamType("dhcp"),
                            ipam_subnet_method="flat-subnet")
        self._vnc_lib.network_ipam_create(ipam2)
        ipam2_uuid=ipam2.uuid

        #ipam3 with flat-subnet
        ipam3 = NetworkIpam('ipam3', project, IpamType("dhcp"),
                            ipam_subnet_method="flat-subnet")
        self._vnc_lib.network_ipam_create(ipam3)
        ipam3_uuid=ipam3.uuid

        #try adding subnets in ipam0, it should fail
        ipam0.set_ipam_subnets(IpamSubnets([ipam0_v4, ipam3_v4]))
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'ipam-subnets are allowed only with flat-subnet') as e:
            self._vnc_lib.network_ipam_update(ipam0)
        ipam0 = self._vnc_lib.network_ipam_read(id=ipam0_uuid)

        #try adding subnets in ipam1, it should fail
        ipam1.set_ipam_subnets(IpamSubnets([ipam1_v4, ipam2_v4]))
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'ipam-subnets are allowed only with flat-subnet') as e:
            self._vnc_lib.network_ipam_update(ipam1)
        ipam1 = self._vnc_lib.network_ipam_read(id=ipam1_uuid)

        #try adding overlap subnets in ipam2, it should fail
        ipam2.set_ipam_subnets(IpamSubnets([ipam2_v4, ipam2_v4_overlap]))
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'Overlapping addresses: \[IPNetwork\(\'12.1.2.0/23\'\), IPNetwork\(\'12.1.3.248/28\'\)\]') as e:
            self._vnc_lib.network_ipam_update(ipam2)
        ipam2 = self._vnc_lib.network_ipam_read(id=ipam2_uuid)

        #add non-overlap subnets in ipam2
        ipam2.set_ipam_subnets(IpamSubnets([ipam2_v4, ipam4_v4]))
        self._vnc_lib.network_ipam_update(ipam2)
        ipam2 = self._vnc_lib.network_ipam_read(id=ipam2_uuid)

        #create vn with ipam0  and with overlapping subnets on vn->ipam0 link
        #create should fail
        vn = VirtualNetwork('vn0', project)
        vn.add_network_ipam(ipam0, VnSubnetsType([ipam0_v4, ipam0_v4_overlap]))
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'Overlapping addresses: \[IPNetwork\(\'10.1.2.0/23\'\), IPNetwork\(\'10.1.3.248/28\'\)\]') as e:
            self._vnc_lib.virtual_network_create(vn)

        #create vn with ipam0 with non-overlapping subnets on vn->ipam link
        vn.add_network_ipam(ipam0, VnSubnetsType([ipam0_v4, ipam3_v4]))
        self._vnc_lib.virtual_network_create(vn)
        vn = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #add ipam1 with overlapping subnets on vn->ipam1 link
        #update network should fail
        vn.add_network_ipam(ipam1, VnSubnetsType([ipam1_v4, ipam1_v4_overlap]))
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'Overlapping addresses: \[IPNetwork\(\'11.1.2.0/23\'\), IPNetwork\(\'11.1.3.248/28\'\)\]') as e:
            self._vnc_lib.virtual_network_update(vn)
        vn = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #change overlapping subnet with another overlapping but overlapping
        #with ipam0
        vn.add_network_ipam(ipam1, VnSubnetsType([ipam1_v4, ipam0_v4_overlap]))

        with ExpectedException(cfgm_common.exceptions.BadRequest) as e:
            self._vnc_lib.virtual_network_update(vn)
        vn = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #change subnets on vn->ipam1 link to make it non-overlap within this
        #link and non overlap with vn->ipam0 link
        vn.add_network_ipam(ipam1, VnSubnetsType([ipam1_v4, ipam5_v4]))
        self._vnc_lib.virtual_network_update(vn)
        vn = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #add ipam2 in the network without any subnets on the link
        vn.add_network_ipam(ipam2, VnSubnetsType([]))
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'flat-subnet is allowed only with l3 network') as e:
            self._vnc_lib.virtual_network_update(vn)
        vn = self._vnc_lib.virtual_network_read(id = vn.uuid)

        vn.set_virtual_network_properties(VirtualNetworkType(forwarding_mode='l3'))
        self._vnc_lib.virtual_network_update(vn)
        vn = self._vnc_lib.virtual_network_read(id = vn.uuid)

        vn.add_network_ipam(ipam2, VnSubnetsType([]))
        self._vnc_lib.virtual_network_update(vn)
        vn = self._vnc_lib.virtual_network_read(id = vn.uuid)

        vn.add_network_ipam(ipam3, VnSubnetsType([]))
        self._vnc_lib.virtual_network_update(vn)
        vn = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #Now update ipam2 with overlapping subnet with a subnet on vn->ipam0
        #link (overlapping with ipam0_v4)
        ipam2.set_ipam_subnets(IpamSubnets([ipam2_v4, ipam4_v4, ipam0_v4_overlap]))
        with ExpectedException(cfgm_common.exceptions.BadRequest) as e:
            self._vnc_lib.network_ipam_update(ipam2)
        vn = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #Now update ipam3 with overlapping subnet with a subnet in ipam2
        # which is a flat subnet (overlapping with ipam2_v4)
        ipam3.set_ipam_subnets(IpamSubnets([ipam2_v4_overlap]))
        with ExpectedException(cfgm_common.exceptions.BadRequest) as e:
            self._vnc_lib.network_ipam_update(ipam3)
        vn = self._vnc_lib.virtual_network_read(id = vn.uuid)

        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam0.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam1.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam2.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam3.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
    #end test_subnet_overlap

    def test_subnet_quota(self):
        # Create Project
        project = Project('v4-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)

        ipam1_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 28))
        ipam2_sn_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 28))
        ipam3_sn_v4 = IpamSubnetType(subnet=SubnetType('13.1.1.0', 28))
        ipam4_sn_v4 = IpamSubnetType(subnet=SubnetType('14.1.1.0', 28))

        #create two ipams
        ipam1 = NetworkIpam('ipam1', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam1)

        ipam2 = NetworkIpam('ipam2', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam2)

        #create virtual network with unlimited subnet quota without any subnets
        vn = VirtualNetwork('my-vn', project)
        vn.add_network_ipam(ipam1, VnSubnetsType([]))
        vn.add_network_ipam(ipam2, VnSubnetsType([]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        #inspect net_obj to make sure we have 0 cidrs
        ipam_refs = net_obj.__dict__.get('network_ipam_refs', [])

        def _get_total_subnets_count(ipam_refs):
            subnet_count = 0
            for ipam_ref in ipam_refs:
                vnsn_data = ipam_ref['attr'].__dict__
                ipam_subnets = vnsn_data.get('ipam_subnets', [])
                for ipam_subnet in ipam_subnets:
                    subnet_dict = ipam_subnet.__dict__.get('subnet', {})
                    if 'ip_prefix' in subnet_dict.__dict__:
                        subnet_count += 1
            return subnet_count

        total_subnets = _get_total_subnets_count(ipam_refs)
        if total_subnets:
            raise Exception("No Subnets expected in Virtual Network")
        self._vnc_lib.virtual_network_delete(id=vn.uuid)

        #keep subnet quota unlimited and have 4 cidrs in two ipams
        vn = VirtualNetwork('my-vn', project)
        vn.add_network_ipam(ipam1, VnSubnetsType([ipam1_sn_v4, ipam3_sn_v4]))
        vn.add_network_ipam(ipam2, VnSubnetsType([ipam2_sn_v4, ipam4_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        #inspect net_obj to make sure we have 4 cidrs
        ipam_refs = net_obj.__dict__.get('network_ipam_refs', [])
        total_subnets = _get_total_subnets_count(ipam_refs)
        if total_subnets != 4:
            raise Exception("4 Subnets expected in Virtual Network")

        #Delete vn and create new one with a subnet quota of 1
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        quota_type = QuotaType()
        quota_type.set_subnet(1)
        project.set_quota(quota_type)
        self._vnc_lib.project_update(project)

        vn = VirtualNetwork('my-new-vn', project)
        vn.add_network_ipam(ipam1, VnSubnetsType([ipam1_sn_v4]))
        vn.add_network_ipam(ipam2, VnSubnetsType([ipam2_sn_v4]))
        with ExpectedException(cfgm_common.exceptions.OverQuota):
            self._vnc_lib.virtual_network_create(vn)
	#increase subnet quota to 2, and network_create will go through..
        quota_type.set_subnet(2)
        project.set_quota(quota_type)
        self._vnc_lib.project_update(project)
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        ipam_refs = net_obj.__dict__.get('network_ipam_refs', [])
        total_subnets = _get_total_subnets_count(ipam_refs)

        if total_subnets != 2:
            raise Exception("2 Subnets expected in Virtual Network")

        #test quota through network_update
        vn.add_network_ipam(ipam1, VnSubnetsType([ipam1_sn_v4, ipam3_sn_v4]))
        vn.add_network_ipam(ipam2, VnSubnetsType([ipam2_sn_v4]))
        with ExpectedException(cfgm_common.exceptions.OverQuota):
            self._vnc_lib.virtual_network_update(vn)

        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        quota_type.set_subnet(4)
        project.set_quota(quota_type)
        self._vnc_lib.project_update(project)


        vn = VirtualNetwork('my-new-vn', project)
        vn.add_network_ipam(ipam1, VnSubnetsType([ipam1_sn_v4]))
        vn.add_network_ipam(ipam2, VnSubnetsType([ipam2_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        vn.add_network_ipam(ipam1, VnSubnetsType([ipam1_sn_v4, ipam3_sn_v4]))
        vn.add_network_ipam(ipam2, VnSubnetsType([ipam2_sn_v4, ipam4_sn_v4]))
        self._vnc_lib.virtual_network_update(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        ipam_refs = net_obj.__dict__.get('network_ipam_refs', [])
        total_subnets = _get_total_subnets_count(ipam_refs)
        if total_subnets != 4:
            raise Exception("4 Subnets expected in Virtual Network")

        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam1.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam2.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
    #end test_subnet_quota

    def test_flat_subnet_ipam_crud(self):
        # Create Project
        project = Project('flat-subnet-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        # create ipam without any subnets or subnet_method
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        ipam = self._vnc_lib.network_ipam_read(id=ipam.uuid)
        self.assertEqual(ipam.get_ipam_subnet_method(), None)

        # try changing subnet-method from None to flat-subnet, vnc_lib should reject
        ipam.set_ipam_subnet_method('flat-subnet')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'ipam_subnet_method can not be changed') as e:
            self._vnc_lib.network_ipam_update(ipam)

        ipam.set_ipam_subnet_method('user-defined-subnet')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
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
        ipam = self._vnc_lib.network_ipam_read(id=ipam.uuid)
        # delete ipam before creating one with flat-subnet
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="flat-subnet",
                           ipam_subnets=IpamSubnets([ipam1_sn_v4]))
        self._vnc_lib.network_ipam_create(ipam)

        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
    #end test_flat_subnet_ipam_crud

    def test_flat_subnet_ipam_user_defined_network(self):
        #retest failing
        project = Project('flat-subnet-proj-%s' %(self.id()), Domain())
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
        with ExpectedException(cfgm_common.exceptions.BadRequest,
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

        vn = VirtualNetwork('my-v4-v6-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam3_sn_v4]))
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'with flat-subnet, network can not have user-defined subnet') as e:
            self._vnc_lib.virtual_network_create(vn)

        #add another ipam in VnSubnetType, should fail again
        vn.set_network_ipam(ipam, VnSubnetsType([ipam3_sn_v4, ipam4_sn_v4]))
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'with flat-subnet, network can not have user-defined subnet') as e:
            self._vnc_lib.virtual_network_create(vn)

        logger.debug('create l2 virtual network')
        vn = VirtualNetwork('my-v4-v6-vn', project,
                            virtual_network_properties=VirtualNetworkType(forwarding_mode='l2'))
        vn.add_network_ipam(ipam, VnSubnetsType([]))
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'flat-subnet is allowed only with l3 network') as e:
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
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'with flat-subnet, network can not have user-defined subnet') as e:
            self._vnc_lib.virtual_network_update(vn)

        vn.set_network_ipam(ipam, VnSubnetsType([]))
        vn.set_virtual_network_properties(VirtualNetworkType(forwarding_mode='l2'))
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'flat-subnet is allowed only with l3 network') as e:
            self._vnc_lib.virtual_network_update(vn)

        vn.set_address_allocation_mode('user-defined-subnet-only')
        vn.set_virtual_network_properties(VirtualNetworkType(forwarding_mode='l3'))
        self._vnc_lib.virtual_network_update(vn)

        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        self.assertEqual(net_obj.get_address_allocation_mode(), 'user-defined-subnet-only')

        # modify some other property of the network without changing ipam_refs
        # or network mode
        vn.set_is_shared(True)
        self._vnc_lib.virtual_network_update(vn)


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
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'Virtual-Network\(\[\'default-domain\', \'flat-subnet-proj-%s\', \'my-v4-v6-vn\'\]\) has no defined subnets' %(self.id())) as e:
            ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)

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

        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'Instance IP Address can not be changed') as e:
            self._vnc_lib.instance_ip_update(ipv4_obj2)

        ipv4_obj2.set_instance_ip_address('11.1.1.4')
        ipv4_obj2 = self._vnc_lib.instance_ip_read(id=ipv4_id2)
        ipv4_addr2 = ipv4_obj2.get_instance_ip_address()

        #delete subnet from ipam from where ip addresses already allocated
        # we should get an exception as instance-ip is in use from the subnet
        ipam.set_ipam_subnets(IpamSubnets([ipam2_sn_v4]))
        with ExpectedException(cfgm_common.exceptions.RefsExistError) as e:
            self._vnc_lib.network_ipam_update(ipam)

        #delete all subnets from ipam, ipam update should fail as instance-ip
        # is from one of the subnet
        ipam.set_ipam_subnets(IpamSubnets([]))
        with ExpectedException(cfgm_common.exceptions.RefsExistError) as e:
            self._vnc_lib.network_ipam_update(ipam)

        #Delete ipam_subnet which is not used for any ip-allocation so far
        #ipam_update should go through
        ipam.set_ipam_subnets(IpamSubnets([ipam1_sn_v4]))
        self._vnc_lib.network_ipam_update(ipam)

        #Restore original ipam_subnet lists and update ipam
        ipam.set_ipam_subnets(IpamSubnets([ipam1_sn_v4, ipam2_sn_v4]))
        self._vnc_lib.network_ipam_update(ipam)

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
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'Virtual-Network\(\[\'default-domain\', \'flat-subnet-proj-%s\', \'my-v4-v6-vn\'\]\) has exhausted subnet\(\[\'11.1.1.0/28\', \'12.1.1.0/28\'\]\)' %(self.id())) as e:
            ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)

        #cleanup
        self._vnc_lib.instance_ip_delete(id=ipv4_id1)
        self._vnc_lib.instance_ip_delete(id=ipv4_id2)
        self._vnc_lib.instance_ip_delete(id=ipv4_id3)
        self._vnc_lib.instance_ip_delete(id=ipv4_id4)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj1.uuid)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
    #end test_flat_subnet_ipam_user_defined_network

    def test_flat_subnet_ipam_flat_subnet_network(self):
        project = Project('flat-subnet-proj-%s' %(self.id()), Domain())
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

        # try updating ipam_subnet_method from flat-subnet to
        # user-defined-subnet, vnc_lib should reject
        ipam.set_ipam_subnet_method('user-defined-subnet')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'ipam_subnet_method can not be changed') as e:
            self._vnc_lib.network_ipam_update(ipam)

        # restore flat-subnet as a subnet_method in ipam
        ipam = self._vnc_lib.network_ipam_read(id=ipam.uuid)
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

        #Remove ipam_subnets from ipam, update should fail with
        #RefsExistError
        ipam.set_ipam_subnets(IpamSubnets([]))
        with ExpectedException(cfgm_common.exceptions.RefsExistError) as e:
            self._vnc_lib.network_ipam_update(ipam)

        #Change ipam by removing one and adding another, update should fail
        # with RefsExistError
        ipam.set_ipam_subnets(IpamSubnets([ipam2_sn_v4]))
        with ExpectedException(cfgm_common.exceptions.RefsExistError) as e:
            self._vnc_lib.network_ipam_update(ipam)

        #Change ipam by adding another subnet, update should pass
        ipam.set_ipam_subnets(IpamSubnets([ipam1_sn_v4, ipam2_sn_v4]))
        self._vnc_lib.network_ipam_update(ipam)

        #Restore ipam_subnets to original set
        ipam.set_ipam_subnets(IpamSubnets([ipam1_sn_v4]))
        self._vnc_lib.network_ipam_update(ipam)

        #next instance_ip allocation should fail due to ip address exhaustion
        logger.debug('Allocating an IPV4 address for Third VM')

        with ExpectedException(cfgm_common.exceptions.BadRequest,
            'Virtual-Network\(\[\'default-domain\', \'flat-subnet-proj-%s\', \'my-v4-v6-vn\'\]\) has no defined subnets' %(self.id())) as e:
            ipv4_id3 = self._vnc_lib.instance_ip_create(ipv4_obj3)

        # update ipam with additional subnet
        ipam.set_ipam_subnets(IpamSubnets([ipam1_sn_v4, ipam2_sn_v4]))
        self._vnc_lib.network_ipam_update(ipam)
        #change allocation_mode to
        vn.set_address_allocation_mode('user-defined-subnet-only')
        self._vnc_lib.virtual_network_update(vn)
        with ExpectedException(cfgm_common.exceptions.BadRequest,
            'Virtual-Network\(\[\'default-domain\', \'flat-subnet-proj-%s\', \'my-v4-v6-vn\'\]\) has no defined subnets' %(self.id())) as e:
            ipv4_id3 = self._vnc_lib.instance_ip_create(ipv4_obj3)

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
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
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
    #end test_flat_subnet_ipam_flat_subnet_network

    def test_hybrid_subnet_ipam_flat_subnet_network(self):
        project = Project('flat-subnet-proj-%s' %(self.id()), Domain())
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

        ipam2 = NetworkIpam('user-defined-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="user-defined-subnet")
        self._vnc_lib.network_ipam_create(ipam2)
        logger.debug('Created network ipam')

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

        # network is hybrid but with allocation-mode is flat-subnet-only
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
            logger.debug('Allocation failed, expected v4 IP Address 12.1.1.8')

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
        with ExpectedException(cfgm_common.exceptions.BadRequest,
            'Virtual-Network\(\[\'default-domain\', \'flat-subnet-proj-%s\', \'my-v4-v6-vn\'\]\) has exhausted subnet\(\[\'11.1.1.0/28\', \'12.1.1.0/28\'\]\)' %(self.id())) as e:
            ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)

        #try allocating specific ip, which has been assigned already
	ipv4_obj5.set_instance_ip_address('12.1.1.4')
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
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
        #failing in 2-3 attempts
    #end test_hybrid_subnet_ipam_flat_subnet_network

    def test_hybrid_subnet_ipam_user_subnet_network(self):
        project = Project('flat-subnet-proj-%s' %(self.id()), Domain())
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
        ipam2 = NetworkIpam('user-defined-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="user-defined-subnet")
        self._vnc_lib.network_ipam_create(ipam2)
        logger.debug('Created network ipam')

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
        with ExpectedException(cfgm_common.exceptions.BadRequest) as e:
            ipv4_id5 = self._vnc_lib.instance_ip_create(ipv4_obj5)

        #try allocating specific ip, which has been assigned already
        ipv4_obj5.set_instance_ip_address('14.1.1.8')
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
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
    #end test_hybrid_subnet_ipam_user_subnet_network

    def test_hybrid_subnet_ipam_ask_ip(self):
        project = Project('flat-subnet-proj-%s' %(self.id()), Domain())
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

        ipam2 = NetworkIpam('user-defined-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="user-defined-subnet")
        self._vnc_lib.network_ipam_create(ipam2)
        logger.debug('Created network ipam')

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
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
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
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
                               'Ip address already in use') as e:
            ipv4_id4 = self._vnc_lib.instance_ip_create(ipv4_obj4)

        #change allocation-mode to user-defined-subnet-preferred and ask ip addr
        # from cidr with user-defined subnet
        vn.set_address_allocation_mode('user-defined-subnet-preferred')
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
    #end test_hybrid_subnet_ipam_ask_ip

    def test_hybrid_subnet_ipam_ip_alloc_from_subnet_uuid(self):
        project = Project('flat-subnet-proj-%s' %(self.id()), Domain())
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

        ipam2 = NetworkIpam('user-defined-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="user-defined-subnet")
        self._vnc_lib.network_ipam_create(ipam2)
        logger.debug('Created network ipam')

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
    #end test_hybrid_subnet_ipam_ip_alloc_from_subnet_uuid

    def test_subnet_alloc_unit(self):
        # Create Project
        project = Project('my-v4-v6-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

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
        with ExpectedException(BadRequest,
            'Virtual-Network\(default-domain:my-v4-v6-proj-%s:my-v4-v6-vn:11.1.1.0/24\) has invalid alloc_unit\(4\) in subnet\(11.1.1.0/24\)' %(self.id())) as e:
            ipv4_id1 = self._vnc_lib.instance_ip_create(ipv4_obj1)

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
    #end

    def test_ipam_subnet_update(self):
        # Create Project
        project = Project('my-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)

        #Create 3 subnets, two with specific gw_ip but with default dns_server
        #one subnet is with default gw_ip and specific dns_server
        ipam1_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24),
                                     default_gateway='11.1.1.100')
        ipam2_sn_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 24),
                                     default_gateway='12.1.1.100')
        ipam3_sn_v4 = IpamSubnetType(subnet=SubnetType('13.1.1.0', 24),
                                     dns_server_address='13.1.1.200')

        # Create NetworkIpams specifying subnet_method as flat-subnet
        # and user-defined-subnet
        ipam = NetworkIpam('flat-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="flat-subnet",
                           ipam_subnets=IpamSubnets([ipam1_sn_v4,
                                                     ipam2_sn_v4,
                                                     ipam3_sn_v4]))
        self._vnc_lib.network_ipam_create(ipam)
        ipam_obj = self._vnc_lib.network_ipam_read(id=ipam.uuid)

        #update dns server to addr mgmt values in two subnets and keep None
        # in one subnet
        ipam2_sn_v4.set_dns_server_address('12.1.1.253')
        ipam3_sn_v4.set_dns_server_address('13.1.1.200')

        #change default gw to the last subnet
        # we should expect bad request exception.
        ipam3_sn_v4.set_default_gateway('13.1.1.50')
        ipam._pending_field_updates.add('ipam_subnets')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'default gateway change is not allowed orig:13.1.1.254, new: 13.1.1.50') as e:
            self._vnc_lib.network_ipam_update(ipam)
        ipam_obj = self._vnc_lib.network_ipam_read(id=ipam.uuid)

        #restore default gw in ipam3_sn_v4 and change in ipam2_sn_v4
        # we should expect bad request exception.
        ipam3_sn_v4.set_default_gateway('13.1.1.254')
        ipam2_sn_v4.set_default_gateway('12.1.1.50')
        ipam._pending_field_updates.add('ipam_subnets')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'default gateway change is not allowed orig:12.1.1.100, new: 12.1.1.50') as e:
            self._vnc_lib.network_ipam_update(ipam)
        ipam_obj = self._vnc_lib.network_ipam_read(id=ipam.uuid)

        #restore default gw in ipam2_sn_v4 and change in ipam1_sn_v4
        # we should expect bad request exception.
        ipam2_sn_v4.set_default_gateway('12.1.1.100')
        ipam1_sn_v4.set_default_gateway('11.1.1.50')
        ipam._pending_field_updates.add('ipam_subnets')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'default gateway change is not allowed orig:11.1.1.100, new: 11.1.1.50') as e:
            self._vnc_lib.network_ipam_update(ipam)
        ipam_obj = self._vnc_lib.network_ipam_read(id=ipam.uuid)

        #restore all gw_ips and change dns_server_address in ipam3_sn_v4
        #we should expect bad request exception.
        ipam1_sn_v4.set_default_gateway('11.1.1.100')
        ipam3_sn_v4.set_dns_server_address('13.1.1.210')
        ipam._pending_field_updates.add('ipam_subnets')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'dns server change is not allowed orig:13.1.1.200, new: 13.1.1.210') as e:
            self._vnc_lib.network_ipam_update(ipam)
        ipam_obj = self._vnc_lib.network_ipam_read(id=ipam.uuid)

        #restore ipam3_sn_v4 dns_server_address and change in ipam2_sn_v4
        #we should expect bad request exception.
        ipam3_sn_v4.set_dns_server_address('13.1.1.200')
        ipam2_sn_v4.set_dns_server_address('12.1.1.200')
        ipam._pending_field_updates.add('ipam_subnets')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'dns server change is not allowed orig:12.1.1.253, new: 12.1.1.200') as e:
            self._vnc_lib.network_ipam_update(ipam)
        ipam_obj = self._vnc_lib.network_ipam_read(id=ipam.uuid)

        #restore ipam2_sn_v4 dns_server_address and change in ipam1_sn_v4
        #we should expect bad request exception.
        ipam2_sn_v4.set_dns_server_address('12.1.1.253')
        ipam1_sn_v4.set_dns_server_address('11.1.1.200')
        ipam._pending_field_updates.add('ipam_subnets')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'dns server change is not allowed orig:11.1.1.253, new: 11.1.1.200') as e:
            self._vnc_lib.network_ipam_update(ipam)
        ipam_obj = self._vnc_lib.network_ipam_read(id=ipam.uuid)

        #cleanup
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
    #end

    def test_network_subnet_update(self):
        # Create Project
        project = Project('my-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)

        #Create 3 subnets, two with specific gw_ip but with default dns_server
        #one subnet is with default gw_ip and specific dns_server
        ipam1_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24),
                                     default_gateway='11.1.1.100')
        ipam2_sn_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 24),
                                     default_gateway='12.1.1.100')
        ipam3_sn_v4 = IpamSubnetType(subnet=SubnetType('13.1.1.0', 24),
                                     dns_server_address='13.1.1.200')

        ipam1 = NetworkIpam('user-defined-ipam', project, IpamType("dhcp"),
                           ipam_subnet_method="user-defined-subnet")
        self._vnc_lib.network_ipam_create(ipam1)

        vn_subnets = VnSubnetsType([ipam1_sn_v4, ipam2_sn_v4, ipam3_sn_v4])
        vn = VirtualNetwork('my-v4-v6-vn', project,
                            virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'))

        vn.add_network_ipam(ipam1, vn_subnets)
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)
        #update dns server to addr mgmt values in two subnets and keep None
        # in middle subnet
        ipam1_sn_v4.set_dns_server_address('11.1.1.253')
        ipam3_sn_v4.set_dns_server_address('13.1.1.200')

        #change default gw to the last subnet
        # we should expect bad request exception.
        ipam3_sn_v4.set_default_gateway('13.1.1.50')
        vn._pending_field_updates.add('network_ipam_refs')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'default gateway change is not allowed orig:13.1.1.254, new: 13.1.1.50') as e:
            self._vnc_lib.virtual_network_update(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #restore default gw in ipam3_sn_v4 and change in ipam2_sn_v4
        # we should expect bad request exception.
        ipam3_sn_v4.set_default_gateway('13.1.1.254')
        ipam2_sn_v4.set_default_gateway('12.1.1.50')
        vn._pending_field_updates.add('network_ipam_refs')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'default gateway change is not allowed orig:12.1.1.100, new: 12.1.1.50') as e:
            self._vnc_lib.virtual_network_update(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #restore default gw in ipam2_sn_v4 and change in ipam1_sn_v4
        # we should expect bad request exception.
        ipam2_sn_v4.set_default_gateway('12.1.1.100')
        ipam1_sn_v4.set_default_gateway('11.1.1.50')
        vn._pending_field_updates.add('network_ipam_refs')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'default gateway change is not allowed orig:11.1.1.100, new: 11.1.1.50') as e:
            self._vnc_lib.virtual_network_update(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #restore all gw_ips and change dns_server_address in ipam3_sn_v4
        #we should expect bad request exception.
        ipam1_sn_v4.set_default_gateway('11.1.1.100')
        ipam3_sn_v4.set_dns_server_address('13.1.1.210')
        vn._pending_field_updates.add('network_ipam_refs')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'dns server change is not allowed orig:13.1.1.200, new: 13.1.1.210') as e:
            self._vnc_lib.virtual_network_update(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #restore ipam3_sn_v4 dns_server_address and change in ipam2_sn_v4
        #we should expect bad request exception.
        ipam3_sn_v4.set_dns_server_address('13.1.1.200')
        ipam2_sn_v4.set_dns_server_address('12.1.1.200')
        vn._pending_field_updates.add('network_ipam_refs')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'dns server change is not allowed orig:12.1.1.253, new: 12.1.1.200') as e:
            self._vnc_lib.virtual_network_update(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        #restore ipam2_sn_v4 dns_server_address and change in ipam1_sn_v4
        #we should expect bad request exception.
        ipam2_sn_v4.set_dns_server_address('12.1.1.253')
        ipam1_sn_v4.set_dns_server_address('11.1.1.200')
        vn._pending_field_updates.add('network_ipam_refs')
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                               'dns server change is not allowed orig:11.1.1.253, new: 11.1.1.200') as e:
            self._vnc_lib.virtual_network_update(vn)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # delete vn and create a new subnet with add from start and add
        # new network
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        
        #create a subnet with allocation from start and test the subnet
        # to make sure gw_ip and dns_server_address is not updateable 
        ipam4_sn_v4 = IpamSubnetType(subnet=SubnetType('14.1.1.0', 24),
                                     addr_from_start=True)

        ipam5_sn_v4 = IpamSubnetType(subnet=SubnetType('15.1.1.0', 24),
                                     default_gateway='15.1.1.100',
                                     dns_server_address='15.1.1.200',
                                     addr_from_start=True)
        vn1_subnets = VnSubnetsType([ipam4_sn_v4, ipam5_sn_v4])

        vn1 = VirtualNetwork('my-v4-v6-vn', project,
                             virtual_network_properties=VirtualNetworkType(forwarding_mode='l3'))

        vn1.add_network_ipam(ipam1, vn1_subnets)
        self._vnc_lib.virtual_network_create(vn1)
        net_obj = self._vnc_lib.virtual_network_read(id = vn1.uuid)

        # change valid network property and and update vn1
        vn1.set_address_allocation_mode('user-defined-subnet-only')
        self._vnc_lib.virtual_network_update(vn1)
        net_obj = self._vnc_lib.virtual_network_read(id = vn1.uuid)

        # change valid subnet property and update vn1
        ipam4_sn_v4.set_subnet_name('subnet4')
        ipam5_sn_v4.set_subnet_name('subnet5')
        vn1._pending_field_updates.add('network_ipam_refs')
        self._vnc_lib.virtual_network_update(vn1)
        net_obj = self._vnc_lib.virtual_network_read(id = vn1.uuid)

        #cleanup
        self._vnc_lib.virtual_network_delete(id=vn1.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam1.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
    #end

    def test_ip_alloction(self):
        # Create Project
        project = Project('my-v4-v6-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

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
    #end

    def test_ip_alloction_pools(self):
        # Create Project
        project = Project('my-v4-v6-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

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
    #end

    def test_subnet_gateway_ip_alloc(self):
        # Create Project
        project = Project('my-v4-v6-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

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
    #end

    def test_bulk_ip_alloc_free(self):
        # Create Project
        project = Project('v4-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

        # Create subnets
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create VN
        vn = VirtualNetwork('v4-vn', project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn)
        logger.debug('Created Virtual Network object %s', vn.uuid)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        subnet_uuid = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[0].subnet_uuid
        # request to allocate 10 ip address using bulk allocation api
        data = {"subnet" : subnet_uuid, "count" : 10}
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
        # We should get 13 ip allocated from this subnet
        # 10 user request + 3 reserved ips (first, last and gw).
        data = {"subnet_list" : [subnet_uuid]}
        url = '/virtual-network/%s/subnet-ip-count' %(vn.uuid)
        rv_json = self._vnc_lib._request_server(rest.OP_POST, url,
                                                json.dumps(data))
        ret_ip_count = json.loads(rv_json)['ip_count_list'][0]
        allocated_ip = ret_ip_count - 3
        self.assertEqual(allocated_ip, 10)

        #free 5 allocated ip addresses from vn
        data = {"ip_addr" : ['11.1.1.252', '11.1.1.251', '11.1.1.250',
                             '11.1.1.249', '11.1.1.248']}
        url = '/virtual-network/%s/ip-free' %(vn.uuid)
        self._vnc_lib._request_server(rest.OP_POST, url, json.dumps(data))

        # Find out number of allocated ips from given VN/subnet
        # We should  get 5+3 ip allocated from this subnet
        data = {"subnet_list" : [subnet_uuid]}
        url = '/virtual-network/%s/subnet-ip-count' %(vn.uuid)
        rv_json = self._vnc_lib._request_server(rest.OP_POST, url,
                                                json.dumps(data))
        ret_ip_count = json.loads(rv_json)['ip_count_list'][0]
        allocated_ip = ret_ip_count - 3
        self.assertEqual(allocated_ip, 5)

        #free remaining 5 allocated ip addresses from vn
        data = {"ip_addr": ['11.1.1.247', '11.1.1.246', '11.1.1.245',
                            '11.1.1.244', '11.1.1.243']}
        url = '/virtual-network/%s/ip-free' %(vn.uuid)
        self._vnc_lib._request_server(rest.OP_POST, url, json.dumps(data))

        data = {"subnet_list" : [subnet_uuid]}
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
    #end

    def test_v4_ip_allocation_exhaust(self):
        # Create Project
        project = Project('v4-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

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
            subnet_uuid = net_obj.network_ipam_refs[0]['attr'].ipam_subnets[0].subnet_uuid
            data = {"subnet_list" : [subnet_uuid]}
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
            with ExpectedException(BadRequest,
                'Virtual-Network\(\[\'default-domain\', \'v4-proj-%s\', \'v4-vn\'\]\) has exhausted subnet\(\[\]\)' %(
                self.id())) as e:
                ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)

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
    #end

    def test_req_ip_allocation(self):
        # Create Project
        project = Project('my-v4-v6-req-ip-proj-%s' %(self.id()), Domain())
        self._vnc_lib.project_create(project)

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)

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

            def __call__(self, _, *args, **kwargs):
                if self._invoked >= 1:
                    raise Exception(
                        "Instance IP was persisted more than once")

                if args[0].startswith('/api-server/subnets'):
                    self._invoked += 1
                return self._orig_method(*args, **kwargs)
        # end SpyCreateNode

        orig_object = self._api_server._db_conn._zk_db._zk_client
        method_name = 'create_node'
        with test_common.patch(orig_object, method_name,
                               SpyCreateNode(orig_object, method_name)):
            self._vnc_lib.instance_ip_create(iip_obj)
            self.assertTill(self.vnc_db_has_ident, obj=iip_obj)

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
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
                               'Ip address already in use') as e:
            self._vnc_lib.instance_ip_create(iip2_obj)

        # allocate instance-ip clashing with existing floating-ip
        iip2_obj.set_instance_ip_address(fip_obj.floating_ip_address)
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
                               'Ip address already in use') as e:
            self._vnc_lib.instance_ip_create(iip2_obj)

        # allocate floating-ip clashing with existing floating-ip
        fip2_obj = FloatingIp('clashing-fip-%s' %(self.id()), fip_pool_obj,
                              floating_ip_address=fip_obj.floating_ip_address)
        fip2_obj.add_project(proj_obj)
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
                               'Ip address already in use') as e:
            self._vnc_lib.floating_ip_create(fip2_obj)

        # allocate alias-ip clashing with existing alias-ip
        aip2_obj = AliasIp('clashing-aip-%s' %(self.id()), aip_pool_obj,
                           alias_ip_address=aip_obj.alias_ip_address)
        aip2_obj.add_project(proj_obj)
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
                               'Ip address already in use') as e:
            self._vnc_lib.alias_ip_create(aip2_obj)

        # allocate floating-ip clashing with existing instance-ip
        fip2_obj.set_floating_ip_address(iip_obj.instance_ip_address)
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
                               'Ip address already in use') as e:
            self._vnc_lib.floating_ip_create(fip2_obj)

        # allocate alias-ip clashing with existing instance-ip
        aip2_obj.set_alias_ip_address(iip_obj.instance_ip_address)
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
                               'Ip address already in use') as e:
            self._vnc_lib.alias_ip_create(aip2_obj)

        # allocate alias-ip clashing with existing floating-ip
        aip2_obj.set_alias_ip_address(fip_obj.floating_ip_address)
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
                               'Ip address already in use') as e:
            self._vnc_lib.alias_ip_create(aip2_obj)

        # allocate floating-ip with gateway ip and verify failure
        fip2_obj.set_floating_ip_address('11.1.1.254')
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
                               'Ip address already in use') as e:
            self._vnc_lib.floating_ip_create(fip2_obj)

        # allocate alias-ip with gateway ip and verify failure
        aip2_obj.set_alias_ip_address('11.1.1.254')
        with ExpectedException(cfgm_common.exceptions.RefsExistError,
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
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                 'Gateway IP cannot be used by VM port') as e:
            self._vnc_lib.instance_ip_update(iip_gw_ip)

        iip_gw_ip.del_virtual_machine_interface(vm_vmi_obj)
        iip_gw_ip.add_virtual_machine_interface(rtr_vmi_obj)
        self._vnc_lib.instance_ip_update(iip_gw_ip)

        iip2_gw_ip.add_virtual_machine_interface(isolated_vmi_obj)
        self._vnc_lib.instance_ip_update(iip2_gw_ip)

        isolated_vmi_obj.add_virtual_machine(vm_obj)
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                 'Gateway IP cannot be used by VM port') as e:
            self._vnc_lib.virtual_machine_interface_update(
                isolated_vmi_obj)
    # end test_ip_alloc_clash

    def test_floating_ip_pool_subnet_create(self):
        """
        Test Floating-Ip-Pool create by specifiying subnet uuid in an Ipam of
        a virtual-network. This testcase covers both valid and invalid subnet
        uuid specification scenarios.
        """
        def _get_ipam_subnet_uuid(ipam_ref, ipam_obj):
            """
            Convenience function to return subnet uuid in a ipam ref on
            a virtual-network.
            """
            if ipam_ref['to'] == ipam_obj.get_fq_name():
                ipam_subnets = ipam_ref['attr'].get_ipam_subnets()
                if ipam_subnets:
                    # We will use the first subnet in the matching IPAM.
                    return True, ipam_subnets[0].get_subnet_uuid()
            return False, None
        #end _get_ipam_subnet_uuid

        # Create a test project.
        proj_obj = Project('proj-%s' %(self.id()), parent_obj=Domain())
        self._vnc_lib.project_create(proj_obj)

        #
        # User-defined subnet.
        #

        # Create a user-defined subnet IPAM.
        ipam_obj = NetworkIpam('user-subnet-ipam-%s' %(self.id()), proj_obj)
        self._vnc_lib.network_ipam_create(ipam_obj)
        ipam_obj = self._vnc_lib.network_ipam_read(\
            fq_name=ipam_obj.get_fq_name())

        # Create a virtual-network and add user-defined IPAM to it.
        vn_name = 'user-vn-%s' %(self.id())
        vn_obj = VirtualNetwork(vn_name, proj_obj)
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType([ipam_sn_v4]))
        self._vnc_lib.virtual_network_create(vn_obj)
        vn_obj = self._vnc_lib.virtual_network_read(\
            fq_name=vn_obj.get_fq_name())

        # Create a Floating-ip-pool that points to an "invalid subnet id
        # on the virtual-network. Verify, floatin-ip-pool create fails.
        fip_subnets = FloatingIpPoolSubnetType(subnet_uuid=[self.id()])
        fip_pool_obj = FloatingIpPool(
            'user-fip-pool-%s' %(self.id()), parent_obj=vn_obj,
            floating_ip_pool_subnets = fip_subnets)
        err_msg = "Subnet %s was not found in virtual-network %s" %\
            (self.id(), vn_obj.get_uuid())
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                 err_msg) as e:
            self._vnc_lib.floating_ip_pool_create(fip_pool_obj)

        # Get subnet uuid of user defined ipam in the virtual-network.
        ipam_refs = vn_obj.get_network_ipam_refs()
        svc_subnet_uuid = None
        for ipam_ref in ipam_refs:
            found, svc_subnet_uuid = _get_ipam_subnet_uuid(ipam_ref, ipam_obj)
            if found:
                break

        # Create a Floating-ip-pool that points to an subnet id of the
        # user-define ipam. Verify, floatin-ip-pool create succeeds.
        fip_subnets = FloatingIpPoolSubnetType(subnet_uuid = [svc_subnet_uuid])
        fip_pool_obj = FloatingIpPool(
            'user-fip-pool-%s' %(self.id()), parent_obj=vn_obj,
            floating_ip_pool_subnets = fip_subnets)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)

        #
        # Flat subnet.
        #

        # Create a flat subnet IPAM.
        ipam_sn_v4 = [IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))]
        ipam_obj = NetworkIpam('flat-subnet-ipam-%s' %(self.id()), proj_obj)
        ipam_obj.set_ipam_subnet_method('flat-subnet')
        ipam_obj.set_ipam_subnets(IpamSubnets(ipam_sn_v4))
        self._vnc_lib.network_ipam_create(ipam_obj)
        ipam_obj = self._vnc_lib.network_ipam_read(\
            fq_name=ipam_obj.get_fq_name())

        # Create a virtual-network and add flat-subnet IPAM to it.
        vn_name = 'flat-vn-%s' %(self.id())
        vn_obj = VirtualNetwork(vn_name, proj_obj)
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType([]))
        vn_obj.set_virtual_network_properties(\
            VirtualNetworkType(forwarding_mode='l3'))
        self._vnc_lib.virtual_network_create(vn_obj)
        vn_obj = self._vnc_lib.virtual_network_read(\
            fq_name=vn_obj.get_fq_name())

        # Create a Floating-ip-pool that points to an "invalid" subnet id
        # on the virtual-network. Verify, floatin-ip-pool create fails.
        fip_subnets = FloatingIpPoolSubnetType(subnet_uuid = [self.id()])
        fip_pool_obj = FloatingIpPool(
            'flat-fip-pool-%s' %(self.id()), parent_obj=vn_obj,
            floating_ip_pool_subnets = fip_subnets)
        err_msg = "Subnet %s was not found in virtual-network %s" %\
            (self.id(), vn_obj.get_uuid())
        with ExpectedException(cfgm_common.exceptions.BadRequest,
                 err_msg) as e:
            self._vnc_lib.floating_ip_pool_create(fip_pool_obj)

        # Get subnet uuid of flat subnet ipam in the virtual-network.
        ipam_refs = vn_obj.get_network_ipam_refs()
        svc_subnet_uuid = None
        for ipam_ref in ipam_refs:
            found, svc_subnet_uuid = _get_ipam_subnet_uuid(ipam_ref, ipam_obj)
            if found:
                break

        # Create a Floating-ip-pool that points to an subnet id of the
        # flat-subnet ipam. Verify, floatin-ip-pool create succeeds.
        fip_subnets = FloatingIpPoolSubnetType(subnet_uuid = [svc_subnet_uuid])
        fip_pool_obj = FloatingIpPool(
            'flat-fip-pool-%s' %(self.id()), parent_obj=vn_obj,
            floating_ip_pool_subnets = fip_subnets)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)

    # end test_floating_ip_pool_subnet_create

    def test_floating_ip_alloc(self):
        """
        Test Floating-Ip create from floating-ip-pools that have been
        created by requesting a specific subnet. Verify that the floating-ip
        created is from the corresponding subnet.
        """
        def _get_ipam_subnet_uuid(ipam_ref, ipam_obj):
            """
            Convenience function to return subnet uuid in a ipam ref on
            a virtual-network.
            """
            if ipam_ref['to'] == ipam_obj.get_fq_name():
                ipam_subnets = ipam_ref['attr'].get_ipam_subnets()
                if ipam_subnets:
                    # We will use the first subnet in the matching IPAM.
                    return True, ipam_subnets[0].get_subnet_uuid()
            return False, None
        #end _get_ipam_subnet_uuid

        # Create a test project.
        proj_obj = Project('proj-%s' %(self.id()), parent_obj=Domain())
        self._vnc_lib.project_create(proj_obj)

        vn_name = 'vn-%s' %(self.id())
        vn_obj = VirtualNetwork(vn_name, proj_obj)

        # Create a user-defined subnet IPAM.
        user_ipam_obj = NetworkIpam('user-subnet-ipam-%s' %(self.id()), proj_obj)
        self._vnc_lib.network_ipam_create(user_ipam_obj)
        user_ipam_obj = self._vnc_lib.network_ipam_read(\
            fq_name=user_ipam_obj.get_fq_name())

        # Add user-defined subnet ipam to virtual-network.
        ipam_sn_v4 = IpamSubnetType(subnet=SubnetType('10.1.1.0', 24))
        vn_obj.add_network_ipam(user_ipam_obj, VnSubnetsType([ipam_sn_v4]))

        # Create a flat subnet IPAM.
        ipam_sn_v4 = [IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))]
        flat_ipam_obj = NetworkIpam(\
            'flat-subnet-ipam-%s' %(self.id()), proj_obj)
        flat_ipam_obj.set_ipam_subnet_method('flat-subnet')
        flat_ipam_obj.set_ipam_subnets(IpamSubnets(ipam_sn_v4))
        self._vnc_lib.network_ipam_create(flat_ipam_obj)
        flat_ipam_obj = self._vnc_lib.network_ipam_read(\
            fq_name=flat_ipam_obj.get_fq_name())

        # Add flat subnet ipam to virtual-network.
        vn_obj.add_network_ipam(flat_ipam_obj, VnSubnetsType([]))
        vn_obj.set_virtual_network_properties(\
            VirtualNetworkType(forwarding_mode='l3'))

        # Create the virtual-network.
        self._vnc_lib.virtual_network_create(vn_obj)
        vn_obj = self._vnc_lib.virtual_network_read(\
            fq_name=vn_obj.get_fq_name())

        # Get subnet uuid of user defined ipam in the virtual-network.
        ipam_refs = vn_obj.get_network_ipam_refs()
        user_subnet_uuid = None
        for ipam_ref in ipam_refs:
            found, user_subnet_uuid = _get_ipam_subnet_uuid(ipam_ref,
                user_ipam_obj)
            if found:
                break

        # Get subnet uuid of flat subnet ipam in the virtual-network.
        flat_subnet_uuid = None
        for ipam_ref in ipam_refs:
            found, flat_subnet_uuid = _get_ipam_subnet_uuid(ipam_ref,
                flat_ipam_obj)
            if found:
                break

        # Create a Floating-ip-pool that points to the user-defined subnet id.
        # Verify, floating-ip-pool create succeeds.
        fip_subnets = FloatingIpPoolSubnetType(subnet_uuid=[user_subnet_uuid])
        fip_pool_obj = FloatingIpPool(
            'user-fip-pool-%s' %(self.id()), parent_obj=vn_obj,
            floating_ip_pool_subnets = fip_subnets)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)

        # Create floating-ip and verify it is from the user-defined subnet.
        fip_obj = FloatingIp('user-fip-%s' %(self.id()), fip_pool_obj)
        fip_obj.add_project(proj_obj)
        self._vnc_lib.floating_ip_create(fip_obj)
        fip_obj = self._vnc_lib.floating_ip_read(id=fip_obj.uuid)
        fip_addr = fip_obj.get_floating_ip_address()
        if ipaddress.ip_address(fip_addr) not in \
            ipaddress.ip_network(u'10.1.1.0/24'):
            raise Exception("Floating-ip not allocated from requested subnet")

        # Create a Floating-ip-pool that points to the flat subnet id.
        # Verify, floating-ip-pool create succeeds.
        fip_subnets = FloatingIpPoolSubnetType(subnet_uuid =[flat_subnet_uuid])
        fip_pool_obj = FloatingIpPool(
            'flat-fip-pool-%s' %(self.id()), parent_obj=vn_obj,
            floating_ip_pool_subnets = fip_subnets)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)

        # Create floating-ip and verify it is from the flat subnet.
        fip_obj = FloatingIp('flat-fip-%s' %(self.id()), fip_pool_obj)
        fip_obj.add_project(proj_obj)
        self._vnc_lib.floating_ip_create(fip_obj)
        fip_obj = self._vnc_lib.floating_ip_read(id=fip_obj.uuid)
        fip_addr = fip_obj.get_floating_ip_address()
        if ipaddress.ip_address(fip_addr) not in \
            ipaddress.ip_network(u'11.1.1.0/24'):
            raise Exception("Floating-ip not allocated from requested subnet")

    # end test_floating_ip_alloc

#end class TestIpAlloc

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    unittest.main()
