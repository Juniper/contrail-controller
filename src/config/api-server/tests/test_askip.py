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

class TestRequestedIp(test_case.ApiServerTestCase):

    def test_requested_ip(self):

        domain_name = 'my-domain'
        proj_name = 'my-proj'
        subnet = '192.168.1.0'
        prefix = 24
        vn_name = 'my-fe'

        domain = Domain(domain_name)
        self._vnc_lib.domain_create(domain)
        print 'Created domain'

        project = Project(proj_name, domain)
        self._vnc_lib.project_create(project)
        print 'Created Project'

        ipam = NetworkIpam('default-network-ipam', project, IpamType("dhcp"))
        self._vnc_lib.network_ipam_create(ipam)
        print 'Created network ipam'

        ipam = self._vnc_lib.network_ipam_read(fq_name=[domain_name, proj_name,
                                               'default-network-ipam'])
        print 'Read network ipam'
        ipam_sn_1 = IpamSubnetType(subnet=SubnetType(subnet, prefix))

        vn = VirtualNetwork(vn_name, project)
        vn.add_network_ipam(ipam, VnSubnetsType([ipam_sn_1]))
        self._vnc_lib.virtual_network_create(vn)
        net_obj = self._vnc_lib.virtual_network_read(id=vn.uuid)

        ip_obj = InstanceIp(name=str(uuid.uuid4()))
        ip_obj.uuid = ip_obj.name
        print 'Created Instance IP object ', ip_obj.uuid

        vm_inst_obj = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj.uuid = vm_inst_obj.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj)

        id_perms = IdPermsType(enable=True)
        port_obj = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj, id_perms=id_perms)
        port_obj.uuid = port_obj.name

        port_obj.set_virtual_network(vn)
        ip_obj.set_virtual_machine_interface(port_obj)
        ip_obj.set_virtual_network(net_obj)
        port_id = self._vnc_lib.virtual_machine_interface_create(port_obj)

        print 'Allocating an IP address'
        ip_id = self._vnc_lib.instance_ip_create(ip_obj)
        ip_obj = self._vnc_lib.instance_ip_read(id=ip_id)
        ip_addr = ip_obj.get_instance_ip_address()
        print ' got ', ip_addr

        # Add test to ask for ip that is already allocated
        # print 'Try to reserve above address ... should fail'

        print
        ask_ip = '192.168.1.6'
        print ' Try to request address 192.168.1.6'
        ip_obj2 = InstanceIp(name=str(uuid.uuid4()))
        ip_obj2.uuid = ip_obj2.name
        print 'Created Instance IP object', ip_obj2.uuid

        vm_inst_obj2 = VirtualMachine(str(uuid.uuid4()))
        vm_inst_obj2.uuid = vm_inst_obj2.name
        self._vnc_lib.virtual_machine_create(vm_inst_obj2)

        id_perms2 = IdPermsType(enable=True)
        port_obj2 = VirtualMachineInterface(
            str(uuid.uuid4()), vm_inst_obj2, id_perms=id_perms2)
        port_obj2.uuid = port_obj2.name
        port_obj2.set_virtual_network(vn)
        ip_obj2.set_virtual_machine_interface(port_obj2)
        ip_obj2.set_virtual_network(net_obj)
        port_id2 = self._vnc_lib.virtual_machine_interface_create(port_obj2)

        ip_obj2.set_instance_ip_address(ask_ip)
        try:
            ip_id2 = self._vnc_lib.instance_ip_create(ip_obj2)
            ip_obj2 = self._vnc_lib.instance_ip_read(id=ip_id2)
            ip_addr2 = ip_obj2.get_instance_ip_address()
            if ip_addr2 == ask_ip:
                print ' Test passed!'
                self._vnc_lib.instance_ip_delete(id=ip_id2)
            else:
                print ' Test failed! got %s' % (ip_addr2)
        except:
            print ' Failed to reserve IP address. Test failed'

        #cleanup
        print
        print 'Cleaning up'
        self._vnc_lib.instance_ip_delete(id=ip_id)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj2.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm_inst_obj2.uuid)
        self._vnc_lib.virtual_network_delete(id=vn.uuid)
        self._vnc_lib.network_ipam_delete(id=ipam.uuid)
        self._vnc_lib.project_delete(id=project.uuid)
        self._vnc_lib.domain_delete(id=domain.uuid)
    #end

#end class TestRequestedIp

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    unittest.main()
