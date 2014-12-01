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
    def check_netconf_config_mesg(self, target, xml_config_str):
        manager = fake_netconf_connect(target)
        self.assertEqual(manager.configs[-1], xml_config_str)
            

    def test_basic_dm(self):
        vn1_name = 'vn1'
        vn2_name = 'vn2'
        vn1_obj = VirtualNetwork(vn1_name)
        vn2_obj = VirtualNetwork(vn2_name)

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)

        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)

        pi = PhysicalInterface('pi1', parent_obj = pr)
        pi_id = self._vnc_lib.physical_interface_create(pi)

        li = LogicalInterface('li1', parent_obj = pi)
        li_id = self._vnc_lib.logical_interface_create(li)
        
        for obj in [vn1_obj, vn2_obj]:
            ident_name = self.get_obj_imid(obj)
            gevent.sleep(2)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))


        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/></routing-options><routing-instances><instance operation="replace"><name>__contrail__default-domain_default-project_vn1</name><instance-type>vrf</instance-type><vrf-import>__contrail__default-domain_default-project_vn1-import</vrf-import><vrf-export>__contrail__default-domain_default-project_vn1-export</vrf-export><vrf-target/><vrf-table-label/></instance></routing-instances><policy-options><policy-statement><name>__contrail__default-domain_default-project_vn1-export</name><term><name>t1</name><then><community><add/><target_64512_8000001/></community><accept/></then></term></policy-statement><policy-statement><name>__contrail__default-domain_default-project_vn1-import</name><term><name>t1</name><from><community>target:64512:8000001</community></from><then><accept/></then></term><then><reject/></then></policy-statement><community><name>target_64512_8000001</name><members>target:64512:8000001</members></community></policy-options></groups><apply-groups>__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        self._vnc_lib.logical_interface_delete(li.get_fq_name())
        self._vnc_lib.physical_interface_delete(pi.get_fq_name())
        self._vnc_lib.physical_router_delete(pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name operation="delete">__contrail__</name></groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
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

        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)

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

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/></routing-options><routing-instances><instance operation="replace"><name>__contrail__default-domain_default-project_vn1</name><instance-type>vrf</instance-type><interface><name>li1</name></interface><vrf-import>__contrail__default-domain_default-project_vn1-import</vrf-import><vrf-export>__contrail__default-domain_default-project_vn1-export</vrf-export><vrf-target/><vrf-table-label/><routing-options><static><route><name>10.0.0.0/24</name><discard/></route></static><auto-export><family><inet><unicast/></inet></family></auto-export></routing-options></instance></routing-instances><policy-options><policy-statement><name>__contrail__default-domain_default-project_vn1-export</name><term><name>t1</name><then><community><add/><target_64512_8000001/></community><accept/></then></term></policy-statement><policy-statement><name>__contrail__default-domain_default-project_vn1-import</name><term><name>t1</name><from><community>target:64512:8000001</community></from><then><accept/></then></term><then><reject/></then></policy-statement><community><name>target_64512_8000001</name><members>target:64512:8000001</members></community></policy-options></groups><apply-groups>__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        self._vnc_lib.logical_interface_delete(li.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(vmi.get_fq_name())
        self._vnc_lib.physical_interface_delete(pi.get_fq_name())
        self._vnc_lib.physical_router_delete(pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name operation="delete">__contrail__</name></groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
    # end test_advance_dm

    def test_bgp_peering(self):
        bgp_router1, pr1 = self.create_router('router1', '1.1.1.1')
        bgp_router2, pr2 = self.create_router('router2', '2.2.2.2')
        families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn'])
        bgp_sess_attrs = [BgpSessionAttributes(address_families=families)]
        bgp_sessions = [BgpSession(attributes=bgp_sess_attrs)]
        bgp_peering_attrs = BgpPeeringAttributes(session=bgp_sessions)
        bgp_router1.add_bgp_router(bgp_router2, bgp_peering_attrs)
        self._vnc_lib.bgp_router_update(bgp_router1)
        gevent.sleep(2)
        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep><neighbor><name>2.2.2.2</name><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn></family></neighbor></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options></groups><apply-groups>__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>2.2.2.2</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep><neighbor><name>1.1.1.1</name><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn></family></neighbor></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options></groups><apply-groups>__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('2.2.2.2', xml_config_str)

        params = bgp_router2.get_bgp_router_parameters()
        params.autonomous_system = 64513
        bgp_router2.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_update(bgp_router2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep><neighbor><name>2.2.2.2</name><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn></family></neighbor></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options></groups><apply-groups>__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>2.2.2.2</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>2.2.2.2</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep><neighbor><name>1.1.1.1</name><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn></family></neighbor></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64513</autonomous-system></routing-options></groups><apply-groups>__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('2.2.2.2', xml_config_str)

        self._vnc_lib.physical_router_delete(pr1.get_fq_name())
        self._vnc_lib.bgp_router_delete(bgp_router1.get_fq_name())
        self._vnc_lib.physical_router_delete(pr2.get_fq_name())
        self._vnc_lib.bgp_router_delete(bgp_router2.get_fq_name())
    # end test_bgp_peering

    def test_network_policy(self):
        vn1_name = 'vn1'
        vn2_name = 'vn2'

        vn1_obj = self.create_virtual_network(vn1_name, '1.0.0.0/24')
        vn2_obj = self.create_virtual_network(vn2_name, '2.0.0.0/24')

        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)
        np = self.create_network_policy(vn1_obj, vn2_obj)

        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        vn1_uuid = self._vnc_lib.virtual_network_update(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_update(vn2_obj)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-instances><instance operation="replace"><name>__contrail__default-domain_default-project_vn1</name><instance-type>vrf</instance-type><vrf-import>__contrail__default-domain_default-project_vn1-import</vrf-import><vrf-export>__contrail__default-domain_default-project_vn1-export</vrf-export><vrf-target/><vrf-table-label/><routing-options><static><route><name>1.0.0.0/24</name><discard/></route></static><auto-export><family><inet><unicast/></inet></family></auto-export></routing-options></instance></routing-instances><policy-options><policy-statement><name>__contrail__default-domain_default-project_vn1-export</name><term><name>t1</name><then><community><add/><target_64512_8000001/><target_64512_8000002/></community><accept/></then></term></policy-statement><policy-statement><name>__contrail__default-domain_default-project_vn1-import</name><term><name>t1</name><from><community>target_64512_8000001</community><community>target_64512_8000002</community></from><then><accept/></then></term><then><reject/></then></policy-statement><community><name>target_64512_8000001</name><members>target:64512:8000001</members></community><community><name>target_64512_8000002</name><members>target:64512:8000002</members></community></policy-options></groups><apply-groups>__contrail__</apply-groups></configuration></config>' 
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
#end
