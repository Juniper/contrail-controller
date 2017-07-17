#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
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
from cfgm_common import vnc_cgitb
from cfgm_common import db_json_exim
vnc_cgitb.enable(format='text')

sys.path.append('../common/tests')
from test_utils import *
import test_common
import test_case

logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)

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

    def test_physical_router_mode_and_params(self):
        phy_rout_name = self.id() + '-e2-1'
        phy_rout_name_2 = self.id() + '-e2-2'
        user_cred_create = UserCredentials(username="test_user",
                                           password="test_pswd")
        user_cred_create_2 = UserCredentials(username="test_user_2",
                                             password="test_pswd_2")

        #1. Verify the router role
        phy_rout = PhysicalRouter(phy_rout_name,
                           physical_router_user_credentials=user_cred_create)
        phy_rout.physical_router_mode = 'non-contrail'
        logger.info('Creating physical-router %s', phy_rout_name)
        self._vnc_lib.physical_router_create(phy_rout)

        phy_rout_2 = PhysicalRouter(phy_rout_name_2,
                           physical_router_user_credentials=user_cred_create_2)
        phy_rout_2.physical_router_mode = 'non-contrail'
        self._vnc_lib.physical_router_create(phy_rout_2)
        logger.info('Creating physical-router %s', phy_rout_name_2)
        ret_phy_rout =  self._vnc_lib.physical_router_read(id=phy_rout.uuid)
        ret_phy_rout_2 =  self._vnc_lib.physical_router_read(id=phy_rout_2.uuid)
        self.assertEqual(ret_phy_rout.physical_router_mode, 'non-contrail')
        self.assertEqual(ret_phy_rout_2.physical_router_mode, 'non-contrail')

        #2. Verify the router properties
        pr_params = PhysicalRouterParams()
        pr_params.snmp = True   
        pr_params.lldp = True
        pr_params.as_number = 100
        pr_params.role = 'access' 
        phy_rout.set_physical_router_property(pr_params)

        telemetry_config = {
            'server-ip': "10.102.144.110",
            'server-port': 3333,
            'resources': [
                         ('physical-interface', '/junos/system/linecard/interface/', 15),
                         ('logical-interface', '/junos/system/linecard/interface/logical/usage/', 15)
                         ]
        }
        tmetry = TelemetryStateInfo()
        for key, value in telemetry_config.iteritems():                                                     
           if 'resources' in key:                                                                         
               for lst in value:
                   telemetry_resource = TelemetryResourceInfo()
                   for index, tlst in enumerate(lst):
                       if index == 0:
                           telemetry_resource.rname = str(tlst)
                       if index == 1:
                           telemetry_resource.rpath = str(tlst)                                           
                       if index == 2:
                           telemetry_resource.rrate = str(tlst)                                           
                   tmetry.resource.append(telemetry_resource)
           if 'server-ip' in key:
               tmetry.server_ip  = value
           if 'server-port' in key:
               tmetry.server_port  = value
        phy_rout.set_telemetry_info(tmetry)
        logger.info('Updating telemetry and router property on %s', phy_rout_name)
        self._vnc_lib.physical_router_update(phy_rout)

        ret_phy_rout =  self._vnc_lib.physical_router_read(id=phy_rout.uuid)
        self.assertEqual(ret_phy_rout.physical_router_property.as_number, 100)
        self.assertEqual(ret_phy_rout.physical_router_property.lldp, True)
        self.assertEqual(ret_phy_rout.physical_router_property.role, 'access')
        self.assertEqual(ret_phy_rout.physical_router_property.snmp, True)

        self.assertEqual(ret_phy_rout.telemetry_info.server_port, 3333)
        self.assertEqual(ret_phy_rout.telemetry_info.server_ip, '10.102.144.110')
        self.assertEqual(len(ret_phy_rout.telemetry_info.resource), \
                         len(telemetry_config['resources']))

        self._vnc_lib.physical_router_delete(id=phy_rout.uuid)
        self._vnc_lib.physical_router_delete(id=phy_rout_2.uuid)

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
        phy_rout.physical_router_mode = 'non-contrail'
        logger.info('Creating physical-router %s', phy_rout_name)
        self._vnc_lib.physical_router_create(phy_rout)

        phy_rout_2 = PhysicalRouter(phy_rout_name_2,
                           physical_router_user_credentials=user_cred_create_2)
        phy_rout_2.physical_router_mode = 'non-contrail'
        logger.info('Creating physical-router %s', phy_rout_name_2)
        self._vnc_lib.physical_router_create(phy_rout_2)

        access_intf = 'ge-0/0/0'
        phy_rout_pi = PhysicalInterface(access_intf, parent_obj = phy_rout)
        phy_rout_pi.set_physical_interface_logical_index(10)
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
        for skey,sval in service_flavor.items():
            service_options = sval
        vn1_name = self.id() + '-vn-e2-1'
        vn1 = VirtualNetwork(vn1_name)
        logger.info('Creating VN %s', vn1_name)
        self._vnc_lib.virtual_network_create(vn1)

        vmi_name = self.id() + '-vmi-e2-1'
        vmi1 = VirtualMachineInterface(vmi_name, parent_obj=Project())
        vmi1.add_virtual_network(vn1)
        logger.info('Creating VMI %s', vmi_name)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        sep_name1 = self.id() + '-sep-e2-1'
        sep1 = ServiceEndpoint(sep_name1)
        endPointParams = ServiceEndPointParams()                                                          
        endPointParams.service_name = sep_name1
        sep1.set_service_endpoint_info(endPointParams)                                                    
        logger.info('Creating service-endpoint %s', sep_name1)
        self._vnc_lib.service_endpoint_create(sep1)  

        scm_name1 = self.id() + '-scm-e2-1'
        scm1 = ServiceConnectionModule(scm_name1)                                                         
        connectionProp = ServiceConnectionProp()                                                          
        connectionProp.e2service       = 'p2p'
        connectionProp.service_type    = service_options                                                  
        set_config = True                                                                                 
        allocated = False
        for skey,sval in service_attributes.items():                                                      
            if skey == 'set-config':                                                                      
                set_config = sval
            else:
                resource1 = ResourceDict()
                resource1.rkey = skey
                resource1.rvalue = sval
                connectionProp.resources.append(resource1)

        connectionProp.set_config      = set_config
        scm1.set_service_connection_info(connectionProp)
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
        soobj = ServiceObject(soobj_name1)
        soobjParams = ServiceConnectionObjParams()
        soobjParams.service_object_name = 'p2p'
        soobj.set_service_object_info(soobjParams)
        logger.info('Creating service-object %s', soobj_name1)
        self._vnc_lib.service_object_create(soobj)
        
        #Attach service endpoints to service-connection-module
        so_fqname = soobj.get_fq_name()
        scm1.set_service_object(soobj)
        self._vnc_lib.service_connection_module_update(scm1)

        #Now verify all the connections
        logger.info('Verifying service-connections')
        ret_so = self._vnc_lib.service_object_read(id=soobj.uuid)
        self.assertEqual(ret_so.service_object_info.service_object_name, 'p2p')
        ret_scm = ret_so.get_service_connection_module_back_refs()
        ret_scm_entry = self._vnc_lib.service_connection_module_read(fq_name=ret_scm[0]['to'])
        ret_sep_list = ret_scm_entry.get_service_endpoint_back_refs()
        for each_sep in ret_sep_list:
            ret_sep_entry = self._vnc_lib.service_endpoint_read(fq_name=each_sep['to'])
            ret_vmis = ret_sep_entry.get_virtual_machine_interface_back_refs()
            ret_vmi_entry = self._vnc_lib.virtual_machine_interface_read(fq_name=ret_vmis[0]['to'])
            self.assertEqual(ret_scm_entry.service_connection_info.e2service, 'p2p')
            self.assertEqual(ret_scm_entry.service_connection_info.service_type, 'vpws-l2ckt')
            self.assertEqual(len(ret_scm_entry.service_connection_info.resources), 2)
            self.assertEqual(ret_sep_entry.service_endpoint_info.service_name, sep_name1)
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

# end class TestVncE2CfgApiServer

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    # unittest.main(failfast=True)
    unittest.main()
