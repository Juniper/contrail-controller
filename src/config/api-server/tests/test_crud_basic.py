#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
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
import random
import netaddr
import tempfile

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
import netaddr

from vnc_api.vnc_api import *
from cfgm_common import exceptions as vnc_exceptions
import vnc_api.gen.vnc_api_test_gen
from vnc_api.gen.resource_test import *
import cfgm_common
from cfgm_common import vnc_plugin_base
from cfgm_common import imid
from cfgm_common import vnc_cgitb
from cfgm_common import db_json_exim
from cfgm_common import SG_NO_RULE_FQ_NAME
vnc_cgitb.enable(format='text')

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

    def test_alias_ip_crud(self):
        pass
    # end test_alias_ip_crud

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


class TestFixtures(test_case.ApiServerTestCase):
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

        logger.info("Creating alias-ip-pool")
        aip_pool_fixt = self.useFixture(
            AliasIpPoolTestFixtureGen(self._vnc_lib, 'aip-pool',
                                         parent_fixt=vn_fixt,
                                         auto_prop_val=False))

        logger.info("Creating auto-alloc alias-ip, expecting 1.1.1.251...")
        aip_fixt = self.useFixture(
            AliasIpTestFixtureGen(
                self._vnc_lib, 'aip1', parent_fixt=aip_pool_fixt,
                auto_prop_val=False))
        ip_allocated = aip_fixt.getObj().alias_ip_address
        self.assertThat(ip_allocated, Equals('1.1.1.251'))
        logger.info("...verified")

        logger.info("Creating specific alias-ip, expecting 1.1.1.4...")
        aip_fixt = self.useFixture(
            AliasIpTestFixtureGen(
                self._vnc_lib, 'aip2', parent_fixt=aip_pool_fixt,
                auto_prop_val=False, alias_ip_address='1.1.1.4'))
        ip_allocated = aip_fixt.getObj().alias_ip_address
        self.assertThat(ip_allocated, Equals('1.1.1.4'))
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

    def test_policy_create_w_sg_in_rules(self):
        policy_obj = NetworkPolicy('test-policy-create-w-sg-in-rules')
        np_rules = [
            PolicyRuleType(direction='<>',
                           action_list=ActionListType(simple_action='pass'),
                           protocol='any',
                           src_addresses=
                               [AddressType(security_group='local')],
                           src_ports=[PortType(-1, -1)],
                           dst_addresses=[AddressType(security_group='any')],
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

        with ExpectedException(BadRequest) as e:
            self._vnc_lib.network_policy_create(policy_obj)

        # cleanup
        self._vnc_lib.network_policy_delete(id=policy_obj.uuid)
    # end test_policy_create_w_sg_in_rules
# end class TestListUpdate

class TestCrud(test_case.ApiServerTestCase):
    def test_create_using_lib_api(self):
        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn_obj)
        self.assertTill(self.ifmap_has_ident, obj=vn_obj)
    # end test_create_using_lib_api

    def test_create_using_rest_api(self):
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port
        url = 'http://%s:%s/virtual-networks' %(listen_ip, listen_port)
        vn_body = {
            'virtual-network': {
                'fq_name': ['default-domain',
                            'default-project',
                            'vn-%s' %(self.id())],
                'parent_type': 'project',
            }}
        requests.post(url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(vn_body))
    # end test_create_using_rest_api

    def test_user_defined_log_statistics_crud(self):
        gsc = self._vnc_lib.global_system_config_read(
                                      fq_name=['default-global-system-config'])
        gsc.add_user_defined_log_statistics(UserDefinedLogStat('Test01',
                    '.*[ab][0-9]s1.*'))
        gsc.add_user_defined_log_statistics(UserDefinedLogStat('Test02',
                    '127.0.0.1'))
        self._vnc_lib.global_system_config_update(gsc)
        gsc_uuid = self._vnc_lib.global_system_configs_list()[
                                    'global-system-configs'][0]['uuid']
        gsc = self._vnc_lib.global_system_config_read(id=gsc_uuid)
        tst_trgt = ('Test01', 'Test02')
        self.assertTrue(reduce(lambda x, y: x and y, map(
                        lambda p: p.name in tst_trgt,
                        gsc.user_defined_log_statistics.statlist),  True))
    #end test_user_defined_log_statistics_crud

    def test_user_defined_log_statistics_bad_add(self):
        gsc = self._vnc_lib.global_system_config_read(
                                      fq_name=['default-global-system-config'])
        gsc.add_user_defined_log_statistics(UserDefinedLogStat('Test01',
                    '.*[ab][0-9]s1.*'))
        # bad regex
        gsc.add_user_defined_log_statistics(UserDefinedLogStat('Test03',
                    '*foo'))
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.global_system_config_update(gsc)
    #end test_user_defined_log_statistics_bad_add

    def test_user_defined_log_statistics_set(self):
        gsc = self._vnc_lib.global_system_config_read(
                                      fq_name=['default-global-system-config'])
        sl = UserDefinedLogStatList()
        sl.add_statlist(UserDefinedLogStat('Test01', '.*[ab][0-9]s1.*'))
        sl.add_statlist(UserDefinedLogStat('Test02', '127.0.0.1'))
        gsc.set_user_defined_log_statistics(sl)

        self._vnc_lib.global_system_config_update(gsc)
        gsc_uuid = self._vnc_lib.global_system_configs_list()[
                                    'global-system-configs'][0]['uuid']
        gsc = self._vnc_lib.global_system_config_read(id=gsc_uuid)
        tst_trgt = ('Test01', 'Test02')
        self.assertTrue(reduce(lambda x, y: x and y, map(
                        lambda p: p.name in tst_trgt,
                        gsc.user_defined_log_statistics.statlist),  True))
    #end test_user_defined_log_statistics_set

    def test_user_defined_log_statistics_bad_set(self):
        gsc = self._vnc_lib.global_system_config_read(
                                      fq_name=['default-global-system-config'])
        sl = UserDefinedLogStatList()
        sl.add_statlist(UserDefinedLogStat('Test01', '.*[ab][0-9]s1.*'))
        sl.add_statlist(UserDefinedLogStat('Test02', '127.0.0.1'))
        sl.add_statlist(UserDefinedLogStat('Test03', '*127.0.0.1'))
        gsc.set_user_defined_log_statistics(sl)

        with ExpectedException(BadRequest) as e:
            self._vnc_lib.global_system_config_update(gsc)
    #end test_user_defined_log_statistics_bad_set

    def test_vlan_tag_on_sub_intefaces(self):
        vn = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn)

        id_perms = IdPermsType(enable=True)
        vmi_prop = VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=256)
        port_obj = VirtualMachineInterface(
                   str(uuid.uuid4()), parent_obj=Project(),
                   virtual_machine_interface_properties=vmi_prop,
                   id_perms=id_perms)
        port_obj.uuid = port_obj.name
        port_obj.set_virtual_network(vn)

        #create port with sub_interface_vlan_tag specified
        port_id = self._vnc_lib.virtual_machine_interface_create(port_obj)

        vmi_prop.sub_interface_vlan_tag = 128
        port_obj.set_virtual_machine_interface_properties(vmi_prop)
        #updating sub_interface_vlan_tag of the port to a new value should fail
        #as vrouter doesn't support it.
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.virtual_machine_interface_update(port_obj)
        # end test_vlan_tag_on_sub_interfaces

    def test_service_interface_type_value(self):
        vn = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn)

        vmi_prop = VirtualMachineInterfacePropertiesType(service_interface_type='Left')
        port_obj = VirtualMachineInterface(
                   str(uuid.uuid4()), parent_obj=Project(),
                   virtual_machine_interface_properties=vmi_prop)

        port_obj.uuid = port_obj.name
        port_obj.set_virtual_network(vn)

        #creation of port should fail as the valid values for
        #service_interface_type are: management|left|right|other[0-9]*
        with ExpectedException(BadRequest) as e:
            port_id = self._vnc_lib.virtual_machine_interface_create(port_obj)
    # end test_service_interface_type_value

    def test_vmi_with_end_to_end_shc(self):
        project = Project()
        vn = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn)
        vmi_obj = VirtualMachineInterface(
                  str(uuid.uuid4()), parent_obj=project)
        vmi_obj.uuid = vmi_obj.name
        vmi_obj.set_virtual_network(vn)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        shc_props = ServiceHealthCheckType()
        shc_props.enabled = True
        shc_props.health_check_type = 'end-to-end'
        shc_obj = ServiceHealthCheck(str(uuid.uuid4()), parent_obj=project,
                                service_health_check_properties=shc_props)
        shc_id = self._vnc_lib.service_health_check_create(shc_obj)
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.ref_update('virtual-machine-interface', vmi_id,
                                 'service-health-check', shc_id, None, 'ADD')
    # end test_vmi_with_end_to_end_shc

    def test_physical_router_credentials(self):
        phy_rout_name = self.id() + '-phy-router-1'
        user_cred_create = UserCredentials(username="test_user", password="test_pswd")
        phy_rout = PhysicalRouter(phy_rout_name, physical_router_user_credentials=user_cred_create)
        self._vnc_lib.physical_router_create(phy_rout)

        phy_rout_obj = self._vnc_lib.physical_router_read(id=phy_rout.uuid)
        user_cred_read = phy_rout_obj.get_physical_router_user_credentials()
        self.assertEqual(user_cred_read.password, '**Password Hidden**')
       # end test_physical_router_credentials

    def test_physical_router_w_no_user_credentials(self):
        phy_rout_name = self.id() + '-phy-router-2'
        phy_router = PhysicalRouter(phy_rout_name)
        self._vnc_lib.physical_router_create(phy_router)
        # reading Physical Router object when user credentials
        # are set to None should be successfull.
        phy_rout_obj = self._vnc_lib.physical_router_read(id=phy_router.uuid)
        # end test_physical_router_w_no_user_credentials

    def test_port_security_and_allowed_address_pairs(self):
        vn = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn)

        port_obj = VirtualMachineInterface(
                   str(uuid.uuid4()), parent_obj=Project(),
                   port_security_enabled=False)
        port_obj.uuid = port_obj.name
        port_obj.set_virtual_network(vn)

        port_id = self._vnc_lib.virtual_machine_interface_create(port_obj)
        addr_pair = AllowedAddressPairs(allowed_address_pair=
                                        [AllowedAddressPair(
                                        ip=SubnetType('1.1.1.0', 24),
                                        mac='02:ce:1b:d7:a6:e7')])
        # updating a port with allowed address pair should throw an exception
        # when port security enabled is set to false
        port_obj.virtual_machine_interface_allowed_address_pairs = addr_pair
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.virtual_machine_interface_update(port_obj)
    # end test_port_security_and_allowed_address_pairs

    def test_physical_router_credentials_list(self):
        phy_rout_name = self.id() + '-phy-router-1'
        phy_rout_name_2 = self.id() + '-phy-router-2'
        user_cred_create = UserCredentials(username="test_user",
                                           password="test_pswd")
        user_cred_create_2 = UserCredentials(username="test_user_2",
                                             password="test_pswd_2")

        phy_rout = PhysicalRouter(phy_rout_name,
                           physical_router_user_credentials=user_cred_create)
        self._vnc_lib.physical_router_create(phy_rout)

        phy_rout_2 = PhysicalRouter(phy_rout_name_2,
                           physical_router_user_credentials=user_cred_create_2)
        self._vnc_lib.physical_router_create(phy_rout_2)

        obj_uuids = []
        obj_uuids.append(phy_rout.uuid)
        obj_uuids.append(phy_rout_2.uuid)

        phy_rtr_list = self._vnc_lib.physical_routers_list(obj_uuids=obj_uuids,
                                                           detail=True)
        for rtr in phy_rtr_list:
            user_cred_read = rtr.get_physical_router_user_credentials()
            self.assertEqual(user_cred_read.password, '**Password Hidden**')
       # end test_physical_router_credentials

    def test_allowed_address_pair_prefix_len(self):
       ip_addresses = {'10.10.10.1': 23,
                       '10.10.10.2': 24,
                       '10.10.10.3': 25,
                       'fe80:0:0:0:0:0:a0a:a0a': 119,
                       'fe80:0:0:0:0:0:a0a:a0b': 120,
                       'fe80:0:0:0:0:0:a0a:a0c': 121,
                      }
       proj = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
       vn = VirtualNetwork()
       for ip_address, prefix in ip_addresses.items():
           ip_family = netaddr.IPNetwork(ip_address).version
           vmi = VirtualMachineInterface('vmi-%s-' % prefix +self.id(), parent_obj=proj)
           print 'Validating with ip (%s) and prefix (%s)' % (ip_address, prefix)
           aap = AllowedAddressPair(ip=SubnetType(ip_address, prefix), address_mode='active-standby')
           aaps = AllowedAddressPairs()
           aaps.allowed_address_pair.append(aap)
           vmi.set_virtual_machine_interface_allowed_address_pairs(aaps)
           vmi.add_virtual_network(vn)
           try:
               self._vnc_lib.virtual_machine_interface_create(vmi)
               if ip_family == 4 and prefix < 24:
                   raise RuntimeError('Prefix of length < 24 should have been rejected')
               if ip_family == 6 and prefix < 120:
                   raise RuntimeError('Prefix of length < 120 should have been rejected')
           except cfgm_common.exceptions.BadRequest:
               if ip_family == 4 and prefix >= 24:
                   print 'ERROR: Prefix >= 24 should be accepted'
                   raise
               if ip_family == 6 and prefix >= 120:
                   print 'ERROR: Prefix >= 120 should be accepted'
                   raise
           finally:
               if ip_family == 4 and prefix >= 24:
                   vmi.del_virtual_machine_interface(vmi)
               if ip_family == 6 and prefix >= 120:
                   vmi.del_virtual_machine_interface(vmi)
    # end test_allowed_address_pair_prefix_len

    def test_bgpaas_ports_shrunk(self):
        gsc = self._vnc_lib.global_system_config_read(
                                      fq_name=['default-global-system-config'])
        bgpaas_param = BGPaaServiceParametersType('2','500')
        gsc.set_bgpaas_parameters(bgpaas_param)
        self._vnc_lib.global_system_config_update(gsc)

        gsc.set_bgpaas_parameters(BGPaaServiceParametersType('4','100'))
        # port range should be allowed to shrunk
        # as no bgpaas obj. is configured
        self._vnc_lib.global_system_config_update(gsc)

        bgpaas = BgpAsAService('bgpaas-%s' % self.id())
        self._vnc_lib.bgp_as_a_service_create(bgpaas)
        gsc.set_bgpaas_parameters(BGPaaServiceParametersType('10','50'))
        # port range should not be allowed to shrunk
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.global_system_config_update(gsc)
    # end test_bgpaas_ports_shrunk

    def test_invalid_parent_type(self):
        vn = VirtualNetwork(self.id())
        vn.fq_name = [vn.name]
        with ExpectedException(BadRequest):
            self._vnc_lib.virtual_network_create(vn)
        vn = VirtualNetwork(self.id())
        vn.parent_type='network_policy'
        with ExpectedException(BadRequest):
            self._vnc_lib.virtual_network_create(vn)
    # end test_invalid_parent_type

# end class TestCrud


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
        self.ignore_err_in_log = True
        api_server = self._server_info['api_server']
        orig_max_pending_updates = api_server._args.rabbit_max_pending_updates
        max_pend_upd = 10
        api_server._args.rabbit_max_pending_updates = str(max_pend_upd)
        orig_rabbitq_pub = api_server._db_conn._msgbus._producer.publish
        orig_rabbitq_conn_drain = api_server._db_conn._msgbus._conn_drain.connect
        orig_rabbitq_conn_publish = api_server._db_conn._msgbus._conn_publish.connect
        try:
            def err_rabbitq_pub(*args, **kwargs):
                raise Exception("Faking Rabbit publish failure")

            def err_rabbitq_conn(*args, **kwargs):
                gevent.sleep(0.1)
                raise Exception("Faking RabbitMQ connection failure")

            api_server._db_conn._msgbus._producer.publish = err_rabbitq_pub
            api_server._db_conn._msgbus._conn_publish.connect = err_rabbitq_conn

            logger.info("Creating objects to hit max rabbit pending.")
            # every VN create, creates RI too
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
            api_server._args.rabbit_max_pending_updates = orig_max_pending_updates
            api_server._db_conn._msgbus._producer.publish = orig_rabbitq_pub
            api_server._db_conn._msgbus._conn_drain.connect = orig_rabbitq_conn_drain
            api_server._db_conn._msgbus._conn_publish.connect = orig_rabbitq_conn_publish

    def test_reconnect_to_rabbit(self):
        self.ignore_err_in_log = True
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

            self.assertTill(self.ifmap_ident_has_link, obj=obj,
                            link_name='display-name')
            self.assertTill(lambda: 'test_update' in self.ifmap_ident_has_link(
                                obj=obj, link_name='display-name')['meta'])
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
        rabbit_conn = self._api_server._db_conn._msgbus._conn_drain
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

        self.assertTill(self.ifmap_ident_has_link, obj=obj,
                        link_name='display-name')
        self.assertTill(lambda: 'test_update_2' in self.ifmap_ident_has_link(
                            obj=obj, link_name='display-name')['meta'])
    # end test_reconnect_to_rabbit

    def test_handle_trap_on_exception(self):
        self.ignore_err_in_log = True
        api_server = self._server_info['api_server']
        orig_read = api_server._db_conn._cassandra_db.object_read

        def exception_on_log_error(*args, **kwargs):
            self.assertTrue(False)

        def exception_on_vn_read(obj_type, *args, **kwargs):
            if obj_type == 'virtual_network':
                raise Exception("fake vn read exception")
            orig_read(obj_type, *args, **kwargs)

        try:
            orig_config_log = api_server.config_log
            api_server.config_log = exception_on_log_error
            with ExpectedException(NoIdError):
                self._vnc_lib.virtual_network_read(fq_name=['foo', 'bar', 'baz'])
        finally:
            api_server.config_log = orig_config_log

        try:
            test_obj = self._create_test_object()
            api_server._db_conn._cassandra_db.object_read = exception_on_vn_read
            with ExpectedException(HttpError):
                self._vnc_lib.virtual_network_read(fq_name=test_obj.get_fq_name())
        finally:
            api_server._db_conn._cassandra_db.object_read = orig_read

    def test_sandesh_trace(self):
        from lxml import etree
        api_server = self._server_info['api_server']
        # the test
        test_obj = self._create_test_object()
        self.assertTill(self.ifmap_has_ident, obj=test_obj)
        self._vnc_lib.virtual_network_delete(id=test_obj.uuid)

        # and validations
        introspect_port = api_server._args.http_server_port
        traces = requests.get('http://localhost:%s/Snh_SandeshTraceRequest?x=RestApiTraceBuf' %(introspect_port))
        self.assertThat(traces.status_code, Equals(200))
        top_elem = etree.fromstring(traces.text)
        self.assertThat(top_elem[0][0][-2].text, Contains('POST'))
        self.assertThat(top_elem[0][0][-2].text, Contains('200 OK'))
        self.assertThat(top_elem[0][0][-1].text, Contains('DELETE'))
        self.assertThat(top_elem[0][0][-1].text, Contains('200 OK'))

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
                  'vn-%s' %(self.id()),
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
        with ExpectedException(RefsExistError) as e:
            iip_fixt = self.useFixture(
                InstanceIpTestFixtureGen(
                    self._vnc_lib, 'iip1', auto_prop_val=False,
                    instance_ip_address=ip_allocated,
                    virtual_network_refs=[vn_fixt.getObj()]))
    # end test_floatingip_as_instanceip

    def test_aliasip_as_instanceip(self):
        ipam_fixt = self.useFixture(NetworkIpamTestFixtureGen(self._vnc_lib))

        project_fixt = self.useFixture(ProjectTestFixtureGen(self._vnc_lib, 'default-project'))

        subnet_vnc = IpamSubnetType(subnet=SubnetType('1.1.1.0', 24))
        vnsn_data = VnSubnetsType([subnet_vnc])
        logger.info("Creating a virtual network")
        logger.info("Creating subnet 1.1.1.0/24")
        vn_fixt = self.useFixture(VirtualNetworkTestFixtureGen(self._vnc_lib,
                  'vn-%s' %(self.id()),
                  network_ipam_ref_infos=[(ipam_fixt.getObj(), vnsn_data)]))
        vn_fixt.getObj().set_router_external(True)
        self._vnc_lib.virtual_network_update(vn_fixt.getObj())

        logger.info("Fetching alias-ip-pool")
        aip_pool_fixt = self.useFixture(
            AliasIpPoolTestFixtureGen(self._vnc_lib, 'alias-ip-pool',
                                         parent_fixt=vn_fixt))

        logger.info("Creating auto-alloc alias-ip")
        aip_fixt = self.useFixture(
            AliasIpTestFixtureGen(
                self._vnc_lib, 'aip1', parent_fixt=aip_pool_fixt,
                project_refs=[project_fixt.getObj()]))
        ip_allocated = aip_fixt.getObj().alias_ip_address

        logger.info("Creating auto-alloc instance-ip, expecting an error")
        with ExpectedException(RefsExistError) as e:
            iip_fixt = self.useFixture(
                InstanceIpTestFixtureGen(
                    self._vnc_lib, 'iip1', auto_prop_val=False,
                    instance_ip_address=ip_allocated,
                    virtual_network_refs=[vn_fixt.getObj()]))
    # end test_aliasip_as_instanceip

    def test_name_with_reserved_xml_char(self, port = '8443'):
        self.skipTest("Skipping test_name_with_reserved_xml_char")
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
        ident_str = self.ifmap_has_ident(obj=vn_obj)['ident']
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
                    name, proj_obj, display_name=name, is_shared=True,
                    router_external=False)
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

        # unanchored summary list with field filters, with extra fields
        resp = self._vnc_lib.virtual_networks_list(
            filters={'display_name':vn_objs[2].display_name},
            fields=['is_shared'])
        vn_dicts = resp['virtual-networks']
        self.assertThat(len(vn_dicts), Equals(1))
        self.assertThat(vn_dicts[0]['uuid'], Equals(vn_objs[2].uuid))
        self.assertThat(set(['fq_name', 'uuid', 'href', 'is_shared']),
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
        self.assertThat(len(read_vn_objs), Not(LessThan(num_objs)))
        read_display_names = [o.display_name for o in read_vn_objs]
        for obj in vn_objs:
            self.assertThat(read_display_names,
                            Contains(obj.display_name))

        # parent anchored summary list without filters, with extra fields
        read_vn_dicts = self._vnc_lib.virtual_networks_list(
            parent_id=proj_obj.uuid,
            fields=['router_external'])['virtual-networks']
        self.assertThat(len(read_vn_dicts), Equals(num_objs))
        for obj in vn_objs:
            # locate created object, should only be one, expect exact fields
            obj_dict = [d for d in read_vn_dicts if d['uuid'] == obj.uuid]
            self.assertThat(len(obj_dict), Equals(1))
            self.assertThat(set(['fq_name', 'uuid', 'href', 'router_external']),
                            Equals(set(obj_dict[0].keys())))
            self.assertThat(obj_dict[0]['fq_name'][:-1],
                Equals(proj_obj.fq_name))
            self.assertEqual(obj_dict[0]['router_external'], False)

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
            self.assertEqual(obj_dict[0]['fq_name'], obj.get_fq_name())
            self.assertThat(set(['fq_name', 'uuid', 'href']),
                            Equals(set(obj_dict[0].keys())))

        # backref anchored summary list with filters, with extra fields
        resp = self._vnc_lib.virtual_networks_list(
            back_ref_id=ipam_obj.uuid,
            filters={'display_name':vn_objs[2].display_name},
            fields=['is_shared', 'router_external'])
        read_vn_dicts = resp['virtual-networks']
        self.assertEqual(len(read_vn_dicts), 1)
        self.assertEqual(read_vn_dicts[0]['uuid'], vn_objs[2].uuid)
        self.assertEqual(read_vn_dicts[0]['is_shared'], True)
        self.assertEqual(read_vn_dicts[0]['router_external'], False)

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

    def test_list_for_coverage(self):
        name = '%s-vn1' %(self.id())
        vn1_obj = VirtualNetwork(
            name, display_name=name, is_shared=True,
            router_external=False)
        self._vnc_lib.virtual_network_create(vn1_obj)

        name = '%s-vn2' %(self.id())
        id_perms = IdPermsType(user_visible=False)
        vn2_obj = VirtualNetwork(
            name, display_name=name, id_perms=id_perms,
            is_shared=True, router_external=False)
        def fake_admin_request(orig_method, *args, **kwargs):
            return True
        with test_common.patch(self._api_server,
            'is_admin_request', fake_admin_request):
            self._vnc_lib.virtual_network_create(vn2_obj)

        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port
        q_params = 'obj_uuids=%s,%s&fields=is_shared,router_external' %(
            vn1_obj.uuid, vn2_obj.uuid)
        url = 'http://%s:%s/virtual-networks?%s' %(
            listen_ip, listen_port, q_params)

        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        read_vn_dicts = json.loads(resp.text)['virtual-networks']
        self.assertEqual(len(read_vn_dicts), 1)
        self.assertEqual(read_vn_dicts[0]['uuid'], vn1_obj.uuid)
        self.assertEqual(read_vn_dicts[0]['is_shared'], True)
        self.assertEqual(read_vn_dicts[0]['router_external'], False)
    # end test_list_for_coverage

    def test_create_with_wrong_type(self):
        vn_obj = VirtualNetwork('%s-bad-prop-type' %(self.id()))
        vn_obj.virtual_network_properties = 'foo' #VirtualNetworkType
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.virtual_network_create(vn_obj)
    #end test_create_with_wrong_type(self):

    def test_update_with_wrong_type(self):
        vn_obj = VirtualNetwork('%s-bad-prop-type' %(self.id()))
        self._vnc_lib.virtual_network_create(vn_obj)
        vn_obj.virtual_network_properties = 'foo' #VirtualNetworkType
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.virtual_network_update(vn_obj)
    #end test_update_with_wrong_type(self):

    def test_read_rest_api(self):
        logger.info("Creating VN, RI, VMI.")
        vn_objs, ipam_objs, ri_objs, vmi_objs = self._create_vn_ri_vmi()

        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        logger.info("Reading VN without filters.")
        url = 'http://%s:%s/virtual-network/%s' %(
            listen_ip, listen_port, vn_objs[0].uuid)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']

        self.assertThat(ret_vn.keys(), Contains('routing_instances'))
        self.assertThat(ret_vn.keys(), Contains('virtual_machine_interface_back_refs'))
        for link_key, linked_obj in [('routing_instances', ri_objs[0]),
            ('virtual_machine_interface_back_refs', vmi_objs[0])]:
            found = False
            for ret_link in ret_vn[link_key]:
                self.assertThat(ret_link, Contains('to'))
                self.assertThat(ret_link, Contains('uuid'))
                if (ret_link['to'] == linked_obj.get_fq_name() and
                    ret_link['uuid'] == linked_obj.uuid):
                    found = True
                    break
            self.assertTrue(found)

        logger.info("Reading VN with children excluded.")
        url = 'http://%s:%s/virtual-network/%s?exclude_children=true' %(
            listen_ip, listen_port, vn_objs[0].uuid)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']

        self.assertThat(ret_vn.keys(), Not(Contains('routing_instances')))
        self.assertThat(ret_vn.keys(), Contains(
            'virtual_machine_interface_back_refs'))
        for link_key, linked_obj in [('virtual_machine_interface_back_refs',
                                     vmi_objs[0])]:
            ret_link = ret_vn[link_key][0]
            self.assertThat(ret_link, Contains('to'))
            self.assertThat(ret_link, Contains('uuid'))
            self.assertEqual(ret_link['to'], linked_obj.get_fq_name())
            self.assertEqual(ret_link['uuid'], linked_obj.uuid)

        logger.info("Reading VN with backrefs excluded.")
        url = 'http://%s:%s/virtual-network/%s?exclude_back_refs=true' %(
            listen_ip, listen_port, vn_objs[0].uuid)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']

        self.assertThat(ret_vn.keys(), Contains('routing_instances'))
        self.assertThat(ret_vn.keys(), Not(Contains(
            'virtual_machine_interface_back_refs')))
        for link_key, linked_obj in [('routing_instances',
                                     ri_objs[0])]:
            found = False
            for ret_link in ret_vn[link_key]:
                self.assertThat(ret_link, Contains('to'))
                self.assertThat(ret_link, Contains('uuid'))
                if (ret_link['to'] == linked_obj.get_fq_name() and
                    ret_link['uuid'] == linked_obj.uuid):
                    found = True
                    break
            self.assertTrue(found)

        logger.info("Reading VN with children and backrefs excluded.")
        query_param_str = 'exclude_children=True&exclude_back_refs=true'
        url = 'http://%s:%s/virtual-network/%s?%s' %(
            listen_ip, listen_port, vn_objs[0].uuid, query_param_str)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']

        self.assertThat(ret_vn.keys(), Not(Contains('routing_instances')))
        self.assertThat(ret_vn.keys(), Not(Contains(
            'virtual_machine_interface_back_refs')))

        # Properties and references are always returned irrespective of what
        # fields are requested
        property = 'virtual_network_network_id'
        reference = 'network_ipam_refs'
        children = 'routing_instances'
        back_reference = 'virtual_machine_interface_back_refs'

        logger.info("Reading VN with one specific property field.")
        query_param_str = 'fields=%s' % property
        url = 'http://%s:%s/virtual-network/%s?%s' % (
            listen_ip, listen_port, vn_objs[0].uuid, query_param_str)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']
        self.assertThat(ret_vn.keys(), Contains(property))
        self.assertThat(ret_vn.keys(), Contains(reference))
        self.assertThat(ret_vn.keys(), Not(Contains(children)))
        self.assertThat(ret_vn.keys(), Not(Contains(back_reference)))

        logger.info("Reading VN with one specific ref field.")
        query_param_str = 'fields=%s' % reference
        url = 'http://%s:%s/virtual-network/%s?%s' % (
            listen_ip, listen_port, vn_objs[0].uuid, query_param_str)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']
        self.assertThat(ret_vn.keys(), Contains(property))
        self.assertThat(ret_vn.keys(), Contains(reference))
        self.assertThat(ret_vn.keys(), Not(Contains(children)))
        self.assertThat(ret_vn.keys(), Not(Contains(back_reference)))

        logger.info("Reading VN with one specific children field.")
        query_param_str = 'fields=%s' % children
        url = 'http://%s:%s/virtual-network/%s?%s' % (
            listen_ip, listen_port, vn_objs[0].uuid, query_param_str)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']
        self.assertThat(ret_vn.keys(), Contains(property))
        self.assertThat(ret_vn.keys(), Contains(reference))
        self.assertThat(ret_vn.keys(), Contains(children))
        self.assertThat(ret_vn.keys(), Not(Contains(back_reference)))

        logger.info("Reading VN with one specific back-reference field.")
        query_param_str = 'fields=%s' % back_reference
        url = 'http://%s:%s/virtual-network/%s?%s' % (
            listen_ip, listen_port, vn_objs[0].uuid, query_param_str)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']
        self.assertThat(ret_vn.keys(), Contains(property))
        self.assertThat(ret_vn.keys(), Contains(reference))
        self.assertThat(ret_vn.keys(), Not(Contains(children)))
        self.assertThat(ret_vn.keys(), Contains(back_reference))

        logger.info("Reading VN with property, reference, children and "
                    "back-reference fields.")
        query_param_str = ('fields=%s,%s,%s,%s' % (property, reference,
                                                   children, back_reference))
        url = 'http://%s:%s/virtual-network/%s?%s' % (
            listen_ip, listen_port, vn_objs[0].uuid, query_param_str)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']
        self.assertThat(ret_vn.keys(), Contains(property))
        self.assertThat(ret_vn.keys(), Contains(reference))
        self.assertThat(ret_vn.keys(), Contains(children))
        self.assertThat(ret_vn.keys(), Contains(back_reference))
    # end test_read_rest_api

    def test_delete_after_unref(self):
        # 2 policies, 1 VN associate to VN, dissociate, delete policies
        def create_vn_and_policies():
            pol1_obj = NetworkPolicy('%s-pol1' %(self.id()))
            self._vnc_lib.network_policy_create(pol1_obj)

            pol2_obj = NetworkPolicy('%s-pol2' %(self.id()))
            self._vnc_lib.network_policy_create(pol2_obj)

            vn_obj = VirtualNetwork('%s-vn' %(self.id()))
            vn_obj.add_network_policy(pol1_obj,
                VirtualNetworkPolicyType(sequence=SequenceType(major=0, minor=0)))
            vn_obj.add_network_policy(pol2_obj,
                VirtualNetworkPolicyType(sequence=SequenceType(major=1, minor=0)))
            self._vnc_lib.virtual_network_create(vn_obj)
            return vn_obj, pol1_obj, pol2_obj

        def delete_vn_and_policies():
            self._vnc_lib.network_policy_delete(id=pol1_obj.uuid)
            self._vnc_lib.network_policy_delete(id=pol2_obj.uuid)
            self._vnc_lib.virtual_network_delete(id=vn_obj.uuid)

        # references could be removed like this...
        vn_obj, pol1_obj, pol2_obj = create_vn_and_policies()
        vn_obj.del_network_policy(pol1_obj)
        vn_obj.del_network_policy(pol2_obj)
        self._vnc_lib.virtual_network_update(vn_obj)
        delete_vn_and_policies()

        # ... or this
        # references could be removed like this...
        vn_obj, pol1_obj, pol2_obj = create_vn_and_policies()
        vn_obj.set_network_policy_list([], [])
        self._vnc_lib.virtual_network_update(vn_obj)
        delete_vn_and_policies()
    # end test_delete_after_unref

    def test_vn_with_native_ri(self):
        logger.info("Creating a VN, expecting auto Native RI creation...")
        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn_obj)
        ri_obj = self._vnc_lib.routing_instance_read(
            fq_name=vn_obj.fq_name+[vn_obj.name])
        ri_children = vn_obj.get_routing_instances()
        self.assertTrue(ri_obj.uuid in [r['uuid'] for r in ri_children])
        logger.info("...VN/RI creation done.")

        logger.info("Deleting a VN, expecting auto Native RI deletion.")
        self._vnc_lib.virtual_network_delete(id=vn_obj.uuid)
        with ExpectedException(NoIdError) as e:
            self._vnc_lib.routing_instance_read(fq_name=ri_obj.fq_name)

        logger.info("Testing delete RI with refs to RI...")
        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn_obj)
        ri_obj = self._vnc_lib.routing_instance_read(
            fq_name=vn_obj.fq_name+[vn_obj.name])
        vmi_obj = VirtualMachineInterface(
            'vmi-%s' %(self.id()), parent_obj=Project())
        # link to vn expected in vmi create in server
        vmi_obj.add_virtual_network(vn_obj)
        vmi_obj.add_routing_instance(ri_obj, PolicyBasedForwardingRuleType())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        logger.info("...VN/RI/VMI creation done...")
        # remove link from vmi before vn delete
        vmi_obj.del_virtual_network(vn_obj)
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        self._vnc_lib.virtual_network_delete(id=vn_obj.uuid)
        with ExpectedException(NoIdError) as e:
            self._vnc_lib.routing_instance_read(fq_name=ri_obj.fq_name)
        vmi_obj = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid)
        ri_refs = vmi_obj.get_routing_instance_refs()
        self.assertIsNone(ri_refs)
        logger.info("...VN/RI deletion done.")
    # end test_vn_with_native_ri

    def test_vmi_links_to_native_ri(self):
        logger.info("Creating a VN/VMI, expecting auto Native RI linking...")
        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn_obj)
        vmi_obj = VirtualMachineInterface(
            'vmi-%s' %(self.id()), parent_obj=Project())
        # link to vn expected in vmi create in server
        vmi_obj.add_virtual_network(vn_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        vmi_obj = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid)

        ri_refs = vmi_obj.get_routing_instance_refs()
        ri_fq_name = vn_obj.fq_name[:]
        ri_fq_name.append(vn_obj.fq_name[-1])
        self.assertEqual(ri_refs[0]['to'], ri_fq_name)
        logger.info("...link to Native RI done.")
    # end test_vmi_links_to_native_ri

    def test_nop_on_empty_body_update(self):
        # library api test
        vn_fq_name = VirtualNetwork().fq_name
        vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        mod_time = vn_obj.id_perms.last_modified
        resp = self._vnc_lib.virtual_network_update(vn_obj)
        self.assertIsNone(resp)
        vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
        self.assertEqual(mod_time, vn_obj.id_perms.last_modified)

        # rest api test
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port
        url = 'http://%s:%s/virtual-network/%s' %(
            listen_ip, listen_port, vn_obj.uuid)
        resp = requests.put(url)
        self.assertEqual(resp.status_code, 200)
        self.assertEqual(resp.text, '')
    # end test_nop_on_empty_body_update

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

    def test_ip_addr_not_released_on_delete_error(self):
        ipam_obj = NetworkIpam('ipam-%s' %(self.id()))
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        vn_obj.add_network_ipam(ipam_obj,
            VnSubnetsType(
                [IpamSubnetType(SubnetType('1.1.1.0', 28))]))
        self._vnc_lib.virtual_network_create(vn_obj)

        # instance-ip test
        iip_obj = InstanceIp('iip-%s' %(self.id()))
        iip_obj.add_virtual_network(vn_obj)
        self._vnc_lib.instance_ip_create(iip_obj)
        # read back to get allocated ip
        iip_obj = self._vnc_lib.instance_ip_read(id=iip_obj.uuid)

        def err_on_delete(orig_method, *args, **kwargs):
            if args[0] == 'instance_ip':
                raise Exception("Faking db delete for instance ip")
            return orig_method(*args, **kwargs)
        with test_common.patch(
            self._api_server._db_conn, 'dbe_delete', err_on_delete):
            try:
                self._vnc_lib.instance_ip_delete(id=iip_obj.uuid)
                self.assertTrue(
                    False, 'Instance IP delete worked unexpectedly')
            except Exception as e:
                self.assertThat(str(e),
                    Contains('"Faking db delete for instance ip"'))
                # assert reservation present in zookeeper and value in iip
                zk_node = "%(#)010d" % {'#': int(netaddr.IPAddress(
                    iip_obj.instance_ip_address))}
                zk_path = '/api-server/subnets/%s:1.1.1.0/28/%s' %(
                    vn_obj.get_fq_name_str(), zk_node)
                mock_zk = self._api_server._db_conn._zk_db._zk_client._zk_client
                self.assertEqual(
                    mock_zk._values[zk_path][0], iip_obj.uuid)
                self.assertEqual(
                    self._vnc_lib.instance_ip_read(
                        id=iip_obj.uuid).instance_ip_address,
                    iip_obj.instance_ip_address)

        # floating-ip test
        fip_pool_obj = FloatingIpPool(
            'fip-pool-%s' %(self.id()), parent_obj=vn_obj)
        self._vnc_lib.floating_ip_pool_create(fip_pool_obj)
        fip_obj = FloatingIp('fip-%s' %(self.id()), parent_obj=fip_pool_obj)
        fip_obj.add_project(Project())
        self._vnc_lib.floating_ip_create(fip_obj)
        # read back to get allocated floating-ip
        fip_obj = self._vnc_lib.floating_ip_read(id=fip_obj.uuid)

        def err_on_delete(orig_method, *args, **kwargs):
            if args[0] == 'floating_ip':
                raise Exception("Faking db delete for floating ip")
            if args[0] == 'alias_ip':
                raise Exception("Faking db delete for alias ip")
            return orig_method(*args, **kwargs)
        with test_common.patch(
            self._api_server._db_conn, 'dbe_delete', err_on_delete):
            try:
                self._vnc_lib.floating_ip_delete(id=fip_obj.uuid)
                self.assertTrue(
                    False, 'Floating IP delete worked unexpectedly')
            except Exception as e:
                self.assertThat(str(e),
                    Contains('"Faking db delete for floating ip"'))
                # assert reservation present in zookeeper and value in iip
                zk_node = "%(#)010d" % {'#': int(netaddr.IPAddress(
                    fip_obj.floating_ip_address))}
                zk_path = '/api-server/subnets/%s:1.1.1.0/28/%s' %(
                    vn_obj.get_fq_name_str(), zk_node)
                mock_zk = self._api_server._db_conn._zk_db._zk_client._zk_client
                self.assertEqual(
                    mock_zk._values[zk_path][0], fip_obj.uuid)
                self.assertEqual(
                    self._vnc_lib.floating_ip_read(
                        id=fip_obj.uuid).floating_ip_address,
                    fip_obj.floating_ip_address)

        # alias-ip test
        aip_pool_obj = AliasIpPool(
            'aip-pool-%s' %(self.id()), parent_obj=vn_obj)
        self._vnc_lib.alias_ip_pool_create(aip_pool_obj)
        aip_obj = AliasIp('aip-%s' %(self.id()), parent_obj=aip_pool_obj)
        aip_obj.add_project(Project())
        self._vnc_lib.alias_ip_create(aip_obj)
        # read back to get allocated alias-ip
        aip_obj = self._vnc_lib.alias_ip_read(id=aip_obj.uuid)

        with test_common.patch(
            self._api_server._db_conn, 'dbe_delete', err_on_delete):
            try:
                self._vnc_lib.alias_ip_delete(id=aip_obj.uuid)
                self.assertTrue(
                    False, 'Alias IP delete worked unexpectedly')
            except Exception as e:
                self.assertThat(str(e),
                    Contains('"Faking db delete for alias ip"'))
                # assert reservation present in zookeeper and value in iip
                zk_node = "%(#)010d" % {'#': int(netaddr.IPAddress(
                    aip_obj.alias_ip_address))}
                zk_path = '/api-server/subnets/%s:1.1.1.0/28/%s' %(
                    vn_obj.get_fq_name_str(), zk_node)
                mock_zk = self._api_server._db_conn._zk_db._zk_client._zk_client
                self.assertEqual(
                    mock_zk._values[zk_path][0], aip_obj.uuid)
                self.assertEqual(
                    self._vnc_lib.alias_ip_read(
                        id=aip_obj.uuid).alias_ip_address,
                    aip_obj.alias_ip_address)
    # end test_ip_addr_not_released_on_delete_error

    def test_uve_trace_delete_name_from_msg(self):
        test_obj = self._create_test_object()
        self.assertTill(self.ifmap_has_ident, obj=test_obj)
        db_client = self._api_server._db_conn

        uve_delete_trace_invoked = []
        uuid_to_fq_name_on_delete_invoked = []
        def spy_uve_trace(orig_method, *args, **kwargs):
            oper = args[0].upper()
            obj_uuid = args[2]
            if oper == 'DELETE' and obj_uuid == test_obj.uuid:
                if not uve_delete_trace_invoked:
                    uve_delete_trace_invoked.append(True)
                def assert_on_call(*args, **kwargs):
                    uuid_to_fq_name_on_delete_invoked.append(True)
                with test_common.patch(db_client,
                    'uuid_to_fq_name', assert_on_call):
                    return orig_method(*args, **kwargs)
            else:
                return orig_method(*args, **kwargs)
        with test_common.patch(db_client,
            'dbe_uve_trace', spy_uve_trace):
            self._delete_test_object(test_obj)
            self.assertTill(self.ifmap_doesnt_have_ident, obj=test_obj)
            self.assertEqual(len(uve_delete_trace_invoked), 1,
                'uve_trace not invoked on object delete')
            self.assertEqual(len(uuid_to_fq_name_on_delete_invoked), 0,
                'uuid_to_fq_name invoked in delete at dbe_uve_trace')
    # end test_uve_trace_delete_name_from_msg

    def test_ref_update_with_resource_type_underscored(self):
        vn_obj = VirtualNetwork('%s-vn' % self.id())
        ipam_obj = NetworkIpam('%s-vmi' % self.id())
        self._vnc_lib.network_ipam_create(ipam_obj)
        self._vnc_lib.virtual_network_create(vn_obj)
        subnet_type = IpamSubnetType(subnet=SubnetType('1.1.1.0', 2))

        self._vnc_lib.ref_update(vn_obj.get_type().replace('-', '_'),
                                 vn_obj.uuid,
                                 ipam_obj.get_type().replace('-', '_'),
                                 ipam_obj.uuid,
                                 ipam_obj.fq_name,
                                 'ADD',
                                 VnSubnetsType([subnet_type]))

        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        fq_name = vn_obj.get_network_ipam_refs()[0]['to']
        ipam_name = self._vnc_lib.network_ipam_read(fq_name=fq_name).name
        self.assertEqual(ipam_obj.name, ipam_name)

    def test_fq_name_to_id_with_resource_type_underscored(self):
        test_obj = self._create_test_object()

        test_uuid = self._vnc_lib.fq_name_to_id(
            test_obj.get_type().replace('-', '_'), test_obj.get_fq_name())

        # check that format is correct
        try:
            uuid.UUID(test_uuid)
        except ValueError:
            self.assertTrue(False, 'Bad form UUID ' + test_uuid)

    def test_resource_list_with_resource_type_underscored(self):
        test_obj = self._create_test_object()

        resources = self._vnc_lib.resource_list(
            test_obj.get_type().replace('-', '_'),
            obj_uuids=[test_obj.uuid])
        resource_ids = [resource['uuid'] for resource in
                             resources['%ss' % test_obj.get_type()]]
        self.assertEqual([test_obj.uuid], resource_ids)

    def test_allocate_vn_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        vn_obj = VirtualNetwork('%s-vn' % self.id())

        self._vnc_lib.virtual_network_create(vn_obj)

        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        vn_id = vn_obj.virtual_network_network_id
        self.assertEqual(vn_obj.get_fq_name_str(),
                         mock_zk.get_vn_from_id(vn_id - 1))

    def test_deallocate_vn_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        vn_obj = VirtualNetwork('%s-vn' % self.id())
        self._vnc_lib.virtual_network_create(vn_obj)
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        vn_id = vn_obj.virtual_network_network_id

        self._vnc_lib.virtual_network_delete(id=vn_obj.uuid)

        self.assertIsNone(mock_zk.get_vn_from_id(vn_id - 1))

    # TODO(ethuleau): As we keep the virtual network ID allocation in
    #                 schema and in the vnc API for one release overlap to
    #                 prevent any upgrade issue, we still authorize to
    #                 set or update the virtual network ID until release
    #                 (3.2 + 1)
    def test_cannot_set_vn_id(self):
        self.skipTest("Skipping test_cannot_set_vn_id")
        vn_obj = VirtualNetwork('%s-vn' % self.id())
        vn_obj.set_virtual_network_network_id(42)

        with ExpectedException(PermissionDenied):
            self._vnc_lib.virtual_network_create(vn_obj)

    # TODO(ethuleau): As we keep the virtual network ID allocation in
    #                 schema and in the vnc API for one release overlap to
    #                 prevent any upgrade issue, we still authorize to
    #                 set or update the virtual network ID until release
    #                 (3.2 + 1)
    def test_cannot_update_vn_id(self):
        self.skipTest("Skipping test_cannot_update_vn_id")
        vn_obj = VirtualNetwork('%s-vn' % self.id())
        self._vnc_lib.virtual_network_create(vn_obj)
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)

        vn_obj.set_virtual_network_network_id(42)
        with ExpectedException(PermissionDenied):
            self._vnc_lib.virtual_network_update(vn_obj)

        # test can update with same value, needed internally
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        vn_obj.set_virtual_network_network_id(
            vn_obj.virtual_network_network_id)
        self._vnc_lib.virtual_network_update(vn_obj)

    def test_allocate_sg_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())

        self._vnc_lib.security_group_create(sg_obj)

        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id
        self.assertEqual(sg_obj.get_fq_name_str(),
                         mock_zk.get_sg_from_id(sg_id))

    def test_deallocate_sg_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())
        self._vnc_lib.security_group_create(sg_obj)
        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id

        self._vnc_lib.security_group_delete(id=sg_obj.uuid)

        self.assertIsNone(mock_zk.get_sg_from_id(sg_id))

    # TODO(ethuleau): As we keep the virtual network ID allocation in
    #                 schema and in the vnc API for one release overlap to
    #                 prevent any upgrade issue, we still authorize to
    #                 set or update the virtual network ID until release
    #                 (3.2 + 1)
    def test_cannot_set_sg_id(self):
        self.skipTest("Skipping test_cannot_set_sg_id")
        sg_obj = SecurityGroup('%s-sg' % self.id())

        sg_obj.set_security_group_id(42)
        with ExpectedException(PermissionDenied):
            self._vnc_lib.security_group_create(sg_obj)

    # TODO(ethuleau): As we keep the virtual network ID allocation in
    #                 schema and in the vnc API for one release overlap to
    #                 prevent any upgrade issue, we still authorize to
    #                 set or update the virtual network ID until release
    #                 (3.2 + 1)
    def test_cannot_update_sg_id(self):
        self.skipTest("Skipping test_cannot_update_sg_id")
        sg_obj = SecurityGroup('%s-sg' % self.id())
        self._vnc_lib.security_group_create(sg_obj)
        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)

        sg_obj.set_security_group_id(42)
        with ExpectedException(PermissionDenied):
            self._vnc_lib.security_group_update(sg_obj)

        # test can update with same value, needed internally
        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)
        sg_obj.set_security_group_id(sg_obj.security_group_id)
        self._vnc_lib.security_group_update(sg_obj)

    def test_create_sg_with_configured_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())
        sg_obj.set_configured_security_group_id(42)

        self._vnc_lib.security_group_create(sg_obj)

        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_id, 42)
        self.assertEqual(configured_sg_id, 42)
        self.assertIsNone(mock_zk.get_sg_from_id(sg_id))

    def test_update_sg_with_configured_id(self):
        mock_zk = self._api_server._db_conn._zk_db
        sg_obj = SecurityGroup('%s-sg' % self.id())

        self._vnc_lib.security_group_create(sg_obj)

        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)
        allocated_sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_obj.get_fq_name_str(),
                         mock_zk.get_sg_from_id(allocated_sg_id))
        self.assertIsNone(configured_sg_id)

        sg_obj.set_configured_security_group_id(42)
        self._vnc_lib.security_group_update(sg_obj)

        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)
        sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_id, 42)
        self.assertEqual(configured_sg_id, 42)
        self.assertIsNone(mock_zk.get_sg_from_id(allocated_sg_id))

        sg_obj.set_configured_security_group_id(0)
        self._vnc_lib.security_group_update(sg_obj)

        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)
        allocated_sg_id = sg_obj.security_group_id
        configured_sg_id = sg_obj.configured_security_group_id
        self.assertEqual(sg_obj.get_fq_name_str(),
                         mock_zk.get_sg_from_id(allocated_sg_id))
        self.assertEqual(configured_sg_id, 0)

    def test_singleton_no_rule_sg_for_openstack_created(self):
        try:
            no_rule_sg = self._vnc_lib.security_group_read(SG_NO_RULE_FQ_NAME)
        except NoIdError:
            self.fail("Cannot read singleton security '%s' for OpenStack" %
                      ':'.join(SG_NO_RULE_FQ_NAME))

        self.assertIsNotNone(no_rule_sg.security_group_id)
        self.assertIsInstance(no_rule_sg.security_group_id, int)
        self.assertGreater(no_rule_sg.security_group_id, 0)

    def test_singleton_no_rule_sg(self):
        try:
            no_rule_sg = self._vnc_lib.security_group_read(SG_NO_RULE_FQ_NAME)
        except NoIdError:
            self.fail("Cannot read singleton security '%s' for OpenStack" %
                      ':'.join(SG_NO_RULE_FQ_NAME))

        sg_obj = SecurityGroup(name=SG_NO_RULE_FQ_NAME[-1])
        self._api_server._create_singleton_entry(sg_obj)
        try:
            no_rule_sg_2 = self._vnc_lib.security_group_read(SG_NO_RULE_FQ_NAME)
        except NoIdError:
            self.fail("Cannot read singleton security '%s' for OpenStack" %
                      ':'.join(SG_NO_RULE_FQ_NAME))
        self.assertEqual(no_rule_sg.security_group_id,
                         no_rule_sg_2.security_group_id)

    def test_qos_config(self):
        qc = QosConfig('qos-config-%s' %(self.id()), Project())
        self._vnc_lib.qos_config_create(qc)
        qc = self._vnc_lib.qos_config_read(fq_name=qc.get_fq_name())
        self.assertEqual(len(qc.get_global_system_config_refs()), 1)

    def test_annotations(self):
        vn_obj = vnc_api.VirtualNetwork('vn-set-%s' %(self.id()))
        vn_obj.set_annotations(
            KeyValuePairs([KeyValuePair(key='k1', value='v1'),
                           KeyValuePair(key=' k2 prime ',
                                        value=json.dumps('v2'))]))
        self._vnc_lib.virtual_network_create(vn_obj)
        ret_vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        self.assertEqual(len(ret_vn_obj.annotations.key_value_pair), 2)
        annotation_check = [a for a in ret_vn_obj.annotations.key_value_pair
            if a.key == ' k2 prime ']
        self.assertEqual(len(annotation_check), 1)
        self.assertEqual(annotation_check[0].value,
                         json.dumps('v2'))

        vn_obj = vnc_api.VirtualNetwork('vn-add-del-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn_obj)

        vn_obj.add_annotations(KeyValuePair(key='k1', value=None))
        vn_obj.add_annotations(KeyValuePair(key='k2', value='v2'))
        vn_obj.add_annotations(KeyValuePair(key='k3', value=str(300)))
        self._vnc_lib.virtual_network_update(vn_obj)
        ret_vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        self.assertEqual(len(ret_vn_obj.annotations.key_value_pair), 3)
        self.assertEqual(set(['k1', 'k2', 'k3']),
            set([a.key for a in ret_vn_obj.annotations.key_value_pair]))

        vn_obj.del_annotations(elem_position='k1')
        self._vnc_lib.virtual_network_update(vn_obj)
        ret_vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        self.assertEqual(len(ret_vn_obj.annotations.key_value_pair), 2)
        self.assertEqual(set(['k2', 'k3']),
            set([a.key for a in ret_vn_obj.annotations.key_value_pair]))
    # end test_annotations

    def test_cert_bundle_refresh(self):
        bundle_dir = tempfile.mkdtemp(self.id())
        try:
            with open(bundle_dir+'cert', 'w') as f:
                f.write("CERT")
            with open(bundle_dir+'ca', 'w') as f:
                f.write("CA")
            with open(bundle_dir+'key', 'w') as f:
                f.write("KEY")
            cfgm_common.utils.getCertKeyCaBundle(bundle_dir+'pem',
                [bundle_dir+x for x in ['cert', 'ca', 'key']])
            with open(bundle_dir+'pem', 'r') as f:
                self.assertEqual(f.readlines()[0], 'CERTCAKEY')

            # sleep so mods to cert/ca/key appear as different epoch
            gevent.sleep(0.1)

            with open(bundle_dir+'cert', 'w') as f:
                f.write("CERTNEW")
            with open(bundle_dir+'ca', 'w') as f:
                f.write("CANEW")
            with open(bundle_dir+'key', 'w') as f:
                f.write("KEYNEW")
            cfgm_common.utils.getCertKeyCaBundle(bundle_dir+'pem',
                [bundle_dir+x for x in ['cert', 'ca', 'key']])
            with open(bundle_dir+'pem', 'r') as f:
                self.assertEqual(f.readlines()[0], 'CERTNEWCANEWKEYNEW')
        finally:
            os.removedirs(bundle_dir)
    # end test_cert_bundle_refresh
# end class TestVncCfgApiServer


class TestStaleLockRemoval(test_case.ApiServerTestCase):
    STALE_LOCK_SECS = '0.2'
    @classmethod
    def setUpClass(cls):
        super(TestStaleLockRemoval, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'stale_lock_seconds',
            cls.STALE_LOCK_SECS)])
    # end setUpClass

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
        uuid_cf = test_common.CassandraCFs.get_cf('config_db_uuid', 'obj_uuid_table')
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
    @classmethod
    def setUpClass(cls):
        super(TestVncCfgApiServerRequests, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'max_requests', 10)])

    def api_requests(self, orig_vn_read, count, vn_name):
        api_server = self._server_info['api_server']
        def slow_response_on_vn_read(obj_type, *args, **kwargs):
            if obj_type == 'virtual_network':
                while self.blocked:
                    gevent.sleep(1)
            return orig_vn_read(obj_type, *args, **kwargs)

        api_server._db_conn._cassandra_db.object_read = slow_response_on_vn_read

        logger.info("Creating a test VN object.")
        test_obj = self.create_virtual_network(vn_name, '1.1.1.0/24')
        logger.info("Making max_requests(%s) to api server" % (count - 1))
        def vn_read():
            self._vnc_lib.virtual_network_read(id=test_obj.uuid)
            gevent.sleep(0)

        self.blocked = True
        for i in range(count):
            gevent.spawn(vn_read)
        gevent.sleep(1)

    def test_max_api_requests(self):
        # Test to make sure api-server accepts requests within max_api_requests
        self.wait_till_api_server_idle()

        # when there are pipe-lined requests, responses have content-length
        # calculated only once. see _cast() in bottle.py for 'out' as bytes.
        # in this test, without resetting as below, read of def-nw-ipam
        # in create_vn will be the size returned for read_vn and
        # deserialization fails
        api_server = self._server_info['api_server']
        def reset_response_content_length():
            if 'Content-Length' in bottle.response:
                del bottle.response['Content-Length']
        api_server.api_bottle.add_hook('after_request', reset_response_content_length)

        orig_vn_read = api_server._db_conn._cassandra_db.object_read
        try:
            vn_name = self.id() + '5testvn1'
            self.api_requests(orig_vn_read, 5, vn_name)
            logger.info("Making one more requests well within the max_requests to api server")
            vn_name = self.id() + 'testvn1'
            try:
                greenlet = gevent.spawn(self.create_virtual_network, vn_name, '10.1.1.0/24')
                gevent.sleep(0)
                vn_obj = greenlet.get(timeout=3)
            except gevent.timeout.Timeout as e:
                self.assertFalse(greenlet.successful(), 'Request failed unexpectedly')
            else:
                self.assertEqual(vn_obj.name, vn_name)
        finally:
            api_server._db_conn._cassandra_db.object_read = orig_vn_read
            self.blocked = False

        # Test to make sure api-server rejects requests over max_api_requests
        self.wait_till_api_server_idle()
        api_server = self._server_info['api_server']
        orig_vn_read = api_server._db_conn._cassandra_db.object_read
        try:
            vn_name = self.id() + '11testvn2'
            self.api_requests(orig_vn_read, 11, vn_name)
            logger.info("Making one more requests (max_requests + 1) to api server")
            try:
                vn_name = self.id() + 'testvn2'
                greenlet = gevent.spawn(self.create_virtual_network, vn_name, '10.1.1.0/24')
                gevent.sleep(0)
                greenlet.get(timeout=3)
            except gevent.timeout.Timeout as e:
                logger.info("max_requests + 1 failed as expected.")
                self.assertFalse(False, greenlet.successful())
            else:
                self.assertTrue(False, 'Request succeeded unexpectedly')
        finally:
            api_server._db_conn._cassandra_db.object_read = orig_vn_read
            self.blocked = False

# end class TestVncCfgApiServerRequests


class TestLocalAuth(test_case.ApiServerTestCase):
    _rbac_role = 'admin'
    @classmethod
    def setUpClass(cls):
        from keystonemiddleware import auth_token
        class FakeAuthProtocol(object):
            _test_case = cls
            def __init__(self, app, *args, **kwargs):
                self._app = app
            # end __init__
            def __call__(self, env, start_response):
                # in multi-tenancy mode only admin role admitted
                # by api-server till full rbac support
                env['HTTP_X_ROLE'] = self._test_case._rbac_role
                return self._app(env, start_response)
            # end __call__
            def get_admin_token(self):
                return None
            # end get_admin_token
        # end class FakeAuthProtocol

        super(TestLocalAuth, cls).setUpClass(
            extra_config_knobs=[
                ('DEFAULTS', 'auth', 'keystone'),
                ('DEFAULTS', 'multi_tenancy', True),
                ('DEFAULTS', 'listen_ip_addr', '0.0.0.0'),
                ('KEYSTONE', 'admin_user', 'foo'),
                ('KEYSTONE', 'admin_password', 'bar'),],
            extra_mocks=[
                (auth_token, 'AuthProtocol', FakeAuthProtocol),
                ])
    # end setUpClass

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
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        # equivalent to curl -u foo:bar http://localhost:8095/documentation/index.html
        logger.info("Positive case")
        def fake_static_file(*args, **kwargs):
            return
        with test_common.patch(
            bottle, 'static_file', fake_static_file):
            url = 'http://%s:%s/documentation/index.html' %(listen_ip, listen_port)
            resp = requests.get(url)
            self.assertThat(resp.status_code, Equals(200))

        logger.info("Negative case without Documentation")
        url = 'http://%s:%s/virtual-networks' %(listen_ip, listen_port)
        orig_rbac_role = TestLocalAuth._rbac_role
        try:
            TestLocalAuth._rbac_role = 'foobar'
            resp = requests.get(url)
            self.assertThat(resp.status_code, Equals(403))
        finally:
            TestLocalAuth._rbac_role = orig_rbac_role

# end class TestLocalAuth

class TestExtensionApi(test_case.ApiServerTestCase):
    test_case = None
    class ResourceApiDriver(vnc_plugin_base.ResourceApi):
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
            if request.method == 'POST':
                obj_type = request.path[1:-1]
                if obj_type != 'virtual-network':
                    return
                obj_name = request.json[obj_type]['fq_name'][-1]
                if 'transform-create' in obj_name:
                    TestExtensionApi.test_case.assertIn('X_TEST_DUMMY', request.environ.keys())
                    TestExtensionApi.test_case.assertNotIn('SERVER_SOFTWARE', request.environ.keys())
                    TestExtensionApi.test_case.assertThat(request.environ['HTTP_X_CONTRAIL_USERAGENT'],
                                               Equals('bar'))
                    bottle.response.status = '234 Transformed Response'
                    response[obj_type]['extra_field'] = 'foo'
        # end transform_response
    # end class ResourceApiDriver

    @classmethod
    def setUpClass(cls):
        test_common.setup_extra_flexmock(
            [(stevedore.extension.ExtensionManager, '__new__',
              FakeExtensionManager)])
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resourceApi'] = \
            [TestExtensionApi.ResourceApiDriver]
        super(TestExtensionApi, cls).setUpClass(extra_mocks=[
            (stevedore.extension.ExtensionManager, '__new__',
              FakeExtensionManager)])

    # end setUpClass

    @classmethod
    def tearDownClass(cls):
        FakeExtensionManager._entry_pt_to_classes['vnc_cfg_api.resourceApi'] = \
            None
        FakeExtensionManager._ext_objs = []
        super(TestExtensionApi, cls).tearDownClass()
    # end tearDownClass

    def setUp(self):
        TestExtensionApi.test_case = self
        super(TestExtensionApi, self).setUp()
    # end setUp

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
        self.ignore_err_in_log = True
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


class TestPropertyWithList(test_case.ApiServerTestCase):
    def assert_kvpos(self, rd_ff_proto, idx, k, v, pos):
        self.assertEqual(rd_ff_proto[idx][0]['protocol'], k)
        self.assertEqual(rd_ff_proto[idx][0]['port'], v)
        self.assertEqual(rd_ff_proto[idx][1], pos)

    def test_set_in_object(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' %(self.id()),
            parent_obj=Project())
        vmi_obj.set_virtual_machine_interface_fat_flow_protocols(
            FatFlowProtocols([ProtocolType(protocol='p1', port=1),
                              ProtocolType(protocol='p2', port=2)]))
        # needed for backend type-specific handling
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        # ensure stored as list order
        rd_vmi_obj = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid)
        rd_ff_proto = rd_vmi_obj.virtual_machine_interface_fat_flow_protocols
        self.assertThat(
            rd_ff_proto.fat_flow_protocol[0].protocol, Equals('p1'))
        self.assertThat(
            rd_ff_proto.fat_flow_protocol[1].protocol, Equals('p2'))

        # verify db storage format (wrapper/container type stripped in storage)
        uuid_cf = test_common.CassandraCFs.get_cf('config_db_uuid', 'obj_uuid_table')
        cols = uuid_cf.get(vmi_obj.uuid,
            column_start='propl:virtual_machine_interface_fat_flow_protocols:',
            column_finish='propl:virtual_machine_interface_fat_flow_protocols;')
        col_name_0, col_val_0 = cols.popitem(last=False)
        col_name_1, col_val_1 = cols.popitem(last=False)
        self.assertThat(col_name_0.split(':')[-1], Equals('0'))
        self.assertThat(json.loads(col_val_0)['protocol'], Equals('p1'))
        self.assertThat(col_name_1.split(':')[-1], Equals('1'))
        self.assertThat(json.loads(col_val_1)['protocol'], Equals('p2'))

        # update and clobber old entries
        #vmi_obj.set_virtual_machine_interface_bindings([])
        vmi_obj.set_virtual_machine_interface_fat_flow_protocols(
            FatFlowProtocols())
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        rd_vmi_obj = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid)
        rd_ff_proto = rd_vmi_obj.virtual_machine_interface_fat_flow_protocols
        self.assertIsNone(rd_ff_proto)
        with ExpectedException(pycassa.NotFoundException) as e:
            cols = uuid_cf.get(vmi_obj.uuid,
                    column_start='propl:virtual_machine_interface_fat_flow_protocols:',
                    column_finish='propl:virtual_machine_interface_fat_flow_protocols;')
    # end test_set_in_object

    def test_add_del_in_object(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' %(self.id()),
            parent_obj=Project())

        for proto,port,pos in [('proto2', 2, 'pos1'), ('proto1', 1, 'pos2'),
                            ('proto3', 3, 'pos3'), ('proto4', 4, None)]:
            vmi_obj.add_virtual_machine_interface_fat_flow_protocols(
                ProtocolType(protocol=proto, port=port), pos)

        # needed for backend type-specific handling
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        rd_ff_proto = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid).virtual_machine_interface_fat_flow_protocols

        self.assertEqual(len(rd_ff_proto.fat_flow_protocol), 4)

        self.assertEqual(rd_ff_proto.fat_flow_protocol[0].protocol, 'proto4')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[0].port, 4)
        self.assertEqual(rd_ff_proto.fat_flow_protocol[1].protocol, 'proto2')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[1].port, 2)
        self.assertEqual(rd_ff_proto.fat_flow_protocol[2].protocol, 'proto1')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[2].port, 1)
        self.assertEqual(rd_ff_proto.fat_flow_protocol[3].protocol, 'proto3')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[3].port, 3)

        for pos in ['pos1', 'pos3']:
            vmi_obj.del_virtual_machine_interface_fat_flow_protocols(
                elem_position=pos)
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        rd_ff_proto = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid).virtual_machine_interface_fat_flow_protocols

        self.assertEqual(len(rd_ff_proto.fat_flow_protocol), 2)

        self.assertEqual(rd_ff_proto.fat_flow_protocol[0].protocol, 'proto4')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[0].port, 4)
        self.assertEqual(rd_ff_proto.fat_flow_protocol[1].protocol, 'proto1')
        self.assertEqual(rd_ff_proto.fat_flow_protocol[1].port, 1)
    # end test_add_del_in_object

    def test_prop_list_add_delete_get_element(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' %(self.id()),
            parent_obj=Project())
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        # 1. Add tests
        # add with element as type
        self._vnc_lib.prop_list_add_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols',
            ProtocolType('proto1', 1))

        # add with element as dict
        self._vnc_lib.prop_list_add_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols',
            {'protocol':'proto2', 'port':2})

        # verify above add without position specified generated uuid'd order
        rd_ff_proto = self._vnc_lib.prop_list_get(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols')
        self.assertEqual(len(rd_ff_proto), 2)

        # add with position specified
        self._vnc_lib.prop_list_add_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols',
            {'protocol':'proto3', 'port':3}, '0.1')
        self._vnc_lib.prop_list_add_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols',
            {'protocol':'proto4', 'port':4}, '0.0')
        self._vnc_lib.prop_list_add_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols',
            {'protocol':'proto5', 'port':5}, '.00')

        # 2. Get tests (specific and all elements)
        # get specific element
        rd_ff_proto = self._vnc_lib.prop_list_get(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols', '0.0')
        self.assertEqual(len(rd_ff_proto), 1)
        self.assert_kvpos(rd_ff_proto, 0, 'proto4', 4, '0.0')

        # get all elements
        rd_ff_proto = self._vnc_lib.prop_list_get(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols')

        self.assertEqual(len(rd_ff_proto), 5)

        self.assert_kvpos(rd_ff_proto, 0, 'proto5', 5, '.00')
        self.assert_kvpos(rd_ff_proto, 1, 'proto4', 4, '0.0')
        self.assert_kvpos(rd_ff_proto, 2, 'proto3', 3, '0.1')

        self.assertTrue(
            isinstance(uuid.UUID(rd_ff_proto[-1][1]), uuid.UUID),
            'Auto-generated position not of uuid form')

        # 3. Delete tests - middle and edges
        self._vnc_lib.prop_list_delete_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols', '0.1')
        self._vnc_lib.prop_list_delete_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols', '.00')
        self._vnc_lib.prop_list_delete_element(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols', rd_ff_proto[-1][1])

        rd_ff_proto = self._vnc_lib.prop_list_get(vmi_obj.uuid,
            'virtual_machine_interface_fat_flow_protocols')

        self.assertEqual(len(rd_ff_proto), 2)
        self.assert_kvpos(rd_ff_proto, 0, 'proto4', 4, '0.0')
        self.assertTrue(
            isinstance(uuid.UUID(rd_ff_proto[-1][1]), uuid.UUID),
            'Deleted incorrect element')
    # end test_prop_list_add_delete_get_element

    def test_set_in_resource_body_rest_api(self):
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port
        url = 'http://%s:%s/virtual-machine-interfaces' %(
            listen_ip, listen_port)
        vmi_body = {
            'virtual-machine-interface': {
                'fq_name': ['default-domain',
                            'default-project',
                            'vmi-%s' %(self.id())],
                'parent_type': 'project',
                'virtual_machine_interface_fat_flow_protocols': {
                    'fat_flow_protocol': [
                        {'protocol': 'proto1', 'port': 1},
                        {'protocol': 'proto1', 'port': 2},
                        {'protocol': 'proto2', 'port': 1},
                        {'protocol': 'proto2', 'port': 2},
                    ]
                },
                'virtual_network_refs': [
                    {'to': ['default-domain',
                            'default-project',
                            'default-virtual-network']}
                ]
            }
        }

        vmi_resp = requests.post(url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(vmi_body))
        vmi_uuid = json.loads(
            vmi_resp.content)['virtual-machine-interface']['uuid']
        vmi_url = 'http://%s:%s/virtual-machine-interface/%s' %(
            listen_ip, listen_port, vmi_uuid)

        vmi_read = json.loads(
            requests.get(vmi_url).content)['virtual-machine-interface']
        rd_ff_proto = vmi_read['virtual_machine_interface_fat_flow_protocols']
        self.assertEqual(len(rd_ff_proto['fat_flow_protocol']), 4)
        self.assertEqual(rd_ff_proto['fat_flow_protocol'][0]['protocol'], 'proto1')
        self.assertEqual(rd_ff_proto['fat_flow_protocol'][1]['protocol'], 'proto1')
        self.assertEqual(rd_ff_proto['fat_flow_protocol'][2]['protocol'], 'proto2')
        self.assertEqual(rd_ff_proto['fat_flow_protocol'][3]['protocol'], 'proto2')

        vmi_body = {
            'virtual-machine-interface': {
                'virtual_machine_interface_fat_flow_protocols': {
                    'fat_flow_protocol': [
                        {'protocol': 'proto3', 'port': 3}
                    ]
                }
            }
        }
        vmi_resp = requests.put(vmi_url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(vmi_body))

        vmi_read = json.loads(
            requests.get(vmi_url).content)['virtual-machine-interface']
        rd_ff_proto = vmi_read['virtual_machine_interface_fat_flow_protocols']
        self.assertEqual(len(rd_ff_proto['fat_flow_protocol']), 1)
        self.assertEqual(rd_ff_proto['fat_flow_protocol'][0]['protocol'], 'proto3')
    # end test_set_in_resource_body_rest_api

    def _rest_vmi_create(self):
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port
        url = 'http://%s:%s/virtual-machine-interfaces' %(
            listen_ip, listen_port)
        vmi_body = {
            'virtual-machine-interface': {
                'fq_name': ['default-domain',
                            'default-project',
                            'vmi-%s' %(self.id())],
                'parent_type': 'project',
                'virtual_network_refs': [
                    {'to': ['default-domain',
                            'default-project',
                            'default-virtual-network']}
                ]
            }
        }

        vmi_resp = requests.post(url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(vmi_body))
        vmi_uuid = json.loads(
            vmi_resp.content)['virtual-machine-interface']['uuid']

        return vmi_uuid
    # end _rest_vmi_create

    def test_prop_list_add_delete_get_rest_api(self):
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        vmi_uuid = self._rest_vmi_create()

        prop_coll_update_url = 'http://%s:%s/prop-collection-update' %(
            listen_ip, listen_port)
        prop_coll_get_url = 'http://%s:%s/prop-collection-get' %(
            listen_ip, listen_port)

        # 1. Add elements
        requests.post(prop_coll_update_url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(
                {'uuid': vmi_uuid,
                 'updates': [
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'add',
                      'value': {'protocol': 'proto1', 'port': 1} },
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'add',
                      'value': {'protocol': 'proto2', 'port': 2},
                      'position': '0.0'},
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'add',
                      'value': {'protocol': 'proto3', 'port': 3},
                      'position': '.01'} ] }))

        # 2. Get elements (all and specific)
        # get all elements
        query_params = {'uuid': vmi_uuid,
                        'fields': ','.join(
                                  ['virtual_machine_interface_fat_flow_protocols'])}
        rd_ff_proto = json.loads(requests.get(prop_coll_get_url,
            params=query_params).content)['virtual_machine_interface_fat_flow_protocols']

        self.assertEqual(len(rd_ff_proto), 3)
        self.assertEqual(rd_ff_proto[0][0]['protocol'], 'proto3')
        self.assertEqual(rd_ff_proto[0][0]['port'], 3)
        self.assertEqual(rd_ff_proto[0][1], '.01')

        self.assertEqual(rd_ff_proto[2][0]['protocol'], 'proto1')
        self.assertEqual(rd_ff_proto[2][0]['port'], 1)
        self.assertTrue(
            isinstance(uuid.UUID(rd_ff_proto[2][1]), uuid.UUID),
            'Autogenerated position not of uuid form')

        # get specific element
        query_params = {'uuid': vmi_uuid,
                        'fields': ','.join(
                                  ['virtual_machine_interface_fat_flow_protocols']),
                        'position': '.01'}
        rd_ff_proto = json.loads(requests.get(prop_coll_get_url,
            params=query_params).content)['virtual_machine_interface_fat_flow_protocols']
        self.assertEqual(len(rd_ff_proto), 1)
        self.assertEqual(rd_ff_proto[0][0]['protocol'], 'proto3')
        self.assertEqual(rd_ff_proto[0][0]['port'], 3)
        self.assertEqual(rd_ff_proto[0][1], '.01')

        # 3. Modify specific elements
        requests.post(prop_coll_update_url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(
                {'uuid': vmi_uuid,
                 'updates': [
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'modify',
                      'value': {'protocol': 'proto2', 'port': 21},
                      'position': '0.0'},
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'modify',
                      'value': {'protocol': 'proto3', 'port': 31},
                      'position': '.01'} ] }))

        query_params = {'uuid': vmi_uuid,
                        'fields': ','.join(
                                  ['virtual_machine_interface_fat_flow_protocols'])}
        rd_ff_proto = json.loads(requests.get(prop_coll_get_url,
            params=query_params).content)['virtual_machine_interface_fat_flow_protocols']

        self.assertEqual(len(rd_ff_proto), 3)
        self.assertEqual(rd_ff_proto[0][0]['protocol'], 'proto3')
        self.assertEqual(rd_ff_proto[0][0]['port'], 31)
        self.assertEqual(rd_ff_proto[0][1], '.01')

        self.assertEqual(rd_ff_proto[1][0]['protocol'], 'proto2')
        self.assertEqual(rd_ff_proto[1][0]['port'], 21)

        # 4. Delete (and add) elements
        requests.post(prop_coll_update_url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(
                {'uuid': vmi_uuid,
                 'updates': [
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'delete',
                      'position': '.01'},
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'delete',
                      'position': '0.0'},
                     {'field': 'virtual_machine_interface_fat_flow_protocols',
                      'operation': 'add',
                      'value': {'protocol': 'proto4', 'port': 4},
                      'position': '.01'} ] }))

        query_params = {'uuid': vmi_uuid,
                        'fields': ','.join(
                                  ['virtual_machine_interface_fat_flow_protocols'])}
        rd_ff_proto = json.loads(requests.get(prop_coll_get_url,
            params=query_params).content)['virtual_machine_interface_fat_flow_protocols']

        self.assertEqual(len(rd_ff_proto), 2)
        self.assertEqual(rd_ff_proto[0][0]['protocol'], 'proto4')
        self.assertEqual(rd_ff_proto[0][0]['port'], 4)
        self.assertEqual(rd_ff_proto[0][1], '.01')
        self.assertEqual(rd_ff_proto[1][0]['protocol'], 'proto1')
        self.assertEqual(rd_ff_proto[1][0]['port'], 1)
    # end test_prop_list_add_delete_get_rest_api

    def test_prop_list_wrong_type_should_fail(self):
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        vmi_uuid = self._rest_vmi_create()

        prop_coll_update_url = 'http://%s:%s/prop-collection-update' %(
            listen_ip, listen_port)
        prop_coll_get_url = 'http://%s:%s/prop-collection-get' %(
            listen_ip, listen_port)

        # 1. Try adding elements to non-prop-list field
        response = requests.post(prop_coll_update_url,
            headers={'Content-type': 'application/json; charset="UTF-8"'},
            data=json.dumps(
                {'uuid': vmi_uuid,
                 'updates': [
                     {'field': 'display_name',
                      'operation': 'add',
                      'value': {'key': 'k3', 'value': 'v3'},
                      'position': '.01'} ] }))
        self.assertEqual(response.status_code, 400)

        # 2. Try getting elements from non-prop-list field
        query_params = {'uuid': vmi_uuid,
                        'fields': ','.join(
                                  ['display_name'])}
        response = requests.get(prop_coll_get_url,
            params=query_params)
        self.assertEqual(response.status_code, 400)
    # end test_prop_list_wrong_type_should_fail

    def test_resource_list_with_field_prop_list(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' % (self.id()),
                                          parent_obj=Project())
        fname = 'virtual_machine_interface_fat_flow_protocols'
        # needed for backend type-specific handling
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        vmis = self._vnc_lib.virtual_machine_interfaces_list(
            obj_uuids=[vmi_obj.uuid], fields=[fname])
        vmi_ids = [vmi['uuid'] for vmi in vmis['virtual-machine-interfaces']]
        self.assertEqual([vmi_obj.uuid], vmi_ids)
        self.assertNotIn(fname, vmis['virtual-machine-interfaces'][0])

        vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_obj.uuid)
        proto_type = ProtocolType(protocol='proto', port='port')
        vmi_obj.add_virtual_machine_interface_fat_flow_protocols(proto_type,
                                                                 'pos')
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        vmis = self._vnc_lib.virtual_machine_interfaces_list(
            obj_uuids=[vmi_obj.uuid], fields=[fname])
        vmi_ids = [vmi['uuid'] for vmi in vmis['virtual-machine-interfaces']]
        self.assertEqual([vmi_obj.uuid], vmi_ids)
        self.assertIn(fname, vmis['virtual-machine-interfaces'][0])
        self.assertDictEqual({'fat_flow_protocol': [vars(proto_type)]},
                             vmis['virtual-machine-interfaces'][0][fname])
# end class TestPropertyWithlist


class TestPropertyWithMap(test_case.ApiServerTestCase):
    _excluded_vmi_bindings = ['vif_type', 'vnic_type']

    def assert_kvpos(self, rd_bindings, idx, k, v, pos):
        self.assertEqual(rd_bindings[idx][0]['key'], k)
        self.assertEqual(rd_bindings[idx][0]['value'], v)
        self.assertEqual(rd_bindings[idx][1], pos)

    def test_set_in_object(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' %(self.id()),
            parent_obj=Project())
        vmi_obj.set_virtual_machine_interface_bindings(
            KeyValuePairs([KeyValuePair(key='k1', value='v1'),
                           KeyValuePair(key='k2', value='v2')]))
        # needed for backend type-specific handling
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        # ensure stored as list order
        rd_bindings = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid).virtual_machine_interface_bindings
        bindings_dict = {binding.key: binding.value for binding in
                         rd_bindings.key_value_pair
                         if binding.key not in self._excluded_vmi_bindings}
        self.assertDictEqual(bindings_dict, {'k1': 'v1', 'k2': 'v2'})

        # verify db storage format (wrapper/container type stripped in storage)
        uuid_cf = test_common.CassandraCFs.get_cf('config_db_uuid','obj_uuid_table')
        cols = uuid_cf.get(vmi_obj.uuid,
            column_start='propm:virtual_machine_interface_bindings:',
            column_finish='propm:virtual_machine_interface_bindings;')
        col_name_0, col_val_0 = cols.popitem(last=False)
        col_name_1, col_val_1 = cols.popitem(last=False)
        self.assertThat(col_name_0.split(':')[-1], Equals('k1'))
        self.assertThat(json.loads(col_val_0)['key'], Equals('k1'))
        self.assertThat(col_name_1.split(':')[-1], Equals('k2'))
        self.assertThat(json.loads(col_val_1)['key'], Equals('k2'))

        # update and clobber old entries
        #vmi_obj.set_virtual_machine_interface_bindings([])
        vmi_obj.set_virtual_machine_interface_bindings(KeyValuePairs())
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        rd_vmi_obj = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid)
        rd_bindings = rd_vmi_obj.virtual_machine_interface_bindings
        self.assertIsNone(rd_bindings)
        with ExpectedException(pycassa.NotFoundException) as e:
            cols = uuid_cf.get(vmi_obj.uuid,
                    column_start='propm:virtual_machine_interface_bindings:',
                    column_finish='propm:virtual_machine_interface_bindings;')
    # end test_set_in_object

    def test_element_add_del_in_object(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' %(self.id()),
            parent_obj=Project())

        fake_bindings_dict = {'k1': 'v1',
                              'k2': 'v2',
                              'k3': 'v3',
                              'k4': 'v4'}
        for key, val in fake_bindings_dict.iteritems():
            vmi_obj.add_virtual_machine_interface_bindings(
                KeyValuePair(key=key, value=val))

        # needed for backend type-specific handling
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)
        rd_bindings = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid).virtual_machine_interface_bindings

        self.assertEqual(len(rd_bindings.key_value_pair), 4)

        bindings_dict = {binding.key: binding.value for binding in
                         rd_bindings.key_value_pair
                         if binding.key not in self._excluded_vmi_bindings}
        self.assertDictEqual(bindings_dict, fake_bindings_dict)

        for pos in ['k1', 'k4']:
            vmi_obj.del_virtual_machine_interface_bindings(elem_position=pos)
            fake_bindings_dict.pop(pos)
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)
        rd_bindings = self._vnc_lib.virtual_machine_interface_read(
            id=vmi_obj.uuid).virtual_machine_interface_bindings

        self.assertEqual(len(rd_bindings.key_value_pair), 2)

        bindings_dict = {binding.key: binding.value for binding in
                         rd_bindings.key_value_pair
                         if binding.key not in self._excluded_vmi_bindings}
        self.assertDictEqual(bindings_dict, fake_bindings_dict)
    # end test_element_set_del_in_object

    def test_resource_list_with_field_prop_map(self):
        vmi_obj = VirtualMachineInterface('vmi-%s' % (self.id()),
                                          parent_obj=Project())
        fname = 'virtual_machine_interface_bindings'
        # needed for backend type-specific handling
        vmi_obj.add_virtual_network(VirtualNetwork())
        self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        vmis = self._vnc_lib.virtual_machine_interfaces_list(
            obj_uuids=[vmi_obj.uuid], fields=[fname])
        vmi_ids = [vmi['uuid'] for vmi in vmis['virtual-machine-interfaces']]
        self.assertEqual([vmi_obj.uuid], vmi_ids)
        self.assertNotIn(fname, vmis['virtual-machine-interfaces'][0])

        vmi_obj = self._vnc_lib.virtual_machine_interface_read(id=vmi_obj.uuid)
        kv_pairs = KeyValuePairs([KeyValuePair(key='k', value='v')])
        vmi_obj.set_virtual_machine_interface_bindings(kv_pairs)
        self._vnc_lib.virtual_machine_interface_update(vmi_obj)

        vmis = self._vnc_lib.virtual_machine_interfaces_list(
            obj_uuids=[vmi_obj.uuid], fields=[fname])
        vmi_ids = [vmi['uuid'] for vmi in vmis['virtual-machine-interfaces']]
        self.assertEqual([vmi_obj.uuid], vmi_ids)
        self.assertIn(fname, vmis['virtual-machine-interfaces'][0])
        self.assertDictEqual(kv_pairs.exportDict()['KeyValuePairs'],
                             vmis['virtual-machine-interfaces'][0][fname])
# end class TestPropertyWithMap


class TestDBAudit(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestDBAudit, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    def setUp(self):
        super(TestDBAudit, self).setUp()
        self._args = '--ifmap-servers %s:%s' % (self._api_server_ip,
                                                self._api_ifmap_port)

    @contextlib.contextmanager
    def audit_mocks(self):
        def fake_ks_prop(*args, **kwargs):
            return {'strategy_options': {'replication_factor': 1}}

        with test_common.patch_imports(
            [('schema_transformer.db',
              flexmock(db=flexmock(
                  SchemaTransformerDB=flexmock(get_db_info=lambda: [('to_bgp_keyspace', ['route_target_table'])]))))]):
            with test_common.flexmocks([
                (pycassa.SystemManager, 'get_keyspace_properties',
                 fake_ks_prop)]):
                yield
    # end audit_mocks

    def _create_vn_subnet_ipam_iip(self, name):
        ipam_obj = vnc_api.NetworkIpam('vn-%s' % name)
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn_obj = vnc_api.VirtualNetwork(name)
        vn_obj.add_network_ipam(ipam_obj,
            VnSubnetsType(
                [IpamSubnetType(SubnetType('1.1.1.0', 28))]))
        self._vnc_lib.virtual_network_create(vn_obj)
        iip_obj = vnc_api.InstanceIp('iip-%s' % name)
        iip_obj.add_virtual_network(vn_obj)
        self._vnc_lib.instance_ip_create(iip_obj)

        return vn_obj, ipam_obj, iip_obj
    # end _create_vn_subnet_ipam_iip

    def _create_security_group(self, name):
        sg_obj = vnc_api.SecurityGroup(name)
        self._vnc_lib.security_group_create(sg_obj)
        return sg_obj

    def test_checker(self):
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            self.assertTill(self.ifmap_has_ident, obj=test_obj)

            with test_common.flexmocks(
                    [(db_manage.DatabaseChecker,
                      'check_fq_name_uuid_ifmap_match', lambda *args: [])]):
                db_manage.db_check(*db_manage._parse_args('check'))
    # end test_checker

    def test_checker_missing_mandatory_fields(self):
        # detect OBJ_UUID_TABLE entry missing required fields
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
            orig_col_val_ts = uuid_cf.get(test_obj.uuid,
                include_timestamp=True)
            omit_col_names = random.sample(set(
                ['type', 'fq_name', 'prop:id_perms']), 1)
            wrong_col_val_ts = dict((k,v) for k,v in orig_col_val_ts.items()
                if k not in omit_col_names)
            with uuid_cf.patch_row(
                test_obj.uuid, wrong_col_val_ts):
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check'))
                errors = db_checker.check_obj_mandatory_fields()
                self.assertIn(db_manage.MandatoryFieldsMissingError,
                    [type(x) for x in errors])
    # end test_checker_missing_mandatory_fields

    def test_checker_fq_name_mismatch_index_to_object(self):
        # detect OBJ_UUID_TABLE and OBJ_FQ_NAME_TABLE inconsistency
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            self.assertTill(self.ifmap_has_ident, obj=test_obj)

            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
            orig_col_val_ts = uuid_cf.get(test_obj.uuid,
                include_timestamp=True)
            wrong_col_val_ts = copy.deepcopy(orig_col_val_ts)
            wrong_col_val_ts['fq_name'] = (json.dumps(['wrong-fq-name']),
                wrong_col_val_ts['fq_name'][1])
            with uuid_cf.patch_row(
                test_obj.uuid, wrong_col_val_ts):
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check'))
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNMismatchError, error_types)
                self.assertIn(db_manage.FQNStaleIndexError, error_types)
                self.assertIn(db_manage.FQNIndexMissingError, error_types)
    # end test_checker_fq_name_mismatch_index_to_object

    def test_checker_fq_name_index_stale(self):
        # fq_name table in cassandra has entry but obj_uuid table doesn't
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            uuid_cf = self.get_cf('config_db_uuid','obj_uuid_table')
            with uuid_cf.patch_row(test_obj.uuid, new_columns=None):
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check'))
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNStaleIndexError, error_types)
    # test_checker_fq_name_mismatch_stale

    def test_checker_fq_name_index_missing(self):
        # obj_uuid table has entry but fq_name table in cassandra doesn't
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            self.assertTill(self.ifmap_has_ident, obj=test_obj)
            uuid_cf = self.get_cf('config_db_uuid','obj_uuid_table')
            fq_name_cf = self.get_cf('config_db_uuid','obj_fq_name_table')
            test_obj_type = test_obj.get_type().replace('-', '_')
            orig_col_val_ts = fq_name_cf.get(test_obj_type,
                include_timestamp=True)
            # remove test obj in fq-name table
            wrong_col_val_ts = dict((k,v) for k,v in orig_col_val_ts.items()
                if ':'.join(test_obj.fq_name) not in k)
            with fq_name_cf.patch_row(test_obj_type, new_columns=wrong_col_val_ts):
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check'))
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNIndexMissingError, error_types)
    # test_checker_fq_name_mismatch_missing

    def test_checker_ifmap_identifier_extra(self):
        # ifmap has identifier but obj_uuid table in cassandra doesn't
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            self.assertTill(self.ifmap_has_ident, obj=test_obj)

            uuid_cf = self.get_cf('config_db_uuid','obj_uuid_table')
            with uuid_cf.patch_row(test_obj.uuid, new_columns=None):
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check'))
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNStaleIndexError, error_types)
    # test_checker_ifmap_identifier_extra

    def test_checker_ifmap_identifier_missing(self):
        # ifmap doesn't have an identifier but obj_uuid table
        # in cassandra does
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            uuid_cf = self.get_cf('config_db_uuid','obj_uuid_table')
            with uuid_cf.patch_row(str(uuid.uuid4()),
                    new_columns={'type': json.dumps(''),
                                 'fq_name':json.dumps(''),
                                 'prop:id_perms':json.dumps('')}):
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check'))
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNIndexMissingError, error_types)
    # test_checker_ifmap_identifier_missing

    def test_checker_useragent_subnet_key_missing(self):
        pass # move to vnc_openstack test
    # test_checker_useragent_subnet_key_missing

    def test_checker_useragent_subnet_id_missing(self):
        pass # move to vnc_openstack test
    # test_checker_useragent_subnet_id_missing

    def test_checker_ipam_subnet_uuid_missing(self):
        pass # move to vnc_openstack test
    # test_checker_ipam_subnet_uuid_missing

    def test_checker_subnet_count_mismatch(self):
        pass # move to vnc_openstack test
    # test_checker_subnet_count_mismatch

    def test_checker_useragent_subnet_missing(self):
        pass # move to vnc_openstack test
    # test_checker_useragent_subnet_missing

    def test_checker_useragent_subnet_extra(self):
        pass # move to vnc_openstack test
    # test_checker_useragent_subnet_extra

    def test_checker_zk_vn_extra(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        fq_name_cf = self.get_cf('config_db_uuid','obj_fq_name_table')
        orig_col_val_ts = fq_name_cf.get('virtual_network',
            include_timestamp=True)
        # remove test obj in fq-name table
        wrong_col_val_ts = dict((k,v) for k,v in orig_col_val_ts.items()
            if ':'.join(vn_obj.fq_name) not in k)
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check'))
            # verify catch of extra ZK VN when name index is mocked
            with fq_name_cf.patch_row('virtual_network',
                new_columns=wrong_col_val_ts):
                errors = db_checker.check_subnet_addr_alloc()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNIndexMissingError, error_types)
    # test_checker_zk_vn_extra

    def test_checker_zk_vn_missing(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check'))

            with db_checker._zk_client.patch_path(
                '%s/%s' %(db_checker.BASE_SUBNET_ZK_PATH,
                          vn_obj.get_fq_name_str())):
                errors = db_checker.check_subnet_addr_alloc()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkVNMissingError, error_types)
                self.assertIn(db_manage.ZkSubnetMissingError, error_types)
    # test_checker_zk_vn_missing

    def test_checker_zk_ip_extra(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check'))

            # verify catch of zk extra ip when iip is mocked absent
            iip_obj = vnc_api.InstanceIp(self.id())
            iip_obj.add_virtual_network(vn_obj)
            self._vnc_lib.instance_ip_create(iip_obj)
            uuid_cf = self.get_cf('config_db_uuid','obj_uuid_table')
            with uuid_cf.patch_row(iip_obj.uuid, None):
                errors = db_checker.check_subnet_addr_alloc()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNStaleIndexError, error_types)
                self.assertIn(db_manage.ZkIpExtraError, error_types)
    # test_checker_zk_ip_extra

    def test_checker_zk_ip_missing(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check'))

            iip_obj = vnc_api.InstanceIp(self.id())
            iip_obj.add_virtual_network(vn_obj)
            self._vnc_lib.instance_ip_create(iip_obj)
            ip_addr = self._vnc_lib.instance_ip_read(
                id=iip_obj.uuid).instance_ip_address
            ip_str = "%(#)010d" % {'#': int(netaddr.IPAddress(ip_addr))}
            with db_checker._zk_client.patch_path(
                '%s/%s:1.1.1.0/28/%s' %(
                    db_checker.BASE_SUBNET_ZK_PATH,
                    vn_obj.get_fq_name_str(), ip_str)):
                errors = db_checker.check_subnet_addr_alloc()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkIpMissingError, error_types)
    # test_checker_zk_ip_missing

    def test_checker_zk_route_target_extra(self):
        pass # move to schema transformer test
    # test_checker_zk_route_target_extra

    def test_checker_zk_route_target_range_wrong(self):
        pass # move to schema transformer test
    # test_checker_zk_route_target_range_wrong

    def test_checker_cass_route_target_range_wrong(self):
        pass # move to schema transformer test
    # test_checker_cass_route_target_range_wrong

    def test_checker_route_target_count_mismatch(self):
        # include user assigned route-targets here
        pass # move to schema transformer test
    # test_checker_route_target_count_mismatch

    def test_checker_zk_virtual_network_id_extra_and_missing(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check'))
            with uuid_cf.patch_column(
                    vn_obj.uuid,
                    'prop:virtual_network_network_id',
                    json.dumps(42)):
                errors = db_checker.check_virtual_networks_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkVNIdExtraError, error_types)
                self.assertIn(db_manage.ZkVNIdMissingError, error_types)
    # test_checker_zk_virtual_network_id_extra_and_missing

    def test_checker_zk_virtual_network_id_duplicate(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        vn1_obj, _, _ = self._create_vn_subnet_ipam_iip('vn1-%s' % self.id())
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_obj.uuid)
        vn2_obj, _, _ = self._create_vn_subnet_ipam_iip('vn2-%s' % self.id())

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check'))
            with uuid_cf.patch_column(
                    vn2_obj.uuid,
                    'prop:virtual_network_network_id',
                    json.dumps(vn1_obj.virtual_network_network_id)):
                errors = db_checker.check_virtual_networks_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.VNDuplicateIdError, error_types)
                self.assertIn(db_manage.ZkVNIdExtraError, error_types)
    # test_checker_zk_virtual_network_id_duplicate

    def test_checker_zk_security_group_id_extra_and_missing(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        sg_obj = self._create_security_group(self.id())

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check'))
            with uuid_cf.patch_column(
                    sg_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(8000042)):
                errors = db_checker.check_security_groups_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkSGIdExtraError, error_types)
                self.assertIn(db_manage.ZkSGIdMissingError, error_types)
    # test_checker_zk_security_group_id_extra_and_missing

    def test_checker_zk_security_group_id_duplicate(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        sg1_obj = self._create_security_group('sg1-%s' % self.id())
        sg1_obj = self._vnc_lib.security_group_read(id=sg1_obj.uuid)
        sg2_obj = self._create_security_group('sg2-%s' % self.id())

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check'))
            with uuid_cf.patch_column(
                    sg2_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(sg1_obj.security_group_id)):
                errors = db_checker.check_security_groups_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.SGDuplicateIdError, error_types)
                self.assertIn(db_manage.ZkSGIdExtraError, error_types)
    # test_checker_zk_security_group_id_duplicate

    def test_checker_security_group_0_missing(self):
        pass # move to schema transformer test
    # test_checker_security_group_0_missing

    def test_cleaner(self):
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_manage.db_clean(*db_manage._parse_args('clean'))
    # end test_cleaner

    def test_cleaner_zk_virtual_network_id(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_cleaner = db_manage.DatabaseCleaner(
                *db_manage._parse_args('--execute clean'))
            fake_id = 42
            with uuid_cf.patch_column(
                    vn_obj.uuid,
                    'prop:virtual_network_network_id',
                    json.dumps(fake_id)):
                db_cleaner.clean_stale_virtual_network_id()
                zk_id_str = "%(#)010d" %\
                    {'#': vn_obj.virtual_network_network_id - 1}
                self.assertIsNone(
                    db_cleaner._zk_client.exists(
                        '%s/%s' % (db_cleaner.BASE_VN_ID_ZK_PATH, zk_id_str))
                )

    def test_healer_zk_virtual_network_id(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_cleaner = db_manage.DatabaseHealer(
                    *db_manage._parse_args('--execute heal'))
            fake_id = 42
            with uuid_cf.patch_column(
                    vn_obj.uuid,
                    'prop:virtual_network_network_id',
                    json.dumps(fake_id)):
                db_cleaner.heal_virtual_networks_id()
                zk_id_str = "%(#)010d" % {'#': fake_id - 1}
                self.assertEqual(
                    db_cleaner._zk_client.exists(
                        '%s/%s' % (
                             db_cleaner.BASE_VN_ID_ZK_PATH,
                             zk_id_str))[0],
                             vn_obj.get_fq_name_str())

    def test_cleaner_zk_security_group_id(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        sg_obj = self._create_security_group(self.id())
        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_cleaner = db_manage.DatabaseCleaner(
                *db_manage._parse_args('--execute clean'))
            with uuid_cf.patch_column(
                    sg_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(8000042)):
                db_cleaner.clean_stale_security_group_id()
                zk_id_str = "%(#)010d" % {'#': sg_obj.security_group_id}
                self.assertIsNone(
                    db_cleaner._zk_client.exists(
                        '%s/%s' % (db_cleaner.BASE_VN_ID_ZK_PATH, zk_id_str))
                )

    def test_healer_zk_security_group_id(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        sg_obj = self._create_security_group(self.id())
        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_cleaner = db_manage.DatabaseHealer(
                *db_manage._parse_args('--execute heal'))
            with uuid_cf.patch_column(
                    sg_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(8000042)):
                db_cleaner.heal_security_groups_id()
                zk_id_str = "%(#)010d" % {'#': 42}
                self.assertEqual(
                    db_cleaner._zk_client.exists(
                        '%s/%s' %
                        (db_cleaner.BASE_SG_ID_ZK_PATH, zk_id_str))[0],
                    sg_obj.get_fq_name_str())

    def test_clean_obj_missing_mandatory_fields(self):
        pass
    # end test_clean_obj_missing_mandatory_fields

    def test_clean_dangling_fq_names(self):
        pass
    # end test_clean_dangling_fq_names()

    def test_clean_dangling_back_refs(self):
        pass
    # end test_clean_dangling_back_refs()

    def test_clean_dangling_children(self):
        pass
    # end test_clean_dangling_children

    def test_healer(self):
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_manage.db_heal(*db_manage._parse_args('heal'))
    # end test_healer

    def test_heal_fq_name_index(self):
        pass
    # end test_heal_fq_name_index

    def test_heal_back_ref_index(self):
        pass
    # end test_heal_back_ref_index

    def test_heal_children_index(self):
        pass
    # end test_heal_children_index

    def test_heal_useragent_subnet_uuid(self):
        pass
    # end test_heal_useragent_subnet_uuid
# end class TestDBAudit


class TestBulk(test_case.ApiServerTestCase):
    def test_list_bulk_collection(self):
        obj_count = self._vnc_lib.POST_FOR_LIST_THRESHOLD + 1
        vn_uuids = []
        ri_uuids = []
        vmi_uuids = []
        logger.info("Creating %s VNs, RIs, VMIs.", obj_count)
        vn_objs, _, ri_objs, vmi_objs = self._create_vn_ri_vmi(obj_count)

        vn_uuids = [o.uuid for o in vn_objs]
        ri_uuids = [o.uuid for o in ri_objs]
        vmi_uuids = [o.uuid for o in vmi_objs]

        bulk_route = [r for r in self._api_server.api_bottle.routes
                        if r.rule == '/list-bulk-collection'][0]
        invoked_bulk = []
        def spy_list_bulk(orig_method, *args, **kwargs):
            invoked_bulk.append(True)
            return orig_method(*args, **kwargs)

        logger.info("Querying VNs by obj_uuids.")
        with test_common.patch(bulk_route, 'callback', spy_list_bulk):
            ret_list = self._vnc_lib.resource_list('virtual-network',
                                                   obj_uuids=vn_uuids)
            ret_uuids = [ret['uuid'] for ret in ret_list['virtual-networks']]
            self.assertThat(set(vn_uuids), Equals(set(ret_uuids)))
            self.assertEqual(len(invoked_bulk), 1)
            invoked_bulk.pop()

            logger.info("Querying RIs by parent_id.")
            ret_list = self._vnc_lib.resource_list('routing-instance',
                                                   parent_id=vn_uuids)
            ret_uuids = [ret['uuid']
                         for ret in ret_list['routing-instances']]
            self.assertThat(set(ri_uuids),
                Equals(set(ret_uuids) & set(ri_uuids)))
            self.assertEqual(len(invoked_bulk), 1)
            invoked_bulk.pop()

            logger.info("Querying VMIs by back_ref_id.")
            ret_list = self._vnc_lib.resource_list('virtual-machine-interface',
                                                   back_ref_id=vn_uuids)
            ret_uuids = [ret['uuid']
                         for ret in ret_list['virtual-machine-interfaces']]
            self.assertThat(set(vmi_uuids), Equals(set(ret_uuids)))
            self.assertEqual(len(invoked_bulk), 1)
            invoked_bulk.pop()

            logger.info("Querying VMIs by back_ref_id and extra fields.")
            ret_list = self._vnc_lib.resource_list('virtual-machine-interface',
                                                   back_ref_id=vn_uuids,
                                                   fields=['virtual_network_refs'])
            ret_uuids = [ret['uuid']
                         for ret in ret_list['virtual-machine-interfaces']]
            self.assertThat(set(vmi_uuids), Equals(set(ret_uuids)))
            self.assertEqual(set(vmi['virtual_network_refs'][0]['uuid']
                for vmi in ret_list['virtual-machine-interfaces']),
                set(vn_uuids))
            self.assertEqual(len(invoked_bulk), 1)
            invoked_bulk.pop()

            logger.info("Querying RIs by parent_id and filter.")
            ret_list = self._vnc_lib.resource_list('routing-instance',
                parent_id=vn_uuids,
                filters={'display_name':'%s-ri-5' %(self.id())})
            self.assertThat(len(ret_list['routing-instances']), Equals(1))
            self.assertEqual(len(invoked_bulk), 1)
            invoked_bulk.pop()

            logger.info("Querying VNs by obj_uuids for children+backref fields.")
            ret_objs = self._vnc_lib.resource_list('virtual-network',
                detail=True, obj_uuids=vn_uuids, fields=['routing_instances',
                'virtual_machine_interface_back_refs'])
            self.assertEqual(len(invoked_bulk), 1)
            invoked_bulk.pop()

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

        self.assertThat(set(ri_uuids),
            Equals(set(ret_ri_uuids) & set(ri_uuids)))
        self.assertThat(set(vmi_uuids), Equals(set(ret_vmi_uuids)))
    # end test_list_bulk_collection

    def test_list_bulk_collection_with_malformed_filters(self):
        obj_count = self._vnc_lib.POST_FOR_LIST_THRESHOLD + 1
        vn_objs, _, _, _ = self._create_vn_ri_vmi()
        vn_uuid = vn_objs[0].uuid
        vn_uuids = [vn_uuid] +\
                   ['bad-uuid'] * self._vnc_lib.POST_FOR_LIST_THRESHOLD

        try:
            results = self._vnc_lib.resource_list('virtual-network',
                                                  obj_uuids=vn_uuids)
            self.assertEqual(len(results['virtual-networks']), 1)
            self.assertEqual(results['virtual-networks'][0]['uuid'], vn_uuid)
        except HttpError:
            self.fail('Malformed object UUID filter was not ignored')

        try:
            results = self._vnc_lib.resource_list('routing-instance',
                                                  parent_id=vn_uuids,
                                                  detail=True)
            self.assertEqual(len(results), 2)
            for ri_obj in results:
                self.assertEqual(ri_obj.parent_uuid, vn_uuid)
        except HttpError:
            self.fail('Malformed parent UUID filter was not ignored')

        try:
            results = self._vnc_lib.resource_list('virtual-machine-interface',
                                                  back_ref_id=vn_uuids,
                                                  detail=True)
            self.assertEqual(len(results), 1)
            vmi_obj = results[0]
            self.assertEqual(vmi_obj.get_virtual_network_refs()[0]['uuid'],
                             vn_uuid)
        except HttpError:
            self.fail('Malformed back-ref UUID filter was not ignored')
# end class TestBulk


class TestCacheWithMetadata(test_case.ApiServerTestCase):
    def setUp(self):
        self.uuid_cf = test_common.CassandraCFs.get_cf(
            'config_db_uuid', 'obj_uuid_table')
        self.cache_mgr = self._api_server._db_conn._cassandra_db._obj_cache_mgr
        return super(TestCacheWithMetadata, self).setUp()
    # end setUp

    def create_test_object(self, name=None):
        vn_name = name or 'vn-%s' %(self.id())
        vn_obj = vnc_api.VirtualNetwork(vn_name)
        vn_obj.display_name = 'test-cache-obj'
        self._vnc_lib.virtual_network_create(vn_obj)
        return vn_obj
    # end create_object

    def prime_test_object(self, vn_obj):
        self._vnc_lib.virtual_networks_list(obj_uuids=[vn_obj.uuid])
        return vn_obj
    # end prime_test_object

    def create_and_prime_test_object(self, name=None):
        vn_name = name or 'vn-%s' %(self.id())
        return self.prime_test_object(self.create_test_object(vn_name))
    # end create_and_prime_test_object

    def test_hit_and_fresh(self):
        vn_obj = self.create_and_prime_test_object()

        uuid_cf = self.uuid_cf
        vn_row = uuid_cf.get(vn_obj.uuid, include_timestamp=True)
        with uuid_cf.patch_row(vn_obj.uuid,
            new_columns={'fq_name': vn_row['fq_name'],
                         'prop:id_perms': vn_row['prop:id_perms'],
                         'type': vn_row['type']}):
            ret_vn_objs = self._vnc_lib.virtual_networks_list(
                obj_uuids=[vn_obj.uuid], detail=True)
            self.assertEqual(ret_vn_objs[0].display_name, vn_obj.display_name)
    # end test_hit_and_fresh

    def test_hit_and_stale(self):
        vn_obj = self.create_and_prime_test_object()
        cache_mgr = self.cache_mgr
        self.assertIn(vn_obj.uuid, cache_mgr._cache.keys())

        uuid_cf = self.uuid_cf
        vn_row = uuid_cf.get(vn_obj.uuid)
        with uuid_cf.patches([
            ('column', (vn_obj.uuid, 'prop:display_name', 'stale-check-name')),
            ('column', (vn_obj.uuid, 'prop:id_perms', vn_row['prop:id_perms'])),
            ]):
            ret_vn_objs = self._vnc_lib.virtual_networks_list(
                obj_uuids=[vn_obj.uuid], detail=True)
            self.assertEqual(
                ret_vn_objs[0].display_name, 'stale-check-name')
    # end test_hit_and_stale

    def test_miss(self):
        vn_obj = self.create_test_object()
        cache_mgr = self.cache_mgr
        self.assertNotIn(vn_obj.uuid, cache_mgr._cache.keys())

        ret_vn_dicts = self._vnc_lib.virtual_networks_list(
            obj_uuids=[vn_obj.uuid],
            fields=['display_name'])['virtual-networks']
        self.assertEqual(ret_vn_dicts[0]['display_name'],
            vn_obj.display_name)
    # end test_miss

    def test_hits_stales_misses(self):
        uuid_cf = self.uuid_cf
        cache_mgr = self.cache_mgr

        vn_hit_fresh_obj = self.create_and_prime_test_object(
            'vn-hit-fresh-%s' %(self.id()))
        vn_hit_stale_obj = self.create_and_prime_test_object(
            'vn-hit-stale-%s' %(self.id()))
        vn_miss_obj = self.create_test_object('vn-miss-%s' %(self.id()))

        self.assertNotIn(vn_miss_obj.uuid, cache_mgr._cache.keys())
        vn_hit_stale_row = uuid_cf.get(vn_hit_stale_obj.uuid)
        with uuid_cf.patches([
            ('column', (vn_hit_fresh_obj.uuid,
                        'prop:display_name', 'fresh-check-name')),
            ('column', (vn_hit_stale_obj.uuid,
                        'prop:display_name', 'stale-check-name')),
            ('column', (vn_hit_stale_obj.uuid,
                        'prop:id_perms', vn_hit_stale_row['prop:id_perms'])),
            ]):
            vn_uuids = [vn_hit_fresh_obj.uuid, vn_hit_stale_obj.uuid,
                        vn_miss_obj.uuid]
            ret_vn_dicts = self._vnc_lib.virtual_networks_list(
                obj_uuids=vn_uuids,
                fields=['display_name'])['virtual-networks']
            self.assertEqual(len(ret_vn_dicts), 3)
            id_name_tuples = [(vn['uuid'], vn['display_name'])
                              for vn in ret_vn_dicts]
            self.assertIn(
                (vn_hit_fresh_obj.uuid, vn_hit_fresh_obj.display_name),
                id_name_tuples)
            self.assertIn((vn_hit_stale_obj.uuid, 'stale-check-name'),
                          id_name_tuples)
            self.assertIn((vn_miss_obj.uuid, vn_miss_obj.display_name),
                          id_name_tuples)
    # end test_hits_stales_misses

    def test_evict_on_ref_type_same(self):
        cache_mgr = self._api_server._db_conn._cassandra_db._obj_cache_mgr

        vn1_name = 'vn-1-%s' %(self.id())
        vn2_name = 'vn-2-%s' %(self.id())
        vn1_obj = self.create_test_object(vn1_name)
        vn2_obj = self.create_test_object(vn2_name)
        # prime RIs to cache
        ri1_obj = self._vnc_lib.routing_instance_read(
            fq_name=vn1_obj.fq_name+[vn1_name])
        ri2_obj = self._vnc_lib.routing_instance_read(
            fq_name=vn2_obj.fq_name+[vn2_name])

        self.assertIn(ri1_obj.uuid, cache_mgr._cache.keys())
        self.assertIn(ri2_obj.uuid, cache_mgr._cache.keys())

        ri1_obj.add_routing_instance(ri2_obj, None)
        self._vnc_lib.routing_instance_update(ri1_obj)
        self.assertNotIn(ri2_obj.uuid, cache_mgr._cache.keys())
    # end test_evict_on_ref_type_same

    def test_stale_for_backref_on_ref_update(self):
        uuid_cf = self.uuid_cf
        cache_mgr = self.cache_mgr

        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        ipam_obj = NetworkIpam('ipam-%s' %(self.id()),
                       display_name='ipam-name')
        self._vnc_lib.network_ipam_create(ipam_obj)
        self._vnc_lib.virtual_network_create(vn_obj)
        # prime ipam in cache
        self._vnc_lib.network_ipam_read(fq_name=ipam_obj.fq_name)
        self.assertIn(ipam_obj.uuid, cache_mgr._cache.keys())

        vn_obj.add_network_ipam(ipam_obj,
            VnSubnetsType(
                [IpamSubnetType(SubnetType('1.1.1.0', 28))]))
        self._vnc_lib.virtual_network_update(vn_obj)
        with uuid_cf.patches([
            ('column',
                 (ipam_obj.uuid, 'prop:display_name', 'stale-check-name'))]):
            # access for ipam without children/backref should hit cache
            ret_ipam_obj = self._vnc_lib.network_ipam_read(
                fq_name=ipam_obj.fq_name)
            self.assertEqual(ret_ipam_obj.display_name, ipam_obj.display_name)
            # access for ipam with backref should hit cache but stale
            ret_ipam_obj = self._vnc_lib.network_ipam_read(
                fq_name=ipam_obj.fq_name, fields=['virtual_network_back_refs'])
            self.assertEqual(ret_ipam_obj.display_name, 'stale-check-name')
    # end test_stale_for_backref_on_ref_update

    def test_read_for_delete_not_from_cache(self):
        uuid_cf = self.uuid_cf
        cache_mgr = self.cache_mgr

        ipam_obj = NetworkIpam('ipam-%s' %(self.id()),
                       display_name='ipam-name')
        self._vnc_lib.network_ipam_create(ipam_obj)
        # prime ipam in cache
        self._vnc_lib.network_ipam_read(fq_name=ipam_obj.fq_name)
        self.assertIn(ipam_obj.uuid, cache_mgr._cache.keys())

        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn_obj)
        with uuid_cf.patches([
            ('column', (ipam_obj.uuid,
                        'backref:virtual_network:%s' %(vn_obj.uuid),
                        json.dumps(None)))
            ]):
            with ExpectedException(RefsExistError,
                ".*Delete when resource still referred.*"):
                self._vnc_lib.network_ipam_delete(id=ipam_obj.uuid)
    # end test_read_for_delete_not_from_cache
# end class TestCacheWithMetadata


class TestCacheWithMetadataEviction(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls):
        return super(TestCacheWithMetadataEviction, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'object_cache_entries',
            '2')])
    # end setUpClass

    def test_evict_on_full(self):
        vn1_obj = vnc_api.VirtualNetwork('vn-1-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn1_obj)

        vn2_obj = vnc_api.VirtualNetwork('vn-2-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn2_obj)

        vn3_obj = vnc_api.VirtualNetwork('vn-3-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn3_obj)

        # prime with vn-1 and vn-2
        cache_mgr = self._api_server._db_conn._cassandra_db._obj_cache_mgr
        self._vnc_lib.virtual_network_read(id=vn1_obj.uuid)
        self._vnc_lib.virtual_network_read(id=vn2_obj.uuid)
        cache_keys = cache_mgr._cache.keys()
        self.assertIn(vn1_obj.uuid, cache_keys)
        self.assertIn(vn2_obj.uuid, cache_keys)
        self.assertNotIn(vn3_obj.uuid, cache_keys)

        # prime vn-3 and test eviction
        self._vnc_lib.virtual_network_read(id=vn3_obj.uuid)
        cache_keys = cache_mgr._cache.keys()
        self.assertIn(vn3_obj.uuid, cache_keys)
        if vn1_obj.uuid in cache_keys:
            self.assertNotIn(vn2_obj.uuid, cache_keys)
        elif vn2_obj.uuid in cache_keys:
            self.assertNotIn(vn1_obj.uuid, cache_keys)
        else:
            self.assertTrue(
                False, 'Eviction failed, all VNs present in cache')
    # end test_evict_on_full
# end class TestCacheWithMetadataEviction


class TestCacheWithMetadataExcludeTypes(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls):
        return super(TestCacheWithMetadataExcludeTypes, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'object_cache_exclude_types',
            'project, network-ipam')])
    # end setUpClass

    def test_exclude_types_not_cached(self):
        # verify not cached for configured types
        obj = vnc_api.Project('proj-%s' %(self.id()))
        self._vnc_lib.project_create(obj)
        self._vnc_lib.project_read(id=obj.uuid)
        cache_mgr = self._api_server._db_conn._cassandra_db._obj_cache_mgr
        self.assertNotIn(obj.uuid, cache_mgr._cache.keys())

        obj = vnc_api.NetworkIpam('ipam-%s' %(self.id()))
        self._vnc_lib.network_ipam_create(obj)
        self._vnc_lib.network_ipam_read(id=obj.uuid)
        cache_mgr = self._api_server._db_conn._cassandra_db._obj_cache_mgr
        self.assertNotIn(obj.uuid, cache_mgr._cache.keys())

        # verify cached for others
        obj = vnc_api.VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(obj)
        self._vnc_lib.virtual_network_read(id=obj.uuid)
        cache_mgr = self._api_server._db_conn._cassandra_db._obj_cache_mgr
        self.assertIn(obj.uuid, cache_mgr._cache.keys())
    # end test_exclude_types_not_cached
# end class TestCacheWithMetadataExcludeTypes

class TestRefValidation(test_case.ApiServerTestCase):
    def test_refs_validation_with_expected_error(self):
        obj = VirtualNetwork('validate-create-error')
        body_dict = {'virtual-network':
                        {'fq_name': obj.fq_name,
                        'parent_type': 'project',
                        'network_ipam_refs': [
                            {'attr':
                                {'host_routes': None,
                                'ipam_subnets': [{'addr_from_start': None,
                                                  'alloc_unit': 1,
                                                  'allocation_pools': [],
                                                  'default_gateway': None,
                                                  'dhcp_option_list': None,
                                                  'dns_nameservers': [],
                                                  'dns_server_address': None,
                                                  'enable_dhcp': True,
                                                  'host_routes': None,
                                                  'subnet': {'ip_prefix': '11.1.1.0',
                                                             'ip_prefix_len': 24},
                                                  'subnet_name': None,
                                                  'subnet_uuid': 12}]},
                            'to': ['default-domain',
                                   'default-project']}]}}
        status, content = self._http_post('/virtual-networks',
                              body=json.dumps(body_dict))
        self.assertThat(status, Equals(400))
        self.assertThat(content, Contains('Bad reference'))
    #end test_refs_validation_with_expected_error

    def test_refs_validation_with_expected_success(self):
        obj = VirtualNetwork('validate-create')
        body_dict = {'virtual-network':
                        {'fq_name': obj.fq_name,
                        'parent_type': 'project',
                        'network_ipam_refs': [
                            {'attr':
                                {'host_routes': None,
                                'ipam_subnets': [{'addr_from_start': None,
                                                  'alloc_unit': 1,
                                                  'allocation_pools': [],
                                                  'default_gateway': None,
                                                  'dhcp_option_list': None,
                                                  'dns_nameservers': [],
                                                  'dns_server_address': None,
                                                  'enable_dhcp': True,
                                                  'host_routes': None,
                                                  'subnet': None,
                                                  'subnet': {'ip_prefix': '10.1.1.0',
                                                             'ip_prefix_len': 24},
                                                  'subnet_name': None,
                                                  'subnet_uuid': None}]},
                            'to': ['default-domain',
                                   'default-project',
                                   'default-network-ipam']}]}}
        status, content = self._http_post('/virtual-networks',
                              body=json.dumps(body_dict))
        self.assertThat(status, Equals(200))
    #end test_refs_validation_with_expected_success
#end class TestRefValidation

class TestVncApiStats(test_case.ApiServerTestCase):
    from cfgm_common.vnc_api_stats import log_api_stats
    _sandesh = None
    @log_api_stats
    def _sample_function(self, obj_type):
        raise cfgm_common.exceptions.HttpError(409, '')

    def _check_sendwith(self, sandesh, stats, *args):
        self.assertEqual(stats.response_code, 409)
        self.assertEqual(stats.obj_type, 'abc')

    def test_response_code_on_exception(self):
        from cfgm_common.vnc_api_stats import VncApiStatistics
        try:
            with test_common.patch(VncApiStatistics, 'sendwith', self._check_sendwith):
                self._sample_function('abc')
        except cfgm_common.exceptions.HttpError:
            pass
        else:
            self.assertThat(0, 'Expecting HttpError to be raised, but was not raised')
    # end test_response_code_on_exception
# end TestVncApiStats

class TestDbJsonExim(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestDbJsonExim, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestDbJsonExim, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def test_db_exim_args(self):
        with ExpectedException(db_json_exim.InvalidArguments,
            'Both --import-from and --export-to cannot be specified'):
            db_json_exim.DatabaseExim("--import-from foo --export-to bar")
    # end test_db_exim_args

    def test_db_export(self):
        with tempfile.NamedTemporaryFile() as export_dump:
            patch_ks = test_common.FakeSystemManager.patch_keyspace
            with patch_ks('to_bgp_keyspace', {}), \
                 patch_ks('svc_monitor_keyspace', {}), \
                 patch_ks('dm_keyspace', {}), \
                 patch_ks('DISCOVERY_SERVER', {}):
                vn_obj = self._create_test_object()
                db_json_exim.DatabaseExim('--export-to %s' %(
                    export_dump.name)).db_export()
                dump = json.loads(export_dump.readlines()[0])
                dump_cassandra = dump['cassandra']
                dump_zk = json.loads(dump['zookeeper'])
                uuid_table = dump_cassandra['config_db_uuid']['obj_uuid_table']
                self.assertEqual(uuid_table[vn_obj.uuid]['fq_name'][0],
                    json.dumps(vn_obj.get_fq_name()))
                zk_node = [node for node in dump_zk
                    if node[0] == '/fq-name-to-uuid/virtual_network:%s/' %(
                        vn_obj.get_fq_name_str())]
                self.assertEqual(len(zk_node), 1)
                self.assertEqual(zk_node[0][1][0], vn_obj.uuid)
    # end test_db_export

    def test_db_export_and_import(self):
        with tempfile.NamedTemporaryFile() as dump_f:
            patch_ks = test_common.FakeSystemManager.patch_keyspace
            with patch_ks('to_bgp_keyspace', {}), \
                 patch_ks('svc_monitor_keyspace', {}), \
                 patch_ks('dm_keyspace', {}), \
                 patch_ks('DISCOVERY_SERVER', {}):
                vn_obj = self._create_test_object()
                db_json_exim.DatabaseExim('--export-to %s' %(
                    dump_f.name)).db_export()
                with ExpectedException(db_json_exim.CassandraNotEmptyError):
                    db_json_exim.DatabaseExim('--import-from %s' %(
                        dump_f.name)).db_import()

                uuid_cf = test_common.CassandraCFs.get_cf(
                    'config_db_uuid', 'obj_uuid_table')
                fq_name_cf = test_common.CassandraCFs.get_cf(
                    'config_db_uuid', 'obj_fq_name_table')
                shared_cf = test_common.CassandraCFs.get_cf(
                    'config_db_uuid', 'obj_shared_table')
                with uuid_cf.patch_cf({}), fq_name_cf.patch_cf({}), shared_cf.patch_cf({}):
                    with ExpectedException(
                         db_json_exim.ZookeeperNotEmptyError):
                        db_json_exim.DatabaseExim('--import-from %s' %(
                            dump_f.name)).db_import()

                exim_obj = db_json_exim.DatabaseExim('--import-from %s' %(
                               dump_f.name))
                with uuid_cf.patch_cf({}), fq_name_cf.patch_cf({}), \
                    exim_obj._zookeeper.patch_path(
                        '/', recursive=True):
                    exim_obj.db_import()
                    dump = json.loads(dump_f.readlines()[0])
                    dump_cassandra = dump['cassandra']
                    dump_zk = json.loads(dump['zookeeper'])
                    uuid_table = dump_cassandra['config_db_uuid']['obj_uuid_table']
                    self.assertEqual(uuid_table[vn_obj.uuid]['fq_name'][0],
                        json.dumps(vn_obj.get_fq_name()))
                    zk_node = [node for node in dump_zk
                        if node[0] == '/fq-name-to-uuid/virtual_network:%s/' %(
                            vn_obj.get_fq_name_str())]
                    self.assertEqual(len(zk_node), 1)
                self.assertEqual(zk_node[0][1][0], vn_obj.uuid)
    # end test_db_export_and_import
# end class TestDbJsonExim

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    # unittest.main(failfast=True)
    unittest.main()