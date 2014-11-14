#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import gevent
sys.path.append("../common/tests")
from testtools.matchers import Equals, Contains, Not
from test_utils import *
import test_common
import test_case

from vnc_api.vnc_api import *
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


class TestDM(test_case.DMTestCase):

    @retries(10, hook=retry_exc_handler)
    def check_netconf_config_mesg(self, xml_config_str):
        self.assertEqual(FakeNetconfManager.configs[-1], xml_config_str)
            

    def _get_ip_fabric_ri_obj(self):
        vnc_lib = self._vnc_lib

        # TODO pick fqname hardcode from common
        rt_inst_obj = vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])

        return rt_inst_obj
    # end 

    def test_basic_dm(self):
        vn1_name = 'vn1'
        vn2_name = 'vn2'
        vn1_obj = VirtualNetwork(vn1_name)
        vn2_obj = VirtualNetwork(vn2_name)

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)

        bgp_router = BgpRouter('router1', parent_obj=self._get_ip_fabric_ri_obj())
        params = BgpRouterParams()
        params.address = '127.0.0.1'
        params.address_families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn',
                                             'inet6-vpn'])
        params.autonomous_system = 64512
        params.vendor = 'mx'
        params.vnc_managed = True
        bgp_router.set_bgp_router_parameters(params)
        bgp_router_id = self._vnc_lib.bgp_router_create(bgp_router)

        pr = PhysicalRouter('router1')
        pr.physical_router_management_ip = '1.1.1.1'
        pr.physical_router_vendor_name = 'mx'
        uc = UserCredentials('user', 'pw')
        pr.set_physical_router_user_credentials(uc)
        pr.set_bgp_router(bgp_router)
        pr.set_virtual_network(vn1_obj)
        pr_id = self._vnc_lib.physical_router_create(pr)

        pi = PhysicalInterface('pi1', parent_obj = pr)
        pi_id = self._vnc_lib.physical_interface_create(pi)

        li = LogicalInterface('li1', parent_obj = pi)
        li_id = self._vnc_lib.logical_interface_create(li)
        
        for obj in [vn1_obj, vn2_obj]:
            ident_name = self.get_obj_imid(obj)
            gevent.sleep(2)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))


        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><local-address>127.0.0.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><e-vpn><unicast/></e-vpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/></routing-options><routing-instances><instance operation="replace"><name>__contrail__default-domain_default-project_vn1</name><instance-type>vrf</instance-type><vrf-target><community>target:64512:8000001</community></vrf-target><vrf-table-label/></instance></routing-instances></groups><apply-groups>__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg(xml_config_str)

        self._vnc_lib.logical_interface_delete(li.get_fq_name())
        self._vnc_lib.physical_interface_delete(pi.get_fq_name())
        self._vnc_lib.physical_router_delete(pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name operation="delete">__contrail__</name></groups></configuration></config>'
        self.check_netconf_config_mesg(xml_config_str)
    # end test_basic_dm
#end

    def test_advance_dm(self):
        vn1_name = 'vn1'
        vn2_name = 'vn2'
        vn1_obj = VirtualNetwork(vn1_name)
        vn2_obj = VirtualNetwork(vn2_name)

        ipam_obj = NetworkIpam('ipam1')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("10.0.0.0", 24))]))

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)

        bgp_router = BgpRouter('router1', parent_obj=self._get_ip_fabric_ri_obj())
        params = BgpRouterParams()
        params.address = '127.0.0.1'
        params.address_families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn',
                                             'inet6-vpn'])
        params.autonomous_system = 64512
        params.vendor = 'mx'
        params.vnc_managed = True
        bgp_router.set_bgp_router_parameters(params)
        bgp_router_id = self._vnc_lib.bgp_router_create(bgp_router)

        pr = PhysicalRouter('router1')
        pr.physical_router_management_ip = '1.1.1.1'
        pr.physical_router_vendor_name = 'mx'
        uc = UserCredentials('user', 'pw')
        pr.set_physical_router_user_credentials(uc)
        pr.set_bgp_router(bgp_router)
        pr.set_virtual_network(vn1_obj)
        pr_id = self._vnc_lib.physical_router_create(pr)

        pi = PhysicalInterface('pi1', parent_obj = pr)
        pi_id = self._vnc_lib.physical_interface_create(pi)

        fq_name = ['default-project', 'vmi1']
        vmi = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        li = LogicalInterface('li1', parent_obj = pi)
        li.vlan_tag = 100
        li.set_virtual_machine_interface(vmi)
        li_id = self._vnc_lib.logical_interface_create(li)

        
        for obj in [vn1_obj, vn2_obj]:
            ident_name = self.get_obj_imid(obj)
            gevent.sleep(2)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))


        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><local-address>127.0.0.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><e-vpn><unicast/></e-vpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/></routing-options><routing-instances><instance operation="replace"><name>__contrail__default-domain_default-project_vn1</name><instance-type>vrf</instance-type><interface><name>li1</name></interface><vrf-target><community>target:64512:8000001</community></vrf-target><vrf-table-label/><routing-options><static><route><name>10.0.0.0/24</name><discard/></route></static></routing-options></instance></routing-instances></groups><apply-groups>__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg(xml_config_str)

        self._vnc_lib.logical_interface_delete(li.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(vmi.get_fq_name())
        self._vnc_lib.physical_interface_delete(pi.get_fq_name())
        self._vnc_lib.physical_router_delete(pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name operation="delete">__contrail__</name></groups></configuration></config>'
        self.check_netconf_config_mesg(xml_config_str)
    # end test_advance_dm
#end
