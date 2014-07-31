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
from testtools.matchers import Equals, MismatchError, Not, Contains
from testtools import content, content_type, ExpectedException
import unittest
import re
import json
import copy
from lxml import etree
import inspect
import pycassa
import kombu
import requests

from vnc_api.vnc_api import *
from vnc_api.common import exceptions as vnc_exceptions
import vnc_api.gen.vnc_api_test_gen
from vnc_api.gen.resource_test import *

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
        pass
    # end test_virtual_DNS_crud

    def test_virtual_DNS_record_crud(self):
        pass
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

    def test_access_control_list_crud(self):
        sg_fixt = self.useFixture(SecurityGroupTestFixtureGen(self._vnc_lib))
        self.useFixture(AccessControlListTestFixtureGen(
            self._vnc_lib, parent_fixt=sg_fixt))

        vn_fixt = self.useFixture(VirtualNetworkTestFixtureGen(self._vnc_lib))
        self.useFixture(AccessControlListTestFixtureGen(
            self._vnc_lib, parent_fixt=vn_fixt))
    # end test_access_control_list_crud
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

    def assertThat(self, *args):
        super(TestNetAddrAlloc, self).assertThat(*args)
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
        ident_name = self.get_obj_imid(test_obj)
        self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

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
        test_body = json.dumps({'operation': 'STORE',
                                'key': 'fookey',
                                'value': 'fooval'})
        self.addDetail('useragent-kv-post-store', content.json_content(test_body))
        (code, msg) = self._http_post('/useragent-kv', test_body)
        self.assertEqual(code, 200)

        test_body = json.dumps({'operation': 'RETRIEVE',
                                'key': 'fookey'})
        self.addDetail('useragent-kv-post-retrieve', content.json_content(test_body))
        (code, msg) = self._http_post('/useragent-kv', test_body)
        self.assertEqual(code, 200)
        self.assertEqual(json.loads(msg)['value'], 'fooval')

        test_body = json.dumps({'operation': 'foo',
                                'key': 'fookey'})
        self.addDetail('useragent-kv-post-wrongop', content.json_content(test_body))
        (code, msg) = self._http_post('/useragent-kv', test_body)
        self.assertEqual(code, 404)
        
    def test_err_on_max_rabbit_pending(self):
        api_server = test_common.vnc_cfg_api_server.server
        max_pend_upd = 10
        api_server._args.rabbit_max_pending_updates = str(max_pend_upd)
        orig_rabbitq_put = api_server._db_conn._msgbus._obj_update_q.put
        try:
            def err_rabbitq_put(*args, **kwargs):
                raise Exception("Faking Rabbit Exception")
            api_server._db_conn._msgbus._obj_update_q.put = err_rabbitq_put

            logger.info("Creating objects to hit max rabbit pending.")
            # every create updates project quota
            test_objs = self._create_test_objects(count=max_pend_upd/2+1)

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

            logger.info("Update of object should fail.")
            test_objs[0].display_name = 'foo'
            try:
                self._vnc_lib.virtual_network_update(test_objs[0])
            except HttpError as e:
                asserts_on_max_pending()

            logger.info("Delete of object should fail.")
            test_objs[0].display_name = 'foo'
            try:
                self._vnc_lib.virtual_network_delete(id=test_objs[0].uuid)
            except HttpError as e:
                asserts_on_max_pending()

            logger.info("Read obj object should be ok.")
            self._vnc_lib.virtual_network_read(id=test_objs[0].uuid)

        finally:
            api_server._db_conn._msgbus._obj_update_q.put = orig_rabbitq_put
 
    def test_handle_trap_on_exception(self):
        api_server = test_common.vnc_cfg_api_server.server

        def exception_on_log_error(*args, **kwargs):
            self.assertTrue(False)

        def exception_on_vn_read(*args, **kwargs):
            raise Exception("fake vn read exception")

        try:
            orig_config_log_error = api_server.config_log_error
            api_server.config_log_error = exception_on_log_error
            with ExpectedException(NoIdError):
                self._vnc_lib.virtual_network_read(fq_name=['foo', 'bar', 'baz'])
        finally:
            api_server.config_log_error = orig_config_log_error

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

# end class TestVncCfgApiServer

class TestLocalAuth(test_case.ApiServerTestCase):
    def __init__(self, *args, **kwargs):
        super(TestLocalAuth, self).__init__(*args, **kwargs)
        self._config_knobs.extend([('DEFAULTS', 'auth', 'keystone'),
                                   ('DEFAULTS', 'multi_tenancy', True),
                                   ('KEYSTONE', 'admin_user', 'foo'),
                                   ('KEYSTONE', 'admin_password', 'bar'),])

    def setup_flexmock(self):
        from keystoneclient.middleware import auth_token
        class FakeAuthProtocol(object):
            def __init__(self, app, *args, **kwargs):
                self._app = app
            # end __init__
            def __call__(self, env, start_response):
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

# end class TestLocalAuth

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    # unittest.main(failfast=True)
    unittest.main()
