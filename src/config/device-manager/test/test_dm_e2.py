#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
from __future__ import print_function
from __future__ import absolute_import
from builtins import str
from builtins import range
import sys
import gevent
from . import test_case
import xmltodict
import collections
from netaddr import IPNetwork
from vnc_api.vnc_api import *
from .test_dm_utils import fake_netconf_connect

def get_ip(ip_w_pfx):
    """get the default network IP."""
    return str(IPNetwork(ip_w_pfx).ip)
# end get_ip

try:
    import device_manager
except ImportError:
    from device_manager import device_manager

from time import sleep

def retry_exc_handler(tries_remaining, exception, delay):
    print("Caught '%s', %d tries remaining, sleeping for %s seconds" % (exception, tries_remaining, delay), file=sys.stderr)


# License: MIT
# Copyright 2012 by Jeff Laughlin Consulting LLC
# https://gist.github.com/n1ywb/2570004
def retries(max_tries, delay=1, backoff=2, exceptions=(Exception,), hook=None):
    def dec(func):
        def f2(*args, **kwargs):
            mydelay = delay
            tries = list(range(max_tries))
            tries.reverse()
            for tries_remaining in tries:
                try:
                   return func(*args, **kwargs)
                except exceptions as e:
                    if tries_remaining > 0:
                        if hook is not None:
                            hook(tries_remaining, e, mydelay)
                        sleep(mydelay)
                        mydelay = mydelay * backoff
                    else:
                        raise
                else:
                    break
        return f2
    return dec

def dictMatch(patn, real):
    """dict match pattern"""
    result = True
    if type(patn) is collections.OrderedDict:
        patn = dict(patn)
    if type(real) is collections.OrderedDict:
        real = dict(real)
    try:
        for pkey, pvalue in list(patn.items()):
            if type(pvalue) is dict or type(pvalue) is collections.OrderedDict:
                if type(real[pkey]) is list:   # it is possible if one more than config object is present
                    result = False
                    for real_item in real[pkey]:
                        result = dictMatch(pvalue, real_item)
                        if result == True:
                            break
                else:
                    result = dictMatch(pvalue, real[pkey])
            elif type(pvalue) is list:
                result = listMatch(pvalue, real[pkey])
            else:
                if real[pkey] != pvalue:
                    result = False
                    #print "dict key '%s' value '%s' not found in real '%s'\nParents - \nreal '%s'\npatn '%s' "%(pkey, pvalue, real[pkey], real, patn)
                else:
                    result = True
            if result == False:
                #print "Dict key '%s' with value '%s' not found in real '%s'\nParent: \n real '%s'patn '%s'"%(pkey, pvalue, real[pkey], real, patn)
                return result
    except (AssertionError, KeyError):
        result = False
    return result

def listMatch(patn, real):
    result = True
    try:
        for item in patn:
            if type(item) is dict or type(item) is collections.OrderedDict:
                result = False
                for real_item in real:
                    if type(real_item) is dict or type(real_item) is collections.OrderedDict:
                        result = dictMatch(item, real_item)
                        if result == True:
                            break
                if result == False:
                    #print "list Item %s not found in real %s"%(item, real)
                    return result
            elif item not in real:
                #print "list Item %s not found in real %s"%(item, real)
                result = False
    except (AssertionError, KeyError):
        result = False
    return result

class TestE2DM(test_case.DMTestCase):

    def __init__(self, *args, **kwargs):
        self.product = "mx"
        super(TestE2DM, self).__init__(*args, **kwargs)

    def setUp(self):
        super(TestE2DM, self).setUp()
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
        super(TestE2DM, self).tearDown()
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

    @retries(10, hook=retry_exc_handler)
    def check_netconf_config_mesg(self, target, xml_config_str):
        manager = fake_netconf_connect(target)
        gen_cfg = xmltodict.parse(manager.configs[-1])
        expect_cfg = xmltodict.parse(xml_config_str)
        result = dictMatch(expect_cfg, gen_cfg)
        self.assertTrue(result)

    def test_dm_physical_router_mode_and_params(self):
        bgp_router, pr = self.create_router(self.id() + 'router1', '1.1.1.1')
        bgp_router.get_bgp_router_parameters().set_hold_time(100)
        self._vnc_lib.bgp_router_update(bgp_router)

        pr.physical_router_role = 'e2-access'

        pr.set_physical_router_snmp(True)
        pr.set_physical_router_lldp(True)

        self._vnc_lib.physical_router_update(pr)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail-e2__</name><snmp><interface>fxp0.0</interface><community><name>public</name><authorization>read-only</authorization></community><community><name>private</name><authorization>read-write</authorization></community></snmp><protocols><lldp><interface><name>all</name></interface></lldp><lldp-med><interface><name>all</name></interface></lldp-med></protocols></groups><apply-groups insert="first">__contrail-e2__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        self._vnc_lib.physical_router_delete(pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(bgp_router.get_fq_name())
    #end test_dm_physical_router_mode_and_params

    def set_telemetry_info(self, pr):
        telemetry_config = {
            'server-ip': "10.102.144.110",
            'server-port': 3333,
            'resources': [
                         ('physical-interface', '/junos/system/linecard/interface/', 15),
                         ('logical-interface', '/junos/system/linecard/interface/logical/usage/', 15)
                         ]
        }
        tmetry = TelemetryStateInfo()
        for key, value in list(telemetry_config.items()):
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
        pr.set_telemetry_info(tmetry)
        self._vnc_lib.physical_router_update(pr)
    #end set_telemetry_info

    def test_dm_telemetry(self):
        bgp_router, pr = self.create_router(self.id() + 'router1', '1.1.1.1')
        bgp_router.get_bgp_router_parameters().set_hold_time(100)
        self._vnc_lib.bgp_router_update(bgp_router)

        pr.physical_router_role = 'e2-access'

        pr.set_physical_router_snmp(True)
        pr.set_physical_router_lldp(True)

        self.set_telemetry_info(pr)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail-e2__</name><snmp><interface>fxp0.0</interface><community><name>public</name><authorization>read-only</authorization></community><community><name>private</name><authorization>read-write</authorization></community></snmp><protocols><lldp><interface><name>all</name></interface></lldp><lldp-med><interface><name>all</name></interface></lldp-med></protocols><services><analytics><streaming-server><name>AnalyticsNode</name><remote-address>10.102.144.110</remote-address><remote-port>3333</remote-port></streaming-server><sensor><name>logical-interface</name><server-name>AnalyticsNode</server-name><export-name>logical-interface-profile</export-name><resource>/junos/system/linecard/interface/logical/usage/</resource></sensor><export-profile><name>logical-interface-profile</name><local-address>1.1.1.1</local-address><local-port>50000</local-port><reporting-rate>15</reporting-rate><format>gpb</format><transport>udp</transport></export-profile><export-profile><name>physical-interface-profile</name><local-address>1.1.1.1</local-address><local-port>50000</local-port><reporting-rate>15</reporting-rate><format>gpb</format><transport>udp</transport></export-profile><sensor><name>physical-interface</name><server-name>AnalyticsNode</server-name><export-name>physical-interface-profile</export-name><resource>/junos/system/linecard/interface/</resource></sensor></analytics></services></groups><apply-groups insert="first">__contrail-e2__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        self._vnc_lib.physical_router_delete(pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(bgp_router.get_fq_name())
    #end test_dm_telemetry

    def test_dm_e2_services(self):

        phy_rout_1_name = 'access-vmx-1'
        phy_rout_2_name = 'pe-vmx-2'
        user_cred_create = UserCredentials(username="test_user",
                                           password="test_pswd")
        user_cred_create_2 = UserCredentials(username="test_user_2",
                                             password="test_pswd_2")


        #1. peer-1 config
        mgmt_ip_1 = '10.92.36.9'
        router_id_1 = '100.100.100.100'
        loopback_ip_1 = '1.1.1.1'
        bgp_router_1 = BgpRouter(phy_rout_1_name, parent_obj=self._get_e2_ri_obj())
        params = BgpRouterParams()
        params.address = router_id_1
        params.address_families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn',
                                             'inet6-vpn'])
        params.autonomous_system = 100
        params.identifier = mgmt_ip_1
        bgp_router_1.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_create(bgp_router_1)

        phy_rout_1 = PhysicalRouter(phy_rout_1_name,
                           physical_router_user_credentials=user_cred_create)
        phy_rout_1.physical_router_role = 'e2-access'
        phy_rout_1.physical_router_management_ip = mgmt_ip_1
        phy_rout_1.physical_router_vendor_name = 'juniper'
        phy_rout_1.physical_router_product_name = 'mx'
        phy_rout_1.physical_router_vnc_managed = True
        phy_rout_1.set_physical_router_snmp(True)
        phy_rout_1.set_physical_router_lldp(True)

        phy_rout_1.set_bgp_router(bgp_router_1)

        phy_rout_1.add_annotations(KeyValuePair(key='physical-interface-pseudo-index', value=str(10)))
        self._vnc_lib.physical_router_create(phy_rout_1)

        access_intf = 'ge-0/0/0'
        phy_rout_1_pi = PhysicalInterface(access_intf, parent_obj = phy_rout_1)
        phy_rout_1_pi.add_annotations(KeyValuePair(key='physical-interface-logical-index', value=str(10)))
        self._vnc_lib.physical_interface_create(phy_rout_1_pi)

        endpoint_intf1 = 'ge-0/0/0.0'
        vlan_id = 100
        phy_rout_1_li = LogicalInterface(endpoint_intf1, parent_obj = phy_rout_1_pi)
        phy_rout_1_li.set_logical_interface_vlan_tag(vlan_id)
        self._vnc_lib.logical_interface_create(phy_rout_1_li)
        self._vnc_lib.physical_interface_update(phy_rout_1_pi)
        self.set_telemetry_info(phy_rout_1)

        #2. peer-2 config
        mgmt_ip_2 = '10.92.36.92'
        router_id_2 = '100.100.100.100'
        loopback_ip_2 = '1.1.1.1'
        bgp_router_2 = BgpRouter(phy_rout_2_name, parent_obj=self._get_e2_ri_obj())
        params = BgpRouterParams()
        params.address = router_id_2
        params.address_families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn',
                                             'inet6-vpn'])
        params.autonomous_system = 100
        params.identifier = mgmt_ip_2
        bgp_router_2.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_create(bgp_router_2)

        phy_rout_2 = PhysicalRouter(phy_rout_2_name,
                           physical_router_user_credentials=user_cred_create)
        phy_rout_2.physical_router_role = 'e2-provider'
        phy_rout_2.physical_router_management_ip = mgmt_ip_2
        phy_rout_2.physical_router_vendor_name = 'juniper'
        phy_rout_2.physical_router_product_name = 'mx'
        phy_rout_2.physical_router_vnc_managed = True
        phy_rout_2.set_physical_router_snmp(True)
        phy_rout_2.set_physical_router_lldp(True)
        phy_rout_2.set_bgp_router(bgp_router_2)

        phy_rout_2.add_annotations(KeyValuePair(key='physical-interface-pseudo-index', value=str(10)))
        self._vnc_lib.physical_router_create(phy_rout_2)

        access_intf = 'ge-0/0/0'
        phy_rout_2_pi = PhysicalInterface(access_intf, parent_obj = phy_rout_2)
        phy_rout_2_pi.add_annotations(KeyValuePair(key='physical-interface-logical-index', value=str(10)))
        self._vnc_lib.physical_interface_create(phy_rout_2_pi)

        endpoint_intf1 = 'ge-0/0/0.0'
        vlan_id = 100
        phy_rout_2_li = LogicalInterface(endpoint_intf1, parent_obj = phy_rout_2_pi)
        phy_rout_2_li.set_logical_interface_vlan_tag(vlan_id)
        self._vnc_lib.logical_interface_create(phy_rout_2_li)
        self._vnc_lib.physical_interface_update(phy_rout_2_pi)
        self.set_telemetry_info(phy_rout_2)

       #1. Create l2circuit service
        service_flavor = {
            'layer2-circuit': 'vpws-l2ckt',
        }
        service_attributes = {
            'set-config': True,
        }
        '''
        service_attributes = {
            'set-config': True,
            'as-number': '100',
            'mtu': '1400',
        }
        '''
        for skey,sval in list(service_flavor.items()):
            service_options = sval
        #Create virtual-network
        vn1_name = 'vn-e2-1'
        vn1 = VirtualNetwork(vn1_name)
        self._vnc_lib.virtual_network_create(vn1)

        #Create virtual-machine-interface
        vmi_name = 'vmi-e2-1'
        vmi1 = VirtualMachineInterface(vmi_name, parent_obj=Project())
        vmi1.add_virtual_network(vn1)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        #Create service endpoints
        sep_name1 = 'sep-e2-1'
        sep1 = ServiceEndpoint(sep_name1)
        self._vnc_lib.service_endpoint_create(sep1)

        #Create service-connection-module
        scm_name1 = 'scm-e2-1'
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
        self._vnc_lib.service_connection_module_create(scm1)

        #Attach service endpoints to service-connection-module
        sep1.set_service_connection_module(scm1)
        sep1.set_physical_router(phy_rout_1)
        self._vnc_lib.service_endpoint_update(sep1)

        #Attach service endpoints to vmi,PR
        vmi1.set_service_endpoint(sep1)
        self._vnc_lib.virtual_machine_interface_update(vmi1)
        phy_rout_1_li.set_virtual_machine_interface(vmi1)
        self._vnc_lib.logical_interface_update(phy_rout_1_li)

        #Create service object and attach to service
        soobj_name1 = 'so-e2-1'
        so_id_perms = IdPermsType(enable=True,
                               description='whether to set the config or not')
        soobj = ServiceObject(soobj_name1, id_perms=so_id_perms)
        self._vnc_lib.service_object_create(soobj)

        #Attach service endpoints to service-connection-module
        so_fqname = soobj.get_fq_name()
        scm1.set_service_object(soobj)
        self._vnc_lib.service_connection_module_update(scm1)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail-e2__</name><snmp><interface>fxp0.0</interface><community><name>public</name><authorization>read-only</authorization></community><community><name>private</name><authorization>read-write</authorization></community></snmp><interfaces><interface><name>ge-0/0/0</name><flexible-vlan-tagging/><encapsulation>flexible-ethernet-services</encapsulation><unit><name>0</name><encapsulation>vlan-ccc</encapsulation><vlan-id>100</vlan-id></unit></interface></interfaces><protocols><l2circuit><neighbor><name>200.200.200.200</name><interface><name>ge-0/0/0.0</name><virtual-circuit-id>1</virtual-circuit-id></interface></neighbor></l2circuit><lldp><interface><name>all</name></interface></lldp><lldp-med><interface><name>all</name></interface></lldp-med></protocols><services><analytics><streaming-server><name>AnalyticsNode</name><remote-address>10.102.144.110</remote-address><remote-port>3333</remote-port></streaming-server><sensor><name>logical-interface</name><server-name>AnalyticsNode</server-name><export-name>logical-interface-profile</export-name><resource>/junos/system/linecard/interface/logical/usage/</resource></sensor><export-profile><name>logical-interface-profile</name><local-address>10.92.36.9</local-address><local-port>50000</local-port><reporting-rate>15</reporting-rate><format>gpb</format><transport>udp</transport></export-profile><export-profile><name>physical-interface-profile</name><local-address>10.92.36.9</local-address><local-port>50000</local-port><reporting-rate>15</reporting-rate><format>gpb</format><transport>udp</transport></export-profile><sensor><name>physical-interface</name><server-name>AnalyticsNode</server-name><export-name>physical-interface-profile</export-name><resource>/junos/system/linecard/interface/</resource></sensor></analytics></services></groups><apply-groups insert="first">__contrail-e2__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('10.92.36.9', xml_config_str)

        self._vnc_lib.logical_interface_delete(phy_rout_1_li.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(vmi1.get_fq_name())
        self._vnc_lib.service_endpoint_delete(sep1.get_fq_name())
        self._vnc_lib.service_connection_module_delete(scm1.get_fq_name())
        self._vnc_lib.service_object_delete(soobj.get_fq_name())
        self._vnc_lib.physical_interface_delete(id=phy_rout_1_pi.uuid)
        self._vnc_lib.physical_router_delete(id=phy_rout_1.uuid)
        self._vnc_lib.bgp_router_delete(bgp_router_1.get_fq_name())

        self._vnc_lib.logical_interface_delete(phy_rout_2_li.get_fq_name())
        self._vnc_lib.physical_interface_delete(id=phy_rout_2_pi.uuid)
        self._vnc_lib.physical_router_delete(id=phy_rout_2.uuid)
        self._vnc_lib.bgp_router_delete(bgp_router_2.get_fq_name())
    #end test_dm_e2_services

    def test_dm_network_object(self):
        bgp_router, pr = self.create_router(self.id() + 'router1', '10.92.36.9')
        bgp_router.get_bgp_router_parameters().set_hold_time(100)
        self._vnc_lib.bgp_router_update(bgp_router)

        pr.physical_router_role = 'e2-access'

        pr.set_physical_router_snmp(True)
        pr.set_physical_router_lldp(True)

        self.set_telemetry_info(pr)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail-e2__</name><snmp><interface>fxp0.0</interface><community><name>public</name><authorization>read-only</authorization></community><community><name>private</name><authorization>read-write</authorization></community></snmp><protocols><lldp><interface><name>all</name></interface></lldp><lldp-med><interface><name>all</name></interface></lldp-med></protocols><services><analytics><streaming-server><name>AnalyticsNode</name><remote-address>10.102.144.110</remote-address><remote-port>3333</remote-port></streaming-server><sensor><name>logical-interface</name><server-name>AnalyticsNode</server-name><export-name>logical-interface-profile</export-name><resource>/junos/system/linecard/interface/logical/usage/</resource></sensor><export-profile><name>logical-interface-profile</name><local-address>10.92.36.9</local-address><local-port>50000</local-port><reporting-rate>15</reporting-rate><format>gpb</format><transport>udp</transport></export-profile><export-profile><name>physical-interface-profile</name><local-address>10.92.36.9</local-address><local-port>50000</local-port><reporting-rate>15</reporting-rate><format>gpb</format><transport>udp</transport></export-profile><sensor><name>physical-interface</name><server-name>AnalyticsNode</server-name><export-name>physical-interface-profile</export-name><resource>/junos/system/linecard/interface/</resource></sensor></analytics></services></groups><apply-groups insert="first">__contrail-e2__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('10.92.36.9', xml_config_str)
        #create network object for configuration fetch
        prconfig_name = 'config-router1'
        prconfig = NetworkDeviceConfig(prconfig_name)
        self._vnc_lib.network_device_config_create(prconfig)
        prconfig.set_physical_router(pr)
        self._vnc_lib.network_device_config_update(prconfig)

        self._vnc_lib.physical_router_update(pr)
        #network config will go to UVE
    #end test_dm_network_object

    def test_dm_e2_vrs(self):

        def add_vrs_client(client_info, vrr, peer_list):
            client = client_info['name']
            sp = E2ServiceProvider(client)
            sp.add_physical_router(vrr)

            # Create peer state
            for peer in peer_list:
                peer_name = client
                peer_name += '.as' + str(peer['as_number'])
                peer_name += '.' + peer['ip']
                peer_name = peer_name.replace(":", ".")

                # Create peer BGP router
                peer_br = BgpRouter(peer_name, parent_obj=self._get_e2_ri_obj())
                params = BgpRouterParams()
                params.address = get_ip(peer['ip'])
                params.vendor = 'juniper'
                params.autonomous_system = peer['as_number']
                params.identifier = get_ip(peer['ip'])
                params.address_families = AddressFamilies(['inet', 'inet6'])
                peer_br.set_bgp_router_parameters(params)
                self._vnc_lib.bgp_router_create(peer_br)

                # Create peer physical router
                peer_pr = PhysicalRouter(peer_name)
                peer_pr.physical_router_management_ip = get_ip(peer['ip'])
                peer_pr.set_bgp_router(peer_br)
                self._vnc_lib.physical_router_create(peer_pr)

                sp.add_physical_router(peer_pr)

            # Update the service-provider object
            sp.set_e2_service_provider_promiscuous(client_info['promiscuous'])

            # Create client
            self._vnc_lib.e2_service_provider_create(sp)

            # Verify client creation
            read_sp = self._vnc_lib.e2_service_provider_read(fq_name=[client])
            self.assertEqual(read_sp.e2_service_provider_promiscuous, True)

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
            self._vnc_lib.e2_service_provider_delete(sp_obj.get_fq_name())

            # Delete all the physical routers in the list
            for pr in pr_list:
                self._vnc_lib.physical_router_delete(pr.get_fq_name())

            # Delete all the BGP routers in the list
            for br in br_list:
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
                            each_sp_obj.del_peering_policy(policy_obj)
                            self._vnc_lib.e2_service_provider_update(each_sp_obj)

                self._vnc_lib.peering_policy_delete(policy_obj.get_fq_name())
        # end delete_vrs_client


        #
        # Add VRR
        #
        vrr_name = self.id() + '-e2vrr'
        user_cred_create = UserCredentials(username="test_user",
                                           password="test_pswd")
        mgmt_ip = '10.92.36.9'
        router_id = '100.100.100.100'

        # Create BGP router
        vrr_br = BgpRouter(vrr_name, parent_obj=self._get_e2_ri_obj())
        params = BgpRouterParams()
        params.address = router_id
        params.vendor = 'juniper'
        params.address_families = AddressFamilies(['inet', 'inet6'])
        params.autonomous_system = 100
        params.identifier = mgmt_ip
        vrr_br.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_create(vrr_br)

        # Create physical router
        vrr = PhysicalRouter(vrr_name, physical_router_user_credentials=user_cred_create)
        vrr.physical_router_management_ip = mgmt_ip
        vrr.physical_router_vendor_name = 'juniper'
        vrr.physical_router_product_name = 'mx'
        vrr.physical_router_vnc_managed = True
        vrr.physical_router_role = 'e2-vrr'
        vrr.set_bgp_router(vrr_br)
        self._vnc_lib.physical_router_create(vrr)

        # Verify VRR creation
        read_pr = self._vnc_lib.physical_router_read(id=vrr.uuid)
        read_br = self._vnc_lib.bgp_router_read(id=read_pr.bgp_router_refs[0]['uuid'])
        self.assertEqual(read_br.bgp_router_parameters.autonomous_system, 100)
        self.assertEqual(read_br.bgp_router_parameters.identifier, mgmt_ip)
        self.assertEqual(read_br.bgp_router_parameters.address, router_id)
        self.assertEqual(read_pr.physical_router_role, 'e2-vrr')

        # Verify VRR config
        gevent.sleep(2)
        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail-e2__</name><system><host-name>test.test_dm_e2.TestE2DM.test_dm_e2_vrs-e2vrr</host-name><services><extension-service><request-response><grpc><clear-text/><skip-authentication/></grpc></request-response></extension-service></services></system><routing-options><router-id>10.92.36.9</router-id><autonomous-system><as-number>100</as-number></autonomous-system></routing-options><policy-options><policy-statement><name>filter-global-comms</name><term><name>block-all</name><from><community>block-all-comm</community></from><then><reject/></then></term><then><next>policy</next></then></policy-statement><policy-statement><name>import-from-all-inst</name><term><name>from-any</name><from><instance-any/></from><then><accept/></then></term><then><reject/></then></policy-statement><policy-statement><name>export-none</name><term><name>from-this</name><then><reject/></then></term></policy-statement><community><name>block-all-comm</name><members>0:100</members></community><community><name>to-all-comm</name><members>100:100</members></community><community><name>to-wildcard-comm</name><members>^1:[0-9]*$</members></community><policy-statement><name>ipv4-only</name><term><name>is-ipv4</name><from><family>inet</family></from><then><next>policy</next></then></term><then><reject/></then></policy-statement><policy-statement><name>ipv6-only</name><term><name>is-ipv6</name><from><family>inet6</family></from><then><next>policy</next></then></term><then><reject/></then></policy-statement></policy-options><routing-instances><instance><name>&lt;*&gt;</name><protocols><bgp><hold-time>300</hold-time></bgp></protocols></instance></routing-instances></groups><apply-groups insert="first">__contrail-e2__</apply-groups></configuration></config>'

        self.check_netconf_config_mesg('10.92.36.9', xml_config_str)

        #
        # Add first client
        #
        client_info = {'name':'Google', \
                       'promiscuous':True}
        peer_list = [{'ip':'1.1.1.1', 'as_number':10, 'auth_key':'test_client_1'},\
                     {'ip':'2.2.2.2', 'as_number':10, 'auth_key':'test_client_1'},\
                     {'ip':'2001:1::1', 'as_number':10, 'auth_key':'test_client_1'}]
        client1 = client_info['name']
        sp1 = add_vrs_client(client_info, vrr, peer_list)

        #
        # Update client parameters
        #
        sp1.set_e2_service_provider_promiscuous(False)
        self._vnc_lib.e2_service_provider_update(sp1)

        # Verify client update
        read_sp = self._vnc_lib.e2_service_provider_read(fq_name=[client1])
        self.assertEqual(read_sp.e2_service_provider_promiscuous, False)

        #
        # Add a second client
        #
        client_info = {'name':'Oracle', \
                       'promiscuous':True}
        peer_list = [{'ip':'3.3.3.3', 'as_number':20, 'auth_key':'test_client_2'},\
                     {'ip':'4.4.4.4', 'as_number':30, 'auth_key':'test_client_2'}]
        client2 = client_info['name']
        sp2 = add_vrs_client(client_info, vrr, peer_list)

        #
        # Connect the clients
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

        # Verify client config
        gevent.sleep(2)
        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail-e2__</name><system><host-name>test.test_dm_e2.TestE2DM.test_dm_e2_vrs-e2vrr</host-name><services><extension-service><request-response><grpc><clear-text/><skip-authentication/></grpc></request-response></extension-service></services></system><routing-options><router-id>10.92.36.9</router-id><autonomous-system><as-number>100</as-number></autonomous-system></routing-options><policy-options><policy-statement><name>filter-global-comms</name><term><name>block-all</name><from><community>block-all-comm</community></from><then><reject/></then></term><then><next>policy</next></then></policy-statement><policy-statement><name>import-from-all-inst</name><term><name>from-any</name><from><instance-any/></from><then><accept/></then></term><then><reject/></then></policy-statement><policy-statement><name>export-none</name><term><name>from-this</name><then><reject/></then></term></policy-statement><community><name>block-all-comm</name><members>0:100</members></community><community><name>to-all-comm</name><members>100:100</members></community><community><name>to-wildcard-comm</name><members>^1:[0-9]*$</members></community><policy-statement><name>ipv4-only</name><term><name>is-ipv4</name><from><family>inet</family></from><then><next>policy</next></then></term><then><reject/></then></policy-statement><policy-statement><name>ipv6-only</name><term><name>is-ipv6</name><from><family>inet6</family></from><then><next>policy</next></then></term><then><reject/></then></policy-statement><policy-statement><name>filter-Oracle-as-20-comms</name><term><name>block-this-rib</name><from><community>block-Oracle-as-20-comm</community></from><then><reject/></then></term><term><name>to-this-rib</name><from><community>to-Oracle-as-20-comm</community></from><then><next>policy</next></then></term><term><name>to-all</name><from><community>to-all-comm</community></from><then><next>policy</next></then></term><term><name>to-any-specific-rib</name><from><community>to-wildcard-comm</community></from><then><reject/></then></term><then><next>policy</next></then></policy-statement><community><name>block-Oracle-as-20-comm</name><members>0:20</members></community><community><name>to-Oracle-as-20-comm</name><members>100:20</members></community><policy-statement><name>filter-Oracle-as-30-comms</name><term><name>block-this-rib</name><from><community>block-Oracle-as-30-comm</community></from><then><reject/></then></term><term><name>to-this-rib</name><from><community>to-Oracle-as-30-comm</community></from><then><next>policy</next></then></term><term><name>to-all</name><from><community>to-all-comm</community></from><then><next>policy</next></then></term><term><name>to-any-specific-rib</name><from><community>to-wildcard-comm</community></from><then><reject/></then></term><then><next>policy</next></then></policy-statement><community><name>block-Oracle-as-30-comm</name><members>0:30</members></community><community><name>to-Oracle-as-30-comm</name><members>100:30</members></community><policy-statement><name>filter-Google-as-10-comms</name><term><name>block-this-rib</name><from><community>block-Google-as-10-comm</community></from><then><reject/></then></term><term><name>to-this-rib</name><from><community>to-Google-as-10-comm</community></from><then><next>policy</next></then></term><term><name>to-all</name><from><community>to-all-comm</community></from><then><next>policy</next></then></term><term><name>to-any-specific-rib</name><from><community>to-wildcard-comm</community></from><then><reject/></then></term><then><next>policy</next></then></policy-statement><community><name>block-Google-as-10-comm</name><members>0:10</members></community><community><name>to-Google-as-10-comm</name><members>100:10</members></community></policy-options><routing-instances><instance><name>&lt;*&gt;</name><protocols><bgp><hold-time>300</hold-time></bgp></protocols></instance><instance><name>Oracle-as-20</name><instance-type>no-forwarding</instance-type><routing-options><router-id>10.92.36.9</router-id><instance-import>ipv4-only</instance-import><instance-import>filter-global-comms</instance-import><instance-import>filter-Oracle-as-20-comms</instance-import><instance-import>import-from-all-inst</instance-import></routing-options><protocols><bgp><peer-as>20</peer-as><group><name>bgp-v4-as-20</name><type>external</type><route-server-client/><mtu-discovery/><family><inet><unicast/></inet></family></group></bgp></protocols></instance><instance><name>Oracle-as-30</name><instance-type>no-forwarding</instance-type><routing-options><router-id>10.92.36.9</router-id><instance-import>ipv4-only</instance-import><instance-import>filter-global-comms</instance-import><instance-import>filter-Oracle-as-30-comms</instance-import><instance-import>import-from-all-inst</instance-import></routing-options><protocols><bgp><peer-as>30</peer-as><group><name>bgp-v4-as-30</name><type>external</type><route-server-client/><mtu-discovery/><family><inet><unicast/></inet></family></group></bgp></protocols></instance><instance><name>Oracle-as-30</name><protocols><bgp><group><name>bgp-v4-as-30</name><neighbor><name>4.4.4.4</name><forwarding-context>master</forwarding-context></neighbor></group></bgp></protocols></instance><instance><name>Oracle-as-20</name><protocols><bgp><group><name>bgp-v4-as-20</name><neighbor><name>3.3.3.3</name><forwarding-context>master</forwarding-context></neighbor></group></bgp></protocols></instance><instance><name>Google-as-10</name><instance-type>no-forwarding</instance-type><routing-options><router-id>10.92.36.9</router-id><instance-import>filter-global-comms</instance-import><instance-import>filter-Google-as-10-comms</instance-import><instance-export>export-none</instance-export></routing-options><protocols><bgp><peer-as>10</peer-as><group><name>bgp-v4-as-10</name><type>external</type><route-server-client/><mtu-discovery/><family><inet><unicast/></inet></family></group><group><name>bgp-v6-as-10</name><type>external</type><route-server-client/><mtu-discovery/><family><inet6><unicast/></inet6></family></group></bgp></protocols></instance><instance><name>Google-as-10</name><protocols><bgp><group><name>bgp-v4-as-10</name><neighbor><name>2.2.2.2</name><forwarding-context>master</forwarding-context></neighbor></group></bgp></protocols></instance><instance><name>Google-as-10</name><protocols><bgp><group><name>bgp-v4-as-10</name><neighbor><name>1.1.1.1</name><forwarding-context>master</forwarding-context></neighbor></group></bgp></protocols></instance><instance><name>Google-as-10</name><protocols><bgp><group><name>bgp-v6-as-10</name><neighbor><name>2001:1::1</name><forwarding-context>master</forwarding-context></neighbor></group></bgp></protocols></instance></routing-instances></groups><apply-groups insert="first">__contrail-e2__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('10.92.36.9', xml_config_str)

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

        #
        # Delete the first client
        #
        delete_vrs_client(sp1)

        # Verify if first client state is cleaned-up
        gevent.sleep(2)
        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail-e2__</name><system><host-name>test.test_dm_e2.TestE2DM.test_dm_e2_vrs-e2vrr</host-name><services><extension-service><request-response><grpc><clear-text/><skip-authentication/></grpc></request-response></extension-service></services></system><routing-options><router-id>10.92.36.9</router-id><autonomous-system><as-number>100</as-number></autonomous-system></routing-options><policy-options><policy-statement><name>filter-global-comms</name><term><name>block-all</name><from><community>block-all-comm</community></from><then><reject/></then></term><then><next>policy</next></then></policy-statement><policy-statement><name>import-from-all-inst</name><term><name>from-any</name><from><instance-any/></from><then><accept/></then></term><then><reject/></then></policy-statement><policy-statement><name>export-none</name><term><name>from-this</name><then><reject/></then></term></policy-statement><community><name>block-all-comm</name><members>0:100</members></community><community><name>to-all-comm</name><members>100:100</members></community><community><name>to-wildcard-comm</name><members>^1:[0-9]*$</members></community><policy-statement><name>ipv4-only</name><term><name>is-ipv4</name><from><family>inet</family></from><then><next>policy</next></then></term><then><reject/></then></policy-statement><policy-statement><name>ipv6-only</name><term><name>is-ipv6</name><from><family>inet6</family></from><then><next>policy</next></then></term><then><reject/></then></policy-statement><policy-statement><name>filter-Oracle-as-20-comms</name><term><name>block-this-rib</name><from><community>block-Oracle-as-20-comm</community></from><then><reject/></then></term><term><name>to-this-rib</name><from><community>to-Oracle-as-20-comm</community></from><then><next>policy</next></then></term><term><name>to-all</name><from><community>to-all-comm</community></from><then><next>policy</next></then></term><term><name>to-any-specific-rib</name><from><community>to-wildcard-comm</community></from><then><reject/></then></term><then><next>policy</next></then></policy-statement><community><name>block-Oracle-as-20-comm</name><members>0:20</members></community><community><name>to-Oracle-as-20-comm</name><members>100:20</members></community><policy-statement><name>filter-Oracle-as-30-comms</name><term><name>block-this-rib</name><from><community>block-Oracle-as-30-comm</community></from><then><reject/></then></term><term><name>to-this-rib</name><from><community>to-Oracle-as-30-comm</community></from><then><next>policy</next></then></term><term><name>to-all</name><from><community>to-all-comm</community></from><then><next>policy</next></then></term><term><name>to-any-specific-rib</name><from><community>to-wildcard-comm</community></from><then><reject/></then></term><then><next>policy</next></then></policy-statement><community><name>block-Oracle-as-30-comm</name><members>0:30</members></community><community><name>to-Oracle-as-30-comm</name><members>100:30</members></community></policy-options><routing-instances><instance><name>&lt;*&gt;</name><protocols><bgp><hold-time>300</hold-time></bgp></protocols></instance><instance><name>Oracle-as-20</name><instance-type>no-forwarding</instance-type><routing-options><router-id>10.92.36.9</router-id><instance-import>ipv4-only</instance-import><instance-import>filter-global-comms</instance-import><instance-import>filter-Oracle-as-20-comms</instance-import><instance-import>import-from-all-inst</instance-import></routing-options><protocols><bgp><peer-as>20</peer-as><group><name>bgp-v4-as-20</name><type>external</type><route-server-client/><mtu-discovery/><family><inet><unicast/></inet></family></group></bgp></protocols></instance><instance><name>Oracle-as-30</name><instance-type>no-forwarding</instance-type><routing-options><router-id>10.92.36.9</router-id><instance-import>ipv4-only</instance-import><instance-import>filter-global-comms</instance-import><instance-import>filter-Oracle-as-30-comms</instance-import><instance-import>import-from-all-inst</instance-import></routing-options><protocols><bgp><peer-as>30</peer-as><group><name>bgp-v4-as-30</name><type>external</type><route-server-client/><mtu-discovery/><family><inet><unicast/></inet></family></group></bgp></protocols></instance><instance><name>Oracle-as-20</name><protocols><bgp><group><name>bgp-v4-as-20</name><neighbor><name>3.3.3.3</name><forwarding-context>master</forwarding-context></neighbor></group></bgp></protocols></instance><instance><name>Oracle-as-30</name><protocols><bgp><group><name>bgp-v4-as-30</name><neighbor><name>4.4.4.4</name><forwarding-context>master</forwarding-context></neighbor></group></bgp></protocols></instance></routing-instances></groups><apply-groups insert="first">__contrail-e2__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('10.92.36.9', xml_config_str)

        #
        # Delete the second client
        #
        delete_vrs_client(sp2)

        # Verify if second client state is cleaned-up
        gevent.sleep(2)
        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail-e2__</name><system><host-name>test.test_dm_e2.TestE2DM.test_dm_e2_vrs-e2vrr</host-name><services><extension-service><request-response><grpc><clear-text/><skip-authentication/></grpc></request-response></extension-service></services></system><routing-options><router-id>10.92.36.9</router-id><autonomous-system><as-number>100</as-number></autonomous-system></routing-options><policy-options><policy-statement><name>filter-global-comms</name><term><name>block-all</name><from><community>block-all-comm</community></from><then><reject/></then></term><then><next>policy</next></then></policy-statement><policy-statement><name>import-from-all-inst</name><term><name>from-any</name><from><instance-any/></from><then><accept/></then></term><then><reject/></then></policy-statement><policy-statement><name>export-none</name><term><name>from-this</name><then><reject/></then></term></policy-statement><community><name>block-all-comm</name><members>0:100</members></community><community><name>to-all-comm</name><members>100:100</members></community><community><name>to-wildcard-comm</name><members>^1:[0-9]*$</members></community><policy-statement><name>ipv4-only</name><term><name>is-ipv4</name><from><family>inet</family></from><then><next>policy</next></then></term><then><reject/></then></policy-statement><policy-statement><name>ipv6-only</name><term><name>is-ipv6</name><from><family>inet6</family></from><then><next>policy</next></then></term><then><reject/></then></policy-statement></policy-options><routing-instances><instance><name>&lt;*&gt;</name><protocols><bgp><hold-time>300</hold-time></bgp></protocols></instance></routing-instances></groups><apply-groups insert="first">__contrail-e2__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('10.92.36.9', xml_config_str)

        #
        # Delete the VRR
        #
        self._vnc_lib.physical_router_delete(id=vrr.uuid)
        self._vnc_lib.bgp_router_delete(id=vrr_br.uuid)

    # end test_dm_e2_vrs

#end
