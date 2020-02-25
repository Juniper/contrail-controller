from __future__ import print_function
from __future__ import absolute_import
from __future__ import division
#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from builtins import str
from builtins import range
from builtins import object
from past.utils import old_div
import gevent
import os
import sys
import socket
import errno
import uuid
import logging
import random
import netaddr
import mock
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
import requests
import bottle
import stevedore
import netaddr
import contextlib

from vnc_api.vnc_api import *
from cfgm_common import exceptions as vnc_exceptions
from netaddr import IPNetwork
import vnc_api.gen.vnc_api_test_gen
from vnc_api.gen.resource_test import *
import cfgm_common
from cfgm_common import vnc_plugin_base
from cfgm_common import vnc_cgitb
from cfgm_common import SGID_MIN_ALLOC
from cfgm_common import rest
from functools import reduce
vnc_cgitb.enable(format='text')

from cfgm_common.tests import test_common
from cfgm_common.tests.test_utils import FakeKazooClient
from cfgm_common.tests.test_utils import FakeKombu
from cfgm_common.tests.test_utils import FakeExtensionManager
from . import test_case
from vnc_cfg_api_server.resources import GlobalSystemConfigServer

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

class TestFixtures(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestFixtures, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestFixtures, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

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

class TestListUpdate(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestListUpdate, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestListUpdate, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

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
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestCrud, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestCrud, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def test_create_using_lib_api(self):
        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn_obj)
        self.assert_vnc_db_has_ident(vn_obj)
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
        self.assertTrue(reduce(lambda x, y: x and y, [p.name in tst_trgt for p in gsc.user_defined_log_statistics.statlist],  True))
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
        self.assertTrue(reduce(lambda x, y: x and y, [p.name in tst_trgt for p in gsc.user_defined_log_statistics.statlist],  True))
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

        vmi_name = self.id() + '-main_port'
        logger.info('Creating port %s', vmi_name)
        main_port_obj = VirtualMachineInterface(vmi_name, parent_obj=Project())
        main_port_obj.add_virtual_network(vn)
        self._vnc_lib.virtual_machine_interface_create(main_port_obj)

        id_perms = IdPermsType(enable=True)
        vmi_prop = VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=256)
        port_obj = VirtualMachineInterface(
                   str(uuid.uuid4()), parent_obj=Project(),
                   virtual_machine_interface_properties=vmi_prop,
                   id_perms=id_perms)
        port_obj.uuid = port_obj.name
        port_obj.set_virtual_network(vn)
        port_obj.set_virtual_machine_interface(main_port_obj)

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

    def test_physical_router_credentials(self):
        phy_rout_name = self.id() + '-phy-router-1'
        user_cred_create = UserCredentials(username="test_user", password="test_pswd")
        phy_rout = PhysicalRouter(phy_rout_name, physical_router_user_credentials=user_cred_create)
        phy_rout.uuid = '123e4567-e89b-12d3-a456-426655440000'
        self._vnc_lib.physical_router_create(phy_rout)

        phy_rout_obj = self._vnc_lib.physical_router_read(id=phy_rout.uuid)
        user_cred_read = phy_rout_obj.get_physical_router_user_credentials()
        self.assertIsNotNone(user_cred_read.password)
        self.assertEqual(user_cred_read.password, 'ngVv1S3pB+rM2SWMnm6XpQ==')
       # end test_physical_router_credentials

    def test_physical_router_w_no_user_credentials(self):
        phy_rout_name = self.id() + '-phy-router-2'
        phy_router = PhysicalRouter(phy_rout_name)
        self._vnc_lib.physical_router_create(phy_router)
        # reading Physical Router object when user credentials
        # are set to None should be successfull.
        phy_rout_obj = self._vnc_lib.physical_router_read(id=phy_router.uuid)

        phy_rout3_name = self.id() + '-phy-router-3'
        phy_router3 = PhysicalRouter(phy_rout3_name)
        self._vnc_lib.physical_router_create(phy_router3)
        phy_router3.set_physical_router_user_credentials(None)
        self._vnc_lib.physical_router_update(phy_router3)
        # reading Physical Router object when user credentials
        # are update to None should be successfull.
        phy_rout_obj = self._vnc_lib.physical_router_read(id=phy_router3.uuid)
        # end test_physical_router_w_no_user_credentials

    def test_bridge_domain_with_multiple_bd_in_vn(self):
        vn1_name = self.id() + '-vn-1'
        vn1 = VirtualNetwork(vn1_name)
        logger.info('Creating VN %s', vn1_name)
        self._vnc_lib.virtual_network_create(vn1)

        vmi_name = self.id() + '-port'
        logger.info('Creating port %s', vmi_name)
        vmi = VirtualMachineInterface(vmi_name, parent_obj=Project())
        vmi.add_virtual_network(vn1)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        bd1_name = self.id() + '-bd-1'
        bd1 = BridgeDomain(bd1_name, parent_obj=vn1)
        bd1.set_isid(200200)
        logger.info('Creating Bridge Domain %s', bd1_name)
        self._vnc_lib.bridge_domain_create(bd1)

        bd2_name = self.id() + '-bd-2'
        bd2 = BridgeDomain(bd2_name, parent_obj=vn1)
        bd2.set_isid(300300)
        logger.info('Creating Bridge Domain %s', bd2_name)
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.bridge_domain_create(bd2)
        # end test_bridge_domain_with_multiple_bd_in_vn

    def test_bridge_domain_link_vmi_and_bd_in_different_vn(self):
        vn1_name = self.id() + '-vn-1'
        vn1 = VirtualNetwork(vn1_name)
        logger.info('Creating VN %s', vn1_name)
        self._vnc_lib.virtual_network_create(vn1)

        vn2_name = self.id() + '-vn-2'
        vn2 = VirtualNetwork(vn2_name)
        logger.info('Creating VN %s', vn2_name)
        self._vnc_lib.virtual_network_create(vn2)

        vmi1_name = self.id() + '-port-1'
        logger.info('Creating port %s', vmi1_name)
        vmi1 = VirtualMachineInterface(vmi1_name, parent_obj=Project())
        vmi1.add_virtual_network(vn1)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        vmi2_name = self.id() + '-port-2'
        logger.info('Creating port %s', vmi2_name)
        vmi2 = VirtualMachineInterface(vmi2_name, parent_obj=Project())
        vmi2.add_virtual_network(vn2)
        self._vnc_lib.virtual_machine_interface_create(vmi2)

        bd1_name = self.id() + '-bd-1'
        bd1 = BridgeDomain(bd1_name, parent_obj=vn1)
        bd1.set_isid(200200)
        logger.info('Creating Bridge Domain %s', bd1_name)
        self._vnc_lib.bridge_domain_create(bd1)

        bd_ref_data1 = BridgeDomainMembershipType(vlan_tag=0)
        vmi2.add_bridge_domain(bd1, bd_ref_data1)
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.virtual_machine_interface_update(vmi2)

        bd_ref_data2 = BridgeDomainMembershipType(vlan_tag=0)
        vmi1.add_bridge_domain(bd1, bd_ref_data2)
        self._vnc_lib.virtual_machine_interface_update(vmi1)
        # end test_bridge_domain_link_vmi_and_bd_in_different_vn

    def test_bridge_domain_delete_vn_ref_with_bd_link(self):
        vn1_name = self.id() + '-vn-1'
        vn1 = VirtualNetwork(vn1_name)
        logger.info('Creating VN %s', vn1_name)
        self._vnc_lib.virtual_network_create(vn1)

        vmi_name = self.id() + '-port'
        logger.info('Creating port %s', vmi_name)
        vmi = VirtualMachineInterface(vmi_name, parent_obj=Project())
        vmi.add_virtual_network(vn1)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        bd1_name = self.id() + '-bd-1'
        bd1 = BridgeDomain(bd1_name, parent_obj=vn1)
        bd1.set_isid(200200)
        logger.info('Creating Bridge Domain %s', bd1_name)
        self._vnc_lib.bridge_domain_create(bd1)

        bd_ref_data = BridgeDomainMembershipType(vlan_tag=0)
        vmi.add_bridge_domain(bd1, bd_ref_data)
        self._vnc_lib.virtual_machine_interface_update(vmi)

        # Try to delete the VN link with BD ref
        vmi_temp = copy.deepcopy(vmi)
        vmi_temp.del_virtual_network(vn1)
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.virtual_machine_interface_update(vmi_temp)

        # Delete the BD ref
        vmi.del_bridge_domain(bd1)
        self._vnc_lib.virtual_machine_interface_update(vmi)

        vmi.del_virtual_network(vn1)
        self._vnc_lib.virtual_machine_interface_update(vmi)
        # end test_bridge_domain_with_multiple_bd_in_vn

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

    def test_sub_interfaces_with_same_vlan_tags(self):
        vn = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn)

        vmi_prop = VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=256)

        vmi_obj = VirtualMachineInterface(
                  str(uuid.uuid4()), parent_obj=Project())

        vmi_obj.uuid = vmi_obj.name
        vmi_obj.set_virtual_network(vn)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        sub_vmi_obj = VirtualMachineInterface(
                      str(uuid.uuid4()), parent_obj=Project(),
                      virtual_machine_interface_properties=vmi_prop)
        sub_vmi_obj.uuid = sub_vmi_obj.name
        sub_vmi_obj.set_virtual_network(vn)
        sub_vmi_obj.set_virtual_machine_interface(vmi_obj)
        sub_vmi_id = self._vnc_lib.virtual_machine_interface_create(sub_vmi_obj)

        sub_vmi_obj2 = VirtualMachineInterface(
                       str(uuid.uuid4()), parent_obj=Project(),
                       virtual_machine_interface_properties=vmi_prop)
        sub_vmi_obj2.uuid = sub_vmi_obj2.name
        sub_vmi_obj2.set_virtual_network(vn)
        sub_vmi_obj2.set_virtual_machine_interface(vmi_obj)

        # creating two sub interfacs with same vlan_tag
        # under same primary port should give an error
        with ExpectedException(BadRequest) as e:
            sub_vmi2_id = self._vnc_lib.virtual_machine_interface_create(sub_vmi_obj2)
    # end test_sub_interfaces_with_same_vlan_tags

    def test_create_sub_vmi_with_primary_vmi_as_another_sub_vmi(self):
        vn = VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn)

        vmi_obj = VirtualMachineInterface(
                  str(uuid.uuid4()), parent_obj=Project())

        vmi_obj.uuid = vmi_obj.name
        vmi_obj.set_virtual_network(vn)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        vmi_prop = VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=128)

        sub_vmi_obj = VirtualMachineInterface(
                      str(uuid.uuid4()), parent_obj=Project(),
                      virtual_machine_interface_properties=vmi_prop)
        sub_vmi_obj.uuid = sub_vmi_obj.name
        sub_vmi_obj.set_virtual_network(vn)
        sub_vmi_obj.set_virtual_machine_interface(vmi_obj)
        sub_vmi_id = self._vnc_lib.virtual_machine_interface_create(sub_vmi_obj)

        sub_vmi_obj2 = VirtualMachineInterface(
                       str(uuid.uuid4()), parent_obj=Project(),
                       virtual_machine_interface_properties=vmi_prop)
        sub_vmi_obj2.uuid = sub_vmi_obj2.name
        sub_vmi_obj2.set_virtual_network(vn)
        # set it's vmi ref (primary port) to another sub interface
        sub_vmi_obj2.set_virtual_machine_interface(sub_vmi_obj)

        # creating a sub interface with it's primary port as
        # another sub interface should give an error
        with ExpectedException(BadRequest) as e:
            sub_vmi2_id = self._vnc_lib.virtual_machine_interface_create(sub_vmi_obj2)
    # end test_create_sub_vmi_with_primary_vmi_as_another_sub_vmi

    def test_sub_interfaces_on_diff_vns_with_same_vlan_tags(self):
        vn1 = VirtualNetwork('vn1-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn1)
        vn2 = VirtualNetwork('vn2-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn2)

        vmi_prop = VirtualMachineInterfacePropertiesType(sub_interface_vlan_tag=256)

        vmi_obj = VirtualMachineInterface(
                  str(uuid.uuid4()), parent_obj=Project())

        vmi_obj2 = VirtualMachineInterface(
                  str(uuid.uuid4()), parent_obj=Project())

        vmi_obj.uuid = vmi_obj.name
        vmi_obj.set_virtual_network(vn1)
        vmi_id = self._vnc_lib.virtual_machine_interface_create(vmi_obj)

        vmi_obj2.uuid = vmi_obj2.name
        vmi_obj2.set_virtual_network(vn2)
        vmi_id2 = self._vnc_lib.virtual_machine_interface_create(vmi_obj2)

        sub_vmi_obj = VirtualMachineInterface(
                      str(uuid.uuid4()), parent_obj=Project(),
                      virtual_machine_interface_properties=vmi_prop)
        sub_vmi_obj.uuid = sub_vmi_obj.name
        sub_vmi_obj.set_virtual_network(vn1)
        sub_vmi_obj.set_virtual_machine_interface(vmi_obj)
        sub_vmi_id = self._vnc_lib.virtual_machine_interface_create(sub_vmi_obj)

        sub_vmi_obj2 = VirtualMachineInterface(
                       str(uuid.uuid4()), parent_obj=Project(),
                       virtual_machine_interface_properties=vmi_prop)
        sub_vmi_obj2.uuid = sub_vmi_obj2.name
        sub_vmi_obj2.set_virtual_network(vn2)
        sub_vmi_obj2.set_virtual_machine_interface(vmi_obj2)

        # creating two sub interfacs with same vlan_tag
        # on different VNs should get succedded
        sub_vmi2_id = self._vnc_lib.virtual_machine_interface_create(sub_vmi_obj2)
    # end test_sub_interfaces_on_diff_vns_with_same_vlan_tags

    def test_physical_router_credentials_list(self):
        phy_rout_name = self.id() + '-phy-router-1'
        phy_rout_name_2 = self.id() + '-phy-router-2'
        phy_rout_name_3 = self.id() + '-phy-router-3'
        phy_rout_name_4 = self.id() + '-phy-router-4'
        phy_rout_name_5 = self.id() + '-phy-router-5'
        user_cred_create = UserCredentials(username="test_user",
                                           password="test_pswd")
        user_cred_create_2 = UserCredentials(username="test_user_2",
                                             password="test_pswd_2")
        # Test the password that's more than 16 bytes
        user_cred_create_3 = UserCredentials(username="test_user_3",
            password="01234567890123456789")
        # Test the password that's more than 32 bytes
        user_cred_create_4 = UserCredentials(username="test_user_4",
            password="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
        # Test the password that is already encrypted
        user_cred_create_5 = UserCredentials(username="test_user_5",
            password="waldIpPkKKud0y0Z6AN4Tg8x7q5JOktwkVCPPRuIC2w=")

        phy_rout = PhysicalRouter(phy_rout_name,
                           physical_router_user_credentials=user_cred_create)
        phy_rout.uuid = '123e4567-e89b-12d3-a456-426655440001'
        self._vnc_lib.physical_router_create(phy_rout)

        phy_rout_2 = PhysicalRouter(phy_rout_name_2,
                           physical_router_user_credentials=user_cred_create_2)
        phy_rout_2.uuid = '123e4567-e89b-12d3-a456-426655440002'
        self._vnc_lib.physical_router_create(phy_rout_2)

        phy_rout_3 = PhysicalRouter(phy_rout_name_3,
                           physical_router_user_credentials=user_cred_create_3)
        phy_rout_3.uuid = '123e4567-e89b-12d3-a456-426655440003'
        self._vnc_lib.physical_router_create(phy_rout_3)

        phy_rout_4 = PhysicalRouter(phy_rout_name_4,
                           physical_router_user_credentials=user_cred_create_4)
        phy_rout_4.uuid = '123e4567-e89b-12d3-a456-426655440004'
        self._vnc_lib.physical_router_create(phy_rout_4)

        phy_rout_5 = PhysicalRouter(phy_rout_name_5,
                           physical_router_user_credentials=user_cred_create_5,
                                    physical_router_encryption_type='local')
        phy_rout_5.uuid = '123e4567-e89b-12d3-a456-426655440005'
        self._vnc_lib.physical_router_create(phy_rout_5)

        obj_uuids = []
        obj_uuids.append(phy_rout.uuid)
        obj_uuids.append(phy_rout_2.uuid)
        obj_uuids.append(phy_rout_3.uuid)
        obj_uuids.append(phy_rout_4.uuid)
        obj_uuids.append(phy_rout_5.uuid)

        phy_rtr_list = self._vnc_lib.physical_routers_list(obj_uuids=obj_uuids,
                                                           detail=True)
        for rtr in phy_rtr_list:
            user_cred_read = rtr.get_physical_router_user_credentials()
            if user_cred_read.username == 'test_user':
                self.assertEqual(user_cred_read.password, 'TtF53zhTfh1DQ66R2h5+Fg==')
            if user_cred_read.username == 'test_user_2':
                self.assertEqual(user_cred_read.password, '+sasYAEDEZd+Nn3X1ojFUw==')
            if user_cred_read.username == 'test_user_3':
                self.assertEqual(user_cred_read.password,
                    'waldIpPkKKud0y0Z6AN4Tg8x7q5JOktwkVCPPRuIC2w=')
            if user_cred_read.username == 'test_user_4':
                self.assertEqual(user_cred_read.password,
                    'd6jW0qMEBKSlUILBnetOdRIjTZGnK76OQ2R5jQgPxly0r+UNSfEqEh5DPqBL58td')
            if user_cred_read.username == 'test_user_5':
                self.assertEqual(user_cred_read.password,
                    'waldIpPkKKud0y0Z6AN4Tg8x7q5JOktwkVCPPRuIC2w=')
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
       for ip_address, prefix in list(ip_addresses.items()):
           ip_family = netaddr.IPNetwork(ip_address).version
           vmi = VirtualMachineInterface('vmi-%s-' % prefix +self.id(), parent_obj=proj)
           print('Validating with ip (%s) and prefix (%s)' % (ip_address, prefix))
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
                   print('ERROR: Prefix >= 24 should be accepted')
                   raise
               if ip_family == 6 and prefix >= 120:
                   print('ERROR: Prefix >= 120 should be accepted')
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

    def test_routing_policy_create_w_asn_of_cluster_asn_negative(self):
        rp_name = self.id() + 'rp1'
        gsc = self._vnc_lib.global_system_config_read(GlobalSystemConfig().fq_name)
        asn = gsc.autonomous_system
        rp_entry = PolicyStatementType(term=[PolicyTermType(
                                                   term_action_list=TermActionListType(
                                                       update=ActionUpdateType(
                                                           as_path=ActionAsPathType(
                                                               expand=AsListType(asn_list=[asn])))))])
        rp = RoutingPolicy(rp_name, routing_policy_entries=rp_entry)
        with ExpectedException(BadRequest):
            self._vnc_lib.routing_policy_create(rp)
        # end test_routing_policy_create_w_asn_of_cluster_asn_negative

# end class TestCrud

class TestVncCfgApiServer(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestVncCfgApiServer, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestVncCfgApiServer, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

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
            test_objs = self._create_test_objects(count=old_div(max_pend_upd,2)+1)

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
            api_server._db_conn._msgbus._conn_publish.connect  = orig_rabbitq_conn_publish

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

            self.assert_vnc_db_has_ident(obj)
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
                    rabbit_consumer.queue.put(payload, None)
                    raise exc_obj
                return msg

            with test_common.patch(rabbit_consumer.queue,
                'get', err_on_consume):
                # create the object to insert 'get' handler,
                # update oper will test the error handling
                self._vnc_lib.virtual_network_create(obj)
                obj.display_name = 'test_update'
                self._vnc_lib.virtual_network_update(obj)
                self.assertTill(lambda: consume_captured[0] == True)
            # unpatch err consume

            self.assertTill(self.vnc_db_ident_has_prop, obj=obj,
                            prop_name='display_name', prop_value='test_update')
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
                rabbit_consumer.queue.put(payload, None)
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
        with test_common.patch(rabbit_consumer.queue,
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

        self.assertTill(self.vnc_db_ident_has_prop, obj=obj,
                        prop_name='display_name', prop_value='test_update_2')
    # end test_reconnect_to_rabbit

    def test_update_implicit(self):
        self.ignore_err_in_log = True
        api_server = self._server_info['api_server']
        orig_rabbitq_pub = api_server._db_conn._msgbus._producer.publish
        try:
            update_implicit = {}

            def rabbitq_pub(*args, **kwargs):
                if args[0]['oper'] == 'UPDATE-IMPLICIT':
                    update_implicit.update(args[0])
                orig_rabbitq_pub(*args, **kwargs)

            logger.info("Creating VN objects")
            # every VN create, creates RI too
            vn_objs = self._create_test_objects(count=2)
            api_server._db_conn._msgbus._producer.publish = rabbitq_pub

            ri_objs = [self._vnc_lib.routing_instance_read(
                fq_name=vn.fq_name + [vn.name]) for vn in vn_objs]
            ri_objs[0].add_routing_instance(ri_objs[1], None)
            self._vnc_lib.routing_instance_update(ri_objs[0])

            for i in range(0, 10):
                gevent.sleep(0.1)
                if update_implicit.get('uuid') == ri_objs[1].uuid:
                    break
            else:
                self.assertTrue(False, 'update-implicit was not published')
        finally:
            api_server._db_conn._msgbus._producer.publish = orig_rabbitq_pub

    def test_handle_trap_on_exception(self):
        self.ignore_err_in_log = True
        api_server = self._server_info['api_server']
        orig_read = api_server._db_conn._object_db.object_read

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
            api_server._db_conn._object_db.object_read = exception_on_vn_read
            with ExpectedException(HttpError):
                self._vnc_lib.virtual_network_read(fq_name=test_obj.get_fq_name())
        finally:
            api_server._db_conn._object_db.object_read = orig_read

    def test_sandesh_trace(self):
        api_server = self._server_info['api_server']
        # the test
        test_obj = self._create_test_object()
        self.assert_vnc_db_has_ident(test_obj)
        self._vnc_lib.virtual_network_delete(id=test_obj.uuid)
        gevent.sleep(0.05)  # wait traces published

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
        ipam_fixt = self.useFixture(NetworkIpamTestFixtureGen(
            self._vnc_lib, network_ipam_name='ipam-%s' % self.id()))

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
        ipam_fixt = self.useFixture(NetworkIpamTestFixtureGen(
            self._vnc_lib, network_ipam_name='ipam-%s' % self.id()))

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

        # unanchored detailed list with filter with multiple values
        filtered_display_names = [
            '%s-%d' %(self.id(), num_objs - 1),
            '%s-%d' %(self.id(), num_objs - 2),
        ]
        read_vn_objs = self._vnc_lib.virtual_networks_list(
            detail=True,
            filters={'display_name': filtered_display_names})
        self.assertEqual(len(read_vn_objs), len(filtered_display_names))
        read_display_names = [o.display_name for o in read_vn_objs]
        self.assertEqual(set(read_display_names), set(filtered_display_names))

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

        # unanchored list with unknown filter
        read_vn_objs = self._vnc_lib.virtual_networks_list(
            parent_id=proj_obj.uuid,
            filters={'foo': 'bar'})['virtual-networks']
        self.assertEqual(len(read_vn_objs), num_objs)

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

    def test_list_with_id_parent_id_backref_id_and_filters(self):
        # Create 2 projects, one with 4 policies (3 with same name) other one
        # with one. One rule in first project but used by all policies in both
        # projects
        # ===========================|===========================
        #              P1            |             P2
        # ===========================|===========================
        #    FP1   FP2   FP3   FP4   |    FP1
        #             \     \   \    |   /
        #              \     \   \   |  /
        #               \_____\__ FR_|_/
        # FP1, FP2 and FP3 in P1 have the same diplay name

        p1 = Project('%s-p1' % self.id())
        self._vnc_lib.project_create(p1)
        p2 = Project('%s-p2' % self.id())
        self._vnc_lib.project_create(p2)

        p1_fr = FirewallRule(
            '%s-fr' % self.id(),
            parent_obj=p1,
            service=FirewallServiceType(),
        )
        self._vnc_lib.firewall_rule_create(p1_fr)

        p1_fp1_fp2_name = '%s-p1-fp1-fp2' % self.id()
        p1_fp1 = FirewallPolicy(
            '%s-p1-fp1' % self.id(),
            parent_obj=p1,
            display_name=p1_fp1_fp2_name)
        p1_fp2 = FirewallPolicy(
            '%s-p1-fp2' % self.id(),
            parent_obj=p1,
            display_name=p1_fp1_fp2_name)
        p1_fp2.add_firewall_rule(p1_fr)
        p1_fp3 = FirewallPolicy(
            '%s-p1-fp3' % self.id(),
            parent_obj=p1,
            display_name=p1_fp1_fp2_name)
        p1_fp3.add_firewall_rule(p1_fr)
        p1_fp4 = FirewallPolicy('%s-p1-fp4' % self.id(), parent_obj=p1)
        p1_fp4.add_firewall_rule(p1_fr)
        p2_fp1 = FirewallPolicy('%s-p2-fp1' % self.id(), parent_obj=p2)
        p2_fp1.add_firewall_rule(p1_fr)
        for fp in [p1_fp1, p1_fp2, p1_fp3, p1_fp4, p2_fp1]:
            self._vnc_lib.firewall_policy_create(fp)

        # list P1 and P2 policies
        list_result = self._vnc_lib.firewall_policys_list(
            parent_id=[p1.uuid, p2.uuid]
        )['firewall-policys']
        self.assertEquals(len(list_result), 5)
        self.assertEquals({r['uuid'] for r in list_result},
                          set([p1_fp1.uuid, p1_fp2.uuid, p1_fp3.uuid,
                               p1_fp4.uuid, p2_fp1.uuid]))

        # list P1 policies
        list_result = self._vnc_lib.firewall_policys_list(
            parent_id=p1.uuid,
        )['firewall-policys']
        self.assertEquals(len(list_result), 4)
        self.assertEquals({r['uuid'] for r in list_result},
                          set([p1_fp1.uuid, p1_fp2.uuid, p1_fp3.uuid,
                               p1_fp4.uuid]))

        # list P1 policies with a ref to FR
        list_result = self._vnc_lib.firewall_policys_list(
            parent_id=p1.uuid,
            back_ref_id=p1_fr.uuid,
        )['firewall-policys']
        self.assertEquals(len(list_result), 3)
        self.assertEquals({r['uuid'] for r in list_result},
                          set([p1_fp2.uuid, p1_fp3.uuid, p1_fp4.uuid]))

        # list P1 policies whit name 'p1_fp1_fp2_name' and with a ref to FR
        list_result = self._vnc_lib.firewall_policys_list(
            parent_id=p1.uuid,
            back_ref_id=p1_fr.uuid,
            filters={'display_name': p1_fp1_fp2_name},
        )['firewall-policys']
        self.assertEquals(len(list_result), 2)
        self.assertEquals({r['uuid'] for r in list_result},
                          set([p1_fp2.uuid, p1_fp3.uuid]))

        # list P1 policies whit name 'p1_fp1_fp2_name', with a ref to FR and
        # with UUID equals to FP1 UUID
        list_result = self._vnc_lib.firewall_policys_list(
            obj_uuids=[p1_fp2.uuid],
            parent_id=p1.uuid,
            back_ref_id=p1_fr.uuid,
            filters={'display_name': p1_fp1_fp2_name},
        )['firewall-policys']
        self.assertEquals(len(list_result), 1)
        self.assertEquals(list_result[0]['uuid'], p1_fp2.uuid)

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

    def test_list_with_malformed_filters(self):
        vn_objs, _, _, _ = self._create_vn_ri_vmi()
        vn_uuid = vn_objs[0].uuid
        vn_uuids = [vn_uuid, 'bad-uuid']

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

    def test_list_filtering_parent_fq_name(self):
        project = Project('project-%s' % self.id())
        self._vnc_lib.project_create(project)
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=project)
        self._vnc_lib.firewall_policy_create(fp)

        fps = self._vnc_lib.firewall_policys_list(
            parent_fq_name=project.fq_name)
        self.assertEqual(len(fps['%ss' % FirewallPolicy.resource_type]), 1)

    @mock.patch.object(GlobalSystemConfigServer, 'pre_dbe_create',
                       return_value=(True, ''))
    def test_list_filtering_parent_fq_name_multiple_parent_types_match(
            self, pre_dbe_create_mock):
        identical_name = 'gsc-and-domain-name-%s' % self.id()
        gsc = GlobalSystemConfig(identical_name)
        self._vnc_lib.global_system_config_create(gsc)
        domain = Domain(identical_name)
        self._vnc_lib.domain_create(domain)
        gsc_aal = ApiAccessList('gsc-aal-%s' % self.id(), parent_obj=gsc)
        self._vnc_lib.api_access_list_create(gsc_aal)
        domain_aal = ApiAccessList('domain-aal-%s' % self.id(), parent_obj=gsc)
        self._vnc_lib.api_access_list_create(domain_aal)

        aals = self._vnc_lib.api_access_lists_list(parent_fq_name=gsc.fq_name)
        self.assertEqual(len(aals['%ss' % ApiAccessList.resource_type]), 2)

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

        self.assertThat(list(ret_vn.keys()), Contains('routing_instances'))
        self.assertThat(list(ret_vn.keys()), Contains('virtual_machine_interface_back_refs'))
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

        self.assertThat(list(ret_vn.keys()), Not(Contains('routing_instances')))
        self.assertThat(list(ret_vn.keys()), Contains(
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

        self.assertThat(list(ret_vn.keys()), Contains('routing_instances'))
        self.assertThat(list(ret_vn.keys()), Not(Contains(
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

        self.assertThat(list(ret_vn.keys()), Not(Contains('routing_instances')))
        self.assertThat(list(ret_vn.keys()), Not(Contains(
            'virtual_machine_interface_back_refs')))

        # id_perms and perms2 are always returned irrespective of what
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
        self.assertThat(list(ret_vn.keys()), Contains(property))
        self.assertThat(list(ret_vn.keys()), Contains('id_perms'))
        self.assertThat(list(ret_vn.keys()), Contains('perms2'))
        self.assertThat(list(ret_vn.keys()), Not(Contains(reference)))
        self.assertThat(list(ret_vn.keys()), Not(Contains(children)))
        self.assertThat(list(ret_vn.keys()), Not(Contains(back_reference)))

        logger.info("Reading VN with one specific ref field.")
        query_param_str = 'fields=%s' % reference
        url = 'http://%s:%s/virtual-network/%s?%s' % (
            listen_ip, listen_port, vn_objs[0].uuid, query_param_str)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']
        self.assertThat(list(ret_vn.keys()), Not(Contains(property)))
        self.assertThat(list(ret_vn.keys()), Contains('id_perms'))
        self.assertThat(list(ret_vn.keys()), Contains('perms2'))
        self.assertThat(list(ret_vn.keys()), Contains(reference))
        self.assertThat(list(ret_vn.keys()), Not(Contains(children)))
        self.assertThat(list(ret_vn.keys()), Not(Contains(back_reference)))

        logger.info("Reading VN with one specific children field.")
        query_param_str = 'fields=%s' % children
        url = 'http://%s:%s/virtual-network/%s?%s' % (
            listen_ip, listen_port, vn_objs[0].uuid, query_param_str)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']
        self.assertThat(list(ret_vn.keys()), Not(Contains(property)))
        self.assertThat(list(ret_vn.keys()), Not(Contains(reference)))
        self.assertThat(list(ret_vn.keys()), Contains('id_perms'))
        self.assertThat(list(ret_vn.keys()), Contains('perms2'))
        self.assertThat(list(ret_vn.keys()), Contains(children))
        self.assertThat(list(ret_vn.keys()), Not(Contains(back_reference)))

        logger.info("Reading VN with one specific back-reference field.")
        query_param_str = 'fields=%s' % back_reference
        url = 'http://%s:%s/virtual-network/%s?%s' % (
            listen_ip, listen_port, vn_objs[0].uuid, query_param_str)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']
        self.assertThat(list(ret_vn.keys()), Not(Contains(property)))
        self.assertThat(list(ret_vn.keys()), Not(Contains(reference)))
        self.assertThat(list(ret_vn.keys()), Contains('id_perms'))
        self.assertThat(list(ret_vn.keys()), Contains('perms2'))
        self.assertThat(list(ret_vn.keys()), Not(Contains(children)))
        self.assertThat(list(ret_vn.keys()), Contains(back_reference))

        logger.info("Reading VN with property, reference, children and "
                    "back-reference fields.")
        query_param_str = ('fields=%s,%s,%s,%s' % (property, reference,
                                                   children, back_reference))
        url = 'http://%s:%s/virtual-network/%s?%s' % (
            listen_ip, listen_port, vn_objs[0].uuid, query_param_str)
        resp = requests.get(url)
        self.assertEqual(resp.status_code, 200)
        ret_vn = json.loads(resp.text)['virtual-network']
        self.assertThat(list(ret_vn.keys()), Contains('id_perms'))
        self.assertThat(list(ret_vn.keys()), Contains('perms2'))
        self.assertThat(list(ret_vn.keys()), Contains(property))
        self.assertThat(list(ret_vn.keys()), Contains(reference))
        self.assertThat(list(ret_vn.keys()), Contains(children))
        self.assertThat(list(ret_vn.keys()), Contains(back_reference))
    # end test_read_rest_api

    def test_bulk_read_rest_api_with_fqns(self):
        num_vn = 4
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        vn_objs, _, _, _ = self._create_vn_ri_vmi(num_vn)
        vn_fqns = [o.fq_name for o in vn_objs]
        vn_fqns_str_list = [':'.join(o.fq_name) for o in vn_objs]
        self.assertEqual(len(vn_fqns_str_list), num_vn)
        ret_list = self._vnc_lib.virtual_networks_list(fq_names=vn_fqns)
        ret_vns = ret_list['virtual-networks']
        ret_fqns_str_list = [':'.join(ret['fq_name']) for ret in ret_vns]
        self.assertEqual(len(ret_fqns_str_list), num_vn)
        self.assertEqual(vn_fqns_str_list.sort(), ret_fqns_str_list.sort())
    #end test_bulk_read_rest_api_with_fqns

    def test_bulk_read_rest_api_with_bad_fqns(self):
        num_vn = 2
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        vn_objs, _, _, _ = self._create_vn_ri_vmi(num_vn)
        vn_fqns = [o.fq_name for o in vn_objs]
        vn_fqns.append(['default-domain', 'default-project', 'bad-vn-fqn'])
        vn_fqns_str_list = [':'.join(o.fq_name) for o in vn_objs]
        self.assertEqual(len(vn_fqns_str_list), num_vn)
        ret_list = self._vnc_lib.resource_list('virtual-network',
                                               fq_names=vn_fqns)
        ret_vns = ret_list['virtual-networks']
        ret_fqns_str_list = [':'.join(ret['fq_name']) for ret in ret_vns]
        self.assertEqual(len(ret_fqns_str_list), num_vn)
        self.assertEqual(vn_fqns_str_list.sort(), ret_fqns_str_list.sort())
    #end test_bulk_read_rest_api_with_bad_fqns

    def test_bulk_read_rest_api_with_fqns_objs(self):
        num_vn = 4
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        vn_objs, _, _, _ = self._create_vn_ri_vmi(num_vn)
        vn_fqns = [o.fq_name for o in vn_objs]
        vn_fqns_str_list = [':'.join(o.fq_name) for o in vn_objs]
        vn_uuids_list = [o.uuid for o in vn_objs]
        self.assertEqual(len(vn_fqns_str_list), num_vn)
        self.assertEqual(len(vn_uuids_list), num_vn)
        # We are adding 1st two in fq_names and last two in obj_uuids
        ret_list = self._vnc_lib.resource_list('virtual-network',
                                               fq_names=vn_fqns[0:2],
                                               obj_uuids=vn_uuids_list[2:])
        ret_vns = ret_list['virtual-networks']
        ret_fqns_str_list = [':'.join(ret['fq_name']) for ret in ret_vns]
        ret_uuids_str_list = [ret['uuid'] for ret in ret_vns]
        self.assertEqual(len(ret_fqns_str_list), num_vn)
        self.assertEqual(ret_fqns_str_list.sort(), vn_fqns_str_list.sort())
        self.assertEqual(ret_uuids_str_list.sort(), vn_uuids_list.sort())
    #end test_bulk_read_rest_api_with_fqns_objs

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
                    Contains('Faking db delete for instance ip'))
                # assert reservation present in zookeeper and value in iip
                zk_node = "%(#)010d" % {'#': int(netaddr.IPAddress(
                    iip_obj.instance_ip_address))}
                zk_path = '%s/api-server/subnets/%s:1.1.1.0/28/%s' %(
                    self._cluster_id, vn_obj.get_fq_name_str(), zk_node)
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
                    Contains('Faking db delete for floating ip'))
                # assert reservation present in zookeeper and value in iip
                zk_node = "%(#)010d" % {'#': int(netaddr.IPAddress(
                    fip_obj.floating_ip_address))}
                zk_path = '%s/api-server/subnets/%s:1.1.1.0/28/%s' %(
                    self._cluster_id, vn_obj.get_fq_name_str(), zk_node)
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
                    Contains('Faking db delete for alias ip'))
                # assert reservation present in zookeeper and value in iip
                zk_node = "%(#)010d" % {'#': int(netaddr.IPAddress(
                    aip_obj.alias_ip_address))}
                zk_path = '%s/api-server/subnets/%s:1.1.1.0/28/%s' %(
                    self._cluster_id, vn_obj.get_fq_name_str(), zk_node)
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
        self.assert_vnc_db_has_ident(test_obj)
        db_client = self._api_server._db_conn

        uve_delete_trace_invoked = []
        uuid_to_fq_name_on_delete_invoked = []
        def spy_uve_trace(orig_method, *args, **kwargs):
            oper = kwargs['oper'].upper()
            obj_uuid = kwargs['uuid']
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
        with test_common.patch(db_client, 'dbe_uve_trace', spy_uve_trace):
            self._delete_test_object(test_obj)
            gevent.sleep(0.5)
            self.assert_vnc_db_doesnt_have_ident(test_obj)
            self.assertEqual(len(uve_delete_trace_invoked), 1,
                'uve_trace not invoked on object delete')
            self.assertEqual(len(uuid_to_fq_name_on_delete_invoked), 0,
                'uuid_to_fq_name invoked in delete at dbe_uve_trace')
    # end test_uve_trace_delete_name_from_msg

    def test_ref_update_with_existing_ref(self):
        ipam_obj = NetworkIpam('ipam-%s' % self.id())
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn_obj = VirtualNetwork('vn-%s' % self.id())
        vn_obj.add_network_ipam(ipam_obj,
            VnSubnetsType(
                [IpamSubnetType(SubnetType('1.1.1.0', 28))]))
        self._vnc_lib.virtual_network_create(vn_obj)

        self._vnc_lib.ref_update('virtual-network',
                                 vn_obj.uuid,
                                 'network-ipam',
                                 ipam_obj.uuid,
                                 ipam_obj.fq_name,
                                 'ADD',
                                 VnSubnetsType([
                                     IpamSubnetType(SubnetType('1.1.1.0', 28)),
                                     IpamSubnetType(SubnetType('2.2.2.0', 28)),
                                 ]))

        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        self.assertEqual(len(vn_obj.network_ipam_refs), 1)
        ipam_subnets = vn_obj.network_ipam_refs[0]['attr'].ipam_subnets
        self.assertEqual(len(ipam_subnets), 2)
        self.assertEqual(ipam_subnets[0].subnet.ip_prefix, '1.1.1.0')
        self.assertEqual(ipam_subnets[1].subnet.ip_prefix, '2.2.2.0')
    # end test_ref_update_with_existing_ref

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

    def test_name_attribute_in_detail_list_resource(self):
        vn_obj = vnc_api.VirtualNetwork('%s-vn' % self.id())
        self._vnc_lib.virtual_network_create(vn_obj)
        query_params = {
            'obj_uuids': vn_obj.uuid,
            'detail': True,
        }
        results = self._vnc_lib._request_server(
            rest.OP_GET,
            '/virtual-networks',
            data=query_params)['virtual-networks']
        self.assertEqual(len(results), 1)
        vn_dict = results[0]['virtual-network']
        self.assertIn('name', vn_dict)
        self.assertEqual(vn_dict['name'], vn_obj.fq_name[-1])

    def test_bgpvpn_type_assoc_with_network_l2_l3_forwarding_mode(self):
        # Create virtual network with forwarding mode set to 'l2' and 'l3'
        vn_l2_l3 = self.create_virtual_network('vn-l2-l3-%s' % self.id())
        # Create l2 bgpvpn
        bgpvpn_l2 = Bgpvpn('bgpvpn-l2-%s' % self.id(), bgpvpn_type='l2')
        self._vnc_lib.bgpvpn_create(bgpvpn_l2)
        # Create l3 bgpvpn
        bgpvpn_l3 = Bgpvpn('bgpvpn-l3-%s' % self.id())
        self._vnc_lib.bgpvpn_create(bgpvpn_l3)

        # Trying to associate a 'l2' bgpvpn on the virtual network
        vn_l2_l3.add_bgpvpn(bgpvpn_l2)
        self._vnc_lib.virtual_network_update(vn_l2_l3)
        vn_l2_l3 = self._vnc_lib.virtual_network_read(id=vn_l2_l3.uuid)

        # Trying to associate a 'l3' bgpvpn on the virtual network
        vn_l2_l3.add_bgpvpn(bgpvpn_l3)
        self._vnc_lib.virtual_network_update(vn_l2_l3)
        vn_l2_l3 = self._vnc_lib.virtual_network_read(id=vn_l2_l3.uuid)

        # Try to change the virtual network forwarding mode to 'l2' only
        with ExpectedException(BadRequest):
            vn_l2_l3.set_virtual_network_properties(
                VirtualNetworkType(forwarding_mode='l2'))
            self._vnc_lib.virtual_network_update(vn_l2_l3)
        vn_l2_l3 = self._vnc_lib.virtual_network_read(id=vn_l2_l3.uuid)

        # Try to change the virtual network forwarding mode to 'l3' only
        with ExpectedException(BadRequest):
            vn_l2_l3.set_virtual_network_properties(
                VirtualNetworkType(forwarding_mode='l3'))
            self._vnc_lib.virtual_network_update(vn_l2_l3)

    def test_bgpvpn_type_assoc_with_network_l2_forwarding_mode(self):
        # Create virtual network with forwarding mode set to 'l2' only
        vn_l2 = self.create_virtual_network('vn-l2-%s' % self.id())
        vn_l2.set_virtual_network_properties(
            VirtualNetworkType(forwarding_mode='l2'))
        self._vnc_lib.virtual_network_update(vn_l2)
        vn_l2 = self._vnc_lib.virtual_network_read(id=vn_l2.uuid)
        # Create l2 bgpvpn
        bgpvpn_l2 = Bgpvpn('bgpvpn-l2-%s' % self.id(), bgpvpn_type='l2')
        self._vnc_lib.bgpvpn_create(bgpvpn_l2)
        # Create l3 bgpvpn
        bgpvpn_l3 = Bgpvpn('bgpvpn-l3-%s' % self.id())
        self._vnc_lib.bgpvpn_create(bgpvpn_l3)

        # Trying to associate a 'l2' bgpvpn on the virtual network
        vn_l2.add_bgpvpn(bgpvpn_l2)
        self._vnc_lib.virtual_network_update(vn_l2)
        vn_l2 = self._vnc_lib.virtual_network_read(id=vn_l2.uuid)

        # Trying to associate a 'l3' bgpvpn on the virtual network
        with ExpectedException(BadRequest):
            vn_l2.add_bgpvpn(bgpvpn_l3)
            self._vnc_lib.virtual_network_update(vn_l2)
        vn_l2 = self._vnc_lib.virtual_network_read(id=vn_l2.uuid)

        # Try to change the virtual network forwarding mode to 'l3' only
        with ExpectedException(BadRequest):
            vn_l2.set_virtual_network_properties(
                VirtualNetworkType(forwarding_mode='l3'))
            self._vnc_lib.virtual_network_update(vn_l2)
        vn_l2 = self._vnc_lib.virtual_network_read(id=vn_l2.uuid)

        # Try to change the virtual network forwarding mode to 'l2' and l3'
        vn_l2.set_virtual_network_properties(
            VirtualNetworkType(forwarding_mode='l2_l3'))
        self._vnc_lib.virtual_network_update(vn_l2)

    def test_bgpvpn_type_assoc_with_network_l3_forwarding_mode(self):
        # Create virtual network with forwarding mode set to 'l3' only
        vn_l3 = self.create_virtual_network('vn-l3-%s' % self.id())
        vn_l3.set_virtual_network_properties(
            VirtualNetworkType(forwarding_mode='l3'))
        self._vnc_lib.virtual_network_update(vn_l3)
        vn_l3 = self._vnc_lib.virtual_network_read(id=vn_l3.uuid)
        # Create l2 bgpvpn
        bgpvpn_l2 = Bgpvpn('bgpvpn-l2-%s' % self.id(), bgpvpn_type='l2')
        self._vnc_lib.bgpvpn_create(bgpvpn_l2)
        # Create l3 bgpvpn
        bgpvpn_l3 = Bgpvpn('bgpvpn-l3-%s' % self.id())
        self._vnc_lib.bgpvpn_create(bgpvpn_l3)

        # Trying to associate a 'l3' bgpvpn on the virtual network
        vn_l3.add_bgpvpn(bgpvpn_l3)
        self._vnc_lib.virtual_network_update(vn_l3)
        vn_l3 = self._vnc_lib.virtual_network_read(id=vn_l3.uuid)

        # Trying to associate a 'l2' bgpvpn on the virtual network
        with ExpectedException(BadRequest):
            vn_l3.add_bgpvpn(bgpvpn_l2)
            self._vnc_lib.virtual_network_update(vn_l3)
        vn_l3 = self._vnc_lib.virtual_network_read(id=vn_l3.uuid)

        # Try to change the virtual network forwarding mode to 'l2' only
        with ExpectedException(BadRequest):
            vn_l3.set_virtual_network_properties(
                VirtualNetworkType(forwarding_mode='l2'))
            self._vnc_lib.virtual_network_update(vn_l3)
        vn_l3 = self._vnc_lib.virtual_network_read(id=vn_l3.uuid)

        # Try to change the virtual network forwarding mode to 'l2' and l3'
        vn_l3.set_virtual_network_properties(
            VirtualNetworkType(forwarding_mode='l2_l3'))
        self._vnc_lib.virtual_network_update(vn_l3)

    def test_bgpvpn_type_limited_to_l3_for_router_assoc(self):
        # Create logical router
        lr, _, _, _ = self.create_logical_router(
            'lr-%s' % self.id(), nb_of_attached_networks=0)
        # Create l2 bgpvpn
        bgpvpn_l2 = Bgpvpn('bgpvpn-l2-%s' % self.id(), bgpvpn_type='l2')
        self._vnc_lib.bgpvpn_create(bgpvpn_l2)

        # Trying to associate a 'l2' bgpvpn on the logical router
        with ExpectedException(BadRequest):
            lr.add_bgpvpn(bgpvpn_l2)
            self._vnc_lib.logical_router_update(lr)

    def test_bgpvpn_fail_assoc_network_with_gw_router_assoc_to_bgpvpn(self):
        # Create one bgpvpn
        bgpvpn = Bgpvpn('bgpvpn-%s' % self.id())
        self._vnc_lib.bgpvpn_create(bgpvpn)
        # Create one virtual network with one logical router as gateway
        lr, vns, _, _ = self.create_logical_router('lr-%s' % self.id())
        # We attached only one virtual network to the logical router
        vn = vns[0]

        # Associate the bgppvpn to the logical router
        lr.add_bgpvpn(bgpvpn)
        self._vnc_lib.logical_router_update(lr)
        lr = self._vnc_lib.logical_router_read(id=lr.uuid)

        # The try to set that same bgpvpn to the virtual network
        with ExpectedException(BadRequest):
            vn.add_bgpvpn(bgpvpn)
            self._vnc_lib.virtual_network_update(vn)

    def test_bgpvpn_fail_assoc_router_with_network_assoc_to_bgpvpn(self):
        # Create one bgpvpn
        bgpvpn = Bgpvpn('bgpvpn-%s' % self.id())
        self._vnc_lib.bgpvpn_create(bgpvpn)
        # Create one virtual network with one logical router as gateway
        lr, vns, vmis, _ = self.create_logical_router('lr-%s' % self.id())
        # We attached only one virtual network to the logical router
        vn = vns[0]
        vmi = vmis[0]

        # Associate the bgpvpn to the virtual network
        vn.add_bgpvpn(bgpvpn)
        self._vnc_lib.virtual_network_update(vn)
        lr = self._vnc_lib.logical_router_read(id=lr.uuid)

        # The try to set that same bgpvpn to the logical router
        with ExpectedException(BadRequest):
            lr.add_bgpvpn(bgpvpn)
            self._vnc_lib.logical_router_update(lr)
        lr = self._vnc_lib.logical_router_read(id=lr.uuid)

        # Detatch the logical router from the virtual network
        lr.del_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_update(lr)
        lr = self._vnc_lib.logical_router_read(id=lr.uuid)

        # Associate the bgpvpn to the logical router
        lr.add_bgpvpn(bgpvpn)
        self._vnc_lib.logical_router_update(lr)
        lr = self._vnc_lib.logical_router_read(id=lr.uuid)

        # Try to reattach the virtual network to the logical router
        with ExpectedException(BadRequest):
            lr.add_virtual_machine_interface(vmi)
            self._vnc_lib.logical_router_update(lr)

    def test_create_singleton_entry_with_zk_alloc_exist(self):
        api_server = self._server_info['api_server']
        vn_obj = VirtualNetwork('vn-%s' %(self.id()))
        orig_dbe_alloc = api_server._db_conn.dbe_alloc
        try:
            def err_dbe_alloc(*args, **kwargs):
                return (False, (409, 'Faking zk ResourceExistsError'))

            api_server._db_conn.dbe_alloc = err_dbe_alloc
            with ExpectedException(HttpError):
                api_server.create_singleton_entry(vn_obj)
        finally:
            api_server._db_conn.dbe_alloc = orig_dbe_alloc
    # end test_create_singleton_entry_with_zk_alloc_exist

    def test_tcp_keepalive_options(self):
        api_server = self._server_info['api_server']
        # Check if the TCP keepalive has been set in the api server args
        self.assertThat(api_server._args.tcp_keepalive_enable, Equals(True))

        # Check if other TCP keepalive options are present in args.
        self.assertIn('tcp_keepalive_idle_time', api_server._args)
        self.assertIn('tcp_keepalive_interval', api_server._args)
        self.assertIn('tcp_keepalive_probes', api_server._args)
    # end test_tcp_keepalive_options

# end class TestVncCfgApiServer


class TestStaleLockRemoval(test_case.ApiServerTestCase):
    STALE_LOCK_SECS = '0.2'
    @classmethod
    def setUpClass(cls):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestStaleLockRemoval, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'stale_lock_seconds',
            cls.STALE_LOCK_SECS)])
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestStaleLockRemoval, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

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
        with ExpectedException(RefsExistError), \
                mock.patch('vnc_cfg_api_server.api_server'\
                           '.VncApiServer.get_args') as get_args_patch:
            get_args_patch.return_value.stale_lock_seconds = sys.maxsize
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
        with ExpectedException(RefsExistError), \
                mock.patch('vnc_cfg_api_server.api_server'\
                           '.VncApiServer.get_args') as get_args_patch:
            get_args_patch.return_value.stale_lock_seconds = sys.maxsize
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
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        with uuid_cf.patch_row(str(vn_UUID),
            new_columns={'fq_name':json.dumps(vn_obj.fq_name),
                         'type':json.dumps(vn_obj._type)}):
            with ExpectedException(ResourceExistsError, ".*at cassandra.*"):
                self._api_server._db_conn.set_uuid(
                    obj_type=vn_obj._type,
                    obj_dict=vn_obj.__dict__,
                    id=vn_UUID,
                    do_lock=True)

            self._api_server._db_conn._object_db.cache_uuid_to_fq_name_del(
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
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestVncCfgApiServerRequests, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'max_requests', 10)])
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestVncCfgApiServerRequests, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def api_requests(self, orig_vn_read, count, vn_name):
        self.blocked = False
        api_server = self._server_info['api_server']
        def slow_response_on_vn_read(obj_type, *args, **kwargs):
            if obj_type == 'virtual_network':
                while self.blocked:
                    gevent.sleep(1)
            return orig_vn_read(obj_type, *args, **kwargs)

        api_server._db_conn._object_db.object_read = slow_response_on_vn_read

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

        orig_vn_read = api_server._db_conn._object_db.object_read
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
            api_server._db_conn._object_db.object_read = orig_vn_read
            self.blocked = False

        # Test to make sure api-server rejects requests over max_api_requests
        self.wait_till_api_server_idle()
        api_server = self._server_info['api_server']
        orig_vn_read = api_server._db_conn._object_db.object_read
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
            api_server._db_conn._object_db.object_read = orig_vn_read
            self.blocked = False

# end class TestVncCfgApiServerRequests


class TestLocalAuth(test_case.ApiServerTestCase):
    _rbac_role = 'admin'

    @classmethod
    def setUpClass(cls):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
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

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestLocalAuth, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

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
                    TestExtensionApi.test_case.assertIn('X_TEST_DUMMY', list(request.environ.keys()))
                    TestExtensionApi.test_case.assertNotIn('SERVER_SOFTWARE', list(request.environ.keys()))
                    TestExtensionApi.test_case.assertThat(request.environ['HTTP_X_CONTRAIL_USERAGENT'],
                                               Equals('bar'))
                    bottle.response.status = '234 Transformed Response'
                    response[obj_type]['extra_field'] = 'foo'
        # end transform_response
    # end class ResourceApiDriver

    @classmethod
    def setUpClass(cls):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
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
        logger.removeHandler(cls.console_handler)
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
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestPropertyWithList, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestPropertyWithList, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

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
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
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
        import pycassa
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
        proto_type = ProtocolType(protocol='proto', port=1)
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

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestPropertyWithMap, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestPropertyWithMap, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

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
        uuid_cf = self.get_cf('config_db_uuid','obj_uuid_table')
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
        import pycassa
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
        for key, val in fake_bindings_dict.items():
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

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestDBAudit, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    @contextlib.contextmanager
    def audit_mocks(self):
        import pycassa
        def fake_ks_prop(*args, **kwargs):
            return {'strategy_options': {'replication_factor': 1}}

        with test_common.patch_imports(
            [('schema_transformer.db',
              flexmock(db=flexmock(
                  SchemaTransformerDB=flexmock(get_db_info=lambda: [('to_bgp_keyspace', ['route_target_table'])]))))]):
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
            self.assertTill(self.vnc_db_has_ident, obj=test_obj)
            db_manage.db_check(*db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
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
            wrong_col_val_ts = dict((k,v) for k,v in list(orig_col_val_ts.items())
                if k not in omit_col_names)
            with uuid_cf.patch_row(
                test_obj.uuid, wrong_col_val_ts):
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
                errors = db_checker.check_obj_mandatory_fields()
                self.assertIn(db_manage.MandatoryFieldsMissingError,
                    [type(x) for x in errors])
    # end test_checker_missing_mandatory_fields


    def test_checker_missing_mandatory_fields_json(self):
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
            fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
            obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
            default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
            orig_col_val_ts = uuid_cf.get(test_obj.uuid,
                include_timestamp=True)
            omit_col_names = random.sample(set(
                ['type', 'fq_name', 'prop:id_perms']), 1)
            wrong_col_val_ts = dict((k,v) for k,v in orig_col_val_ts.items()
                if k not in omit_col_names)
            with uuid_cf.patch_row(
                test_obj.uuid, wrong_col_val_ts):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )

                errors = db_checker.check_obj_mandatory_fields()
                self.assertIn(db_manage.MandatoryFieldsMissingError,
                    [type(x) for x in errors])
                os.remove('ut_db.json')

    def test_checker_fq_name_mismatch_index_to_object(self):
        # detect OBJ_UUID_TABLE and OBJ_FQ_NAME_TABLE inconsistency
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            self.assert_vnc_db_has_ident(test_obj)

            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
            orig_col_val_ts = uuid_cf.get(test_obj.uuid,
                include_timestamp=True)
            wrong_col_val_ts = copy.deepcopy(orig_col_val_ts)
            wrong_col_val_ts['fq_name'] = (json.dumps(['wrong-fq-name']),
                wrong_col_val_ts['fq_name'][1])
            with uuid_cf.patch_row(
                test_obj.uuid, wrong_col_val_ts):
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNMismatchError, error_types)
                self.assertIn(db_manage.FQNStaleIndexError, error_types)
                self.assertIn(db_manage.FQNIndexMissingError, error_types)
    # end test_checker_fq_name_mismatch_index_to_object

    def test_checker_fq_name_mismatch_index_to_object_json(self):
        # detect OBJ_UUID_TABLE and OBJ_FQ_NAME_TABLE inconsistency
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            self.assert_vnc_db_has_ident(test_obj)
            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
            fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
            obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
            default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
            orig_col_val_ts = uuid_cf.get(test_obj.uuid,
                include_timestamp=True)
            wrong_col_val_ts = copy.deepcopy(orig_col_val_ts)
            wrong_col_val_ts['fq_name'] = (json.dumps(['wrong-fq-name']),
                wrong_col_val_ts['fq_name'][1])
            with uuid_cf.patch_row(
                test_obj.uuid, wrong_col_val_ts):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )

                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNMismatchError, error_types)
                self.assertIn(db_manage.FQNStaleIndexError, error_types)
                self.assertIn(db_manage.FQNIndexMissingError, error_types)

    def test_checker_fq_name_index_stale(self):
        # fq_name table in cassandra has entry but obj_uuid table doesn't
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            uuid_cf = self.get_cf('config_db_uuid','obj_uuid_table')
            with uuid_cf.patch_row(test_obj.uuid, new_columns=None):
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNStaleIndexError, error_types)
    # test_checker_fq_name_mismatch_stale

    def test_checker_fq_name_index_stale_json(self):
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
            fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
            obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
            default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
            with uuid_cf.patch_row(test_obj.uuid, new_columns=None):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNStaleIndexError, error_types)
                os.remove('ut_db.json') 

    def test_checker_fq_name_index_missing(self):
        # obj_uuid table has entry but fq_name table in cassandra doesn't
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            self.assert_vnc_db_has_ident(test_obj)
            uuid_cf = self.get_cf('config_db_uuid','obj_uuid_table')
            fq_name_cf = self.get_cf('config_db_uuid','obj_fq_name_table')
            test_obj_type = test_obj.get_type().replace('-', '_')
            orig_col_val_ts = fq_name_cf.get(test_obj_type,
                include_timestamp=True)
            # remove test obj in fq-name table
            wrong_col_val_ts = dict((k,v) for k,v in list(orig_col_val_ts.items())
                if ':'.join(test_obj.fq_name) not in k)
            with fq_name_cf.patch_row(test_obj_type, new_columns=wrong_col_val_ts):
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNIndexMissingError, error_types)
    # test_checker_fq_name_mismatch_missing

    def test_checker_fq_name_index_missing_json(self):
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            self.assert_vnc_db_has_ident(test_obj)
            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
            fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
            obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
            default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
            test_obj_type = test_obj.get_type().replace('-', '_')
            orig_col_val_ts = fq_name_cf.get(test_obj_type,
                include_timestamp=True)
            # remove test obj in fq-name table
            wrong_col_val_ts = dict((k,v) for k,v in orig_col_val_ts.items()
                if ':'.join(test_obj.fq_name) not in k)
            with fq_name_cf.patch_row(test_obj_type, new_columns=wrong_col_val_ts):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )

                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNIndexMissingError, error_types)
                os.remove('ut_db.json')

    def test_checker_ifmap_identifier_extra(self):
        # ifmap has identifier but obj_uuid table in cassandra doesn't
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            self.assert_vnc_db_has_ident(test_obj)

            uuid_cf = self.get_cf('config_db_uuid','obj_uuid_table')
            with uuid_cf.patch_row(test_obj.uuid, new_columns=None):
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNStaleIndexError, error_types)
    # test_checker_ifmap_identifier_extra

    def test_checker_ifmap_identifier_extra_json(self):
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            self.assert_vnc_db_has_ident(test_obj)
            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
            fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
            obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
            default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
            with uuid_cf.patch_row(test_obj.uuid, new_columns=None):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNStaleIndexError, error_types)
    
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
                    *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNIndexMissingError, error_types)
    # test_checker_ifmap_identifier_missing

    def test_checker_ifmap_identifier_missing_json(self):
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_obj = self._create_test_object()
            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
            fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
            obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
            default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
            with uuid_cf.patch_row(str(uuid.uuid4()),
                    new_columns={'type': json.dumps(''),
                                 'fq_name':json.dumps(''),
                                 'prop:id_perms':json.dumps('')}):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )
                errors = db_checker.check_fq_name_uuid_match()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNIndexMissingError, error_types)
                os.remove('ut_db.json')
        
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
        wrong_col_val_ts = dict((k,v) for k,v in list(orig_col_val_ts.items())
            if ':'.join(vn_obj.fq_name) not in k)
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
            # verify catch of extra ZK VN when name index is mocked
            with fq_name_cf.patch_row('virtual_network',
                new_columns=wrong_col_val_ts):
                errors = db_checker.check_subnet_addr_alloc()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNIndexMissingError, error_types)
    # test_checker_zk_vn_extra

    def test_checker_zk_vn_extra_json(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
        obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
        default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
        orig_col_val_ts = fq_name_cf.get('virtual_network',
            include_timestamp=True)
        # remove test obj in fq-name table
        wrong_col_val_ts = dict((k,v) for k,v in orig_col_val_ts.items()
            if ':'.join(vn_obj.fq_name) not in k)
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            with fq_name_cf.patch_row('virtual_network',
                new_columns=wrong_col_val_ts):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )

                errors = db_checker.check_subnet_addr_alloc()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNIndexMissingError, error_types)
                os.remove('ut_db.json')
        # remove test obj in fq-name table
    
    def test_checker_zk_vn_missing(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))

            with db_checker._zk_client.patch_path(
                '%s%s/%s' %(self._cluster_id,
                          db_checker.BASE_SUBNET_ZK_PATH,
                          vn_obj.get_fq_name_str())):
                errors = db_checker.check_subnet_addr_alloc()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkVNMissingError, error_types)
                self.assertIn(db_manage.ZkSubnetMissingError, error_types)
    # test_checker_zk_vn_missing

    def test_checker_zk_vn_missing_json(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
            fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
            obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
            default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
            test_zk = FakeKazooClient() 
            with test_zk.patch_path(
                '%s%s/%s' %(self._cluster_id,
                          '/api-server/subnets',
                          vn_obj.get_fq_name_str())):
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj = self._create_cassandra_json(default_ks_list)
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )

                errors = db_checker.check_subnet_addr_alloc()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkVNMissingError, error_types)
                self.assertIn(db_manage.ZkSubnetMissingError, error_types)
                os.remove('ut_db.json')
    
    def test_checker_zk_ip_extra(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))

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

    def test_checker_zk_ip_extra_json(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
            fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
            obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
            iip_obj = vnc_api.InstanceIp(self.id())
            iip_obj.add_virtual_network(vn_obj)
            self._vnc_lib.instance_ip_create(iip_obj)
            default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
            with uuid_cf.patch_row(iip_obj.uuid, None):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )
            
                errors = db_checker.check_subnet_addr_alloc()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.FQNStaleIndexError, error_types)
                self.assertIn(db_manage.ZkIpExtraError, error_types)
                os.remove('ut_db.json')
    
    def test_checker_zk_ip_missing(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))

            iip_obj = vnc_api.InstanceIp(self.id())
            iip_obj.add_virtual_network(vn_obj)
            self._vnc_lib.instance_ip_create(iip_obj)
            ip_addr = self._vnc_lib.instance_ip_read(
                id=iip_obj.uuid).instance_ip_address
            ip_str = "%(#)010d" % {'#': int(netaddr.IPAddress(ip_addr))}
            with db_checker._zk_client.patch_path(
                '%s%s/%s:1.1.1.0/28/%s' %(
                    self._cluster_id, db_checker.BASE_SUBNET_ZK_PATH,
                    vn_obj.get_fq_name_str(), ip_str)):
                errors = db_checker.check_subnet_addr_alloc()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkIpMissingError, error_types)
    # test_checker_zk_ip_missing

    def test_checker_zk_ip_missing_json(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
            fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
            obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
            default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
            iip_obj = vnc_api.InstanceIp(self.id())
            iip_obj.add_virtual_network(vn_obj)
            self._vnc_lib.instance_ip_create(iip_obj)
            ip_addr = self._vnc_lib.instance_ip_read(
                id=iip_obj.uuid).instance_ip_address
            ip_str = "%(#)010d" % {'#': int(netaddr.IPAddress(ip_addr))}
            test_zk = FakeKazooClient()
            with test_zk.patch_path(
                '%s%s/%s:1.1.1.0/28/%s' %(
                    self._cluster_id, db_manage.DatabaseManager.BASE_SUBNET_ZK_PATH,
                    vn_obj.get_fq_name_str(), ip_str)):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )
                errors = db_checker.check_subnet_addr_alloc()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkIpMissingError, error_types)
                os.remove('ut_db.json')
        
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
                *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
            with uuid_cf.patch_column(
                    vn_obj.uuid,
                    'prop:virtual_network_network_id',
                    json.dumps(42)):
                errors = db_checker.check_virtual_networks_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkVNIdExtraError, error_types)
                self.assertIn(db_manage.ZkVNIdMissingError, error_types)
    # test_checker_zk_virtual_network_id_extra_and_missing

    def test_checker_zk_virtual_network_id_extra_and_missing_json(self):
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
        obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
        default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            with uuid_cf.patch_column(
                    vn_obj.uuid,
                    'prop:virtual_network_network_id',
                    json.dumps(42)):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )

                errors = db_checker.check_virtual_networks_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkVNIdExtraError, error_types)
                self.assertIn(db_manage.ZkVNIdMissingError, error_types)
    
    def test_checker_zk_virtual_network_id_duplicate(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        vn1_obj, _, _ = self._create_vn_subnet_ipam_iip('vn1-%s' % self.id())
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_obj.uuid)
        vn2_obj, _, _ = self._create_vn_subnet_ipam_iip('vn2-%s' % self.id())

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
            with uuid_cf.patch_column(
                    vn2_obj.uuid,
                    'prop:virtual_network_network_id',
                    json.dumps(vn1_obj.virtual_network_network_id)):
                errors = db_checker.check_virtual_networks_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.VNDuplicateIdError, error_types)
                self.assertIn(db_manage.ZkVNIdExtraError, error_types)
    # test_checker_zk_virtual_network_id_duplicate

    def test_checker_zk_virtual_network_id_duplicate_json(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
        obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
        default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
        vn1_obj, _, _ = self._create_vn_subnet_ipam_iip('vn1-%s' % self.id())
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_obj.uuid)
        vn2_obj, _, _ = self._create_vn_subnet_ipam_iip('vn2-%s' % self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            with uuid_cf.patch_column(
                    vn2_obj.uuid,
                    'prop:virtual_network_network_id',
                    json.dumps(vn1_obj.virtual_network_network_id)):
                
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )
                
                errors = db_checker.check_virtual_networks_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.VNDuplicateIdError, error_types)
                self.assertIn(db_manage.ZkVNIdExtraError, error_types)
                os.remove('ut_db.json')
    
    def test_checker_zk_security_group_id_extra_and_missing(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        sg_obj = self._create_security_group(self.id())

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
            with uuid_cf.patch_column(
                    sg_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(8000042)):
                errors = db_checker.check_security_groups_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkSGIdExtraError, error_types)
                self.assertIn(db_manage.ZkSGIdMissingError, error_types)
    # test_checker_zk_security_group_id_extra_and_missing

    def test_checker_zk_security_group_id_extra_and_missing_json(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
        obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
        default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
        sg_obj = self._create_security_group(self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            with uuid_cf.patch_column(
                    sg_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(8000042)):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )
                errors = db_checker.check_security_groups_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.ZkSGIdExtraError, error_types)
                self.assertIn(db_manage.ZkSGIdMissingError, error_types)
                os.remove('ut_db.json')
    
    def test_checker_zk_security_group_id_duplicate(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        sg1_obj = self._create_security_group('sg1-%s' % self.id())
        sg1_obj = self._vnc_lib.security_group_read(id=sg1_obj.uuid)
        sg2_obj = self._create_security_group('sg2-%s' % self.id())

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check --cluster_id %s' %(self._cluster_id)))
            with uuid_cf.patch_column(
                    sg2_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(sg1_obj.security_group_id)):
                errors = db_checker.check_security_groups_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.SGDuplicateIdError, error_types)
                self.assertIn(db_manage.ZkSGIdExtraError, error_types)
    # test_checker_zk_security_group_id_duplicate

    def test_checker_zk_security_group_id_duplicate_json(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
        obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
        default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
        sg1_obj = self._create_security_group('sg1-%s' % self.id())
        sg1_obj = self._vnc_lib.security_group_read(id=sg1_obj.uuid)
        sg2_obj = self._create_security_group('sg2-%s' % self.id())
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            with uuid_cf.patch_column(
                    sg2_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(sg1_obj.security_group_id)):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_checker = db_manage.DatabaseChecker(
                    *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
                )
                errors = db_checker.check_security_groups_id()
                error_types = [type(x) for x in errors]
                self.assertIn(db_manage.SGDuplicateIdError, error_types)
                self.assertIn(db_manage.ZkSGIdExtraError, error_types)
    
    def test_checker_security_group_0_missing(self):
        pass # move to schema transformer test
    # test_checker_security_group_0_missing

    def test_checker_route_targets_id_with_vn_rt_list_set_to_none(self):
        project = Project('project-%s' % self.id())
        self._vnc_lib.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self._vnc_lib.virtual_network_create(vn)
        vn.set_route_target_list(None)
        self._vnc_lib.virtual_network_update(vn)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            args = db_manage._parse_args(
                'check --cluster_id %s' % self._cluster_id)
            db_checker = db_manage.DatabaseChecker(*args)
            db_checker.audit_route_targets_id()
    
    def test_checker_route_targets_id_with_vn_rt_list_set_to_none_json(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
        obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
        default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
        project = Project('project-%s' % self.id())
        self._vnc_lib.project_create(project)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self._vnc_lib.virtual_network_create(vn)
        vn.set_route_target_list(None)
        self._vnc_lib.virtual_network_update(vn)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            test_json_obj = self._create_cassandra_json(default_ks_list)
            zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
            test_json_obj['zookeeper'] = zookeeper_json
            with open('ut_db.json' , 'w') as outfile:
                json.dump(test_json_obj, outfile, indent=4)
            db_checker = db_manage.DatabaseChecker(
                *db_manage._parse_args('check --in_json ut_db.json --cluster_id %s' %(self._cluster_id))
            )

            db_checker.audit_route_targets_id()
            os.remove('ut_db.json')

    def test_cleaner(self):
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_manage.db_clean(*db_manage._parse_args('clean --skip_backup --cluster_id %s' %(self._cluster_id)))
    # end test_cleaner

    def test_cleaner_zk_virtual_network_id(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_cleaner = db_manage.DatabaseCleaner(
                *db_manage._parse_args('--execute clean --cluster_id %s' %(self._cluster_id)))
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
                        '%s%s/%s' % (
                            self._cluster_id, db_cleaner.BASE_VN_ID_ZK_PATH,
                            zk_id_str))
                )

    def test_cleaner_zk_virtual_network_id_json(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table') 
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
        obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
        default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            fake_id = 42
            with uuid_cf.patch_column(
                    vn_obj.uuid,
                    'prop:virtual_network_network_id',
                    json.dumps(fake_id)):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_cleaner = db_manage.DatabaseCleaner(
                    *db_manage._parse_args('--execute clean --in_json ut_db.json --out_json ut_db_out.json --cluster_id %s' %(self._cluster_id)))
                db_cleaner.clean_stale_virtual_network_id()
                zk_id_str = "%(#)010d" %\
                    {'#': vn_obj.virtual_network_network_id - 1}
                self.assertIsNone (
                    db_cleaner.zk_exists(
                        '%s%s/%s' % (
                            self._cluster_id, db_cleaner.BASE_VN_ID_ZK_PATH,
                            zk_id_str))
                )
                os.remove('ut_db.json')
        
    def test_healer_zk_virtual_network_id(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_cleaner = db_manage.DatabaseHealer(
                    *db_manage._parse_args('--execute heal --cluster_id %s' % (
                        self._cluster_id)))
            fake_id = 42
            with uuid_cf.patch_column(
                    vn_obj.uuid,
                    'prop:virtual_network_network_id',
                    json.dumps(fake_id)):
                db_cleaner.heal_virtual_networks_id()
                zk_id_str = "%(#)010d" % {'#': fake_id - 1}
                self.assertEqual(
                    db_cleaner._zk_client.exists(
                        '%s%s/%s' % (
                             self._cluster_id, db_cleaner.BASE_VN_ID_ZK_PATH,
                             zk_id_str))[0],
                             vn_obj.get_fq_name_str())

    def test_healer_zk_virtual_network_id_json(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
        obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
        default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
        vn_obj, _, _ = self._create_vn_subnet_ipam_iip(self.id())
        vn_obj = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            fake_id = 42
            with uuid_cf.patch_column(
                    vn_obj.uuid,
                    'prop:virtual_network_network_id',
                    json.dumps(fake_id)):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_healer = db_manage.DatabaseHealer (
                    *db_manage._parse_args('--execute heal --in_json ut_db.json --out_json ut_db_out.json --cluster_id %s' %(self._cluster_id)))
                db_healer.heal_virtual_networks_id()
                zk_id_str = "%(#)010d" % {'#': fake_id - 1}
                self.assertEqual(
                    db_healer.zk_exists(
                        '%s%s/%s' % (
                             self._cluster_id, db_healer.BASE_VN_ID_ZK_PATH,
                             zk_id_str)),
                             vn_obj.get_fq_name_str())
                os.remove('ut_db.json')
    
    def test_cleaner_zk_security_group_id(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        sg_obj = self._create_security_group(self.id())
        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_cleaner = db_manage.DatabaseCleaner(
                *db_manage._parse_args('--execute clean --cluster_id %s' %(self._cluster_id)))
            with uuid_cf.patch_column(
                    sg_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(8000042)):
                db_cleaner.clean_stale_security_group_id()
                zk_id_str = "%(#)010d" % {'#': sg_obj.security_group_id}
                self.assertIsNone(
                    db_cleaner._zk_client.exists(
                        '%s%s/%s' % (
                            self._cluster_id, db_cleaner.BASE_VN_ID_ZK_PATH,
                            zk_id_str))
                )

    def test_cleaner_zk_security_group_id_json(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
        obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
        default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
        sg_obj = self._create_security_group(self.id())
        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            with uuid_cf.patch_column(
                    sg_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(8000042)):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_cleaner = db_manage.DatabaseCleaner(
                    *db_manage._parse_args('--execute clean --in_json ut_db.json --out_json ut_db_out.json --cluster_id %s' %(self._cluster_id)))
                db_cleaner.clean_stale_security_group_id()
                zk_id_str = "%(#)010d" % {'#': sg_obj.security_group_id}
                self.assertIsNone(
                    db_cleaner.zk_exists(
                        '%s%s/%s' % (
                            self._cluster_id, db_cleaner.BASE_VN_ID_ZK_PATH,
                            zk_id_str))
                )

                os.remove('ut_db.json')

    def test_healer_zk_security_group_id(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        sg_obj = self._create_security_group(self.id())
        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)

        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            db_cleaner = db_manage.DatabaseHealer(
                *db_manage._parse_args('--execute heal --cluster_id %s' %(self._cluster_id)))
            with uuid_cf.patch_column(
                    sg_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(8000042)):
                db_cleaner.heal_security_groups_id()
                zk_id_str = "%(#)010d" % {'#': 42}
                self.assertEqual(
                    db_cleaner._zk_client.exists(
                        '%s%s/%s' %
                        (self._cluster_id, db_cleaner.BASE_SG_ID_ZK_PATH,
                         zk_id_str))[0],
                    sg_obj.get_fq_name_str())

    #Hello_Rajit
    def test_healer_zk_security_group_id_json(self):
        uuid_cf = self.get_cf('config_db_uuid', 'obj_uuid_table')
        fq_name_cf = self.get_cf('config_db_uuid', 'obj_fq_name_table')
        obj_shared_cf = self.get_cf('config_db_uuid', 'obj_shared_table')
        default_ks_list = [uuid_cf, fq_name_cf, obj_shared_cf]
        sg_obj = self._create_security_group(self.id())
        sg_obj = self._vnc_lib.security_group_read(id=sg_obj.uuid)
        with self.audit_mocks():
            from vnc_cfg_api_server import db_manage
            with uuid_cf.patch_column(
                    sg_obj.uuid,
                    'prop:security_group_id',
                    json.dumps(8000042)):
                test_json_obj = self._create_cassandra_json(default_ks_list)
                zookeeper_json = self._create_zookeeper_json(self.get_zk_values())
                test_json_obj['zookeeper'] = zookeeper_json
                with open('ut_db.json' , 'w') as outfile:
                    json.dump(test_json_obj, outfile, indent=4)
                db_healer = db_manage.DatabaseHealer (
                    *db_manage._parse_args('--execute heal --in_json ut_db.json --out_json ut_db_out.json --cluster_id %s' %(self._cluster_id)))
                db_healer.heal_security_groups_id()
                zk_id_str = "%(#)010d" % {'#': 42}
                self.assertEqual(
                    db_healer.zk_exists(
                        '%s%s/%s' %
                        (self._cluster_id, db_healer.BASE_SG_ID_ZK_PATH,
                         zk_id_str)),
                    sg_obj.get_fq_name_str())
                os.remove('ut_db.json')

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
            db_manage.db_heal(*db_manage._parse_args('heal --skip_backup --cluster_id %s' %(self._cluster_id)))
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
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestBulk, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestBulk, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

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
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestCacheWithMetadata, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestCacheWithMetadata, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def setUp(self):
        self.uuid_cf = self.get_cf( 'config_db_uuid', 'obj_uuid_table')
        self.cache_mgr = self._api_server._db_conn._object_db._obj_cache_mgr
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
        self.assertIn(vn_obj.uuid, list(cache_mgr._cache.keys()))

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
        self.assertNotIn(vn_obj.uuid, list(cache_mgr._cache.keys()))

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

        self.assertNotIn(vn_miss_obj.uuid, list(cache_mgr._cache.keys()))
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
        cache_mgr = self._api_server._db_conn._object_db._obj_cache_mgr

        vn1_name = 'vn-1-%s' %(self.id())
        vn2_name = 'vn-2-%s' %(self.id())
        vn1_obj = self.create_test_object(vn1_name)
        vn2_obj = self.create_test_object(vn2_name)
        # prime RIs to cache
        ri1_obj = self._vnc_lib.routing_instance_read(
            fq_name=vn1_obj.fq_name+[vn1_name])
        ri2_obj = self._vnc_lib.routing_instance_read(
            fq_name=vn2_obj.fq_name+[vn2_name])

        self.assertIn(ri1_obj.uuid, list(cache_mgr._cache.keys()))
        self.assertIn(ri2_obj.uuid, list(cache_mgr._cache.keys()))

        ri1_obj.add_routing_instance(ri2_obj, None)
        self._vnc_lib.routing_instance_update(ri1_obj)
        self.assertNotIn(ri2_obj.uuid, list(cache_mgr._cache.keys()))
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
        self.assertIn(ipam_obj.uuid, list(cache_mgr._cache.keys()))

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
                fq_name=ipam_obj.fq_name, fields=['display_name',
                'virtual_network_back_refs'])
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
        self.assertIn(ipam_obj.uuid, list(cache_mgr._cache.keys()))

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
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        return super(TestCacheWithMetadataEviction, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'object_cache_entries',
            '2')])
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestCacheWithMetadataEviction, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def test_evict_on_full(self):
        vn1_obj = vnc_api.VirtualNetwork('vn-1-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn1_obj)

        vn2_obj = vnc_api.VirtualNetwork('vn-2-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn2_obj)

        vn3_obj = vnc_api.VirtualNetwork('vn-3-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(vn3_obj)

        # prime with vn-1 and vn-2
        cache_mgr = self._api_server._db_conn._object_db._obj_cache_mgr
        self._vnc_lib.virtual_network_read(id=vn1_obj.uuid)
        self._vnc_lib.virtual_network_read(id=vn2_obj.uuid)
        cache_keys = list(cache_mgr._cache.keys())
        self.assertIn(vn1_obj.uuid, cache_keys)
        self.assertIn(vn2_obj.uuid, cache_keys)
        self.assertNotIn(vn3_obj.uuid, cache_keys)

        # prime vn-3 and test eviction
        self._vnc_lib.virtual_network_read(id=vn3_obj.uuid)
        cache_keys = list(cache_mgr._cache.keys())
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
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        return super(TestCacheWithMetadataExcludeTypes, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'object_cache_exclude_types',
            'project, network-ipam')])
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestCacheWithMetadataExcludeTypes, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def test_exclude_types_not_cached(self):
        # verify not cached for configured types
        obj = vnc_api.Project('proj-%s' %(self.id()))
        self._vnc_lib.project_create(obj)
        self._vnc_lib.project_read(id=obj.uuid)
        cache_mgr = self._api_server._db_conn._object_db._obj_cache_mgr
        self.assertNotIn(obj.uuid, list(cache_mgr._cache.keys()))

        obj = vnc_api.NetworkIpam('ipam-%s' %(self.id()))
        self._vnc_lib.network_ipam_create(obj)
        self._vnc_lib.network_ipam_read(id=obj.uuid)
        cache_mgr = self._api_server._db_conn._object_db._obj_cache_mgr
        self.assertNotIn(obj.uuid, list(cache_mgr._cache.keys()))

        # verify cached for others
        obj = vnc_api.VirtualNetwork('vn-%s' %(self.id()))
        self._vnc_lib.virtual_network_create(obj)
        self._vnc_lib.virtual_network_read(id=obj.uuid)
        cache_mgr = self._api_server._db_conn._object_db._obj_cache_mgr
        self.assertIn(obj.uuid, list(cache_mgr._cache.keys()))
    # end test_exclude_types_not_cached
# end class TestCacheWithMetadataExcludeTypes

class TestRefValidation(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestRefValidation, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestRefValidation, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

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

    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestVncApiStats, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestVncApiStats, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

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
        cls.to_bgp_ks = '%s_to_bgp_keyspace' %(cls._cluster_id)
        cls.svc_mon_ks = '%s_svc_monitor_keyspace' %(cls._cluster_id)
        cls.dev_mgr_ks = '%s_dm_keyspace' %(cls._cluster_id)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestDbJsonExim, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def test_db_exim_args(self):
        from cfgm_common import db_json_exim
        with ExpectedException(db_json_exim.InvalidArguments,
            'Both --import-from and --export-to cannot be specified'):
            db_json_exim.DatabaseExim("--import-from foo --export-to bar")
    # end test_db_exim_args

    def test_db_export(self):
        from pycassa.system_manager import SystemManager
        from cfgm_common import db_json_exim
        with tempfile.NamedTemporaryFile() as export_dump:
            patch_ks = SystemManager.patch_keyspace
            with patch_ks(self.to_bgp_ks, {}), \
                 patch_ks(self.svc_mon_ks, {}), \
                 patch_ks(self.dev_mgr_ks, {}):
                vn_obj = self._create_test_object()
                db_json_exim.DatabaseExim('--export-to %s --cluster_id %s' %(
                    export_dump.name, self._cluster_id)).db_export()
                dump = json.loads(export_dump.readlines()[0])
                dump_cassandra = dump['cassandra']
                dump_zk = json.loads(dump['zookeeper'])
                uuid_table = dump_cassandra['config_db_uuid']['obj_uuid_table']
                self.assertEqual(uuid_table[vn_obj.uuid]['fq_name'][0],
                    json.dumps(vn_obj.get_fq_name()))
                zk_node = [node for node in dump_zk
                    if node[0] == '%s/fq-name-to-uuid/virtual_network:%s/' %(
                        self._cluster_id, vn_obj.get_fq_name_str())]
                self.assertEqual(len(zk_node), 1)
                self.assertEqual(zk_node[0][1][0], vn_obj.uuid)
    # end test_db_export

    def test_db_export_with_omit_keyspaces(self):
        from cfgm_common import db_json_exim
        with tempfile.NamedTemporaryFile() as export_dump:
            vn_obj = self._create_test_object()

            omit_ks = set(db_json_exim.KEYSPACES) - set(['config_db_uuid'])
            args = '--export-to %s --omit-keyspaces ' %(export_dump.name)
            for ks in list(omit_ks):
                args += '%s ' %(ks)
            args += '--cluster_id %s' %(self._cluster_id)
            db_json_exim.DatabaseExim(args).db_export()
            dump = json.loads(export_dump.readlines()[0])
            dump_cassandra = dump['cassandra']
            dump_zk = json.loads(dump['zookeeper'])
            uuid_table = dump_cassandra['config_db_uuid']['obj_uuid_table']
            self.assertEqual(uuid_table[vn_obj.uuid]['fq_name'][0],
                json.dumps(vn_obj.get_fq_name()))
            zk_node = [node for node in dump_zk
                if node[0] == '%s/fq-name-to-uuid/virtual_network:%s/' %(
                    self._cluster_id, vn_obj.get_fq_name_str())]
            self.assertEqual(len(zk_node), 1)
            self.assertEqual(zk_node[0][1][0], vn_obj.uuid)
    # end test_db_export_with_omit_keyspaces

    def test_db_export_and_import(self):
        from pycassa.system_manager import SystemManager
        from cfgm_common import db_json_exim
        with tempfile.NamedTemporaryFile() as dump_f:
            patch_ks = SystemManager.patch_keyspace
            with patch_ks(self.to_bgp_ks, {}), \
                 patch_ks(self.svc_mon_ks, {}), \
                 patch_ks(self.dev_mgr_ks, {}):
                vn_obj = self._create_test_object()
                db_json_exim.DatabaseExim('--export-to %s --cluster_id %s' %(
                    dump_f.name, self._cluster_id)).db_export()
                with ExpectedException(db_json_exim.CassandraNotEmptyError):
                    db_json_exim.DatabaseExim(
                        '--import-from %s --cluster_id %s' %(
                        dump_f.name, self._cluster_id)).db_import()

                uuid_cf = self.get_cf(
                    'config_db_uuid', 'obj_uuid_table')
                fq_name_cf = self.get_cf(
                    'config_db_uuid', 'obj_fq_name_table')
                shared_cf = self.get_cf(
                    'config_db_uuid', 'obj_shared_table')
                with uuid_cf.patch_cf({}), fq_name_cf.patch_cf({}), \
                     shared_cf.patch_cf({}):
                    with ExpectedException(
                         db_json_exim.ZookeeperNotEmptyError):
                        db_json_exim.DatabaseExim(
                            '--import-from %s --cluster_id %s' %(
                            dump_f.name, self._cluster_id)).db_import()

                exim_obj = db_json_exim.DatabaseExim(
                    '--import-from %s --cluster_id %s' %(
                    dump_f.name, self._cluster_id))
                with uuid_cf.patch_cf({}), fq_name_cf.patch_cf({}), \
                    shared_cf.patch_cf({}), exim_obj._zookeeper.patch_path(
                        '%s/' %(self._cluster_id), recursive=True):
                    exim_obj.db_import()
                    dump = json.loads(dump_f.readlines()[0])
                    dump_cassandra = dump['cassandra']
                    dump_zk = json.loads(dump['zookeeper'])
                    uuid_table = dump_cassandra['config_db_uuid']['obj_uuid_table']
                    self.assertEqual(uuid_table[vn_obj.uuid]['fq_name'][0],
                        json.dumps(vn_obj.get_fq_name()))
                    zk_node = [node for node in dump_zk
                        if node[0] == '%s/fq-name-to-uuid/virtual_network:%s/' %(
                            self._cluster_id, vn_obj.get_fq_name_str())]
                    self.assertEqual(len(zk_node), 1)
                self.assertEqual(zk_node[0][1][0], vn_obj.uuid)
    # end test_db_export_and_import
# end class TestDbJsonExim

class TestPagination(test_case.ApiServerTestCase):
    default_paginate_count = 5
    @classmethod
    def setUpClass(cls):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        return super(TestPagination, cls).setUpClass(
            extra_config_knobs=[('DEFAULTS', 'paginate_count',
            TestPagination.default_paginate_count)])
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestPagination, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    class FetchExpect(object):
        def __init__(self, num_objs, marker):
            self.num_objs = num_objs
            self.marker = marker
    # end FetchExpect

    def _create_vn_collection(self, count, proj_obj=None):
        return self._create_test_objects(count=count, proj_obj=proj_obj)
    # end _create_vn_collection

    def _create_vmi_collection(self, count, vn_obj):
        proj_obj = self._vnc_lib.project_read(id=vn_obj.parent_uuid)
        vmi_objs = []
        for i in range(count):
            vmi_obj = VirtualMachineInterface(
                'vmi-%s-%s-%s' %(self.id(), vn_obj.name, i),
                parent_obj=proj_obj)
            vmi_obj.add_virtual_network(vn_obj)
            self._vnc_lib.virtual_machine_interface_create(vmi_obj)
            vmi_objs.append(vmi_obj)

        return vmi_objs
    # end _create_vmi_collection

    def test_validate_input(self):
        # * fail 400 if last part of non-None page_marker is not alphanumeric
        #   (non-None marker is uuid in anchored walks and fq_name_str_uuid
        #    in unanchored walks)
        # * fail 400 if page_limit is not number(None, string, array, dict)
        pass
    # end test_validate_input

    def test_unanchored(self):
        # 1. create a collection of n
        # * cover with marker=None, no limit specified, run should be
        #    n/(default limit)
        # * cover with marker=None, limit=n, run should be 1
        # * cover with marker=None, limit=n/2, run should be 2
        # * cover with marker=None, limit=1, run should be n
        # * cover with marker=None, limit<=0, run should be 1
        # * cover with marker=1, limit=n, run should be 1
        # * cover with marker=n, limit=n, run should be 1 and empty
        # * cover with marker=1, limit<=0, run should be 1
        # * test with unicode/non-ascii char in fqn
        vn_objs = self._create_vn_collection(self.default_paginate_count*2)
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        def verify_collection_walk(page_limit=None):
            marker = None
            all_vn_ids = []
            all_vn_count = self._vnc_lib.virtual_networks_list(
                count=True)['virtual-networks']['count']
            max_fetches = (old_div(all_vn_count,
                           (page_limit or self.default_paginate_count))) + 1
            fetches = 0
            while True:
                if ((max_fetches > 0) and (fetches > max_fetches)):
                    break
                fetches += 1
                url = 'http://%s:%s/virtual-networks?page_marker=%s' %(
                    listen_ip, listen_port, marker)
                if page_limit is not None:
                    url += '&page_limit=%s' %(page_limit)
                resp = requests.get(url,
                    headers={'Content-type': 'application/json; charset="UTF-8"'})
                if page_limit is not None and page_limit <= 0:
                    self.assertEqual(resp.status_code, 400)
                    return

                self.assertEqual(resp.status_code, 200)
                read_vn_ids = [vn['uuid']
                    for vn in json.loads(resp.text)['virtual-networks']]
                all_vn_ids.extend(read_vn_ids)
                marker = json.loads(resp.text)['marker']
                if marker is not None:
                    self.assertEqual(len(read_vn_ids),
                        page_limit or self.default_paginate_count)
                else:
                    # all fetched
                    break

            self.assertLessEqual(fetches, max_fetches)
            self.assertEqual(set([o.uuid for o in vn_objs]) - set(all_vn_ids),
                             set([]))
        # end verify_collection_walk

        verify_collection_walk()
        verify_collection_walk(page_limit=-1)
        verify_collection_walk(page_limit=0)
        verify_collection_walk(page_limit=10000)
        verify_collection_walk(page_limit=1)
        verify_collection_walk(page_limit=2)
        logger.info("Verified unanchored pagination fetch.")
    # end test_unanchored

    def test_anchored_by_one_parent(self):
        proj_obj = Project('%s-project' %(self.id()))
        self._vnc_lib.project_create(proj_obj)
        vn_objs = self._create_vn_collection(
            self.default_paginate_count*2, proj_obj)

        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        def verify_collection_walk(page_limit=None, fetch_expects=None):
            marker = None
            all_vn_ids = []
            for fe_obj in fetch_expects or []:
                url = 'http://%s:%s/virtual-networks?page_marker=%s&parent_id=%s' %(
                    listen_ip, listen_port, marker, proj_obj.uuid)
                if page_limit is not None:
                    url += '&page_limit=%s' %(page_limit)
                resp = requests.get(url,
                    headers={'Content-type': 'application/json; charset="UTF-8"'})
                if page_limit is not None and page_limit <= 0:
                    self.assertEqual(resp.status_code, 400)
                    return
                self.assertEqual(resp.status_code, 200)
                read_vn_ids = [vn['uuid']
                    for vn in json.loads(resp.text)['virtual-networks']]
                self.assertEqual(len(read_vn_ids), fe_obj.num_objs)
                marker = json.loads(resp.text)['marker']
                self.assertEqual(marker, fe_obj.marker)
                all_vn_ids.extend(read_vn_ids)

            self.assertEqual(set([o.uuid for o in vn_objs]) - set(all_vn_ids),
                         set([]))
        # end verify_collection_walk

        sorted_vn_uuid = sorted([o.uuid for o in vn_objs])
        FetchExpect = self.FetchExpect
        verify_collection_walk(fetch_expects=[
            FetchExpect(self.default_paginate_count,
                sorted_vn_uuid[self.default_paginate_count-1]),
            FetchExpect(self.default_paginate_count,
                sorted_vn_uuid[(self.default_paginate_count*2)-1]),
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=-1, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=0, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=1, fetch_expects=[
            FetchExpect(1, val) for idx,val in enumerate(sorted_vn_uuid)] +
            [FetchExpect(0, None)])
        verify_collection_walk(page_limit=2, fetch_expects=[
            FetchExpect(2, sorted_vn_uuid[(i*2)+1])
                for i in range(old_div(len(vn_objs),2))] +
            [FetchExpect(0, None)])

        logger.info("Verified anchored pagination fetch with one parent.")
    # end test_anchored_by_one_parent

    def test_anchored_by_one_backref(self):
        proj_obj = Project('%s-project' %(self.id()))
        self._vnc_lib.project_create(proj_obj)
        vn_obj = VirtualNetwork('vn1', parent_obj=proj_obj)
        self._vnc_lib.virtual_network_create(vn_obj)
        vmi_objs = self._create_vmi_collection(
            (self.default_paginate_count*2)-1, vn_obj)
        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        def verify_collection_walk(page_limit=None, fetch_expects=None):
            marker = None
            all_vmi_ids = []
            for fe_obj in fetch_expects or []:
                url = 'http://%s:%s/virtual-machine-interfaces?page_marker=%s&back_ref_id=%s' %(
                    listen_ip, listen_port, marker, vn_obj.uuid)
                if page_limit is not None:
                    url += '&page_limit=%s' %(page_limit)
                resp = requests.get(url,
                    headers={'Content-type': 'application/json; charset="UTF-8"'})
                if page_limit is not None and page_limit <= 0:
                    self.assertEqual(resp.status_code, 400)
                    return
                self.assertEqual(resp.status_code, 200)
                read_vmi_ids = [vmi['uuid']
                    for vmi in json.loads(resp.text)['virtual-machine-interfaces']]
                self.assertEqual(len(read_vmi_ids), fe_obj.num_objs)
                marker = json.loads(resp.text)['marker']
                self.assertEqual(marker, fe_obj.marker)
                all_vmi_ids.extend(read_vmi_ids)

            self.assertEqual(set([o.uuid for o in vmi_objs]) - set(all_vmi_ids),
                set([]))
        # end verify_collection_walk

        sorted_vmi_uuid = sorted([o.uuid for o in vmi_objs])
        FetchExpect = self.FetchExpect
        verify_collection_walk(fetch_expects=[
            FetchExpect(self.default_paginate_count,
                sorted_vmi_uuid[self.default_paginate_count-1]),
            FetchExpect(self.default_paginate_count-1,
                None)])
        verify_collection_walk(page_limit=-1, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=0, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=1, fetch_expects=[
            FetchExpect(1, val) for idx,val in enumerate(sorted_vmi_uuid)] +
            [FetchExpect(0, None)])
        verify_collection_walk(page_limit=2, fetch_expects=[
            FetchExpect(2, sorted_vmi_uuid[1]),
            FetchExpect(2, sorted_vmi_uuid[3]),
            FetchExpect(2, sorted_vmi_uuid[5]),
            FetchExpect(2, sorted_vmi_uuid[7]),
            FetchExpect(1, None)])

        logger.info("Verified anchored pagination fetch with one backref.")
    # end test_anchored_by_one_backref

    def test_anchored_by_parent_list(self):
        proj1_obj = Project('%s-project1' %(self.id()))
        self._vnc_lib.project_create(proj1_obj)
        proj2_obj = Project('%s-project2' %(self.id()))
        self._vnc_lib.project_create(proj2_obj)

        vn_p1_objs = self._create_vn_collection(
            self.default_paginate_count+1, proj1_obj)
        vn_p2_objs = self._create_vn_collection(2, proj2_obj)

        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        def verify_collection_walk(page_limit=None, fetch_expects=None):
            all_vn_ids = []

            def request_with_query_params(marker):
                url = 'http://%s:%s/virtual-networks?page_marker=%s&parent_id=%s,%s' %(
                    listen_ip, listen_port, marker, proj1_obj.uuid, proj2_obj.uuid)
                if page_limit is not None:
                    url += '&page_limit=%s' %(page_limit)
                resp = requests.get(url,
                    headers={'Content-type': 'application/json; charset="UTF-8"'})
                return resp

            def request_with_bulk_post(marker):
                url = 'http://%s:%s/list-bulk-collection' %(listen_ip, listen_port)
                body = {'type': 'virtual-network',
                        'parent_id': '%s,%s' %(proj1_obj.uuid, proj2_obj.uuid),
                        'page_marker': marker}
                if page_limit is not None:
                    body['page_limit'] = page_limit
                resp = requests.post(url,
                    headers={'Content-type': 'application/json; charset="UTF-8"'},
                    data=json.dumps(body))
                return resp

            for req_method in [request_with_query_params,
                               request_with_bulk_post]:
                marker = None
                for fe_obj in fetch_expects or []:
                    resp = req_method(marker)
                    if page_limit is not None and page_limit <= 0:
                        self.assertEqual(resp.status_code, 400)
                        break
                    self.assertEqual(resp.status_code, 200)
                    read_vn_ids = [vn['uuid']
                        for vn in json.loads(resp.text)['virtual-networks']]
                    self.assertEqual(len(read_vn_ids), fe_obj.num_objs)
                    marker = json.loads(resp.text)['marker']
                    self.assertEqual(marker, fe_obj.marker)
                    all_vn_ids.extend(read_vn_ids)

                if page_limit is not None and page_limit <= 0:
                    continue

                self.assertEqual(
                    set([vn.uuid for vn in vn_p1_objs+vn_p2_objs]) - set(all_vn_ids),
                    set([]))
            # end for req_method
        # end verify_collection_walk

        sorted_vn_uuid = sorted([o.uuid for o in (vn_p1_objs+vn_p2_objs)])
        FetchExpect = self.FetchExpect
        verify_collection_walk(fetch_expects=[
            FetchExpect(self.default_paginate_count,
                sorted_vn_uuid[self.default_paginate_count-1]),
            FetchExpect(3, None)])
        verify_collection_walk(page_limit=-1, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=0, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=1, fetch_expects=[
            FetchExpect(1, val) for idx, val in enumerate(sorted_vn_uuid)] +
            [FetchExpect(0, None)])
        verify_collection_walk(page_limit=2, fetch_expects=[
            FetchExpect(2, sorted_vn_uuid[1]),
            FetchExpect(2, sorted_vn_uuid[3]),
            FetchExpect(2, sorted_vn_uuid[5]),
            FetchExpect(2, sorted_vn_uuid[7]),
            FetchExpect(0, None)])
    # end test_anchored_by_parent_list

    def test_anchored_by_backref_list(self):
        proj_obj = Project('%s-project' %(self.id()))
        self._vnc_lib.project_create(proj_obj)
        vn1_obj = VirtualNetwork('vn1', parent_obj=proj_obj)
        self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_obj = VirtualNetwork('vn2', parent_obj=proj_obj)
        self._vnc_lib.virtual_network_create(vn2_obj)

        vmi_vn1_objs = self._create_vmi_collection(
            self.default_paginate_count-1, vn1_obj)
        vmi_vn2_objs = self._create_vmi_collection(
            self.default_paginate_count-1, vn2_obj)

        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        def verify_collection_walk(page_limit=None, fetch_expects=None):
            all_vmi_ids = []

            def request_with_query_params(marker):
                url = 'http://%s:%s/virtual-machine-interfaces?page_marker=%s&back_ref_id=%s,%s' %(
                    listen_ip, listen_port, marker, vn1_obj.uuid, vn2_obj.uuid)
                if page_limit is not None:
                    url += '&page_limit=%s' %(page_limit)
                resp = requests.get(url,
                    headers={'Content-type': 'application/json; charset="UTF-8"'})
                return resp

            def request_with_bulk_post(marker):
                url = 'http://%s:%s/list-bulk-collection' %(listen_ip, listen_port)
                body = {'type': 'virtual-machine-interface',
                        'back_ref_id': '%s,%s' %(vn1_obj.uuid, vn2_obj.uuid),
                        'page_marker': marker}
                if page_limit is not None:
                    body['page_limit'] = page_limit
                resp = requests.post(url,
                    headers={'Content-type': 'application/json; charset="UTF-8"'},
                    data=json.dumps(body))
                return resp

            for req_method in [request_with_query_params,
                               request_with_bulk_post]:
                marker = None
                for fe_obj in fetch_expects or []:
                    resp = req_method(marker)
                    if page_limit is not None and page_limit <= 0:
                        self.assertEqual(resp.status_code, 400)
                        break
                    self.assertEqual(resp.status_code, 200)
                    read_vmi_ids = [vmi['uuid']
                        for vmi in json.loads(resp.text)['virtual-machine-interfaces']]
                    self.assertEqual(len(read_vmi_ids), fe_obj.num_objs)
                    marker = json.loads(resp.text)['marker']
                    self.assertEqual(marker, fe_obj.marker)
                    all_vmi_ids.extend(read_vmi_ids)

                if page_limit is not None and page_limit <= 0:
                    continue

                self.assertEqual(
                    set([vmi.uuid for vmi in vmi_vn1_objs+vmi_vn2_objs]) - set(all_vmi_ids),
                    set([]))
            # end for req_method
        # end verify_collection_walk

        sorted_vmi_uuid = sorted([o.uuid for o in (vmi_vn1_objs+vmi_vn2_objs)])
        FetchExpect = self.FetchExpect
        verify_collection_walk(fetch_expects=[
            FetchExpect(self.default_paginate_count,
                sorted_vmi_uuid[self.default_paginate_count-1]),
            FetchExpect(3, None)])
        verify_collection_walk(page_limit=-1, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=0, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=1, fetch_expects=[
            FetchExpect(1, val) for idx, val in enumerate(sorted_vmi_uuid)] +
            [FetchExpect(0, None)])
        verify_collection_walk(page_limit=2, fetch_expects=[
            FetchExpect(2, sorted_vmi_uuid[1]),
            FetchExpect(2, sorted_vmi_uuid[3]),
            FetchExpect(2, sorted_vmi_uuid[5]),
            FetchExpect(2, sorted_vmi_uuid[7]),
            FetchExpect(0, None)])
    # end test_anchored_by_backref_list

    def test_by_obj_list(self):
        proj_objs = [Project('%s-proj%s' %(self.id(), i))
                     for i in range(self.default_paginate_count+2)]
        for proj_obj in proj_objs:
            self._vnc_lib.project_create(proj_obj)

        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        def verify_collection_walk(page_limit=None, fetch_expects=None):
            all_proj_ids = []

            def request_with_query_params(marker):
                url = 'http://%s:%s/projects?page_marker=%s&obj_uuids=%s' %(
                    listen_ip, listen_port, marker,
                    ','.join([o.uuid for o in proj_objs]))
                if page_limit is not None:
                    url += '&page_limit=%s' %(page_limit)
                resp = requests.get(url,
                    headers={'Content-type': 'application/json; charset="UTF-8"'})
                return resp

            def request_with_bulk_post(marker):
                url = 'http://%s:%s/list-bulk-collection' %(listen_ip, listen_port)
                body = {'type': 'project',
                        'obj_uuids': '%s' %(','.join([o.uuid for o in proj_objs])),
                        'page_marker': marker}
                if page_limit is not None:
                    body['page_limit'] = page_limit
                resp = requests.post(url,
                    headers={'Content-type': 'application/json; charset="UTF-8"'},
                    data=json.dumps(body))
                return resp

            for req_method in [request_with_query_params,
                               request_with_bulk_post]:
                marker = None
                for fe_obj in fetch_expects or []:
                    resp = req_method(marker)
                    if page_limit is not None and page_limit <= 0:
                        self.assertEqual(resp.status_code, 400)
                        break
                    self.assertEqual(resp.status_code, 200)
                    read_proj_ids = [proj['uuid']
                        for proj in json.loads(resp.text)['projects']]
                    self.assertEqual(len(read_proj_ids), fe_obj.num_objs)
                    marker = json.loads(resp.text)['marker']
                    self.assertEqual(marker, fe_obj.marker)
                    all_proj_ids.extend(read_proj_ids)

                if page_limit is not None and page_limit <= 0:
                    continue

                self.assertEqual(
                    set([proj.uuid for proj in proj_objs]) - set(all_proj_ids),
                    set([]))
            # end for req_method
        # end verify_collection_walk

        proj_uuids = [o.uuid for o in proj_objs]
        FetchExpect = self.FetchExpect
        verify_collection_walk(fetch_expects=[
            FetchExpect(self.default_paginate_count,
                proj_uuids[self.default_paginate_count-1]),
            FetchExpect(2, None)])
        verify_collection_walk(page_limit=-1, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=0, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=1, fetch_expects=[
            FetchExpect(1, val) for idx, val in enumerate(proj_uuids)] +
            [FetchExpect(0, None)])
        verify_collection_walk(page_limit=2, fetch_expects=[
            FetchExpect(2, proj_uuids[1]),
            FetchExpect(2, proj_uuids[3]),
            FetchExpect(2, proj_uuids[5]),
            FetchExpect(1, None)])
    # end test_by_obj_list

    def test_anchored_by_parent_list_shared(self):
        proj1_obj = Project('%s-project1' %(self.id()))
        self._vnc_lib.project_create(proj1_obj)
        proj2_obj = Project('%s-project2' %(self.id()))
        self._vnc_lib.project_create(proj2_obj)

        vn_p1_objs = self._create_vn_collection(
            self.default_paginate_count+1, proj1_obj)
        vn_p2_objs = self._create_vn_collection(2, proj2_obj)

        listen_ip = self._api_server_ip
        listen_port = self._api_server._args.listen_port

        # create couple of globally shared obj and verify they appear at
        # end of pagination
        proj3_obj = Project('%s-project3' %(self.id()))
        self._vnc_lib.project_create(proj3_obj)
        vn_p3_objs = self._create_vn_collection(
            2, proj3_obj)
        url = 'http://%s:%s/chmod' %(listen_ip, listen_port)
        for vn_obj in vn_p3_objs:
            body = {'uuid': vn_obj.uuid,
                    'global_access': cfgm_common.PERMS_R}
            resp = requests.post(url,
                headers={'Content-type': 'application/json; charset="UTF-8"'},
                data=json.dumps(body))

        def verify_collection_walk(page_limit=None, fetch_expects=None):
            all_vn_ids = []

            def request_with_query_params(marker):
                url = 'http://%s:%s/virtual-networks?page_marker=%s&parent_id=%s,%s&shared=True' %(
                    listen_ip, listen_port, marker, proj1_obj.uuid, proj2_obj.uuid)
                if page_limit is not None:
                    url += '&page_limit=%s' %(page_limit)
                resp = requests.get(url,
                    headers={'Content-type': 'application/json; charset="UTF-8"',
                             'X_USER_DOMAIN_ID': str(uuid.uuid4())})
                return resp

            def request_with_bulk_post(marker):
                url = 'http://%s:%s/list-bulk-collection' %(listen_ip, listen_port)
                body = {'type': 'virtual-network',
                        'parent_id': '%s,%s' %(proj1_obj.uuid, proj2_obj.uuid),
                        'page_marker': marker,
                        'shared': True}
                if page_limit is not None:
                    body['page_limit'] = page_limit
                resp = requests.post(url,
                    headers={'Content-type': 'application/json; charset="UTF-8"',
                             'X_USER_DOMAIN_ID': str(uuid.uuid4())},
                    data=json.dumps(body))
                return resp

            for req_method in [request_with_query_params,
                               request_with_bulk_post]:
                marker = None
                for fe_obj in fetch_expects or []:
                    resp = req_method(marker)
                    if page_limit is not None and page_limit <= 0:
                        self.assertEqual(resp.status_code, 400)
                        break
                    self.assertEqual(resp.status_code, 200)
                    read_vn_ids = [vn['uuid']
                        for vn in json.loads(resp.text)['virtual-networks']]
                    self.assertEqual(len(read_vn_ids), fe_obj.num_objs)
                    marker = json.loads(resp.text)['marker']
                    self.assertEqual(marker, fe_obj.marker)
                    all_vn_ids.extend(read_vn_ids)

                if page_limit is not None and page_limit <= 0:
                    continue

                self.assertEqual(
                    set([vn.uuid for vn in vn_p1_objs+vn_p2_objs+vn_p3_objs]) -
                        set(all_vn_ids),
                    set([]))
            # end for req_method
        # end verify_collection_walk

        sorted_vn_uuid = sorted([o.uuid for o in (vn_p1_objs+vn_p2_objs)])
        sorted_shared_vn_uuid = sorted([o.uuid for o in vn_p3_objs])
        FetchExpect = self.FetchExpect
        verify_collection_walk(fetch_expects=[
            FetchExpect(self.default_paginate_count,
                sorted_vn_uuid[self.default_paginate_count-1]),
            FetchExpect(self.default_paginate_count,
                'shared:%s' %(sorted_shared_vn_uuid[-1])),
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=-1, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=0, fetch_expects=[
            FetchExpect(0, None)])
        verify_collection_walk(page_limit=1, fetch_expects=[
            FetchExpect(1, val) for idx, val in enumerate(sorted_vn_uuid)] +
            [FetchExpect(1, 'shared:%s' %(val))
                for idx, val in enumerate(sorted_shared_vn_uuid)] +
            [FetchExpect(0, None)])
        verify_collection_walk(page_limit=2, fetch_expects=[
            FetchExpect(2, sorted_vn_uuid[1]),
            FetchExpect(2, sorted_vn_uuid[3]),
            FetchExpect(2, sorted_vn_uuid[5]),
            FetchExpect(2, sorted_vn_uuid[7]),
            FetchExpect(2, 'shared:%s' %(sorted_shared_vn_uuid[-1])),
            FetchExpect(0, None)])
    # end test_anchored_by_parent_list_shared
# end class TestPagination

class TestSubCluster(test_case.ApiServerTestCase):
    default_subcluster_count = 5

    def _get_rt_inst_obj(self):
        vnc_lib = self._vnc_lib

        rt_inst_obj = vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])

        return rt_inst_obj
    # end _get_rt_inst_obj

    def _get_ip(self, ip_w_pfx):
        return str(IPNetwork(ip_w_pfx).ip)
    # end _get_ip


    def test_subcluster(self):
        sub_cluster_obj = SubCluster(
            'test-host',
            sub_cluster_asn=64514)

        self._vnc_lib.sub_cluster_create(sub_cluster_obj)
        sub_cluster_obj = self._vnc_lib.sub_cluster_read(
                fq_name=sub_cluster_obj.get_fq_name())
        sub_cluster_obj.set_sub_cluster_asn(64515)
        cant_modify = False
        try:
            self._vnc_lib.sub_cluster_update(sub_cluster_obj)
        except Exception as e:
            cant_modify = True
        finally:
            self.assertTrue(cant_modify,'subcluster asn cannot be modified')
            sub_cluster_obj.set_sub_cluster_asn(64514)

        # Now that subcluster is created add a bgp router
        # with different ASN
        rt_inst_obj = self._get_rt_inst_obj()

        address_families = ['route-target', 'inet-vpn', 'e-vpn', 'erm-vpn',
                                'inet6-vpn']
        bgp_addr_fams = AddressFamilies(address_families)
        bgp_sess_attrs = [
            BgpSessionAttributes(address_families=bgp_addr_fams)]
        bgp_sessions = [BgpSession(attributes=bgp_sess_attrs)]
        bgp_peering_attrs = BgpPeeringAttributes(session=bgp_sessions)
        router_params = BgpRouterParams(router_type='external-control-node',
            vendor='unknown', autonomous_system=64515,
            identifier=self._get_ip('1.1.1.1'),
            address=self._get_ip('1.1.1.1'),
            port=179, address_families=bgp_addr_fams)

        bgp_router_obj = BgpRouter('bgp-router', rt_inst_obj,
                                   bgp_router_parameters=router_params)
        bgp_router_obj.add_sub_cluster(sub_cluster_obj)
        create_exception = False
        try:
            cur_id = self._vnc_lib.bgp_router_create(bgp_router_obj)
        except Exception as e:
            create_exception = True
        finally:
            self.assertTrue(cant_modify,'subcluster asn bgp asn should be same')

        # Now create the bgp with the same asn
        bgp_router_obj.bgp_router_parameters.autonomous_system = 64514
        try:
            cur_id = self._vnc_lib.bgp_router_create(bgp_router_obj)
        except Exception as e:
            create_exception = False
        finally:
            self.assertTrue(cant_modify,'subcluster asn bgp asn should be same')

        # Now that bgp object is created, modify asn
        bgp_router_obj = self._vnc_lib.bgp_router_read(id=cur_id)
        bgp_router_parameters = bgp_router_obj.get_bgp_router_parameters()
        bgp_router_parameters.autonomous_system = 64515
        bgp_router_obj.set_bgp_router_parameters(bgp_router_parameters)
        modify_exception = False
        try:
            self._vnc_lib.bgp_router_update(bgp_router_obj)
        except Exception as e:
            modify_exception = True
        finally:
            self.assertTrue(modify_exception,'subcluster asn bgp asn should be same')

        # Now create a new sub cluster with different asn and move bgp object
        # to that sub cluster
        sub_cluster_obj1 = SubCluster(
            'test-host1',
            sub_cluster_asn=64515)

        self._vnc_lib.sub_cluster_create(sub_cluster_obj1)
        sub_cluster_obj1 = self._vnc_lib.sub_cluster_read(
                fq_name=sub_cluster_obj1.get_fq_name())
        bgp_router_obj = self._vnc_lib.bgp_router_read(id=cur_id)
        bgp_router_parameters = bgp_router_obj.get_bgp_router_parameters()
        bgp_router_parameters.autonomous_system = 64515
        bgp_router_obj.set_bgp_router_parameters(bgp_router_parameters)
        bgp_router_obj.set_sub_cluster(sub_cluster_obj1)
        try:
            self._vnc_lib.bgp_router_update(bgp_router_obj)
        except Exception as e:
            modify_exception = False
        finally:
            self.assertTrue(modify_exception,'subcluster asn bgp asn should be same')

        # Detach subcluster from the bgp object
        bgp_router_obj = self._vnc_lib.bgp_router_read(id=cur_id)
        bgp_router_obj.del_sub_cluster(sub_cluster_obj1)
        no_delete_exception = True
        try:
            self._vnc_lib.bgp_router_update(bgp_router_obj)
        except Exception as e:
            no_delete_exception = False
        finally:
            self.assertTrue(no_delete_exception,'sub cluster couldnot be detached')
    # end test_subcluster

# end class TestSubCluster


if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    # unittest.main(failfast=True)
    unittest.main()
