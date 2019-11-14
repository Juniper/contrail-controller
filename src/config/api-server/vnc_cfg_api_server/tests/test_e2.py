from __future__ import absolute_import
#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import os
import sys
import socket
import errno
import uuid
import logging
import tempfile

import fixtures
import testtools
from testtools.matchers import Equals, MismatchError, Not, Contains, LessThan
from testtools import content, content_type, ExpectedException
import unittest
import re
import json
import copy
from lxml import etree

from vnc_api.vnc_api import *
from cfgm_common import exceptions as vnc_exceptions
import vnc_api.gen.vnc_api_test_gen
from vnc_api.gen.resource_test import *
import cfgm_common
from cfgm_common import vnc_plugin_base
from cfgm_common import vnc_cgitb
from cfgm_common import db_json_exim
vnc_cgitb.enable(format='text')

from . import test_case
from netaddr import IPNetwork

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

def get_ip(ip_w_pfx):
    """get the default network IP."""
    return str(IPNetwork(ip_w_pfx).ip)
# end get_ip

class TestVncE2CfgApiServer(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestVncE2CfgApiServer, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestVncE2CfgApiServer, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def setUp(self):
        super(TestVncE2CfgApiServer, self).setUp()
        """ create e2 domain """
        try:
            domain_name = "admin"
            self._domain_obj = Domain(domain_name)
            self._vnc_lib.domain_create(self._domain_obj)
        except RefsExistError:
            self._domain_obj = self._vnc_lib.domain_read(fq_name=[domain_name])
        """ create default e2 project """
        try:
            project_name = "e2-project" + domain_name
            self._project_obj = Project(project_name, self._domain_obj)
            self._vnc_lib.project_create(self._project_obj)
        except RefsExistError:
            self._project_obj = \
                    self._vnc_lib.project_read(fq_name=[project_name])
        """ create default e2 network """
        try:
            ipam_name = "e2-network-ipam"
            self._netipam_obj = NetworkIpam(ipam_name, self._project_obj)
            self._vnc_lib.network_ipam_create(self._netipam_obj)
        except RefsExistError:
            self._netipam_obj = \
                    self._vnc_lib.network_ipam_read(fq_name=[ipam_name])
        """ create default e2 virtual-network """
        try:
            vn_name = "e2-vn"
            vn = None
            self._vn_obj = VirtualNetwork(vn_name, parent_obj=self._project_obj)
            self._vnc_lib.virtual_network_create(self._vn_obj)
        except RefsExistError:
            self._vn_obj = \
            self._vnc_lib.virtual_network_read(fq_name=[vn_name])
        """ create default e2 routing-instance """
        try:
            routing_instance_name = "e2-ri"
            routing_instance = None
            self._routing_instance = \
                 RoutingInstance(routing_instance_name, parent_obj=self._vn_obj)
            self._vnc_lib.routing_instance_create(self._routing_instance)
        except RefsExistError:
            self._routing_instance = \
            self._vnc_lib.routing_instance_read(fq_name=[routing_instance_name])
        # end setUp

    def tearDown(self):
        super(TestVncE2CfgApiServer, self).tearDown()
        """ remove default e2 routing-instance """
        self._vnc_lib.routing_instance_delete(id=self._routing_instance.uuid)
        """ remove default e2 virtual-network """
        self._vnc_lib.virtual_network_delete(id=self._vn_obj.uuid)
        """ remove default e2 network """
        self._vnc_lib.network_ipam_delete(id=self._netipam_obj.uuid)
        """ remove default e2 project """
        self._vnc_lib.project_delete(id=self._project_obj.uuid)
        """ remove e2 domain """
        self._vnc_lib.domain_delete(id=self._domain_obj.uuid)
    # end tearDown

    def _get_e2_ri_obj(self):
        rt_inst_obj = self._vnc_lib.routing_instance_read(
            id=self._routing_instance.uuid)
        return rt_inst_obj

    def test_physical_router_mode_and_params(self):

        phy_rout_name = self.id() + '-e2-1'
        phy_rout_name_2 = self.id() + '-e2-2'
        user_cred_create = UserCredentials(username="test_user",
                                           password="test_pswd")
        user_cred_create_2 = UserCredentials(username="test_user_2",
                                             password="test_pswd_2")

        mgmt_ip = '10.92.36.9'
        router_id = '100.100.100.100'
        loopback_ip = '1.1.1.1'
        #1. Create BGP router
        bgp_router = BgpRouter(phy_rout_name, parent_obj=self._get_e2_ri_obj())
        params = BgpRouterParams()
        params.address = router_id
        params.address_families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn',
                                             'inet6-vpn'])
        params.autonomous_system = 100
        params.identifier = mgmt_ip
        bgp_router.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_create(bgp_router)

        #2. Verify the router role
        phy_rout = PhysicalRouter(phy_rout_name,
                           physical_router_user_credentials=user_cred_create)
        phy_rout.physical_router_role = 'e2-access'
        logger.info('Creating physical-router %s', phy_rout_name)
        phy_rout.set_bgp_router(bgp_router)

        phy_rout.add_annotations(KeyValuePair(key='physical-interface-pseudo-index', value=str(10)))
        self._vnc_lib.physical_router_create(phy_rout)

        phy_rout_2 = PhysicalRouter(phy_rout_name_2,
                           physical_router_user_credentials=user_cred_create_2)
        phy_rout_2.physical_router_role = 'e2-provider'
        self._vnc_lib.physical_router_create(phy_rout_2)
        logger.info('Creating physical-router %s', phy_rout_name_2)
        ret_phy_rout =  self._vnc_lib.physical_router_read(id=phy_rout.uuid)
        ret_phy_rout_2 =  self._vnc_lib.physical_router_read(id=phy_rout_2.uuid)

        self.assertEqual(ret_phy_rout.physical_router_role, 'e2-access')
        ret_bgp_router = self._vnc_lib.bgp_router_read(id=ret_phy_rout.bgp_router_refs[0]['uuid'])
        self.assertEqual(ret_bgp_router.bgp_router_parameters.autonomous_system, 100)
        self.assertEqual(ret_bgp_router.bgp_router_parameters.identifier, mgmt_ip)
        self.assertEqual(ret_bgp_router.bgp_router_parameters.address, router_id)
        self.assertEqual(ret_phy_rout.physical_router_role, 'e2-access')

        self.assertEqual(ret_phy_rout_2.physical_router_role, 'e2-provider')

        #3. Verify the router properties
        phy_rout.set_physical_router_snmp(True)
        phy_rout.set_physical_router_lldp(True)
        self._vnc_lib.physical_router_update(phy_rout)

        telemetry_config = {
            'server-ip': "10.102.144.110",
            'server-port': 3333,
            'resources': [
                         ('physical-interface', '/junos/system/linecard/interface/', 15),
                         ('logical-interface', '/junos/system/linecard/interface/logical/usage/', 15)
                         ]
        }
        tmetry = TelemetryStateInfo()
        for key, value in telemetry_config.items():
           if 'resources' in key:
               for lst in value:
                   telemetry_resource = TelemetryResourceInfo()
                   for index, tlst in enumerate(lst):
                       if index == 0:
                           telemetry_resource.name = str(tlst)
                       if index == 1:
                           telemetry_resource.path = str(tlst)
                       if index == 2:
                           telemetry_resource.rate = str(tlst)
                   tmetry.resource.append(telemetry_resource)
           if 'server-ip' in key:
               tmetry.server_ip  = value
           if 'server-port' in key:
               tmetry.server_port  = value
        phy_rout.set_telemetry_info(tmetry)
        logger.info('Updating telemetry and router property on %s', phy_rout_name)
        self._vnc_lib.physical_router_update(phy_rout)

        ret_phy_rout =  self._vnc_lib.physical_router_read(id=phy_rout.uuid)
        self.assertEqual(ret_phy_rout.physical_router_lldp, True)
        self.assertEqual(ret_phy_rout.physical_router_snmp, True)

        self.assertEqual(ret_phy_rout.telemetry_info.server_port, 3333)
        self.assertEqual(ret_phy_rout.telemetry_info.server_ip, '10.102.144.110')
        self.assertEqual(len(ret_phy_rout.telemetry_info.resource), \
                         len(telemetry_config['resources']))

        self._vnc_lib.physical_router_delete(id=phy_rout.uuid)
        self._vnc_lib.physical_router_delete(id=phy_rout_2.uuid)

        self._vnc_lib.bgp_router_delete(id=bgp_router.uuid)

    # end test_physical_router_mode_and_params

    def test_e2_services(self):

        phy_rout_name = self.id() + '-e2-1'
        phy_rout_name_2 = self.id() + '-e2-2'
        user_cred_create = UserCredentials(username="test_user",
                                           password="test_pswd")
        user_cred_create_2 = UserCredentials(username="test_user_2",
                                             password="test_pswd_2")

        phy_rout = PhysicalRouter(phy_rout_name,
                           physical_router_user_credentials=user_cred_create)
        phy_rout.physical_router_role = 'e2-access'
        logger.info('Creating physical-router %s', phy_rout_name)
        self._vnc_lib.physical_router_create(phy_rout)

        phy_rout_2 = PhysicalRouter(phy_rout_name_2,
                           physical_router_user_credentials=user_cred_create_2)
        phy_rout_2.physical_router_role = 'e2-provider'
        logger.info('Creating physical-router %s', phy_rout_name_2)
        self._vnc_lib.physical_router_create(phy_rout_2)

        access_intf = 'ge-0/0/0'
        phy_rout_pi = PhysicalInterface(access_intf, parent_obj = phy_rout)
        phy_rout.add_annotations(KeyValuePair(key='physical-interface-logical-index', value=str(10)))
        logger.info('Creating physical-interface %s', access_intf)
        self._vnc_lib.physical_interface_create(phy_rout_pi)

        endpoint_intf1 = 'ge-0/0/0.0'
        vlan_id = 100
        phy_rout_li = LogicalInterface(endpoint_intf1, parent_obj = phy_rout_pi)
        phy_rout_li.set_logical_interface_vlan_tag(vlan_id)
        logger.info('Creating logical-interface %s', endpoint_intf1)
        self._vnc_lib.logical_interface_create(phy_rout_li)
        self._vnc_lib.physical_interface_update(phy_rout_pi)

        service_flavor = {
            'layer2-circuit': 'vpws-l2ckt',
        }
        service_attributes = {
            'set-config': True,
            'as-number': '100',
            'mtu': '1400',
        }
        for skey,sval in list(service_flavor.items()):
            service_options = sval
        #Create virtual-network
        vn1_name = self.id() + '-vn-e2-1'
        vn1 = VirtualNetwork(vn1_name)
        logger.info('Creating VN %s', vn1_name)
        self._vnc_lib.virtual_network_create(vn1)

        #Create virtual-machine-interface
        vmi_name = self.id() + '-vmi-e2-1'
        vmi1 = VirtualMachineInterface(vmi_name, parent_obj=Project())
        vmi1.add_virtual_network(vn1)
        logger.info('Creating VMI %s', vmi_name)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        #Create service endpoints
        sep_name1 = self.id() + '-sep-e2-1'
        sep1 = ServiceEndpoint(sep_name1)
        logger.info('Creating service-endpoint %s', sep_name1)
        self._vnc_lib.service_endpoint_create(sep1)

        #Create service-connection-module
        scm_name1 = self.id() + '-scm-e2-1'
        set_config = False
        for skey,sval in list(service_attributes.items()):
            if skey == 'set-config':
                set_config = True
        scm_id_perms = IdPermsType(enable=set_config,
                               description='whether to set the config or not')
        scm1 = ServiceConnectionModule(scm_name1, id_perms=scm_id_perms)
        scm1.e2_service      = 'point-to-point'
        scm1.service_type    = service_options
        for skey,sval in list(service_attributes.items()):
            if skey != 'set-config':
                scm1.add_annotations(KeyValuePair(key=skey, value=sval))
        logger.info('Creating service-connection-module %s', scm_name1)
        self._vnc_lib.service_connection_module_create(scm1)

        #Attach service endpoints to service-connection-module
        sep1.set_service_connection_module(scm1)
        sep1.set_physical_router(phy_rout)
        self._vnc_lib.service_endpoint_update(sep1)

        #Attach service endpoints to vmi,PR
        vmi1.set_service_endpoint(sep1)
        self._vnc_lib.virtual_machine_interface_update(vmi1)
        phy_rout_li.set_virtual_machine_interface(vmi1)
        self._vnc_lib.logical_interface_update(phy_rout_li)

        #Create service object and attach to service
        soobj_name1 = self.id() + '-so-e2-1'
        so_id_perms = IdPermsType(enable=True,
                               description='whether to set the config or not')
        soobj = ServiceObject(soobj_name1, id_perms=so_id_perms)
        logger.info('Creating service-object %s', soobj_name1)
        self._vnc_lib.service_object_create(soobj)

        #Attach service endpoints to service-connection-module
        so_fqname = soobj.get_fq_name()
        scm1.set_service_object(soobj)
        self._vnc_lib.service_connection_module_update(scm1)

        #Now verify all the connections
        logger.info('Verifying service-connections')
        ret_so = self._vnc_lib.service_object_read(id=soobj.uuid)
        self.assertEqual(ret_so.get_fq_name(), so_fqname)
        ret_so_id_perms = ret_so.get_id_perms()
        ret_scm = ret_so.get_service_connection_module_back_refs()
        ret_scm_entry = self._vnc_lib.service_connection_module_read(fq_name=ret_scm[0]['to'])
        ret_scm_entry_id_perms = ret_scm_entry.get_id_perms()
        self.assertEqual(ret_scm_entry.e2_service, 'point-to-point')
        self.assertEqual(ret_scm_entry.service_type, 'vpws-l2ckt')
        self.assertEqual(len(ret_scm_entry.annotations.key_value_pair), 2)
        ret_sep_list = ret_scm_entry.get_service_endpoint_back_refs()
        for each_sep in ret_sep_list:
            ret_sep_entry = self._vnc_lib.service_endpoint_read(fq_name=each_sep['to'])
            self.assertEqual(ret_sep_entry.get_fq_name_str(), str(sep_name1))
            ret_vmis = ret_sep_entry.get_virtual_machine_interface_back_refs()
            ret_vmi_entry = self._vnc_lib.virtual_machine_interface_read(fq_name=ret_vmis[0]['to'])
            ret_lis = ret_vmi_entry.get_logical_interface_back_refs()
            for each_li in ret_lis:
                ret_li = self._vnc_lib.logical_interface_read(fq_name=each_li['to'])
                self.assertEqual(ret_li.logical_interface_vlan_tag, 100)

        #delete and verify it's deleted
        logger.info('Deleting services, and physical-routers %s', phy_rout_name)
        self._vnc_lib.logical_interface_delete(ret_li.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(ret_vmi_entry.get_fq_name())
        self._vnc_lib.service_endpoint_delete(ret_sep_entry.get_fq_name())
        self._vnc_lib.service_connection_module_delete(ret_scm_entry.get_fq_name())
        self._vnc_lib.service_object_delete(ret_so.get_fq_name())
        self._vnc_lib.physical_interface_delete(id=phy_rout_pi.uuid)
        self._vnc_lib.physical_router_delete(id=phy_rout.uuid)
        self._vnc_lib.physical_router_delete(id=phy_rout_2.uuid)

    # end test_e2_services

    def test_e2_vrs(self):

        def add_vrs_client(client_info, vrr, peer_list):
            client = client_info['name']
            sp = E2ServiceProvider(client)
            sp.add_physical_router(vrr)

            # Create peer state
            for peer in peer_list:
                peer_name = client
                peer_name += '.as' + str(peer['as_number'])
                peer_name += '.' + peer['ip']

                # Create peer BGP router
                peer_br = BgpRouter(peer_name, parent_obj=self._get_e2_ri_obj())
                params = BgpRouterParams()
                params.address = get_ip(peer['ip'])
                params.autonomous_system = peer['as_number']
                params.identifier = get_ip(peer['ip'])
                params.address_families = AddressFamilies(['inet', 'inet6'])
                peer_br.set_bgp_router_parameters(params)
                logger.info('Creating BGP router %s', peer_name)
                self._vnc_lib.bgp_router_create(peer_br)

                # Create peer physical router
                peer_pr = PhysicalRouter(peer_name)
                peer_pr.physical_router_management_ip = get_ip(peer['ip'])
                peer_pr.set_bgp_router(peer_br)
                logger.info('Creating physical-router %s', peer_name)
                self._vnc_lib.physical_router_create(peer_pr)

                sp.add_physical_router(peer_pr)

            # Update the service-provider object
            sp.set_e2_service_provider_promiscuous(client_info['promiscuous'])

            # Create client
            self._vnc_lib.e2_service_provider_create(sp)
            logger.info('Created all state for client %s', client)

            # Verify client creation
            read_sp = self._vnc_lib.e2_service_provider_read(fq_name=[client])
            self.assertEqual(read_sp.e2_service_provider_promiscuous, True)
            logger.info('Client verification success for client %s', client)

            return sp
        # end add_vrs_client

        def delete_vrs_client(sp_obj):
            pr_list = []
            br_list = []
            policy_list = []

            # Get the physical routers referred to
            pr_refs = sp_obj.get_physical_router_refs()
            if pr_refs is not None:
                for pr_ref in pr_refs:
                    pr = None
                    try:
                        pr = self._vnc_lib.physical_router_read(pr_ref['to'])
                    except NoIdError:
                        pass
                    if pr is not None:
                        # Exclude the VRR physical router
                        if pr.physical_router_role == "e2-vrr":
                            continue
                        pr_list.append(pr)

                        # Get the BGP routers referred to
                        bgp_refs = pr.get_bgp_router_refs()
                        if bgp_refs is not None:
                            for bgp_ref in bgp_refs:
                                br = None
                                try:
                                    br = self._vnc_lib.bgp_router_read(bgp_ref['to'])
                                except NoIdError:
                                    pass
                                if br is not None:
                                    br_list.append(br)

            # Get the policy objects referred to
            policy_refs = sp_obj.get_peering_policy_refs()
            if policy_refs is not None:
                for policy_ref in policy_refs:
                    # Get the policy object
                    policy_obj = None
                    try:
                        policy_obj = self._vnc_lib.peering_policy_read(policy_ref['to'])
                    except NoIdError:
                        pass
                    if policy_obj is not None:
                        policy_list.append(policy_obj)

            # Delete the service-provider-endpoint object
            logger.info('Deleting client %s', sp_obj.get_fq_name()[0])
            self._vnc_lib.e2_service_provider_delete(sp_obj.get_fq_name())

            # Delete all the physical routers in the list
            for pr in pr_list:
                logger.info('Deleting physical router %s', pr.get_display_name())
                self._vnc_lib.physical_router_delete(pr.get_fq_name())

            # Delete all the BGP routers in the list
            for br in br_list:
                logger.info('Deleting BGP router %s', br.get_display_name())
                self._vnc_lib.bgp_router_delete(br.get_fq_name())

            # Delete all the policy objects in the list
            for policy_obj in policy_list:
                # Get all service-providers and disconnect them
                sp_list = policy_obj.get_e2_service_provider_back_refs()

                if sp_list is not None:
                    for each_sp in sp_list:
                        # Get the service-provider object
                        each_sp_obj = None
                        try:
                            each_sp_obj = self._vnc_lib.e2_service_provider_read(id=each_sp['uuid'])
                        except NoIdError:
                            pass
                        if each_sp_obj is not None:
                            logger.info('Disconnecting policy %s from client %s', \
                                         policy_obj.get_display_name(), \
                                         each_sp_obj.get_display_name())
                            each_sp_obj.del_peering_policy(policy_obj)
                            self._vnc_lib.e2_service_provider_update(each_sp_obj)

                logger.info('Deleting policy %s', policy_obj.get_display_name())
                self._vnc_lib.peering_policy_delete(policy_obj.get_fq_name())
        # end delete_vrs_client

        logger.info('Verifying E2 VRS')

        #
        # Add VRR
        #
        logger.info(' ')
        vrr_name = self.id() + '-e2-vrr'
        user_cred_create = UserCredentials(username="test_user",
                                           password="test_pswd")
        mgmt_ip = '10.92.36.9'
        router_id = '100.100.100.100'

        # Create BGP router
        vrr_br = BgpRouter(vrr_name, parent_obj=self._get_e2_ri_obj())
        params = BgpRouterParams()
        params.address = router_id
        params.address_families = AddressFamilies(['inet', 'inet6'])
        params.autonomous_system = 100
        params.identifier = mgmt_ip
        vrr_br.set_bgp_router_parameters(params)
        logger.info('Creating BGP router %s', vrr_name)
        self._vnc_lib.bgp_router_create(vrr_br)

        # Create physical router
        vrr = PhysicalRouter(vrr_name, physical_router_user_credentials=user_cred_create)
        vrr.physical_router_role = 'e2-vrr'
        vrr.set_bgp_router(vrr_br)
        logger.info('Creating physical-router %s', vrr_name)
        self._vnc_lib.physical_router_create(vrr)

        # Verify VRR creation
        read_pr = self._vnc_lib.physical_router_read(id=vrr.uuid)
        read_br = self._vnc_lib.bgp_router_read(id=read_pr.bgp_router_refs[0]['uuid'])
        self.assertEqual(read_br.bgp_router_parameters.autonomous_system, 100)
        self.assertEqual(read_br.bgp_router_parameters.identifier, mgmt_ip)
        self.assertEqual(read_br.bgp_router_parameters.address, router_id)
        self.assertEqual(read_pr.physical_router_role, 'e2-vrr')
        logger.info('VRR verification success !')
        logger.info(' ')

        #
        # Add first client
        #
        client_info = {'name':'Google', \
                       'promiscuous':True}
        peer_list = [{'ip':'1.1.1.1', 'as_number':10, 'auth_key':'test_client_1'},\
                     {'ip':'2.2.2.2', 'as_number':10, 'auth_key':'test_client_1'}]
        client1 = client_info['name']
        sp1 = add_vrs_client(client_info, vrr, peer_list)
        logger.info(' ')

        #
        # Update client parameters
        #
        sp1.set_e2_service_provider_promiscuous(False)
        self._vnc_lib.e2_service_provider_update(sp1)

        # Verify client update
        read_sp = self._vnc_lib.e2_service_provider_read(fq_name=[client1])
        self.assertEqual(read_sp.e2_service_provider_promiscuous, False)
        logger.info('Client update success for client %s', client1)
        logger.info(' ')

        #
        # Add a second client
        #
        client_info = {'name':'Oracle', \
                       'promiscuous':True}
        peer_list = [{'ip':'3.3.3.3', 'as_number':20, 'auth_key':'test_client_2'},\
                     {'ip':'4.4.4.4', 'as_number':30, 'auth_key':'test_client_2'}]
        client2 = client_info['name']
        sp2 = add_vrs_client(client_info, vrr, peer_list)
        logger.info(' ')

        #
        # Connect the clients
        #
        policy_name = 'policy.' + client1 + '.' + client2

        # Create peering policy
        policy_obj = PeeringPolicy(policy_name)
        policy_obj.peering_service = 'public-peering'
        self._vnc_lib.peering_policy_create(policy_obj)

        # Connect the clients to policy object and update
        logger.info('Connecting %s and %s using policy %s', client1, client2, policy_name)
        sp1.add_peering_policy(policy_obj)
        self._vnc_lib.e2_service_provider_update(sp1)
        sp2.add_peering_policy(policy_obj)
        self._vnc_lib.e2_service_provider_update(sp2)

        # Verify connection status
        read_policy = self._vnc_lib.peering_policy_read(fq_name=[policy_name])
        client_list = [client1, client2]
        read_sp_list = read_policy.get_e2_service_provider_back_refs()
        if read_sp_list is not None:
            self.assertEqual(len(read_sp_list), 2)
            for read_sp in read_sp_list:
                uclient = read_sp['to'][0]
                uclient.encode("utf-8")
                self.assertIn(uclient, client_list)
                logger.info('Policy is connected to client %s', uclient)
        logger.info(' ')

        #
        # Disconnect the clients
        #
        sp1.del_peering_policy(policy_obj)
        self._vnc_lib.e2_service_provider_update(sp1)
        sp2.del_peering_policy(policy_obj)
        self._vnc_lib.e2_service_provider_update(sp2)

        # Verify dis-connect
        read_policy = self._vnc_lib.peering_policy_read(fq_name=[policy_name])
        read_sp_list = read_policy.get_e2_service_provider_back_refs()
        self.assertEqual(read_sp_list, None)

        # Delete policy
        self._vnc_lib.peering_policy_delete(policy_obj.get_fq_name())
        logger.info('Disconnected %s and %s', client1, client2)
        logger.info(' ')

        #
        # Connect the clients again
        # This time we will cleanup using delete client
        #
        policy_name = 'policy.' + client1 + '.' + client2

        # Create peering policy
        policy_obj = PeeringPolicy(policy_name)
        policy_obj.peering_service = 'public-peering'
        self._vnc_lib.peering_policy_create(policy_obj)

        # Connect the clients to policy object and update
        sp1.add_peering_policy(policy_obj)
        self._vnc_lib.e2_service_provider_update(sp1)
        sp2.add_peering_policy(policy_obj)
        self._vnc_lib.e2_service_provider_update(sp2)
        logger.info('Connected %s and %s using policy %s', client1, client2, policy_name)
        logger.info(' ')

        #
        # Delete the first client
        #
        delete_vrs_client(sp1)
        logger.info(' ')

        #
        # Delete the second client
        #
        delete_vrs_client(sp2)
        logger.info(' ')

        #
        # Delete the VRR
        #
        logger.info('Deleting VRR physical-router %s', vrr_name)
        self._vnc_lib.physical_router_delete(id=vrr.uuid)
        logger.info('Deleting VRR BGP router %s', vrr_name)
        self._vnc_lib.bgp_router_delete(id=vrr_br.uuid)

    # end test_e2_vrs

# end class TestVncE2CfgApiServer

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    # unittest.main(failfast=True)
    unittest.main()
