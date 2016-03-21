#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
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

import fixtures
import testtools
from testtools.matchers import Equals, MismatchError, Not, Contains, LessThan
from testtools import content, content_type, ExpectedException
import unittest
from flexmock import flexmock
import re
import json
import copy
from lxml import etree
import inspect
import pycassa
import kombu
import requests
import bottle
import stevedore

from vnc_api.vnc_api import *
from cfgm_common import exceptions as vnc_exceptions
import vnc_api.gen.vnc_api_test_gen
from vnc_api.gen.resource_test import *
import cfgm_common
from cfgm_common import vnc_plugin_base
from cfgm_common import imid

sys.path.append('../common/tests')
from test_utils import *
import test_common
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestCrudBasic(object):
#class TestCrudBasic(gen.vnc_api_test_gen.VncApiTestGen):

    def setUp(self):
        super(TestCrudBasic, self).setUp()
        test_common.setup_flexmock()

        api_server_ip = socket.gethostbyname(socket.gethostname())
        api_server_port = get_free_port()
        http_server_port = get_free_port()
        self._api_svr = gevent.spawn(test_common.launch_api_server,
                                     api_server_ip, api_server_port,
                                     http_server_port)
        block_till_port_listened(api_server_ip, api_server_port)
        self._vnc_lib = VncApi('u', 'p', api_server_host=api_server_ip,
                               api_server_port=api_server_port)
    # end setUp

    def tearDown(self):
        super(TestCrudBasic, self).tearDown()
        #gevent.kill(self._api_svr, gevent.GreenletExit)
        # gevent.joinall([self._api_svr])
    # end tearDown

    def test_virtual_DNS_crud(self):
        vdns_fixt = self.useFixture(VirtualDnsTestFixtureGen(self._vnc_lib))
        vdns = vdns_fixt._obj
        d = vdns.get_virtual_DNS_data()
        d.set_domain_name('test-domain')
        vdns.set_virtual_DNS_data(d)
        self._vnc_lib.virtual_DNS_update(vdns)
    # end test_virtual_DNS_crud

    def test_virtual_DNS_record_crud(self):
        vdns_fixt = self.useFixture(VirtualDnsTestFixtureGen(self._vnc_lib))
        self.useFixture(VirtualDnsRecordTestFixtureGen(self._vnc_lib,
                                                       parent_fixt=vdns_fixt))
    # end test_virtual_DNS_record_crud

    def test_access_control_list_crud(self):
        sg_fixt = self.useFixture(SecurityGroupTestFixtureGen(self._vnc_lib))
        self.useFixture(AccessControlListTestFixtureGen(self._vnc_lib,
                                                        parent_fixt=sg_fixt))

        vn_fixt = self.useFixture(VirtualNetworkTestFixtureGen(self._vnc_lib))
        self.useFixture(AccessControlListTestFixtureGen(self._vnc_lib,
                                                        parent_fixt=vn_fixt))
    #end test_access_control_list_crud

    def test_instance_ip_crud(self):
        pass
    # end test_instance_ip_crud

    def test_floating_ip_crud(self):
        pass
    # end test_floating_ip_crud

    def test_id_perms(self):
        # create object in enabled state
        # create object in disabled state
        # update to enable
        # create with description set
        # create with perms set
        # update perms
        # update id, verify fails
        pass
    # end test_id_perms
# end class TestCrudBasic


class TestFixtures(object):
#class TestFixtures(testtools.TestCase, fixtures.TestWithFixtures):
    def setUp(self):
        super(TestFixtures, self).setUp()
        test_common.setup_flexmock()

        api_server_ip = socket.gethostbyname(socket.gethostname())
        api_server_port = get_free_port()
        http_server_port = get_free_port()
        self._api_svr = gevent.spawn(test_common.launch_api_server,
                                     api_server_ip, api_server_port,
                                     http_server_port)
        block_till_port_listened(api_server_ip, api_server_port)
        self._vnc_lib = VncApi('u', 'p', api_server_host=api_server_ip,
                               api_server_port=api_server_port)
    # end setUp

    def tearDown(self):
        super(TestFixtures, self).tearDown()
        #gevent.kill(self._api_svr, gevent.GreenletExit)
        # gevent.joinall([self._api_svr])
    # end tearDown

    def assertThat(self, *args):
        super(TestFixtures, self).assertThat(*args)
    # end assertThat

    def test_fixture_ref(self):
        proj_fixt = self.useFixture(
            ProjectTestFixtureGen(self._vnc_lib, project_name='admin'))

        # 2 policies, 2 VNs associate and check
        pol_1_fixt = self.useFixture(NetworkPolicyTestFixtureGen(
            self._vnc_lib, network_policy_name='policy1111',
            parent_fixt=proj_fixt))
        pol_2_fixt = self.useFixture(NetworkPolicyTestFixtureGen(
            self._vnc_lib, network_policy_name='policy2222',
            parent_fixt=proj_fixt))

        ref_tuple = [(pol_1_fixt._obj,
                      VirtualNetworkPolicyType(
                          sequence=SequenceType(major=0, minor=0)))]
        ref_tuple2 = [(pol_2_fixt._obj,
                       VirtualNetworkPolicyType(
                           sequence=SequenceType(major=0, minor=0)))]

        vn_blue = self.useFixture(
            VirtualNetworkTestFixtureGen(
                self._vnc_lib, virtual_network_name='vnblue',
                parent_fixt=proj_fixt, id_perms=IdPermsType(enable=True),
                network_policy_ref_infos=ref_tuple))
        vn_red = self.useFixture(
            VirtualNetworkTestFixtureGen(
                self._vnc_lib, virtual_network_name='vnred',
                parent_fixt=proj_fixt, id_perms=IdPermsType(enable=True),
                network_policy_ref_infos=ref_tuple2))

        policy_name = vn_blue.get_network_policys()[0].fixture()[0].name
        self.assertThat(policy_name, Equals('policy1111'))

        policy_name = vn_red.get_network_policys()[0].fixture()[0].name
        self.assertThat(policy_name, Equals('policy2222'))

        # ipam referring to virtual dns
        vdns_data = VirtualDnsType(domain_name='abc.net', record_order='fixed',
                                   default_ttl_seconds=360)
        vdns_fixt = self.useFixture(
            VirtualDnsTestFixtureGen(self._vnc_lib, virtual_DNS_name='vdns1',
                                     virtual_DNS_data=vdns_data))

        dns_method = "virtual-dns-server"
        dns_server = IpamDnsAddressType(
            virtual_dns_server_name=vdns_fixt.getObj().get_fq_name_str())
        ipam_mgmt = IpamType(
            ipam_dns_method=dns_method, ipam_dns_server=dns_server)
        ipam_fixt = self.useFixture(
            NetworkIpamTestFixtureGen(
                self._vnc_lib, network_ipam_name='ipam1',
                parent_fixt=proj_fixt,
                network_ipam_mgmt=ipam_mgmt,
                virtual_DNS_refs=[vdns_fixt.getObj()]))

    # end test_fixture_ref

    def test_fixture_reuse_policy(self):
        proj_fixt = self.useFixture(
            ProjectTestFixtureGen(self._vnc_lib, project_name='admin'))
        pol_fixt = self.useFixture(NetworkPolicyTestFixtureGen(
            self._vnc_lib, network_policy_name='policy1111',
            parent_fixt=proj_fixt))

        ref_tuple = [(pol_fixt._obj,
                     VirtualNetworkPolicyType(
                         sequence=SequenceType(major=0, minor=0)))]

        vn1 = self.useFixture(
            VirtualNetworkTestFixtureGen(
                self._vnc_lib, virtual_network_name='vn1',
                parent_fixt=proj_fixt, id_perms=IdPermsType(enable=True),
                network_policy_ref_infos=ref_tuple))
        vn2 = self.useFixture(
            VirtualNetworkTestFixtureGen(
                self._vnc_lib, virtual_network_name='vn2',
                parent_fixt=proj_fixt, id_perms=IdPermsType(enable=True),
                network_policy_ref_infos=ref_tuple))
        vn3 = self.useFixture(
            VirtualNetworkTestFixtureGen(
                self._vnc_lib, virtual_network_name='vn3',
                parent_fixt=proj_fixt, id_perms=IdPermsType(enable=True),
                network_policy_ref_infos=ref_tuple))
        vn4 = self.useFixture(
            VirtualNetworkTestFixtureGen(
                self._vnc_lib, virtual_network_name='vn4',
                parent_fixt=proj_fixt, id_perms=IdPermsType(enable=True),
                network_policy_ref_infos=ref_tuple))
        vn5 = self.useFixture(
            VirtualNetworkTestFixtureGen(
                self._vnc_lib, virtual_network_name='vn5',
                parent_fixt=proj_fixt, id_perms=IdPermsType(enable=True),
                network_policy_ref_infos=ref_tuple))

        npolicy_children = len(proj_fixt.getObj().get_network_policys())
        self.assertThat(npolicy_children, Equals(1))

    # end test_fixture_reuse_policy
# end class TestFixtures

class TestNetAddrAlloc(object):
#class TestNetAddrAlloc(testtools.TestCase, fixtures.TestWithFixtures):

    def setUp(self):
        super(TestNetAddrAlloc, self).setUp()
        test_common.setup_flexmock()

        api_server_ip = socket.gethostbyname(socket.gethostname())
        api_server_port = get_free_port()
        http_server_port = get_free_port()
        self._api_svr = gevent.spawn(test_common.launch_api_server,
                                     api_server_ip, api_server_port,
                                     http_server_port)
        block_till_port_listened(api_server_ip, api_server_port)
        self._vnc_lib = VncApi('u', 'p', api_server_host=api_server_ip,
                               api_server_port=api_server_port)
    # end setUp

    def tearDown(self):
        super(TestNetAddrAlloc, self).tearDown()
        #gevent.kill(self._api_svr, gevent.GreenletExit)
        # gevent.joinall([self._api_svr])
    # end tearDown

    def test_ip_alloc_on_net(self):
        # create subnet on default-virtual-network, auto allocate ip
        ipam_fixt = self.useFixture(NetworkIpamTestFixtureGen(self._vnc_lib))
        vn_fixt = self.useFixture(VirtualNetworkTestFixtureGen(self._vnc_lib))

        subnet_vnc = IpamSubnetType(subnet=SubnetType('1.1.1.0', 24))
        vnsn_data = VnSubnetsType([subnet_vnc])
        logger.info("Creating subnet 1.1.1.0/24")
        vn_fixt.add_network_ipam(ipam_fixt.getObj(), vnsn_data)

        logger.info("Creating auto-alloc instance-ip, expecting 1.1.1.253...")
        iip_fixt = self.useFixture(
            InstanceIpTestFixtureGen(
                self._vnc_lib, 'iip1', auto_prop_val=False,
                virtual_network_refs=[vn_fixt.getObj()]))
        ip_allocated = iip_fixt.getObj().instance_ip_address
        self.assertThat(ip_allocated, Equals('1.1.1.253'))
        logger.info("...verified")

        logger.info("Creating specific instance-ip, expecting 1.1.1.2...")
        iip_fixt = self.useFixture(
            InstanceIpTestFixtureGen(
                self._vnc_lib, 'iip2', auto_prop_val=False,
                instance_ip_address='1.1.1.2',
                virtual_network_refs=[vn_fixt.getObj()]))
        ip_allocated = iip_fixt.getObj().instance_ip_address
        self.assertThat(ip_allocated, Equals('1.1.1.2'))
        logger.info("...verified")

        logger.info("Creating floating-ip-pool")
        fip_pool_fixt = self.useFixture(
            FloatingIpPoolTestFixtureGen(self._vnc_lib, 'fip-pool',
                                         parent_fixt=vn_fixt,
                                         auto_prop_val=False))

        logger.info("Creating auto-alloc floating-ip, expecting 1.1.1.252...")
        fip_fixt = self.useFixture(
            FloatingIpTestFixtureGen(
                self._vnc_lib, 'fip1', parent_fixt=fip_pool_fixt,
                auto_prop_val=False))
        ip_allocated = fip_fixt.getObj().floating_ip_address
        self.assertThat(ip_allocated, Equals('1.1.1.252'))
        logger.info("...verified")

        logger.info("Creating specific floating-ip, expecting 1.1.1.3...")
        fip_fixt = self.useFixture(
            FloatingIpTestFixtureGen(
                self._vnc_lib, 'fip2', parent_fixt=fip_pool_fixt,
                auto_prop_val=False, floating_ip_address='1.1.1.3'))
        ip_allocated = fip_fixt.getObj().floating_ip_address
        self.assertThat(ip_allocated, Equals('1.1.1.3'))
        logger.info("...verified")

        logger.info("Creating subnet 2.2.2.0/24, gateway 2.2.2.128")
        subnet_vnc = IpamSubnetType(subnet=SubnetType('2.2.2.0', 24),
                                    default_gateway='2.2.2.128')
        vnsn_data.add_ipam_subnets(subnet_vnc)
        vn_fixt.add_network_ipam(ipam_fixt.getObj(), vnsn_data)

        logger.info("Creating specific instance-ip, expecting 2.2.2.254...")
        iip_fixt = self.useFixture(
            InstanceIpTestFixtureGen(
                self._vnc_lib, 'iip3', auto_prop_val=False,
                instance_ip_address='2.2.2.254',
                virtual_network_refs=[vn_fixt.getObj()]))
        ip_allocated = iip_fixt.getObj().instance_ip_address
        self.assertThat(ip_allocated, Equals('2.2.2.254'))
        logger.info("...verified")

        # Create a subnet with ip. try deleting subnet verify it fails.
        # Remove the ip and retry delete and verify it passes.
        logger.info("Creating subnet 3.3.3.0/24, and instance-ip in it")
        vn_obj = vn_fixt.getObj()
        subnet_vnc_3 = IpamSubnetType(subnet=SubnetType('3.3.3.0', 24))
        vnsn_data.add_ipam_subnets(subnet_vnc_3)
        vn_fixt.add_network_ipam(ipam_fixt.getObj(), vnsn_data)
        iip_obj = InstanceIp(instance_ip_address='3.3.3.1')
        iip_obj.add_virtual_network(vn_obj)
        self._vnc_lib.instance_ip_create(iip_obj)

        logger.info("Trying to remove 3.3.3.0/24, expecting failure...")
        vnsn_data.delete_ipam_subnets(subnet_vnc_3)
        vn_obj.set_network_ipam(ipam_fixt.getObj(), vnsn_data)
        #with self.assertRaises(vnc_exceptions.RefsExistError) as e:
        try:
            self._vnc_lib.virtual_network_update(vn_obj)
        except vnc_exceptions.RefsExistError as e:
            logger.info("...verified")

        logger.info("Removing IIP and delete 3.3.3.0/24, expecting success...")
        self._vnc_lib.instance_ip_delete(id=iip_obj.uuid)
        self._vnc_lib.virtual_network_update(vn_obj)
        logger.info("...verified")
    # end test_ip_alloc_on_net

    def test_ip_alloc_on_ip_fabric(self):
        pass
    # end test_ip_alloc_on_ip_fabric

    def test_ip_alloc_on_link_local(self):
        pass
    # end test_ip_alloc_on_link_local

    def test_alloc_with_subnet_id(self):
        ipam_fixt = self.useFixture(NetworkIpamTestFixtureGen(self._vnc_lib))

        subnet_vnc = IpamSubnetType(subnet=SubnetType('1.1.1.0', 24))
        vnsn_data = VnSubnetsType([subnet_vnc])
        subnet_vnc_1 = IpamSubnetType(subnet=SubnetType('2.2.2.0', 24),
                                      default_gateway='2.2.2.128')
        vnsn_data.add_ipam_subnets(subnet_vnc_1)
        vn_fixt = self.useFixture(VirtualNetworkTestFixtureGen(self._vnc_lib,
                  network_ipam_ref_infos=[(ipam_fixt.getObj(), vnsn_data)]))
        vn_fixt.getObj().set_router_external(True)
        self._vnc_lib.virtual_network_update(vn_fixt.getObj())

        vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fixt.getobj().get_fq_name())
        ipam_subnets = vn_obj.network_ipam_refs[0]['attr'].get_ipam_subnets()
        # This should be using the first subnet, ie 1.1.1.x
        iip_fixt_1 = self.useFixture(
            InstanceIpTestFixtureGen(
                self._vnc_lib, 'iip1', auto_prop_val=False,
                virtual_network_refs=[vn_fixt.getObj()]))
        self.assertEqual(iip_fixt_1.getObj().instance_ip_address[:6], "1.1.1.")

        # This should be using the first subnet since its uuid is used
        iip_fixt_2 = self.useFixture(
            InstanceIpTestFixtureGen(
                self._vnc_lib, 'iip2', auto_prop_val=False,
                subnet_uuid=ipam_subnets[0].subnet_uuid,
                virtual_network_refs=[vn_fixt.getObj()]))
        self.assertEqual(iip_fixt_2.getObj().instance_ip_address[:6], "1.1.1.")

        # Since second subnet's uuid is used, we should get IP from that
        iip_fixt_3 = self.useFixture(
            InstanceIpTestFixtureGen(
                self._vnc_lib, 'iip3', auto_prop_val=False,
                subnet_uuid=ipam_subnets[1].subnet_uuid,
                virtual_network_refs=[vn_fixt.getObj()]))
        self.assertEqual(iip_fixt_3.getObj().instance_ip_address[:6], "2.2.2.")

        # Mismatched UUID and IP address combination, should catch an exception
        with self.assertRaises(cfgm_common.exceptions.HttpError) as e:
            iip_fixt_4 = self.useFixture(
                InstanceIpTestFixtureGen(
                    self._vnc_lib, 'iip4', auto_prop_val=False,
                    subnet_uuid=ipam_subnets[1].subnet_uuid,
                    instance_ip_address='1.1.1.4',
                    virtual_network_refs=[vn_fixt.getObj()]))
    #end test_alloc_with_subnet_id

# end class TestNetAddrAlloc

class DemoFixture(object):
#class DemoFixture(fixtures.Fixture):

    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib
    # end __init__

    def setUp(self):
        super(DemoFixture, self).setUp()
        dom_fixt = self.useFixture(
            gen.vnc_api_test_gen.DomainTestFixtureGen(self._vnc_lib))
        proj_1_fixt = self.useFixture(
            gen.vnc_api_test_gen.ProjectTestFixtureGen(self._vnc_lib,
                                                       'proj-1', dom_fixt))
        proj_2_fixt = self.useFixture(
            gen.vnc_api_test_gen.ProjectTestFixtureGen(self._vnc_lib,
                                                       'proj-2', dom_fixt))
        self.useFixture(gen.vnc_api_test_gen.VirtualNetworkTestFixtureGen(
            self._vnc_lib, 'front-end', proj_1_fixt))
        self.useFixture(gen.vnc_api_test_gen.VirtualNetworkTestFixtureGen(
            self._vnc_lib, 'back-end', proj_1_fixt))
        self.useFixture(gen.vnc_api_test_gen.VirtualNetworkTestFixtureGen(
            self._vnc_lib, 'public', proj_2_fixt))
    # end setUp

    def cleanUp(self):
        super(DemoFixture, self).cleanUp()
    # end cleanUp

# end class DemoFixture


# class TestDemo(testtools.TestCase, fixtures.TestWithFixtures):
class TestDemo(object):

    def setUp(self):
        super(TestDemo, self).setUp()
        test_common.setup_flexmock()

        api_server_ip = socket.gethostbyname(socket.gethostname())
        api_server_port = get_free_port()
        http_server_port = get_free_port()
        self._api_svr = gevent.spawn(test_common.launch_api_server,
                                     api_server_ip, api_server_port,
                                     http_server_port)
        block_till_port_listened(api_server_ip, api_server_port)
        self._vnc_lib = VncApi('u', 'p', api_server_host=api_server_ip,
                               api_server_port=api_server_port)
    # end setUp

    def tearDown(self):
        super(TestDemo, self).tearDown()
        #gevent.kill(self._api_svr, gevent.GreenletExit)
        # gevent.joinall([self._api_svr])
    # end tearDown

    def test_demo(self):
        self.useFixture(DemoFixture(self._vnc_lib))
    # end test_demo

# end class TestDemo


# class TestRefUpdate(unittest.TestCase):
class TestRefUpdate(object):

    def setUp(self):
        test_common.setup_flexmock()

        api_server_ip = socket.gethostbyname(socket.gethostname())
        api_server_port = get_free_port()
        http_server_port = get_free_port()
        gevent.spawn(test_common.launch_api_server,
                     api_server_ip, api_server_port, http_server_port)
        block_till_port_listened(api_server_ip, api_server_port)
        self._vnc_lib = VncApi('u', 'p', api_server_host=api_server_ip,
                               api_server_port=api_server_port)
    # end setUp

    def tearDown(self):
        pass
    # end tearDown

    def test_vn_ipam_ref(self):
        vnc_lib = self._vnc_lib

        # create with ref
        vn_obj = VirtualNetwork('vn1')
        ipam_obj = NetworkIpam('ipam1')
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType())
        vnc_lib.network_ipam_create(ipam_obj)
        vnc_lib.virtual_network_create(vn_obj)

        vn_obj = vnc_lib.virtual_network_read(id=vn_obj.uuid)
        fq_name = vn_obj.get_network_ipam_refs()[0]['to']
        ipam_name = vnc_lib.network_ipam_read(fq_name=fq_name).name
        self.assertEqual(ipam_obj.name, ipam_name)

        # ref after create
        vn_obj = VirtualNetwork('vn2')
        ipam_obj = NetworkIpam('ipam2')
        vnc_lib.network_ipam_create(ipam_obj)
        vnc_lib.virtual_network_create(vn_obj)
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType())
        vnc_lib.virtual_network_update(vn_obj)

        vn_obj = vnc_lib.virtual_network_read(id=vn_obj.uuid)
        fq_name = vn_obj.get_network_ipam_refs()[0]['to']
        ipam_name = vnc_lib.network_ipam_read(fq_name=fq_name).name
        self.assertEqual(ipam_obj.name, ipam_name)

        # del should fail when someone is referring to us
        with self.assertRaises(vnc_exceptions.RefsExistError) as e:
            vnc_lib.network_ipam_delete(id=ipam_obj.uuid)

        # del should succeed when refs are detached
        vn_obj.del_network_ipam(ipam_obj)
        vnc_lib.virtual_network_update(vn_obj)
        vnc_lib.network_ipam_delete(id=ipam_obj.uuid)
    # end test_vn_ipam_ref(self):

# end class TestRefUpdate


class TestListUpdate(test_case.ApiServerTestCase):
    def test_policy_create_w_rules(self):
        proj_fixt = self.useFixture(ProjectTestFixtureGen(self._vnc_lib))

        policy_obj = NetworkPolicy(
            'test-policy-create-w-rules', proj_fixt.getObj())
        np_rules = [
            PolicyRuleType(direction='<>',
                           action_list=ActionListType(simple_action='pass'),
                           protocol='any',
                           src_addresses=
                               [AddressType(virtual_network='local')],
                           src_ports=[PortType(-1, -1)],
                           dst_addresses=[AddressType(virtual_network='any')],
                           dst_ports=[PortType(-1, -1)]),

            PolicyRuleType(direction='<>',
                           action_list=ActionListType(simple_action='deny'),
                           protocol='any',
                           src_addresses=
                               [AddressType(virtual_network='local')],
                           src_ports=[PortType(-1, -1)],
                           dst_addresses=[AddressType(virtual_network='any')],
                           dst_ports=[PortType(-1, -1)]),
        ]
        policy_obj.set_network_policy_entries(PolicyEntriesType(np_rules))

        self._vnc_lib.network_policy_create(policy_obj)

        # cleanup
        self._vnc_lib.network_policy_delete(id=policy_obj.uuid)
    # end test_policy_create_w_rules

    def test_policy_create_wo_rules(self):
        proj_fixt = self.useFixture(ProjectTestFixtureGen(self._vnc_lib))

        policy_obj = NetworkPolicy(
            'test-policy-create-wo-rules', proj_fixt.getObj())
        self._vnc_lib.network_policy_create(policy_obj)

        np_rules = [
            PolicyRuleType(direction='<>',
                   action_list=ActionListType(simple_action='pass'),
                   protocol='any',
                   src_addresses=
                       [AddressType(virtual_network='local')],
                   src_ports=[PortType(1, 2)],
                   dst_addresses=[AddressType(virtual_network='any')],
                   dst_ports=[PortType(3, 4)]),

            PolicyRuleType(direction='<>', 
                           action_list=ActionListType(simple_action='deny'),
                           protocol='any',
                           src_addresses=
                               [AddressType(virtual_network='local')],
                           src_ports=[PortType(5, 6)],
                           dst_addresses=[AddressType(virtual_network='any')],
                           dst_ports=[PortType(7, 8)]),
        ]
        policy_entries = PolicyEntriesType(np_rules)
        policy_obj.set_network_policy_entries(policy_entries)

        self._vnc_lib.network_policy_update(policy_obj)

        policy_entries.policy_rule.append(
            PolicyRuleType(direction='<>',
                           action_list=ActionListType(simple_action= 'deny'),
                           protocol='any',
                           src_addresses=
                               [AddressType(virtual_network='local')],
                           src_ports=[PortType(9, 10)],
                           dst_addresses=[AddressType(virtual_network='any')],
                           dst_ports=[PortType(11, 12)])
        )
        policy_obj.set_network_policy_entries(policy_entries)

        self._vnc_lib.network_policy_update(policy_obj)

        # cleanup
        self._vnc_lib.network_policy_delete(id=policy_obj.uuid)
    # end test_policy_create_wo_rules

# end class TestListUpdate

class TestCrud(test_case.ApiServerTestCase):
    def test_create(self):
        test_obj = self._create_test_object()
        self.assertTill(self.ifmap_has_ident, obj=test_obj)

class TestVncCfgApiServer(test_case.ApiServerTestCase):
    def test_fq_name_to_id_http_post(self):
        test_obj = self._create_test_object()
        test_uuid = self._vnc_lib.fq_name_to_id('virtual-network', test_obj.get_fq_name())
        # check that format is correct
        try:
            uuid.UUID(test_uuid)
        except ValueError:
            self.assertTrue(False, 'Bad form UUID ' + test_uuid)

        with ExpectedException(NoIdError) as e:
            test_uuid = self._vnc_lib.fq_name_to_id('project', test_obj.get_fq_name())

    def test_id_to_fq_name_http_post(self):
        test_obj = self._create_test_object()
        fq_name = self._vnc_lib.id_to_fq_name(test_obj.uuid)
        self.assertEqual(test_obj.fq_name, fq_name)
        with ExpectedException(NoIdError) as e:
            self._vnc_lib.id_to_fq_name(str(uuid.uuid4()))

    def test_useragent_kv_http_post(self):
        # unikey store
        test_body = json.dumps({'operation': 'STORE',
                                'key': 'fookey',
                                'value': 'fooval'})
        self.addDetail('useragent-kv-post-store', content.json_content(test_body))
        (code, msg) = self._http_post('/useragent-kv', test_body)
        self.assertEqual(code, 200)

        # unikey retrieve
        test_body = json.dumps({'operation': 'RETRIEVE',
                                'key': 'fookey'})
        self.addDetail('useragent-kv-post-retrieve', content.json_content(test_body))
        (code, msg) = self._http_post('/useragent-kv', test_body)
        self.assertEqual(code, 200)
        self.assertEqual(json.loads(msg)['value'], 'fooval')

        # multikey retrieve
        test_body = json.dumps({'operation': 'STORE',
                                'key': 'barkey',
                                'value': 'barval'})
        self.addDetail('useragent-kv-post-store', content.json_content(test_body))
        (code, msg) = self._http_post('/useragent-kv', test_body)
        self.assertEqual(code, 200)
        test_body = json.dumps({'operation': 'RETRIEVE',
                                'key': ['fookey', 'barkey']})
        self.addDetail('useragent-kv-post-multikey-retrieve',
                       content.json_content(test_body))
        (code, msg) = self._http_post('/useragent-kv', test_body)
        self.assertEqual(code, 200)
        self.assertEqual(len(json.loads(msg)['value']), 2)
        self.assertThat(json.loads(msg)['value'], Contains('fooval'))
        self.assertThat(json.loads(msg)['value'], Contains('barval'))

        # wrong op test
        test_body = json.dumps({'operation': 'foo',
                                'key': 'fookey'})
        self.addDetail('useragent-kv-post-wrongop', content.json_content(test_body))
        (code, msg) = self._http_post('/useragent-kv', test_body)
        self.assertEqual(code, 404)
        
    def test_err_on_max_rabbit_pending(self):
        api_server = test_common.vnc_cfg_api_server.server
        max_pend_upd = 10
        api_server._args.rabbit_max_pending_updates = str(max_pend_upd)
        orig_rabbitq_pub = api_server._db_conn._msgbus._producer.publish
        orig_rabbitq_conn = api_server._db_conn._msgbus._conn.connect
        try:
            def err_rabbitq_pub(*args, **kwargs):
                raise Exception("Faking Rabbit publish failure")

            def err_rabbitq_conn(*args, **kwargs):
                gevent.sleep(0.1)
                raise Exception("Faking RabbitMQ connection failure")

            api_server._db_conn._msgbus._producer.publish = err_rabbitq_pub
            api_server._db_conn._msgbus._conn.connect = err_rabbitq_conn

            logger.info("Creating objects to hit max rabbit pending.")
            # every create updates project quota
            test_objs = self._create_test_objects(count=max_pend_upd+1)

            def asserts_on_max_pending():
                self.assertEqual(e.status_code, 500)
                self.assertIn("Too many pending updates", e.content)

            logger.info("Creating one more object expecting failure.")
            obj = VirtualNetwork('vn-to-fail')
            self.addDetail('expecting-failed-create', content.text_content(obj.name))
            try:
                self._vnc_lib.virtual_network_create(obj)
            except HttpError as e:
                asserts_on_max_pending()
            else:
                self.assertTrue(False, 'Create succeeded unexpectedly')

            logger.info("Update of object should fail.")
            test_objs[0].display_name = 'foo'
            try:
                self._vnc_lib.virtual_network_update(test_objs[0])
            except HttpError as e:
                asserts_on_max_pending()
            else:
                self.assertTrue(False, 'Update succeeded unexpectedly')

            logger.info("Delete of object should fail.")
            test_objs[0].display_name = 'foo'
            try:
                self._vnc_lib.virtual_network_delete(id=test_objs[0].uuid)
            except HttpError as e:
                asserts_on_max_pending()
            else:
                self.assertTrue(False, 'Delete succeeded unexpectedly')

            logger.info("Read obj object should be ok.")
            self._vnc_lib.virtual_network_read(id=test_objs[0].uuid)

        finally:
            api_server._db_conn._msgbus._producer.publish = orig_rabbitq_pub
            api_server._db_conn._msgbus._conn.connect = orig_rabbitq_conn

    def test_err_on_ifmap_publish(self):
        api_server = test_common.vnc_cfg_api_server.server
        orig_call_async_result = api_server._db_conn._ifmap_db._mapclient.call_async_result
        def err_call_async_result(*args, **kwargs):
            # restore orig method and return error to check handling
            api_server._db_conn._ifmap_db._mapclient.call_async_result = orig_call_async_result
            publish_err_xml = '<?xml version="1.0" encoding="UTF-8" standalone="yes"?><ns3:Envelope xmlns:ns2="http://www.trustedcomputinggroup.org/2010/IFMAP/2" xmlns:ns3="http://www.w3.org/2003/05/soap-envelope"><ns3:Body><ns2:response><errorResult errorCode="AccessDenied"><errorString>Existing SSRC</errorString></errorResult></ns2:response></ns3:Body></ns3:Envelope>'
            return publish_err_xml

        api_server._db_conn._ifmap_db._mapclient.call_async_result = err_call_async_result
        test_obj = self._create_test_object()
        self.assertTill(self.ifmap_has_ident, obj=test_obj)

    def test_reconnect_to_rabbit(self):
        exceptions = [(FakeKombu.Connection.ConnectionException(), 'conn'),
                      (FakeKombu.Connection.ChannelException(), 'chan'),
                      (Exception(), 'generic')]

        # fake problem on publish to rabbit
        # restore, ensure retry and successful publish
        for exc_obj, exc_type in exceptions:
            obj = VirtualNetwork('%s-pub-%s' %(self.id(), exc_type))
            obj.uuid = str(uuid.uuid4())
            publish_captured = [False]
            def err_on_publish(orig_method, *args, **kwargs):
                msg = args[0]
                if msg['oper'] == 'CREATE' and msg['uuid'] == obj.uuid:
                    publish_captured[0] = True
                    raise exc_obj
                return orig_method(*args, **kwargs)

            rabbit_producer = self._api_server._db_conn._msgbus._producer
            with test_common.patch(rabbit_producer,
                'publish', err_on_publish):
                self._vnc_lib.virtual_network_create(obj)
                self.assertTill(lambda: publish_captured[0] == True)
            # unpatch err publish

            self.assertTill(self.ifmap_has_ident, obj=obj)
        # end exception types on publish

        # fake problem on consume from rabbit
        # restore, ensure retry and successful consume
        for exc_obj, exc_type in exceptions:
            obj = VirtualNetwork('%s-sub-%s' %(self.id(), exc_type))
            obj.uuid = str(uuid.uuid4())
            consume_captured = [False]
            consume_test_payload = [None]
            rabbit_consumer = self._api_server._db_conn._msgbus._consumer
            def err_on_consume(orig_method, *args, **kwargs):
                msg = orig_method()
                payload = msg.payload
                if payload['oper'] == 'UPDATE' and payload['uuid'] == obj.uuid:
                    if (consume_test_payload[0] == payload):
                        return msg
                    consume_captured[0] = True
                    consume_test_payload[0] = payload
                    rabbit_consumer.queues.put(payload, None)
                    raise exc_obj
                return msg

            with test_common.patch(rabbit_consumer.queues,
                'get', err_on_consume):
                # create the object to insert 'get' handler,
                # update oper will test the error handling
                self._vnc_lib.virtual_network_create(obj)
                obj.display_name = 'test_update'
                self._vnc_lib.virtual_network_update(obj)
                self.assertTill(lambda: consume_captured[0] == True)
            # unpatch err consume

            def ifmap_has_ident_update():
                ifmap_id = imid.get_ifmap_id_from_fq_name(obj.get_type(),
                    obj.get_fq_name())
                node = FakeIfmapClient._graph.get(ifmap_id)
                if not node:
                    return False
                meta = node.get('links', {}).get('contrail:display-name',
                    {}).get('meta')
                if not meta:
                    return False
                if not 'test_update' in etree.tostring(meta):
                    return False

                return True

            self.assertTill(ifmap_has_ident_update)
        # end exception types on consume

        # fake problem on consume and publish at same time
        # restore, ensure retry and successful publish + consume
        obj = VirtualNetwork('%s-pub-sub' %(self.id()))
        obj.uuid = str(uuid.uuid4())

        msgbus = self._api_server._db_conn._msgbus
        pub_greenlet = msgbus._publisher_greenlet
        sub_greenlet = msgbus._connection_monitor_greenlet
        setattr(pub_greenlet, 'unittest', {'name': 'producer'})
        setattr(sub_greenlet, 'unittest', {'name': 'consumer'})

        consume_captured = [False]
        consume_test_payload = [None]
        publish_connect_done = [False]
        publish_captured = [False]
        def err_on_consume(orig_method, *args, **kwargs):
            msg = orig_method()
            payload = msg.payload
            if payload['oper'] == 'UPDATE' and payload['uuid'] == obj.uuid:
                if (consume_test_payload[0] == payload):
                    return msg
                consume_captured[0] = True
                consume_test_payload[0] = payload
                rabbit_consumer = self._api_server._db_conn._msgbus._consumer
                rabbit_consumer.queues.put(payload, None)
                raise exc_obj
            return msg

        def block_on_connect(orig_method, *args, **kwargs):
            # block consumer till publisher does update,
            # fake consumer connect exceptions till publisher connects fine
            utvars = getattr(gevent.getcurrent(), 'unittest', None)
            if utvars and utvars['name'] == 'producer':
                publish_connect_done[0] = True
                return orig_method(*args, **kwargs)

            while not publish_captured[0]:
                gevent.sleep(0.1)

            while not publish_connect_done[0]:
                gevent.sleep(0.1)
                raise Exception('Faking connection fail')

            return orig_method(*args, **kwargs)

        rabbit_consumer = self._api_server._db_conn._msgbus._consumer
        rabbit_conn = self._api_server._db_conn._msgbus._conn
        with test_common.patch(rabbit_consumer.queues,
                               'get', err_on_consume):
            with test_common.patch(rabbit_conn,
                               'connect', block_on_connect):
                # create the object to insert 'get' handler,
                # update oper will test the error handling
                self._vnc_lib.virtual_network_create(obj)
                obj.display_name = 'test_update_1'
                self._vnc_lib.virtual_network_update(obj)
                self.assertTill(lambda: consume_captured[0] == True)

                def err_on_publish(orig_method, *args, **kwargs):
                    msg = args[0]
                    if msg['oper'] == 'UPDATE' and msg['uuid'] == obj.uuid:
                        publish_captured[0] = True
                        raise exc_obj
                    return orig_method(*args, **kwargs)
                rabbit_producer = self._api_server._db_conn._msgbus._producer
                with test_common.patch(rabbit_producer,
                    'publish', err_on_publish):
                    obj.display_name = 'test_update_2'
                    self._vnc_lib.virtual_network_update(obj)
                    self.assertTill(lambda: publish_captured[0] == True)
                # unpatch err publish
            # unpatch connect
        # unpatch err consume

        def ifmap_has_update_2():
            ifmap_id = imid.get_ifmap_id_from_fq_name(obj.get_type(),
                obj.get_fq_name())
            node = FakeIfmapClient._graph.get(ifmap_id)
            if not node:
                return False
            meta = node.get('links', {}).get('contrail:display-name',
                {}).get('meta')
            if meta is None:
                return False
            if not 'test_update_2' in etree.tostring(meta):
                return False

            return True

        self.assertTill(ifmap_has_update_2)
    # end test_reconnect_to_rabbit
 
    def test_handle_trap_on_exception(self):
        api_server = test_common.vnc_cfg_api_server.server

        def exception_on_log_error(*args, **kwargs):
            self.assertTrue(False)

        def exception_on_vn_read(*args, **kwargs):
            raise Exception("fake vn read exception")

        try:
            orig_config_log = api_server.config_log
            api_server.config_log = exception_on_log_error
            with ExpectedException(NoIdError):
                self._vnc_lib.virtual_network_read(fq_name=['foo', 'bar', 'baz'])
        finally:
            api_server.config_log = orig_config_log

        try:
            orig_vn_read = api_server._db_conn._cassandra_db._cassandra_virtual_network_read
            test_obj = self._create_test_object()
            api_server._db_conn._cassandra_db._cassandra_virtual_network_read = exception_on_vn_read
            with ExpectedException(HttpError):
                self._vnc_lib.virtual_network_read(fq_name=test_obj.get_fq_name())
        finally:
            api_server._db_conn._cassandra_db._cassandra_virtual_network_read = orig_vn_read

    def test_sandesh_trace(self):
        from lxml import etree
        api_server = test_common.vnc_cfg_api_server.server
        # the test
        test_obj = self._create_test_object()
        self.assertTill(self.ifmap_has_ident, obj=test_obj)
        self._vnc_lib.virtual_network_delete(id=test_obj.uuid)

        # and validations
        introspect_port = api_server._args.http_server_port
        traces = requests.get('http://localhost:%s/Snh_SandeshTraceRequest?x=RestApiTraceBuf' %(introspect_port))
        self.assertThat(traces.status_code, Equals(200))
        top_elem = etree.fromstring(traces.text)
        self.assertThat(top_elem[0][0][0].text, Contains('POST'))
        self.assertThat(top_elem[0][0][0].text, Contains('200 OK'))
        self.assertThat(top_elem[0][0][1].text, Contains('DELETE'))
        self.assertThat(top_elem[0][0][1].text, Contains('200 OK'))

        traces = requests.get('http://localhost:%s/Snh_SandeshTraceRequest?x=DBRequestTraceBuf' %(introspect_port))
        self.assertThat(traces.status_code, Equals(200))
        top_elem = etree.fromstring(traces.text)
        self.assertThat(top_elem[0][0][-1].text, Contains('delete'))
        self.assertThat(top_elem[0][0][-1].text, Contains(test_obj.name))

        traces = requests.get('http://localhost:%s/Snh_SandeshTraceRequest?x=MessageBusNotifyTraceBuf' %(introspect_port))
        self.assertThat(traces.status_code, Equals(200))
        top_elem = etree.fromstring(traces.text)
        self.assertThat(top_elem[0][0][-1].text, Contains('DELETE'))
        self.assertThat(top_elem[0][0][-1].text, Contains(test_obj.name))

        traces = requests.get('http://localhost:%s/Snh_SandeshTraceRequest?x=IfmapTraceBuf' %(introspect_port))
        self.assertThat(traces.status_code, Equals(200))
        top_elem = etree.fromstring(traces.text)
        print top_elem[0][0][-1].text
        self.assertThat(top_elem[0][0][-1].text, Contains('delete'))
        self.assertThat(top_elem[0][0][-1].text, Contains(test_obj.name))

    def test_dup_create_with_same_uuid(self):
        dom_name = self.id() + '-domain'
        logger.info('Creating Domain %s', dom_name)
        domain_obj = Domain(dom_name)
        self._vnc_lib.domain_create(domain_obj)

        project_name = self.id() + '-project'
        logger.info('Creating Project %s', project_name)
        orig_project_obj = Project(project_name, domain_obj)
        self._vnc_lib.project_create(orig_project_obj)
 
        logger.info('Creating Dup Project in default domain with same uuid')
        dup_project_obj = Project(project_name)
        dup_project_obj.uuid = orig_project_obj.uuid
        with ExpectedException(RefsExistError) as e:
            self._vnc_lib.project_create(dup_project_obj)

    def test_dup_create_port_timing(self):
        # test for https://bugs.launchpad.net/juniperopenstack/r2.0/+bug/1382385
        vn_name = self.id() + '-network'
        vn_obj = VirtualNetwork(vn_name, parent_obj=Project())
        self._vnc_lib.virtual_network_create(vn_obj)

        vmi_name = self.id() + '-port'
        logger.info('Creating port %s', vmi_name)
        vmi_obj = VirtualMachineInterface(vmi_name, parent_obj=Project())
        vmi_obj.add_virtual_network(vn_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        vmi_name = self.id() + '-port'
        logger.info('Creating dup port %s', vmi_name)
        vmi_obj = VirtualMachineInterface(vmi_name, parent_obj=Project())
        vmi_obj.add_virtual_network(vn_obj)

        orig_fq_name_to_uuid = self._api_server._db_conn.fq_name_to_uuid
        def dummy_fq_name_to_uuid(obj_type, *args, **kwargs):
            if obj_type == 'virtual-machine-interface':
                raise NoIdError('')
            return orig_fq_name_to_uuid(obj_type, *args, **kwargs)
        self._api_server._db_conn.fq_name_to_uuid = dummy_fq_name_to_uuid
        try:
            with ExpectedException(RefsExistError) as e:
                self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        finally:
            self._api_server._db_conn.fq_name_to_uuid= orig_fq_name_to_uuid

    def test_put_on_wrong_type(self):
        vn_name = self.id()+'-vn'
        vn_obj = VirtualNetwork(vn_name)
        self._add_detail('Creating network with name %s' %(vn_name))
        self._vnc_lib.virtual_network_create(vn_obj)
        listen_port = self._api_server._args.listen_port
        uri = '/network-ipam/%s' %(vn_obj.uuid)
        self._add_detail('Trying to update uuid as network-ipam, expecting 404')
        code, msg = self._http_put(uri, json.dumps({'network-ipam': {'display_name': 'foobar'}}))
        self.assertThat(code, Equals(404))

        self._add_detail('Updating display_name as network, expecting success')
        uri = '/virtual-network/%s' %(vn_obj.uuid)
        code, msg = self._http_put(uri, json.dumps({'virtual-network': {'display_name': 'foobar'}}))
        self.assertThat(code, Equals(200))
        rb_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        self.assertThat(rb_obj.display_name, Equals('foobar'))

    def test_floatingip_as_instanceip(self):
        ipam_fixt = self.useFixture(NetworkIpamTestFixtureGen(self._vnc_lib))

        project_fixt = self.useFixture(ProjectTestFixtureGen(self._vnc_lib, 'default-project'))

        subnet_vnc = IpamSubnetType(subnet=SubnetType('1.1.1.0', 24))
        vnsn_data = VnSubnetsType([subnet_vnc])
        logger.info("Creating a virtual network")
        logger.info("Creating subnet 1.1.1.0/24")
        vn_fixt = self.useFixture(VirtualNetworkTestFixtureGen(self._vnc_lib,
                  network_ipam_ref_infos=[(ipam_fixt.getObj(), vnsn_data)]))
        vn_fixt.getObj().set_router_external(True)
        self._vnc_lib.virtual_network_update(vn_fixt.getObj())

        logger.info("Fetching floating-ip-pool")
        fip_pool_fixt = self.useFixture(
            FloatingIpPoolTestFixtureGen(self._vnc_lib, 'floating-ip-pool',
                                         parent_fixt=vn_fixt))

        logger.info("Creating auto-alloc floating-ip")
        fip_fixt = self.useFixture(
            FloatingIpTestFixtureGen(
                self._vnc_lib, 'fip1', parent_fixt=fip_pool_fixt,
                project_refs=[project_fixt.getObj()]))
        ip_allocated = fip_fixt.getObj().floating_ip_address

        logger.info("Creating auto-alloc instance-ip, expecting an error")
        with ExpectedException(PermissionDenied) as e:
            iip_fixt = self.useFixture(
                InstanceIpTestFixtureGen(
                    self._vnc_lib, 'iip1', auto_prop_val=False,
                    instance_ip_address=ip_allocated,
                    virtual_network_refs=[vn_fixt.getObj()]))
    # end test_floatingip_as_instanceip

    def test_name_with_reserved_xml_char(self):
        self.skipTest('single quote char test broken')
        vn_name = self.id()+'-&vn<1>"2\''
        vn_obj = VirtualNetwork(vn_name)
        # fq_name, fq_name_str has non-escape val, ifmap-id has escaped val
        ifmap_id = cfgm_common.imid.get_ifmap_id_from_fq_name(vn_obj.get_type(),
                       vn_obj.get_fq_name())
        self.assertIsNot(re.search("&amp;vn&lt;1&gt;&quot;2&apos;", ifmap_id), None)
        fq_name_str = cfgm_common.imid.get_fq_name_str_from_ifmap_id(ifmap_id)
        self.assertIsNone(re.search("&amp;vn&lt;1&gt;&quot;2&apos;", fq_name_str))

        self._add_detail('Creating network with name %s expecting success' %(vn_name))
        self._vnc_lib.virtual_network_create(vn_obj)
        self.assertTill(self.ifmap_has_ident, obj=vn_obj)
        ident_elem = FakeIfmapClient._graph[ifmap_id]['ident']
        ident_str = etree.tostring(ident_elem)
        mch = re.search("&amp;vn&lt;1&gt;&quot;2&apos;", ident_str)
        self.assertIsNot(mch, None)
        self._vnc_lib.virtual_network_delete(id=vn_obj.uuid)

        vn_name = self.id()+'-vn'
        vn_obj = VirtualNetwork(vn_name)
        self._add_detail('Creating network with name %s expecting success' %(vn_name))
        self._vnc_lib.virtual_network_create(vn_obj)
        self.assertTill(self.ifmap_has_ident, obj=vn_obj)
        self._vnc_lib.virtual_network_delete(id=vn_obj.uuid)

        rt_name = self.id()+'-route-target:1'
        rt_obj = RouteTarget(rt_name)
        self._add_detail('Creating network with name %s expecting success' %(rt_name))
        self._vnc_lib.route_target_create(rt_obj)
        self.assertTill(self.ifmap_has_ident, obj=rt_obj)
        self._vnc_lib.route_target_delete(id=rt_obj.uuid)
    # end test_name_with_reserved_xml_char

    def test_list_bulk_collection(self):
        obj_count = self._vnc_lib.POST_FOR_LIST_THRESHOLD + 1
        vn_uuids = []
        ri_uuids = []
        vmi_uuids = []
        logger.info("Creating %s VNs, RIs, VMIs.", obj_count)
        for i in range(obj_count):
            vn_obj = VirtualNetwork('%s-vn-%s' %(self.id(), i))
            self._vnc_lib.virtual_network_create(vn_obj)
            vn_uuids.append(vn_obj.uuid)

            ri_obj = RoutingInstance('%s-ri-%s' %(self.id(), i),
                                     parent_obj=vn_obj)
            self._vnc_lib.routing_instance_create(ri_obj)
            ri_uuids.append(ri_obj.uuid)

            vmi_obj = VirtualMachineInterface('%s-vmi-%s' %(self.id(), i),
                                              parent_obj=Project())
            vmi_obj.add_virtual_network(vn_obj)
            self._vnc_lib.virtual_machine_interface_create(vmi_obj)
            vmi_uuids.append(vmi_obj.uuid)

        logger.info("Querying VNs by obj_uuids.")
        flexmock(self._api_server).should_call('_list_collection').once()
        ret_list = self._vnc_lib.resource_list('virtual-network',
                                               obj_uuids=vn_uuids)
        ret_uuids = [ret['uuid'] for ret in ret_list['virtual-networks']]
        self.assertThat(set(vn_uuids), Equals(set(ret_uuids)))

        logger.info("Querying RIs by parent_id.")
        flexmock(self._api_server).should_call('_list_collection').once()
        ret_list = self._vnc_lib.resource_list('routing-instance',
                                               parent_id=vn_uuids)
        ret_uuids = [ret['uuid']
                     for ret in ret_list['routing-instances']]
        self.assertThat(set(ri_uuids), Equals(set(ret_uuids)))

        logger.info("Querying VMIs by back_ref_id.")
        flexmock(self._api_server).should_call('_list_collection').once()
        ret_list = self._vnc_lib.resource_list('virtual-machine-interface',
                                               back_ref_id=vn_uuids)
        ret_uuids = [ret['uuid']
                     for ret in ret_list['virtual-machine-interfaces']]
        self.assertThat(set(vmi_uuids), Equals(set(ret_uuids)))

        logger.info("Querying RIs by parent_id and filter.")
        flexmock(self._api_server).should_call('_list_collection').once()
        ret_list = self._vnc_lib.resource_list('routing-instance',
            parent_id=vn_uuids,
            filters={'display_name':'%s-ri-5' %(self.id())})
        self.assertThat(len(ret_list['routing-instances']), Equals(1))

        logger.info("Querying VNs by obj_uuids for children+backref fields.")
        flexmock(self._api_server).should_call('_list_collection').once()
        ret_objs = self._vnc_lib.resource_list('virtual-network',
            detail=True, obj_uuids=vn_uuids, fields=['routing_instances',
            'virtual_machine_interface_back_refs'])

        ret_ri_uuids = []
        ret_vmi_uuids = []
        for vn_obj in ret_objs:
            ri_children = getattr(vn_obj, 'routing_instances',
                'RI children absent')
            self.assertNotEqual(ri_children, 'RI children absent')
            ret_ri_uuids.extend([ri['uuid'] for ri in ri_children])
            vmi_back_refs = getattr(vn_obj,
                'virtual_machine_interface_back_refs',
                'VMI backrefs absent')
            self.assertNotEqual(ri_children, 'VMI backrefs absent')
            ret_vmi_uuids.extend([vmi['uuid'] for vmi in vmi_back_refs])

        self.assertThat(set(ri_uuids), Equals(set(ret_ri_uuids)))
        self.assertThat(set(vmi_uuids), Equals(set(ret_vmi_uuids)))
    # end test_list_bulk_collection

    def test_list_lib_api(self):
        num_objs = 5
        proj_obj = Project('%s-project' %(self.id()))
        self._vnc_lib.project_create(proj_obj)
        ipam_obj = NetworkIpam('%s-ipam' %(self.id()), parent_obj=proj_obj)
        self._vnc_lib.network_ipam_create(ipam_obj)
        def create_vns():
            objs = []
            for i in range(num_objs):
                name = '%s-%s' %(self.id(), i)
                obj = VirtualNetwork(
                    name, proj_obj, display_name=name, is_shared=True)
                obj.add_network_ipam(ipam_obj,
                    VnSubnetsType(
                        [IpamSubnetType(SubnetType('1.1.%s.0' %(i), 28))]))
                self._vnc_lib.virtual_network_create(obj)
                objs.append(obj)
            return objs

        vn_objs = create_vns()

        # unanchored summary list without filters
        read_vn_dicts = self._vnc_lib.virtual_networks_list()['virtual-networks']
        self.assertThat(len(read_vn_dicts), Not(LessThan(num_objs)))
        for obj in vn_objs:
            # locate created object, should only be one, expect exact fields
            obj_dict = [d for d in read_vn_dicts if d['uuid'] == obj.uuid]
            self.assertThat(len(obj_dict), Equals(1))
            self.assertThat(set(['fq_name', 'uuid', 'href']),
                            Equals(set(obj_dict[0].keys())))

        # unanchored summary list with field filters
        resp = self._vnc_lib.virtual_networks_list(
            filters={'display_name':vn_objs[2].display_name})
        vn_dicts = resp['virtual-networks']
        self.assertThat(len(vn_dicts), Equals(1))
        self.assertThat(vn_dicts[0]['uuid'], Equals(vn_objs[2].uuid))
        self.assertThat(set(['fq_name', 'uuid', 'href']),
                        Equals(set(vn_dicts[0].keys())))

        # unanchored detailed list without filters
        read_vn_objs = self._vnc_lib.virtual_networks_list(
            detail=True)
        self.assertThat(len(read_vn_objs), Not(LessThan(num_objs)))
        read_display_names = [o.display_name for o in read_vn_objs]
        for obj in vn_objs:
            self.assertThat(read_display_names,
                            Contains(obj.display_name))

        # unanchored detailed list with filters
        read_vn_objs = self._vnc_lib.virtual_networks_list(
            detail=True,
            filters={'is_shared':True})
        self.assertThat(len(read_vn_objs), Equals(num_objs))

        # parent anchored summary list without filters
        read_vn_dicts = self._vnc_lib.virtual_networks_list(
            parent_id=proj_obj.uuid)['virtual-networks']
        self.assertThat(len(read_vn_dicts), Equals(num_objs))
        for obj in vn_objs:
            # locate created object, should only be one, expect exact fields
            obj_dict = [d for d in read_vn_dicts if d['uuid'] == obj.uuid]
            self.assertThat(len(obj_dict), Equals(1))
            self.assertThat(set(['fq_name', 'uuid', 'href']),
                            Equals(set(obj_dict[0].keys())))
            self.assertThat(obj_dict[0]['fq_name'][:-1],
                Equals(proj_obj.fq_name))

        # parent anchored summary list with filters
        resp = self._vnc_lib.virtual_networks_list(
            parent_id=proj_obj.uuid,
            filters={'is_shared': vn_objs[2].is_shared})
        read_vn_dicts = resp['virtual-networks']
        self.assertThat(len(read_vn_dicts), Equals(num_objs))
        for obj in vn_objs:
            # locate created object, should only be one, expect exact fields
            obj_dict = [d for d in read_vn_dicts if d['uuid'] == obj.uuid]
            self.assertThat(len(obj_dict), Equals(1))
            self.assertThat(set(['fq_name', 'uuid', 'href']),
                            Equals(set(obj_dict[0].keys())))
            self.assertThat(obj_dict[0]['fq_name'][:-1],
                Equals(proj_obj.fq_name))

        # parent anchored detailed list without filters
        read_vn_objs = self._vnc_lib.virtual_networks_list(
            parent_id=proj_obj.uuid, detail=True)
        self.assertThat(len(read_vn_objs), Equals(num_objs))
        read_display_names = [o.display_name for o in read_vn_objs]
        read_fq_names = [o.fq_name for o in read_vn_objs]
        for obj in vn_objs:
            self.assertThat(read_display_names,
                            Contains(obj.display_name))
        for fq_name in read_fq_names:
            self.assertThat(fq_name[:-1], Equals(proj_obj.fq_name))

        # parent anchored detailed list with filters
        read_vn_objs = self._vnc_lib.virtual_networks_list(
            parent_id=proj_obj.uuid, detail=True,
            filters={'display_name':vn_objs[2].display_name})
        self.assertThat(len(read_vn_objs), Equals(1))
        self.assertThat(read_vn_objs[0].fq_name[:-1],
            Equals(proj_obj.fq_name))

        # backref anchored summary list without filters
        resp = self._vnc_lib.virtual_networks_list(
            back_ref_id=ipam_obj.uuid,
            filters={'is_shared':vn_objs[2].is_shared})
        read_vn_dicts = resp['virtual-networks']
        self.assertThat(len(read_vn_dicts), Equals(num_objs))
        for obj in vn_objs:
            # locate created object, should only be one, expect exact fields
            obj_dict = [d for d in read_vn_dicts if d['uuid'] == obj.uuid]
            self.assertThat(len(obj_dict), Equals(1))
            self.assertThat(set(['fq_name', 'uuid', 'href']),
                            Equals(set(obj_dict[0].keys())))

        # backref anchored summary list with filters
        resp = self._vnc_lib.virtual_networks_list(
            back_ref_id=ipam_obj.uuid,
            filters={'display_name':vn_objs[2].display_name})
        read_vn_dicts = resp['virtual-networks']
        self.assertThat(len(read_vn_dicts), Equals(1))
        self.assertThat(read_vn_dicts[0]['uuid'], Equals(vn_objs[2].uuid))

        # backref anchored detailed list without filters
        read_vn_objs = self._vnc_lib.virtual_networks_list(
            back_ref_id=ipam_obj.uuid, detail=True)
        self.assertThat(len(read_vn_objs), Equals(num_objs))
        read_display_names = [o.display_name for o in read_vn_objs]
        read_ipam_uuids = [o.network_ipam_refs[0]['uuid'] 
                           for o in read_vn_objs]
        for obj in vn_objs:
            self.assertThat(read_display_names,
                            Contains(obj.display_name))
        for ipam_uuid in read_ipam_uuids:
            self.assertThat(ipam_uuid, Equals(ipam_obj.uuid))

        # backref anchored detailed list with filters
        read_vn_objs = self._vnc_lib.virtual_networks_list(
            back_ref_id=ipam_obj.uuid, detail=True,
            filters={'display_name':vn_objs[2].display_name,
                     'is_shared':vn_objs[2].is_shared})
        self.assertThat(len(read_vn_objs), Equals(1))
        read_ipam_fq_names = [o.network_ipam_refs[0]['to'] 
                              for o in read_vn_objs]
        for ipam_fq_name in read_ipam_fq_names:
            self.assertThat(ipam_fq_name,
                Equals(ipam_obj.fq_name))

        # id-list detailed without filters
        read_vn_objs = self._vnc_lib.virtual_networks_list(
            obj_uuids=[o.uuid for o in vn_objs], detail=True)
        self.assertThat(len(read_vn_objs), Equals(num_objs))
        read_display_names = [o.display_name for o in read_vn_objs]
        for obj in vn_objs:
            self.assertThat(read_display_names,
                            Contains(obj.display_name))

        # id-list detailed with filters
        read_vn_objs = self._vnc_lib.virtual_networks_list(
            obj_uuids=[o.uuid for o in vn_objs], detail=True,
            filters={'is_shared':False})
        self.assertThat(len(read_vn_objs), Equals(0))
    # end test_list_lib_api

    def test_id_perms_uuid_update_should_fail(self):
        vn_obj = self._create_test_object()
        # read in id-perms
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        orig_id_perms = copy.deepcopy(vn_obj.id_perms)
        wrong_id_perms = copy.deepcopy(vn_obj.id_perms)
        wrong_id_perms.uuid.uuid_mslong += 1
        wrong_id_perms.uuid.uuid_lslong += 1
        vn_obj.set_id_perms(wrong_id_perms)
        self._vnc_lib.virtual_network_update(vn_obj)
        read_id_perms = self._vnc_lib.virtual_network_read(id=vn_obj.uuid).id_perms
        self.assertEqual(read_id_perms.uuid.uuid_mslong,
                         orig_id_perms.uuid.uuid_mslong)
        self.assertEqual(read_id_perms.uuid.uuid_lslong,
                         orig_id_perms.uuid.uuid_lslong)
    # end test_id_perms_uuid_update_should_fail
# end class TestVncCfgApiServer


class TestStaleLockRemoval(test_case.ApiServerTestCase):
    STALE_LOCK_SECS = '0.1'
    def __init__(self, *args, **kwargs):
        super(TestStaleLockRemoval, self).__init__(*args, **kwargs)
        self._config_knobs.extend([('DEFAULTS', 'stale_lock_seconds',
            self.STALE_LOCK_SECS),])
    # end __init__

    def test_stale_fq_name_lock_removed_on_partial_create(self):
        # 1. partially create an object i.e zk done, cass 
        #    cass silently not(simulating process restart).
        # 2. create same object again, expect RefsExist
        # 3. wait for stale_lock_seconds and attempt create
        #    of same object. should succeed.
        def stub(*args, **kwargs):
            return (True, '')

        with test_common.flexmocks([
            (self._api_server._db_conn, 'dbe_create', stub),
            (self._api_server.get_resource_class('virtual-network'),
             'post_dbe_create', stub)]):
            self._create_test_object()
            with ExpectedException(RefsExistError) as e:
                self._create_test_object()
            gevent.sleep(float(self.STALE_LOCK_SECS))

        self._create_test_object()
    # end test_stale_fq_name_lock_removed_on_partial_create

    def test_stale_fq_name_lock_removed_on_partial_delete(self):
        # 1. partially delete an object i.e removed from cass
        #    but not from zk silently (simulating process restart)
        # 2. create same object again, expect RefsExist
        # 3. wait for stale_lock_seconds and attempt create
        #    of same object. should succeed.
        def stub(*args, **kwargs):
            return (True, '')

        vn_obj = self._create_test_object()
        with test_common.flexmocks([
            (self._api_server._db_conn, 'dbe_release', stub)]):
            self._vnc_lib.virtual_network_delete(id=vn_obj.uuid)
            with ExpectedException(RefsExistError) as e:
                self._create_test_object()
            gevent.sleep(float(self.STALE_LOCK_SECS))

        self._create_test_object()
    # end test_stale_fq_name_lock_removed_on_partial_delete

    def test_stale_fq_name_lock_removed_coverage(self):
        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        vn_obj.__dict__['id_perms'] = {}
        vn_UUID = uuid.uuid4()

        # create zk-node
        self._api_server._db_conn.set_uuid(
            obj_type=vn_obj._type,
            obj_dict=vn_obj.__dict__,
            id=vn_UUID,
            do_lock=True)

        # assert we hit the zk-node on re-create
        with ExpectedException(ResourceExistsError, ".*at zookeeper.*"):
            self._api_server._db_conn.set_uuid(
                obj_type=vn_obj._type,
                obj_dict=vn_obj.__dict__,
                id=vn_UUID,
                do_lock=True)

        # create entry in cassandra too and assert
        # not a stale lock on re-create
        uuid_cf = test_common.CassandraCFs.get_cf('obj_uuid_table')
        with uuid_cf.patch_row(str(vn_UUID),
            new_columns={'fq_name':json.dumps(vn_obj.fq_name),
                         'type':json.dumps(vn_obj._type)}):
            with ExpectedException(ResourceExistsError, ".*at cassandra.*"):
                self._api_server._db_conn.set_uuid(
                    obj_type=vn_obj._type,
                    obj_dict=vn_obj.__dict__,
                    id=vn_UUID,
                    do_lock=True)

            self._api_server._db_conn._cassandra_db.cache_uuid_to_fq_name_del(
                str(vn_UUID))

        # sleep and re-create and now it should be fine
        gevent.sleep(float(self.STALE_LOCK_SECS))
        self._api_server._db_conn.set_uuid(
            obj_type=vn_obj._type,
            obj_dict=vn_obj.__dict__,
            id=vn_UUID,
            do_lock=True)
    # end test_stale_fq_name_lock_removed_coverage

# end TestStaleLockRemoval


class TestVncCfgApiServerRequests(test_case.ApiServerTestCase):
    """ Tests to verify the max_requests config parameter of api-server."""
    def __init__(self, *args, **kwargs):
        super(TestVncCfgApiServerRequests, self).__init__(*args, **kwargs)
        self._config_knobs.extend([('DEFAULTS', 'max_requests', 10),])


    def api_requests(self, orig_vn_read, count):
        api_server = test_common.vnc_cfg_api_server.server
        self.blocked = True
        def slow_response_on_vn_read(*args, **kwargs):
            while self.blocked:
                gevent.sleep(1)
            return orig_vn_read(*args, **kwargs)

        api_server._db_conn._cassandra_db._cassandra_virtual_network_read = slow_response_on_vn_read

        logger.info("Creating a test VN object.")
        test_obj = self._create_test_object()
        logger.info("Making max_requests(%s) to api server" % (count - 1))
        def vn_read():
            self._vnc_lib.virtual_network_read(id=test_obj.uuid)
            gevent.sleep(0)

        for i in range(count):
            gevent.spawn(vn_read)
        gevent.sleep(1)

    def test_within_max_api_requests(self):
        api_server = test_common.vnc_cfg_api_server.server
        orig_vn_read = api_server._db_conn._cassandra_db._cassandra_virtual_network_read
        try:
            self.api_requests(orig_vn_read, 5)
            logger.info("Making one more requests well within the max_requests to api server")
            vn_name = self.id() + 'testvn'
            try:
                greenlet = gevent.spawn(self.create_virtual_network, vn_name, '10.1.1.0/24')
                gevent.sleep(0)
                vn_obj = greenlet.get(timeout=3)
            except gevent.timeout.Timeout as e:
                self.assertFalse(greenlet.successful(), 'Request failed unexpectedly')
            else:
                self.assertEqual(vn_obj.name, vn_name)
        finally:
            api_server._db_conn._cassandra_db._cassandra_virtual_network_read = orig_vn_read

    def test_err_on_max_api_requests(self):
        api_server = test_common.vnc_cfg_api_server.server
        orig_vn_read = api_server._db_conn._cassandra_db._cassandra_virtual_network_read
        try:
            self.api_requests(orig_vn_read, 11)
            logger.info("Making one more requests (max_requests + 1) to api server")
            try:
                greenlet = gevent.spawn(self.create_virtual_network, 'testvn', '10.1.1.0/24')
                gevent.sleep(0)
                greenlet.get(timeout=3)
            except gevent.timeout.Timeout as e:
                logger.info("max_requests + 1 failed as expected.")
                self.blocked = False
                self.assertFalse(False, greenlet.successful())
            else:
                self.assertTrue(False, 'Request succeeded unexpectedly')
        finally:
            api_server._db_conn._cassandra_db._cassandra_virtual_network_read = orig_vn_read

# end class TestVncCfgApiServerRequests


class TestLocalAuth(test_case.ApiServerTestCase):
    def __init__(self, *args, **kwargs):
        super(TestLocalAuth, self).__init__(*args, **kwargs)
        self._config_knobs.extend([('DEFAULTS', 'auth', 'keystone'),
                                   ('DEFAULTS', 'multi_tenancy', True),
                                   ('KEYSTONE', 'admin_user', 'foo'),
                                   ('KEYSTONE', 'admin_password', 'bar'),])

    def setup_flexmock(self):
        from keystonemiddleware import auth_token
        class FakeAuthProtocol(object):
            def __init__(self, app, *args, **kwargs):
                self._app = app
            # end __init__
            def __call__(self, env, start_response):
                # in multi-tenancy mode only admin role admitted
                # by api-server till full rbac support
                env['HTTP_X_ROLE'] = 'admin'
                return self._app(env, start_response)
            # end __call__
            def get_admin_token(self):
                return None
            # end get_admin_token
        # end class FakeAuthProtocol
        test_common.setup_extra_flexmock([(auth_token, 'AuthProtocol', FakeAuthProtocol)])
    # end setup_flexmock

    def setUp(self):
        self.setup_flexmock()
        super(TestLocalAuth, self).setUp()
    # end setUp

    def test_local_auth_on_8095(self):
        from requests.auth import HTTPBasicAuth
        admin_port = self._api_server._args.admin_port

        # equivalent to curl -u foo:bar http://localhost:8095/virtual-networks
        logger.info("Positive case")
        url = 'http://localhost:%s/virtual-networks' %(admin_port)
        resp = requests.get(url, auth=HTTPBasicAuth('foo', 'bar'))
        self.assertThat(resp.status_code, Equals(200))

        logger.info("Negative case without header")
        resp = requests.get(url)
        self.assertThat(resp.status_code, Equals(401))
        self.assertThat(resp.text, Contains('HTTP_AUTHORIZATION header missing'))

        logger.info("Negative case with wrong creds")
        resp = requests.get(url, auth=HTTPBasicAuth('bar', 'foo'))
        self.assertThat(resp.status_code, Equals(401))

    def test_doc_auth(self):
        self.skipTest('doc auth test broken')
        listen_port = self._api_server._args.listen_port

        # equivalent to curl -u foo:bar http://localhost:8095/documentation/index.html
        logger.info("Positive case")
        url = 'http://localhost:%s/documentation/' %(listen_port)
        resp = requests.get(url)
        self.assertThat(resp.status_code, Equals(200))

        logger.info("Negative case without Documentation")
        url = 'http://localhost:%s/' %(listen_port)
        resp = requests.get(url)
        self.assertThat(resp.status_code, Equals(401))

# end class TestLocalAuth

class TestExtensionApi(test_case.ApiServerTestCase):
    def __init__(self, *args, **kwargs):
        super(TestExtensionApi, self).__init__(*args, **kwargs)
    # end __init__

    class ResourceApiDriver(vnc_plugin_base.ResourceApi):
        _test_case = None

        def __init__(self, *args, **kwargs):
            pass
        # end __init__

        def transform_request(self, request):
            # add/del/mod envvar
            request.environ['X_TEST_DUMMY'] = 'foo'
            request.environ['HTTP_X_CONTRAIL_USERAGENT'] = 'bar'
            del request.environ['SERVER_SOFTWARE']

            # /virtual-networks -> virtual-network
            obj_type = request.path[1:-1]
            if request.method == 'POST' and obj_type == 'virtual-network':
                obj_name = request.json[obj_type]['fq_name'][-1]
                if 'transform-create' in obj_name:
                    # add/del/mod body
                    request.json[obj_type]['dummy_field'] = 'foo'
                    request.json[obj_type]['fq_name'][-1] = obj_name + '-foo'
                    del request.json[obj_type]['uuid']
            elif request.method == 'GET':
                request.environ['QUERY_STRING'] = \
                    request.environ['QUERY_STRING'].replace('replace-me','')

        # end transform_request

        def validate_request(self, request):
            # /virtual-networks -> virtual-network
            obj_type = request.path[1:-1]
            if request.method == 'POST' and obj_type == 'virtual-network':
                obj_name = request.json[obj_type]['fq_name'][-1]
                if 'validate-create' in obj_name:
                    raise bottle.abort(456, 'invalidating create request')
            elif request.method == 'GET':
                mch = re.match('/virtual-network/.*', request.path)
                if (mch and
                    'fail-me' in request.environ['QUERY_STRING']):
                    raise bottle.abort(456, 'invalidating read request')
            elif request.method == 'PUT':
                mch = re.match('/virtual-network/.*', request.path)
                if (mch and
                    request.json['virtual-network'].get('is_shared')):
                    raise bottle.abort(456, 'invalidating update request')
            elif request.method == 'DELETE':
                mch = re.match('/virtual-network/.*', request.path)
                if mch:
                    raise bottle.abort(456, 'invalidating delete request')
        # end validate_request

        def transform_response(self, request, response):
            self._test_case.assertIn('X_TEST_DUMMY', request.environ.keys())
            self._test_case.assertNotIn('SERVER_SOFTWARE', request.environ.keys())
            self._test_case.assertThat(request.environ['HTTP_X_CONTRAIL_USERAGENT'],
                                       Equals('bar'))
            if request.method == 'POST':
                obj_type = request.path[1:-1]
                obj_name = request.json[obj_type]['fq_name'][-1]
                if 'transform-create' in obj_name:
                    bottle.response.status = '234 Transformed Response'
                    response[obj_type]['extra_field'] = 'foo'
        # end transform_response
    # end class ResourceApiDriver

    def setUp(self):
        test_common.setup_extra_flexmock(
            [(stevedore.extension.ExtensionManager, '__new__',
              FakeExtensionManager)])
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resourceApi'] = \
            [TestExtensionApi.ResourceApiDriver]
        super(TestExtensionApi, self).setUp()
        TestExtensionApi.ResourceApiDriver._test_case = self
    # end setUp

    def tearDown(self):
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resourceApi'] = \
            None
        super(TestExtensionApi, self).tearDown()
    # end tearDown

    def test_transform_request(self):
        # create
        obj = VirtualNetwork('transform-create')
        obj_request_uuid = str(uuid.uuid4())
        body_dict = {'virtual-network':
                          {'fq_name': obj.fq_name,
                           'parent_type': 'project',
                           'uuid': obj_request_uuid}}
        status, content = self._http_post('/virtual-networks',
                              body=json.dumps(body_dict))
        self.assertThat(status, Equals(234))
        obj_dict = json.loads(content)['virtual-network']
        obj_allocd_uuid = obj_dict['uuid']

        self.assertThat(obj_allocd_uuid, Not(Equals(obj_request_uuid)))
        self.assertThat(obj_dict['fq_name'][-1], Equals('transform-create-foo'))
        self.assertThat(obj_dict['extra_field'], Equals('foo'))

        # read
        status, content = self._http_get('/virtual-networks',
            query_params={'obj_uuids':'replace-me'+obj_dict['uuid']})
        self.assertThat(status, Equals(200))
        objs_dict = json.loads(content)['virtual-networks']
        self.assertThat(len(objs_dict), Equals(1))
        self.assertThat(objs_dict[0]['fq_name'][-1],
                        Equals('transform-create-foo'))

        # update
        body_dict = {'virtual-network':
                         {'display_name': 'foo'}}
        status, content = self._http_put('/virtual-network/'+obj_allocd_uuid,
                                         body=json.dumps(body_dict))
        obj = self._vnc_lib.virtual_network_read(id=obj_allocd_uuid)
        self.assertThat(obj.display_name, Equals('foo'))
    # end test_transform_request

    def test_validate_request(self):
        # create
        obj = VirtualNetwork('validate-create')
        body_dict = {'virtual-network':
                        {'fq_name': obj.fq_name,
                        'parent_type': 'project'}}
        status, content = self._http_post('/virtual-networks',
                              body=json.dumps(body_dict))
        self.assertThat(status, Equals(456))
        self.assertThat(content, Contains('invalidating create request'))
        with ExpectedException(NoIdError) as e:
            self._vnc_lib.virtual_network_read(fq_name=obj.fq_name)

        # read
        obj = self._create_test_object()
        status, content = self._http_get('/virtual-network/'+obj.uuid,
            query_params={'fail-me': 1})
        self.assertThat(status, Equals(456))
        self.assertThat(content, Contains('invalidating read request'))

        # update
        obj.is_shared = True
        body_dict = {'virtual-network':
                        {'is_shared': True}}
        status, content = self._http_put('/virtual-network/'+obj.uuid,
                               body=json.dumps(body_dict))
        self.assertThat(status, Equals(456))
        self.assertThat(content, Contains('invalidating update request'))
        obj = self._vnc_lib.virtual_network_read(id=obj.uuid)
        self.assertThat(obj.is_shared, Equals(None))

        # delete
        status, content = self._http_delete('/virtual-network/'+obj.uuid,
                               body=None)
        self.assertThat(status, Equals(456))
        self.assertThat(content, Contains('invalidating delete request'))
        obj = self._vnc_lib.virtual_network_read(id=obj.uuid)
    # end test_validate_request

# end class TestExtensionApi

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    # unittest.main(failfast=True)
    unittest.main()
