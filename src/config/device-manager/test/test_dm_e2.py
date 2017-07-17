#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
import sys
import gevent
sys.path.append("../common/tests")
from testtools.matchers import Equals, Contains, Not
from test_utils import *
import test_common
import test_case
import xmltodict
import collections
import itertools
import copy
from unittest import skip
from vnc_api.vnc_api import *
from test_dm_utils import fake_netconf_connect

try:
    import device_manager
except ImportError:
    from device_manager import device_manager

from time import sleep

def retry_exc_handler(tries_remaining, exception, delay):
    print >> sys.stderr, "Caught '%s', %d tries remaining, sleeping for %s seconds" % (exception, tries_remaining, delay)


def retries(max_tries, delay=1, backoff=2, exceptions=(Exception,), hook=None):
    def dec(func):
        def f2(*args, **kwargs):
            mydelay = delay
            tries = range(max_tries)
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
        for pkey, pvalue in patn.iteritems():
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
        except Exception as e:
            pass
        """ create default e2 project """
        try:
            project_name = "e2-project" + domain_name
            self._project_obj = Project(project_name, self._domain_obj)
            self._vnc_lib.project_create(self._project_obj)
        except Exception as e:
            pass
        """ create default e2 network """
        try:
            ipam_name = "e2-network-ipam"
            self._netipam_obj = NetworkIpam(ipam_name, self._project_obj)
            self._vnc_lib.network_ipam_create(self._netipam_obj)
        except Exception as e:
            pass
        """ create default e2 global-system-configs """
        try:
            tenant_group_name = "e2-global-system-config"
            self._tenant_group_obj = GlobalSystemConfig(tenant_group_name, autonomous_system=64513)
            self._vnc_lib.global_system_config_create(self._tenant_group_obj)
        except Exception as e:
            pass
        """ create default e2 virtual-network """
        try:
            vn_name = "e2-vn"
            vn = None
            self._vn_obj = VirtualNetwork(vn_name, parent_obj=self._project_obj)
            self._vnc_lib.virtual_network_create(self._vn_obj)
        except Exception as e:
            pass
        """ create default e2 routing-instance """
        try:
            routing_instance_name = "e2-ri"
            routing_instance = None
            self._routing_instance = RoutingInstance(routing_instance_name, parent_obj=self._vn_obj)
            self._vnc_lib.routing_instance_create(self._routing_instance)
        except Exception as e:
            pass
        # end setUp

    def tearDown(self):
        super(TestE2DM, self).tearDown()
        """ remove default e2 routing-instance """
        self._vnc_lib.routing_instance_delete(id=self._routing_instance.uuid)
        """ remove default e2 virtual-network """
        self._vnc_lib.virtual_network_delete(id=self._vn_obj.uuid)
        """ remove default e2 global-system-configs """
        self._vnc_lib.global_system_config_delete(id=self._tenant_group_obj.uuid)
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
        bgp_router, pr = self.create_router('router1', '1.1.1.1')
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
        for key, value in telemetry_config.iteritems():
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
        bgp_router, pr = self.create_router('router1', '1.1.1.1')
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
        for skey,sval in service_flavor.items():
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
        for skey,sval in service_attributes.items():
            if skey == 'set-config':
                set_config = True
        scm_id_perms = IdPermsType(enable=set_config,
                               description='whether to set the config or not')
        scm1 = ServiceConnectionModule(scm_name1, id_perms=scm_id_perms)
        scm1.e2_service      = 'point-to-point'
        scm1.service_type    = service_options
        for skey,sval in service_attributes.items():
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

#end
