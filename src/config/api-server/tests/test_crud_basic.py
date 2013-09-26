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

import cgitb
cgitb.enable(format='text')

import fixtures
import testtools
from testtools.matchers import Equals, MismatchError
import unittest
import re
import json
import copy
from lxml import etree
import inspect
import pycassa

from vnc_api.vnc_api import *
from cfgm_common import exceptions as vnc_exceptions
from cfgm_common.test_utils import *
import gen.vnc_api_test_gen
from gen.resource_test import *
import discovery.client as disc_client

import test_common

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

# class TestCrudBasic(object):


class TestCrudBasic(gen.vnc_api_test_gen.VncApiTestGen):

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

    def test_access_control_list_crud(self):
        sg_fixt = self.useFixture(SecurityGroupTestFixtureGen(self._vnc_lib))
        self.useFixture(AccessControlListTestFixtureGen(
            self._vnc_lib, parent_fixt=sg_fixt))

        vn_fixt = self.useFixture(VirtualNetworkTestFixtureGen(self._vnc_lib))
        self.useFixture(AccessControlListTestFixtureGen(
            self._vnc_lib, parent_fixt=vn_fixt))
    # end test_access_control_list_crud
# end class TestCrudBasic


class TestFixtures(testtools.TestCase, fixtures.TestWithFixtures):

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
        try:
            super(TestFixtures, self).assertThat(*args)
        except MismatchError as e:
            import pdb
            pdb.set_trace()
            raise e
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
        vdns_data = VirtualDnsType(domain_name='abc.net')
        vdns_fixt = self.useFixture(
            VirtualDnsTestFixtureGen(self._vnc_lib, virtual_DNS_name='vdns1',
                                     virtual_DNS_data=vdns_data))

        dns_method = "virtual-dns-server"
        dns_server = IpamDnsAddressType(
            virtual_dns_server_name=vdns_fixt.getObj().get_fq_name_str())
        ipam_mgmt = IpamType(
            ipam_dns_method='vdns-server', ipam_dns_server=dns_server)
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

# class TestNetAddrAlloc(object):


class TestNetAddrAlloc(testtools.TestCase, fixtures.TestWithFixtures):

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

    def assertThat(self, *args):
        try:
            super(TestNetAddrAlloc, self).assertThat(*args)
        except MismatchError as e:
            import pdb
            pdb.set_trace()
            raise e
    # end assertThat

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

    # end test_ip_alloc_on_net

    def test_ip_alloc_on_ip_fabric(self):
        pass
    # end test_ip_alloc_on_ip_fabric

    def test_ip_alloc_on_link_local(self):
        pass
    # end test_ip_alloc_on_link_local

# end class TestNetAddrAlloc


class DemoFixture(fixtures.Fixture):

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

# class TestListUpdate(testtools.TestCase, fixtures.TestWithFixtures):


class TestListUpdate(object):

    def setUp(self):
        super(TestListUpdate, self).setUp()
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

    def test_policy_create_w_rules(self):
        proj_fixt = self.useFixture(ProjectTestFixtureGen(self._vnc_lib))

        policy_obj = NetworkPolicy(
            'test-policy-create-w-rules', proj_fixt.getObj())
        np_rules = [
            PolicyRuleType(None, '<>', 'pass', 'any',
                   [AddressType(virtual_network='local')], [
                       PortType(-1, -1)], None,
                [AddressType(
                 virtual_network='any')], [PortType(-1, -1)], None),
            PolicyRuleType(None, '<>', 'deny', 'any',
                           [AddressType(virtual_network='local')], [
                               PortType(-1, -1)], None,
                           [AddressType(
                            virtual_network='any')], [PortType(-1, -1)], None),
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
            PolicyRuleType(None, '<>', 'pass', 'any',
                   [AddressType(virtual_network='local')], [
                       PortType(1, 2)], None,
                [AddressType(
                 virtual_network='any')], [PortType(3, 4)], None),
            PolicyRuleType(None, '<>', 'deny', 'any',
                           [AddressType(virtual_network='local')], [
                               PortType(5, 6)], None,
                           [AddressType(
                            virtual_network='any')], [PortType(7, 8)], None),
        ]
        policy_entries = PolicyEntriesType(np_rules)
        policy_obj.set_network_policy_entries(policy_entries)

        self._vnc_lib.network_policy_update(policy_obj)

        policy_entries.policy_rule.append(
            PolicyRuleType(None, '<>', 'deny', 'any',
                           [AddressType(virtual_network='local')],
                           [PortType(9, 10)], None,
                           [AddressType(virtual_network='any')],
                           [PortType(11, 12)], None)
        )
        policy_obj.set_network_policy_entries(policy_entries)

        self._vnc_lib.network_policy_update(policy_obj)

        # cleanup
        self._vnc_lib.network_policy_delete(id=policy_obj.uuid)
    # end test_policy_create_wo_rules

# end class TestListUpdate

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    # unittest.main(failfast=True)
    unittest.main()
