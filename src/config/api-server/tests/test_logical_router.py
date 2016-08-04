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
import netaddr

from vnc_api.vnc_api import *
import vnc_api.gen.vnc_api_test_gen
from vnc_api.gen.resource_test import *
from netaddr import IPNetwork, IPAddress

import cfgm_common

sys.path.append('../common/tests')
from test_utils import *
import test_common
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestLogicalRouter(test_case.ApiServerTestCase):
    def __init__(self, *args, **kwargs):
        ch = logging.StreamHandler()
        ch.setLevel(logging.DEBUG)
        logger.addHandler(ch)
        super(TestLogicalRouter, self).__init__(*args, **kwargs)

    def test_lr_v4_subnets(self):
        logger.debug('test logical router creation and interface-add of v4 subnets')

        # Create Domain
        domain = Domain('my-lr-domain')
        self._vnc_lib.domain_create(domain)
        logger.debug('Created domain ')

        # Create Project
        project = Project('my-lr-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        logger.debug('Created network ipam')

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-lr-domain', 'my-lr-proj',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

        # Create subnets
        ipam_sn_v4_vn1 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        ipam_sn_v6_vn1 = IpamSubnetType(subnet=SubnetType('fd11::', 120))
        ipam_sn_v4_vn2 = IpamSubnetType(subnet=SubnetType('11.1.2.0', 24))
        ipam_sn_v6_vn2 = IpamSubnetType(subnet=SubnetType('fd12::', 120))

        # Create VN my-vn-1
        vn1 = VirtualNetwork('my-vn-1', project)
        vn1.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4_vn1, ipam_sn_v6_vn1]))
        self._vnc_lib.virtual_network_create(vn1)
        logger.debug('Created Virtual Network object for my-vn-1: %s', vn1.uuid)
        net_obj1 = self._vnc_lib.virtual_network_read(id = vn1.uuid)

        # Create VN my-vn-2
        vn2 = VirtualNetwork('my-vn-2', project)
        vn2.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4_vn2, ipam_sn_v6_vn2]))
        self._vnc_lib.virtual_network_create(vn2)
        logger.debug('Created Virtual Network object for my-vn-2: %s', vn2.uuid)
        net_obj2 = self._vnc_lib.virtual_network_read(id = vn2.uuid)

        # Create Logical Router
        lr = LogicalRouter('router-test-v4', project)
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        logger.debug('Created Logical Router ')

        # Create a Virtual Machine Interface belonging to my-vn-1
        id_perms = IdPermsType(enable=True) 
        port_obj1 = VirtualMachineInterface(
           str(uuid.uuid4()), parent_obj=project, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name 
        port_obj1.set_virtual_network(vn1)
        port_obj1.set_virtual_machine_interface_device_owner('DEVICE_OWNER_ROUTER_INTF')
        #Assign gateway ip
        ipam_refs = net_obj1.get_network_ipam_refs() 
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets() 
            for subnet in subnets:
                cidr = '%s/%s' % (subnet.subnet.get_ip_prefix(),
                                  subnet.subnet.get_ip_prefix_len())
                if IPNetwork(cidr).version is 4:
                    gateway_ip = subnet.get_default_gateway()
                    logger.debug(' subnet gateway (%s)' %(gateway_ip))
        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)
        logger.debug('Created Virtual Machine Interface')

        # Create v4 Ip object
        ip_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_address=gateway_ip,
                             instance_ip_family='v4')
        ip_obj1.uuid = ip_obj1.name
        ip_obj1.set_virtual_machine_interface(port_obj1)
        ip_obj1.set_virtual_network(net_obj1)
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1) 
 
        # Add Router Interface (test being subnet)
        lr.add_virtual_machine_interface(port_obj1)
        self._vnc_lib.logical_router_update(lr)
        logger.debug('Linked VMI object (VN1) and LR object')

        # Create a Virtual Machine Interface belonging to my-vn-2
        port_obj2 = VirtualMachineInterface(
           str(uuid.uuid4()), parent_obj=project, id_perms=id_perms)
        port_obj2.uuid = port_obj2.name 
        port_obj2.set_virtual_network(vn2)
        port_obj2.set_virtual_machine_interface_device_owner('DEVICE_OWNER_ROUTER_INTF')
        #Assign gateway ip
        ipam_refs = net_obj2.get_network_ipam_refs() 
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets() 
            for subnet in subnets:
                cidr = '%s/%s' % (subnet.subnet.get_ip_prefix(),
                                  subnet.subnet.get_ip_prefix_len())
                if IPNetwork(cidr).version is 4:
                    gateway_ip = subnet.get_default_gateway()
                    logger.debug(' subnet gateway (%s)' %(gateway_ip))
        port_id2 = self._vnc_lib.virtual_machine_interface_create(port_obj2)
        logger.debug('Created Virtual Machine Interface')

        # Create v4 Ip object
        ip_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_address=gateway_ip,
                             instance_ip_family='v4')
        ip_obj2.uuid = ip_obj2.name
        ip_obj2.set_virtual_machine_interface(port_obj2)
        ip_obj2.set_virtual_network(net_obj2)
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2) 
 
        # Add Router Interface (test being subnet)
        lr.add_virtual_machine_interface(port_obj2)
        self._vnc_lib.logical_router_update(lr)
        logger.debug('Linked VMI object (VN2) and LR object')
        
        # Verify logical-router dumps
        lr.dump()

        # TODO: Schema transformer not integrated in the tests,
        #       hence route-target refs not set yet
        # Verify Route Target Creation
        rt_refs = lr.get_route_target_refs()
        if not rt_refs:
            logger.debug(' !!! Schema Transformer not integrated in test yet !!!')
            logger.debug(' !!! route-target not associated to Logical Router')
        else:
            for rt_ref in rt_refs:
                logger.debug(' Route Target (%s)' %(rt_ref['to']))
                rt_obj = self._vnc_lib.route_target_read(id=rt_ref['uuid'])
                ri_refs = rt_obj.get_routing_instance_back_refs()
                for ri_ref in ri_refs:
                    ri_obj = self.vnc_lib.routing_instance_read(id=ri_ref['uuid'])
                    ri_name = ri_obj.get_display_name()
                    logger.debug(' Routing Instance (%s)' %(ri_name))
                    if ((ri_name != 'my-vn-1') and  (ri_name != 'my-vn-2')):
                        logger.debug(' Failure, Logical-Router not associated to expected VN')

        #cleanup
        logger.debug('Cleaning up')
        self._vnc_lib.instance_ip_delete(id=ip_id1)
        self._vnc_lib.instance_ip_delete(id=ip_id2)
        self._vnc_lib.logical_router_delete(id=lr_uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj2.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1.uuid)
        self._vnc_lib.virtual_network_delete(id=vn2.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end
    
    def test_lr_v6_subnets(self):
        logger.debug('test logical router creation and interface-add of v6 subnets')

        # Create Domain
        domain = Domain('my-lr-domain')
        self._vnc_lib.domain_create(domain)
        logger.debug('Created domain ')

        # Create Project
        project = Project('my-lr-proj', domain)
        self._vnc_lib.project_create(project)
        logger.debug('Created Project')

        # Create NetworkIpam
        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        logger.debug('Created network ipam')

        ipam = self._vnc_lib.network_ipam_read(fq_name=['my-lr-domain', 'my-lr-proj',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

        # Create subnets
        ipam_sn_v4_vn1 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))
        ipam_sn_v6_vn1 = IpamSubnetType(subnet=SubnetType('fd11::', 120))
        ipam_sn_v4_vn2 = IpamSubnetType(subnet=SubnetType('11.1.2.0', 24))
        ipam_sn_v6_vn2 = IpamSubnetType(subnet=SubnetType('fd12::', 120))

        # Create VN my-vn-1
        vn1 = VirtualNetwork('my-vn-1', project)
        vn1.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4_vn1, ipam_sn_v6_vn1]))
        self._vnc_lib.virtual_network_create(vn1)
        logger.debug('Created Virtual Network object for my-vn-1: %s', vn1.uuid)
        net_obj1 = self._vnc_lib.virtual_network_read(id = vn1.uuid)

        # Create VN my-vn-2
        vn2 = VirtualNetwork('my-vn-2', project)
        vn2.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4_vn2, ipam_sn_v6_vn2]))
        self._vnc_lib.virtual_network_create(vn2)
        logger.debug('Created Virtual Network object for my-vn-2: %s', vn2.uuid)
        net_obj2 = self._vnc_lib.virtual_network_read(id = vn2.uuid)

        # Create Logical Router
        lr = LogicalRouter('router-test-v6', project)
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        logger.debug('Created Logical Router ')

        # Create a Virtual Machine Interface belonging to my-vn-1
        id_perms = IdPermsType(enable=True) 
        port_obj1 = VirtualMachineInterface(
           str(uuid.uuid4()), parent_obj=project, id_perms=id_perms)
        port_obj1.uuid = port_obj1.name 
        port_obj1.set_virtual_network(vn1)
        port_obj1.set_virtual_machine_interface_device_owner('DEVICE_OWNER_ROUTER_INTF')
        #Assign gateway ip
        ipam_refs = net_obj1.get_network_ipam_refs() 
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets() 
            for subnet in subnets:
                cidr = '%s/%s' % (subnet.subnet.get_ip_prefix(),
                                  subnet.subnet.get_ip_prefix_len())
                if IPNetwork(cidr).version is 6:
                    gateway_ip = subnet.get_default_gateway()
                    logger.debug(' subnet gateway (%s)' %(gateway_ip))
        port_id1 = self._vnc_lib.virtual_machine_interface_create(port_obj1)
        logger.debug('Created Virtual Machine Interface')

        # Create v6 Ip object
        ip_obj1 = InstanceIp(name=str(uuid.uuid4()), instance_ip_address=gateway_ip,
                             instance_ip_family='v6')
        ip_obj1.uuid = ip_obj1.name
        ip_obj1.set_virtual_machine_interface(port_obj1)
        ip_obj1.set_virtual_network(net_obj1)
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1) 
 
        # Add Router Interface (test being subnet)
        lr.add_virtual_machine_interface(port_obj1)
        lr_obj = self._vnc_lib.logical_router_read(id=lr_uuid)
        self._vnc_lib.logical_router_update(lr_obj)
        logger.debug('Linked VMI object (VN1) and LR object')

        # Create a Virtual Machine Interface belonging to my-vn-2
        port_obj2 = VirtualMachineInterface(
           str(uuid.uuid4()), parent_obj=project, id_perms=id_perms)
        port_obj2.uuid = port_obj2.name 
        port_obj2.set_virtual_network(vn2)
        port_obj2.set_virtual_machine_interface_device_owner('DEVICE_OWNER_ROUTER_INTF')
        #Assign gateway ip
        ipam_refs = net_obj2.get_network_ipam_refs() 
        for ipam_ref in ipam_refs:
            subnets = ipam_ref['attr'].get_ipam_subnets() 
            for subnet in subnets:
                cidr = '%s/%s' % (subnet.subnet.get_ip_prefix(),
                                  subnet.subnet.get_ip_prefix_len())
                if IPNetwork(cidr).version is 6:
                    gateway_ip = subnet.get_default_gateway()
                    logger.debug(' subnet gateway (%s)' %(gateway_ip))
        port_id2 = self._vnc_lib.virtual_machine_interface_create(port_obj2)
        logger.debug('Created Virtual Machine Interface')

        # Create v6 Ip object
        ip_obj2 = InstanceIp(name=str(uuid.uuid4()), instance_ip_address=gateway_ip,
                             instance_ip_family='v6')
        ip_obj2.uuid = ip_obj2.name
        ip_obj2.set_virtual_machine_interface(port_obj2)
        ip_obj2.set_virtual_network(net_obj2)
        ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2) 
 
        # Add Router Interface (test being subnet)
        lr.add_virtual_machine_interface(port_obj2)
        lr_obj = self._vnc_lib.logical_router_read(id=lr_uuid)
        self._vnc_lib.logical_router_update(lr_obj)
        logger.debug('Linked VMI object (VN2) and LR object')

        # Verify logical-router dumps
        lr.dump()

        # TODO: Schema transformer not integrated in the tests,
        #       hence route-target refs not set yet
        # Verify Route Target Creation
        rt_refs = lr.get_route_target_refs()
        if not rt_refs:
            logger.debug(' !!! Schema Transformer not integrated in test yet !!!')
            logger.debug(' !!! route-target not associated to Logical Router')
        else:
            for rt_ref in rt_refs:
                logger.debug(' Route Target (%s)' %(rt_ref['to']))
                rt_obj = self._vnc_lib.route_target_read(id=rt_ref['uuid'])
                ri_refs = rt_obj.get_routing_instance_back_refs()
                for ri_ref in ri_refs:
                    ri_obj = self.vnc_lib.routing_instance_read(id=ri_ref['uuid'])
                    ri_name = ri_obj.get_display_name()
                    logger.debug(' Routing Instance (%s)' %(ri_name))
                    if ((ri_name() != 'my-vn-1') and (ri_name() != 'my-vn-2')):
                        logger.debug(' Failure, Logical-Router not associated to expected VN')


        #cleanup
        logger.debug('Cleaning up')
        self._vnc_lib.instance_ip_delete(id=ip_id1)
        self._vnc_lib.instance_ip_delete(id=ip_id2)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj2.uuid)
        self._vnc_lib.logical_router_delete(id=lr_uuid)
        self._vnc_lib.virtual_network_delete(id=vn1.uuid)
        self._vnc_lib.virtual_network_delete(id=vn2.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

    def test_route_table_prefixes(self):
        rt = RouteTable("rt1")
        routes = RouteTableType()
        route1 = RouteType(prefix="1.1.1.1/0", next_hop="10.10.10.10", next_hop_type="ip-address")
        route2 = RouteType(prefix="1.1.1.1/0", next_hop="20.20.20.20", next_hop_type="ip-address")
        routes.add_route(route1)
        routes.add_route(route2)
        rt.set_routes(routes)
        try:
            self._vnc_lib.route_table_create(rt)
            self.assertTrue(False, 'Create succeeded unexpectedly - duplicate prefixe routes')
        except cfgm_common.exceptions.BadRequest as e:
            pass

        routes.delete_route(route2)
        route2 = RouteType(prefix="1.1.1.2/0", next_hop="20.20.20.20", next_hop_type="ip-address")
        routes.add_route(route2)
        rt.set_routes(routes)
        try:
            self._vnc_lib.route_table_create(rt)
        except:
            self.assertTrue(False, 'Create failed')

        routes.delete_route(route2)
        route2 = RouteType(prefix="1.1.1.1/0", next_hop="20.20.20.20", next_hop_type="ip-address")
        routes.add_route(route2)
        rt.set_routes(routes)
        try:
            self._vnc_lib.route_table_update(rt)
            self.assertTrue(False, 'Update succeeded unexpectedly - duplicate prefixe routes')
        except cfgm_common.exceptions.BadRequest as e:
            pass
    #end test_route_table_prefixes

    def test_vm_port_not_added_to_lr(self):
        logger.debug("test interface-add not allowing vm's port to be"
                     "attached to logical router")

        project = self._vnc_lib.project_read(fq_name=['default-domain',
                                                      'default-project'])
        ipam = self._vnc_lib.network_ipam_read(fq_name=['default-domain', 'default-project',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

        # Create subnets
        ipam_sn_v4_vn = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create VN my-vn
        vn = VirtualNetwork('%s-vn' % self.id(), project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4_vn]))
        self._vnc_lib.virtual_network_create(vn)
        logger.debug('Created Virtual Network object for my-vn: %s', vn.uuid)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        # Create v4 Ip object
        ip_obj = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ip_obj.uuid = ip_obj.name
        logger.debug('Created Instance IP object 1 %s', ip_obj.uuid)

        # Create VM
        vm_inst_obj = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj.uuid = vm_inst_obj.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj)

        id_perms = IdPermsType(enable=True)
        vm_port_obj = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj, id_perms=id_perms)
        vm_port_obj.uuid = vm_port_obj.name
        vm_port_obj.set_virtual_network(vn)
        ip_obj.set_virtual_machine_interface(vm_port_obj)
        ip_obj.set_virtual_network(net_obj)
        port_id = self._vnc_lib.virtual_machine_interface_create(vm_port_obj)

        logger.debug('Allocating an IP4 address for VM')
        ip_id = self._vnc_lib.instance_ip_create(ip_obj)

        # Create Logical Router
        lr = LogicalRouter('router-test-v4', project)
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        logger.debug('Created Logical Router ')

        # Add Router Interface
        lr.add_virtual_machine_interface(vm_port_obj)
        logger.debug("Trying to Link VM's VMI object and LR object")
        with ExpectedException(cfgm_common.exceptions.BadRequest) as e:
            self._vnc_lib.logical_router_update(lr)
        logger.debug("Linking VM's VMI object and LR object failed as expected")
        lr.del_virtual_machine_interface(vm_port_obj)

        # Create Port
        logger.debug("Add internal interface to LR")
        port_obj = self.create_port(project, net_obj)
        lr.add_virtual_machine_interface(port_obj)
        self._vnc_lib.logical_router_update(lr)
        logger.debug("Link VM to internal interface of a LR")
        with ExpectedException(cfgm_common.exceptions.BadRequest) as e:
            port_obj.add_virtual_machine(vm_inst_obj)
            self._vnc_lib.virtual_machine_interface_update(port_obj)
        logger.debug("Linking VM to internal interface of LR failed as expected")
        self._vnc_lib.logical_router_delete(id=lr.uuid)
    # end test_vm_port_not_added_to_lr

    def create_port(self, project, vn):
        ## Create v4 Ip object
        ip_obj = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ip_obj.uuid = ip_obj.name
        logger.debug('Created Instance IP object 1 %s', ip_obj.uuid)

        # Create Port
        id_perms = IdPermsType(enable=True)
        port_obj = VirtualMachineInterface(
            str(uuid.uuid4()), parent_obj=project, id_perms=id_perms)
        port_obj.uuid = port_obj.name
        port_obj.set_virtual_network(vn)
        ip_obj.set_virtual_machine_interface(port_obj)
        ip_obj.set_virtual_network(vn)
        port_id = self._vnc_lib.virtual_machine_interface_create(port_obj)

        #logger.debug('Allocating an IP4 address for port')
        ip_id = self._vnc_lib.instance_ip_create(ip_obj)
        return port_obj
    # end create_port

    def test_same_network_not_attached_to_lr(self):
        logger.debug("test interface-add gateway-set not allowing"
                     "same network to be attached as both internal and external"
                     "network of logical router")

        project = self._vnc_lib.project_read(fq_name=['default-domain',
                                                      'default-project'])
        ipam = self._vnc_lib.network_ipam_read(fq_name=['default-domain', 'default-project',
                                                        'default-network-ipam'])
        logger.debug('Read network ipam')

        # Create subnets
        ipam_sn_v4_vn = IpamSubnetType(subnet=SubnetType('11.1.1.0', 24))

        # Create VN my-vn
        vn = VirtualNetwork('%s-vn' % self.id(), project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_v4_vn]))
        self._vnc_lib.virtual_network_create(vn)
        logger.debug('Created Virtual Network object for my-vn: %s', vn.uuid)
        net_obj = self._vnc_lib.virtual_network_read(id = vn.uuid)

        ## Create v4 Ip object
        ip_obj = InstanceIp(name=str(uuid.uuid4()), instance_ip_family='v4')
        ip_obj.uuid = ip_obj.name
        logger.debug('Created Instance IP object 1 %s', ip_obj.uuid)

        # Create Port
        port_obj = self.create_port(project, net_obj)

        # Create Logical Router
        lr = LogicalRouter('router-test-v4', project)
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        logger.debug('Created Logical Router ')

        # Add Router Interface
        lr.add_virtual_machine_interface(port_obj)
        self._vnc_lib.logical_router_update(lr)

        # set router_external
        net_obj.set_router_external(True)
        self._vnc_lib.virtual_network_update(net_obj)

        logger.debug("Try adding gateway from same network as of interface to LR object")
        with ExpectedException(cfgm_common.exceptions.BadRequest) as e:
            lr.add_virtual_network(net_obj)
            self._vnc_lib.logical_router_update(lr)
        logger.debug("Adding gateway from same network as of interface to LR object failed as expected")
        lr.del_virtual_network(net_obj)

        logger.debug("Removing the interface attached to LR")
        lr.del_virtual_machine_interface(port_obj)
        self._vnc_lib.logical_router_update(lr)
        logger.debug("Adding external gateway to LR")
        lr.add_virtual_network(net_obj)
        self._vnc_lib.logical_router_update(lr)

        # Create Port
        port_obj = self.create_port(project, net_obj)
        logger.debug("Try adding interafce from same network as of gateway to LR object")
        with ExpectedException(cfgm_common.exceptions.BadRequest) as e:
            lr.add_virtual_machine_interface(port_obj)
            self._vnc_lib.logical_router_update(lr)
        logger.debug("Adding interface from same network as of gateway to LR object failed as expected")
        self._vnc_lib.logical_router_delete(id=lr.uuid)

    # end test_same_network_not_attached_to_lr
#end class TestLogicalRouter

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    unittest.main()
