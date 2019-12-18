#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from __future__ import print_function
from __future__ import absolute_import
from builtins import range
import sys
import gevent
from . import test_case
import xmltodict
import collections
import itertools
import copy
from unittest import skip
from vnc_api.vnc_api import *
from .test_dm_utils import fake_netconf_connect

try:
    import device_manager
except ImportError:
    from device_manager import device_manager

from time import sleep

def retry_exc_handler(tries_remaining, exception, delay):
    print("Caught '%s', %d tries remaining, sleeping for %s seconds" % (exception, tries_remaining, delay), file=sys.stderr)


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

# An example usage of above dict compare utilties
#d1 = {'x': [{'a':'2', 'b':{3: ['iii', 'bb']}}]}
#d2 = {'x': [{'a':'2', 'b':{5: 'xx', 3: ['iii', 'bb'], 4: 'iv'},'c':'4'}]}
#b = dictMatch(d1, d2)   # True

def check_dep(l, key, dep):
    for i in l:
        if i == key and len(dep) > 0:
            return False
        if i in dep:
            dep.remove(i)
        if len(dep) == 0:
            return True
    return True
#end chek_dep

def get_valid_permutations(blocks, dep_map):
    seq_list = list(itertools.permutations(blocks))
    valid_seq_list = list()
    for seq in seq_list:
        good_one = True
        for key, dep in list(dep_map.items()):
            if check_dep(seq, key, copy.deepcopy(dep)) == False:
                good_one = False
                break
        if good_one == True:
            valid_seq_list.append(seq)
    return valid_seq_list

class TestDM(test_case.DMTestCase):

    @retries(10, hook=retry_exc_handler)
    def check_netconf_config_mesg(self, target, xml_config_str):
        manager = fake_netconf_connect(target)
        # convert xmls to dict and see if expected xml config is present in generated config
        # expected config is only the minimum config expected from a test case where as
        # generated config may contain more than that
        #print "\n gen: %s\n expect: %s\n"%(manager.configs[-1], xml_config_str)
        gen_cfg = xmltodict.parse(manager.configs[-1])
        expect_cfg = xmltodict.parse(xml_config_str)
        result = dictMatch(expect_cfg, gen_cfg)
        self.assertTrue(result)

    @skip("stale tests")
    def test_dm_bgp_hold_time_config(self):
        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        bgp_router.get_bgp_router_parameters().set_hold_time(100)
        self._vnc_lib.bgp_router_update(bgp_router)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>100</hold-time><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>100</hold-time><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

    @skip("stale tests")
    def test_dm_md5_auth_config(self):
        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        key = AuthenticationKeyItem(0, 'bgppswd')
        bgp_router.get_bgp_router_parameters().set_auth_data(AuthenticationData('md5', [key]))
        self._vnc_lib.bgp_router_update(bgp_router)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><authentication-key>bgppswd</authentication-key><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><authentication-key>bgppswd</authentication-key><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        #bgp peering, auth validate
        bgp_router1, pr1 = self.create_router('router2', '10.1.1.1')
        bgp_router2, pr2 = self.create_router('router3', '20.2.2.2')
        families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn'])
        key1 = AuthenticationKeyItem(0, 'bgppswd')
        auth1 = AuthenticationData('md5', [key1])
        bgp_router1.get_bgp_router_parameters().set_auth_data(auth1)
        key2 = AuthenticationKeyItem(0, 'bgppswd-neigh')
        auth2 = AuthenticationData('md5', [key2])
        bgp_sess_attrs = [BgpSessionAttributes(address_families=families, auth_data=auth2)]
        bgp_sessions = [BgpSession(attributes=bgp_sess_attrs)]
        bgp_peering_attrs = BgpPeeringAttributes(session=bgp_sessions)
        bgp_router1.add_bgp_router(bgp_router2, bgp_peering_attrs)
        self._vnc_lib.bgp_router_update(bgp_router1)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>10.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><authentication-key>bgppswd</authentication-key><keep>all</keep><neighbor><name>20.2.2.2</name><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn></family><authentication-key>bgppswd-neigh</authentication-key></neighbor></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>10.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><authentication-key>bgppswd</authentication-key><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

    #end test_dm_md5_auth_config

    @skip("stale tests")
    def test_tunnels_dm(self):
        b1 = """
gs = self._vnc_lib.global_system_config_read(fq_name=[u'default-global-system-config'])
gs.set_ip_fabric_subnets(SubnetListType([SubnetType("10.0.0.0", 24), SubnetType("20.0.0.0", 32)]))
self._vnc_lib.global_system_config_update(gs)
        """

        b2 = """
bgp_router, pr = self.create_router('router1', '1.1.1.1')
        """

        b3 = """
pr.physical_router_dataplane_ip = "5.5.5.5"
self._vnc_lib.physical_router_update(pr)
        """

        # clean up code
        b4 = """
gs = self._vnc_lib.global_system_config_read(fq_name=[u'default-global-system-config'])
gs.set_ip_fabric_subnets(SubnetListType([]))
self._vnc_lib.global_system_config_update(gs)
self._vnc_lib.physical_router_delete(pr.get_fq_name())
self._vnc_lib.bgp_router_delete(bgp_router.get_fq_name())
        """
        # set of code blocks
        code_blocks = [b1, b2, b3]
        # add dependency map
        dep_map = { b3:[b2] } #b3 is dependent on b2 execution, so b2 comes first always
        # generate valid permutations considering dependencies
        code_blocks_seq = get_valid_permutations(code_blocks, dep_map)
        cleanup_block = b4

        for code_blocks in code_blocks_seq:
            for code in code_blocks:
                obj = compile(code, '<string>', 'exec')
                exec(obj)
            gevent.sleep(2)
            xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-options><dynamic-tunnels><dynamic-tunnel><name>__contrail__</name><source-address>5.5.5.5</source-address><gre/><destination-networks><name>10.0.0.0/24</name></destination-networks><destination-networks><name>20.0.0.0/32</name></destination-networks><destination-networks><name>1.1.1.1/32</name></destination-networks></dynamic-tunnel></dynamic-tunnels></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'
            print("Checking xml tags")
            self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

            obj = compile(cleanup_block, '<string>', 'exec')
            exec(obj)
            gevent.sleep(2)

    #end test_tunnels_dm

    #dynamic tunnel test case - 1
    # 1. configure ip fabric subnets,
    # 2. create physical  router with data plane source ip
    # 3. check netconf XML config generated
    @skip("stale tests")
    def test_tunnels_dm_1(self):
        gs = self._vnc_lib.global_system_config_read(fq_name=[u'default-global-system-config'])
        gs.set_ip_fabric_subnets(SubnetListType([SubnetType("10.0.0.0", 24), SubnetType("20.0.0.0", 32)]))
        self._vnc_lib.global_system_config_update(gs)

        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        pr.physical_router_dataplane_ip = "5.5.5.5"
        self._vnc_lib.physical_router_update(pr)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-options><dynamic-tunnels><dynamic-tunnel><name>__contrail__</name><source-address>5.5.5.5</source-address><gre/><destination-networks><name>10.0.0.0/24</name></destination-networks><destination-networks><name>20.0.0.0/32</name></destination-networks><destination-networks><name>1.1.1.1/32</name></destination-networks></dynamic-tunnel></dynamic-tunnels></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'

        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

    #end test_tunnels_dm_1

    #dynamic tunnel test case - 2
    # 1. create physical  router with data plane ip
    # 2. configure ip fabric subnets,
    # 3. check netconf XML config generated
    @skip("stale tests")
    def test_tunnels_dm_2(self):

        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        pr.physical_router_dataplane_ip = "5.5.5.5"
        self._vnc_lib.physical_router_update(pr)

        gs = self._vnc_lib.global_system_config_read(fq_name=[u'default-global-system-config'])
        gs.set_ip_fabric_subnets(SubnetListType([SubnetType("10.0.0.0", 24), SubnetType("20.0.0.0", 32)]))
        self._vnc_lib.global_system_config_update(gs)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-options><dynamic-tunnels><dynamic-tunnel><name>__contrail__</name><source-address>5.5.5.5</source-address><gre/><destination-networks><name>10.0.0.0/24</name></destination-networks><destination-networks><name>20.0.0.0/32</name></destination-networks></dynamic-tunnel></dynamic-tunnels></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
    #end test_tunnels_dm_2

    #dynamic tunnel test case - 3
    # 1. create physical  router with no data plane ip
    # 2. configure ip fabric subnets,
    # 3. update physical  router data plane ip
    # 4. check netconf XML config generated
    @skip("stale tests")
    def test_tunnels_dm_3(self):
        bgp_router, pr = self.create_router('router1', '1.1.1.1')

        gs = self._vnc_lib.global_system_config_read(fq_name=[u'default-global-system-config'])
        gs.set_ip_fabric_subnets(SubnetListType([SubnetType("10.0.0.0", 24), SubnetType("20.0.0.0", 32)]))
        self._vnc_lib.global_system_config_update(gs)

        pr.physical_router_dataplane_ip = "5.5.5.5"
        self._vnc_lib.physical_router_update(pr)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-options><dynamic-tunnels><dynamic-tunnel><name>__contrail__</name><source-address>5.5.5.5</source-address><gre/><destination-networks><name>10.0.0.0/24</name></destination-networks><destination-networks><name>20.0.0.0/32</name></destination-networks></dynamic-tunnel></dynamic-tunnels></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
    #end test_tunnels_dm_3

    #dynamic tunnel test case - 4
    # 1. create physical  router with data plane ip
    # 2. configure ip fabric subnets,
    # 4. check netconf XML config generated
    # 5. modify ip fabric subnets,
    # 6. check netconf XML config generated
    # 7. remove one ip fabric subnet,
    # 8. check netconf XML config generated
    @skip("stale tests")
    def test_tunnels_dm_4(self):
        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        pr.physical_router_dataplane_ip = "5.5.5.5"
        self._vnc_lib.physical_router_update(pr)

        gs = self._vnc_lib.global_system_config_read(fq_name=[u'default-global-system-config'])
        gs.set_ip_fabric_subnets(SubnetListType([SubnetType("10.0.0.0", 24), SubnetType("20.0.0.0", 32)]))
        self._vnc_lib.global_system_config_update(gs)


        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-options><dynamic-tunnels><dynamic-tunnel><name>__contrail__</name><source-address>5.5.5.5</source-address><gre/><destination-networks><name>10.0.0.0/24</name></destination-networks><destination-networks><name>20.0.0.0/32</name></destination-networks></dynamic-tunnel></dynamic-tunnels></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        gs = self._vnc_lib.global_system_config_read(fq_name=[u'default-global-system-config'])
        gs.set_ip_fabric_subnets(SubnetListType([SubnetType("30.0.0.0", 24), SubnetType("40.0.0.0", 32)]))
        self._vnc_lib.global_system_config_update(gs)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-options><dynamic-tunnels><dynamic-tunnel><name>__contrail__</name><source-address>5.5.5.5</source-address><gre/><destination-networks><name>30.0.0.0/24</name></destination-networks><destination-networks><name>40.0.0.0/32</name></destination-networks></dynamic-tunnel></dynamic-tunnels></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        gs = self._vnc_lib.global_system_config_read(fq_name=[u'default-global-system-config'])
        gs.set_ip_fabric_subnets(SubnetListType([SubnetType("30.0.0.0", 24)]))
        self._vnc_lib.global_system_config_update(gs)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-options><dynamic-tunnels><dynamic-tunnel><name>__contrail__</name><source-address>5.5.5.5</source-address><gre/><destination-networks><name>30.0.0.0/24</name></destination-networks></dynamic-tunnel></dynamic-tunnels></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
    #end test_tunnels_dm_3

    #dynamic tunnel test case - 5
    # 1. configure ip fabric subnets,
    # 2. create physical  router with data plane source ip
    # 3. check netconf XML config generated
    # 4. add a new bgp peer
    # 5. check if new peer ip is present in netconf XML config generated for dynamic tunnels
    @skip("stale tests")
    def test_tunnels_dm_5(self):
        gs = self._vnc_lib.global_system_config_read(fq_name=[u'default-global-system-config'])
        gs.set_ip_fabric_subnets(SubnetListType([SubnetType("10.0.0.0", 24), SubnetType("20.0.0.0", 16)]))
        self._vnc_lib.global_system_config_update(gs)

        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        pr.physical_router_dataplane_ip = "5.5.5.5"
        self._vnc_lib.physical_router_update(pr)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-options><dynamic-tunnels><dynamic-tunnel><name>__contrail__</name><source-address>5.5.5.5</source-address><gre/><destination-networks><name>10.0.0.0/24</name></destination-networks><destination-networks><name>20.0.0.0/16</name></destination-networks><destination-networks><name>1.1.1.1/32</name></destination-networks></dynamic-tunnel></dynamic-tunnels></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'

        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        bgp_router2, pr2 = self.create_router('router2', '20.2.2.2')
        families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn'])
        bgp_sess_attrs = [BgpSessionAttributes(address_families=families)]
        bgp_sessions = [BgpSession(attributes=bgp_sess_attrs)]
        bgp_peering_attrs = BgpPeeringAttributes(session=bgp_sessions)
        bgp_router.add_bgp_router(bgp_router2, bgp_peering_attrs)
        self._vnc_lib.bgp_router_update(bgp_router)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-options><dynamic-tunnels><dynamic-tunnel><name>__contrail__</name><source-address>5.5.5.5</source-address><gre/><destination-networks><name>10.0.0.0/24</name></destination-networks><destination-networks><name>20.0.0.0/16</name></destination-networks><destination-networks><name>1.1.1.1/32</name></destination-networks><destination-networks><name>20.2.2.2/32</name></destination-networks></dynamic-tunnel></dynamic-tunnels></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'

        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

    #end test_tunnels_dm_5

    #dynamic tunnel test case - 6
    # 1. dont configure ip fabric subnets,
    # 2. create physical  router with data plane source ip
    # 3. check netconf XML config generated, should show dynamic tunnel config with only bgp router ip
    # 4. add a new bgp peer
    # 5. check if new peer ip is present in netconf XML config generated for dynamic tunnels
    @skip("stale tests")
    def test_tunnels_dm_6(self):
        gs = self._vnc_lib.global_system_config_read(fq_name=[u'default-global-system-config'])

        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        pr.physical_router_dataplane_ip = "5.5.5.5"
        self._vnc_lib.physical_router_update(pr)

        gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-options><dynamic-tunnels><dynamic-tunnel><name>__contrail__</name><source-address>5.5.5.5</source-address><gre/><destination-networks><name>1.1.1.1/32</name></destination-networks></dynamic-tunnel></dynamic-tunnels></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'

        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        bgp_router2, pr2 = self.create_router('router2', '20.2.2.2')
        families = AddressFamilies(['route-target', 'inet-vpn', 'e-vpn'])
        bgp_sess_attrs = [BgpSessionAttributes(address_families=families)]
        bgp_sessions = [BgpSession(attributes=bgp_sess_attrs)]
        bgp_peering_attrs = BgpPeeringAttributes(session=bgp_sessions)
        bgp_router.add_bgp_router(bgp_router2, bgp_peering_attrs)
        self._vnc_lib.bgp_router_update(bgp_router)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-options><dynamic-tunnels><dynamic-tunnel><name>__contrail__</name><source-address>5.5.5.5</source-address><gre/><destination-networks><name>1.1.1.1/32</name></destination-networks><destination-networks><name>20.2.2.2/32</name></destination-networks></dynamic-tunnel></dynamic-tunnels></routing-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'

        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

    #end test_tunnels_dm_6

    @skip("stale tests")
    def test_basic_dm(self):
        b1 = """
vn1_obj = VirtualNetwork('vn1')
vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        """

        b2 = """
bgp_router, pr = self.create_router('router1', '1.1.1.1')
        """

        b3 = """
pr.set_virtual_network(vn1_obj)
self._vnc_lib.physical_router_update(pr)
        """

        b4 = """
pi = PhysicalInterface('pi1', parent_obj = pr)
pi_id = self._vnc_lib.physical_interface_create(pi)
        """

        b5 = """
li = LogicalInterface('li1', parent_obj = pi)
li_id = self._vnc_lib.logical_interface_create(li)
        """

        cleanup_block = """
self._vnc_lib.logical_interface_delete(li.get_fq_name())
self._vnc_lib.physical_interface_delete(pi.get_fq_name())
self._vnc_lib.physical_router_delete(pr.get_fq_name())
self._vnc_lib.bgp_router_delete(bgp_router.get_fq_name())

self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        """

        # set of code blocks
        code_blocks = [b1, b2, b3, b4, b5]
        # add dependency map
        dep_map = { b3:[b1, b2], b4:[b2], b5:[b4]}   #dependency b3 =>(b1, b2), b4=>(b2), b5=>(b4)
        # generate valid permutations considering dependencies
        code_blocks_seq = get_valid_permutations(code_blocks, dep_map)

        for code_blocks in code_blocks_seq:
            for code in code_blocks:
                obj = compile(code, '<string>', 'exec')
                exec(obj)
            gevent.sleep(2)
            xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><hold-time>90</hold-time><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-instances><instance operation="replace"><name>__contrail__8c8174da-67a6-4bc1-b076-1536d11d944e_vn1</name><instance-type>vrf</instance-type><vrf-import>__contrail__8c8174da-67a6-4bc1-b076-1536d11d944e_vn1-import</vrf-import><vrf-export>__contrail__8c8174da-67a6-4bc1-b076-1536d11d944e_vn1-export</vrf-export><vrf-table-label/></instance></routing-instances><policy-options><policy-statement><name>__contrail__8c8174da-67a6-4bc1-b076-1536d11d944e_vn1-export</name><term><name>t1</name><then><community><add/><community-name>target_64512_8000001</community-name></community><accept/></then></term></policy-statement><policy-statement><name>__contrail__8c8174da-67a6-4bc1-b076-1536d11d944e_vn1-import</name><term><name>t1</name><from><community>target_64512_8000001</community></from><then><accept/></then></term><then><reject/></then></policy-statement><community><name>target_64512_8000001</name><members>target:64512:8000001</members></community></policy-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'
            self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
            obj = compile(cleanup_block, '<string>', 'exec')
            exec(obj)
            gevent.sleep(2)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name operation="delete">__contrail__</name></groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
    # end test_basic_dm

    @skip("stale tests")
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
            self.assertTill(self.ifmap_has_ident, obj=obj)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/></routing-options><routing-instances><instance operation="replace"><name>__contrail__default-domain_default-project_vn1</name><instance-type>vrf</instance-type><interface><name>li1</name></interface><vrf-import>__contrail__default-domain_default-project_vn1-import</vrf-import><vrf-export>__contrail__default-domain_default-project_vn1-export</vrf-export><vrf-target/><vrf-table-label/><routing-options><static><route><name>10.0.0.0/24</name><discard/></route></static><auto-export><family><inet><unicast/></inet></family></auto-export></routing-options></instance></routing-instances><policy-options><policy-statement><name>__contrail__default-domain_default-project_vn1-export</name><term><name>t1</name><then><community><add/><target_64512_8000001/></community><accept/></then></term></policy-statement><policy-statement><name>__contrail__default-domain_default-project_vn1-import</name><term><name>t1</name><from><community>target_64512_8000001</community></from><then><accept/></then></term><then><reject/></then></policy-statement><community><name>target_64512_8000001</name><members>target:64512:8000001</members></community></policy-options></groups><apply-groups>__contrail__</apply-groups></configuration></config>'
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

    @skip("stale tests")
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

    @skip("stale tests")
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

    @skip("stale tests")
    def test_public_vrf_dm(self):
        vn1_name = 'vn1'
        vn1_obj = VirtualNetwork(vn1_name)
        vn1_obj.set_router_external(True)
        ipam_obj = NetworkIpam('ipam1')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)

        bgp_router, pr = self.create_router('router10', '1.1.1.1')
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)

        pi = PhysicalInterface('pi1', parent_obj = pr)
        pi_id = self._vnc_lib.physical_interface_create(pi)

        li = LogicalInterface('li1', parent_obj = pi)
        li_id = self._vnc_lib.logical_interface_create(li)

        self.assertTill(self.ifmap_has_ident, obj=vn1_obj)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-instances><instance operation="replace"><name>__contrail__default-domain_default-project_vn1</name><instance-type>vrf</instance-type><vrf-import>__contrail__default-domain_default-project_vn1-import</vrf-import><vrf-export>__contrail__default-domain_default-project_vn1-export</vrf-export><vrf-target/><vrf-table-label/><routing-options><static><route><name>192.168.7.0/24</name><discard/><inet.0/></route><route><name>0.0.0.0/0</name><next-table/><inet.0/></route></static><auto-export><family><inet><unicast/></inet></family></auto-export></routing-options></instance></routing-instances><policy-options><policy-statement><name>__contrail__default-domain_default-project_vn1-export</name><term><name>t1</name><then><community><add/><target_64512_8000001/></community><accept/></then></term></policy-statement><policy-statement><name>__contrail__default-domain_default-project_vn1-import</name><term><name>t1</name><from><community>target_64512_8000001</community></from><then><accept/></then></term><then><reject/></then></policy-statement><community><name>target_64512_8000001</name><members>target:64512:8000001</members></community></policy-options><firewall><filter><name>redirect_to___contrail__default-domain_default-project_vn1_vrf</name><term><name>t1</name><from><destination-address>192.168.7.0/24</destination-address></from><then><routing-instance>__contrail__default-domain_default-project_vn1</routing-instance></then></term><term><name>t2</name><then><accept/></then></term></filter></firewall><forwarding-options><family><name>inet</name><filter><input>redirect_to___contrail__default-domain_default-project_vn1_vrf</input></filter></family></forwarding-options></groups><apply-groups>__contrail__</apply-groups></configuration></config>'

        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        self._vnc_lib.logical_interface_delete(li.get_fq_name())
        self._vnc_lib.physical_interface_delete(pi.get_fq_name())
        self._vnc_lib.physical_router_delete(pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name operation="delete">__contrail__</name></groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
    # end test_basic_dm

    @skip("stale tests")
    def test_evpn(self):
        vn1_name = 'vn1'
        vn1_obj = VirtualNetwork(vn1_name)
        vn1_obj.set_router_external(True)
        ipam_obj = NetworkIpam('ipam1')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))

        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_vxlan_network_identifier(2000)
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)

        bgp_router, pr = self.create_router('router10', '1.1.1.1')
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)

        pi = PhysicalInterface('pi1', parent_obj = pr)
        pi_id = self._vnc_lib.physical_interface_create(pi)

        fq_name = ['default-project', 'vmi1']
        vmi1 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi1.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        fq_name = ['default-project', 'vmi2']
        vmi2 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi2.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi2)

        li1 = LogicalInterface('li1', parent_obj = pi)
        li1.set_virtual_machine_interface(vmi1)
        li1_id = self._vnc_lib.logical_interface_create(li1)

        li2 = LogicalInterface('li2', parent_obj = pi)
        li2.set_virtual_machine_interface(vmi2)
        li2_id = self._vnc_lib.logical_interface_create(li2)

        self.assertTill(self.ifmap_has_ident, obj=vn1_obj)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-instances><instance operation="replace"><name>__contrail__default-domain_default-project_vn1</name><instance-type>virtual-switch</instance-type><vrf-import>__contrail__default-domain_default-project_vn1-import</vrf-import><vrf-export>__contrail__default-domain_default-project_vn1-export</vrf-export><vrf-target/><vrf-table-label/><routing-options><static><route><name>192.168.7.0/24</name><discard/><inet.0/></route><route><name>0.0.0.0/0</name><next-table/><inet.0/></route></static><auto-export><family><inet><unicast/></inet></family></auto-export></routing-options><bridge-domains><bd-2000><vlan-id>2000</vlan-id><vxlan><vni>2000</vni><ingress-node-replication/></vxlan><interface><name>li1</name></interface><interface><name>li2</name></interface><routing-interface><name>irb.2000</name></routing-interface><routing-interface/></bd-2000></bridge-domains><protocols><evpn><encapsulation>vxlan</encapsulation><extended-vni-all/></evpn></protocols></instance></routing-instances><interfaces><interface><name>irb</name><gratuitous-arp-reply/><unit><name>2000</name><family><inet><address><name>192.168.7.254</name></address></inet></family></unit></interface><interface><name>lo0</name><unit><name>0</name><family><inet/><address><name>1.1.1.1</name></address></family></unit></interface><interface><name>li1</name><encapsulation>ethernet-bridge</encapsulation><unit><name>0</name><family><bridge/></family></unit></interface><interface><name>li2</name><encapsulation>ethernet-bridge</encapsulation><unit><name>0</name><family><bridge/></family></unit></interface></interfaces><policy-options><policy-statement><name>__contrail__default-domain_default-project_vn1-export</name><term><name>t1</name><then><community><add/><target_64512_8000001/></community><accept/></then></term></policy-statement><policy-statement><name>__contrail__default-domain_default-project_vn1-import</name><term><name>t1</name><from><community>target_64512_8000001</community></from><then><accept/></then></term><then><reject/></then></policy-statement><community><name>target_64512_8000001</name><members>target:64512:8000001</members></community></policy-options><firewall><filter><name>redirect_to___contrail__default-domain_default-project_vn1_vrf</name><term><name>t1</name><from><destination-address>192.168.7.0/24</destination-address></from><then><routing-instance>__contrail__default-domain_default-project_vn1</routing-instance></then></term><term><name>t2</name><then><accept/></then></term></filter></firewall><forwarding-options><family><name>inet</name><filter><input>redirect_to___contrail__default-domain_default-project_vn1_vrf</input></filter></family></forwarding-options></groups><apply-groups>__contrail__</apply-groups></configuration></config>'

        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)

        self._vnc_lib.logical_interface_delete(li1.get_fq_name())
        self._vnc_lib.logical_interface_delete(li2.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(vmi1.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(vmi2.get_fq_name())
        self._vnc_lib.physical_interface_delete(pi.get_fq_name())
        self._vnc_lib.physical_router_delete(pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups><name operation="delete">__contrail__</name></groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
    # end test_evpn

    @skip("stale tests")
    def test_fip(self):
        vn1_name = 'vn-private'
        vn1_obj = VirtualNetwork(vn1_name)
        ipam_obj = NetworkIpam('ipam1')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("10.0.0.0", 24))]))
        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)

        #MX router
        bgp_router1, pr_mx = self.create_router('mx_router', '1.1.1.1')
        pr_mx.set_virtual_network(vn1_obj)
        junos_service_ports = JunosServicePorts()
        junos_service_ports.service_port.append("ge-0/0/0")
        pr_mx.set_physical_router_junos_service_ports(junos_service_ports)

        self._vnc_lib.physical_router_update(pr_mx)

        pi_mx = PhysicalInterface('pi-mx', parent_obj = pr_mx)
        pi_mx_id = self._vnc_lib.physical_interface_create(pi_mx)

        #TOR
        bgp_router2, pr_tor = self.create_router('tor_router', '2.2.2.2')
        pr_tor.set_virtual_network(vn1_obj)
        pr_tor.vnc_managed = False
        self._vnc_lib.physical_router_update(pr_tor)
        pi_tor = PhysicalInterface('pi-tor', parent_obj = pr_tor)
        pi_tor_id = self._vnc_lib.physical_interface_create(pi_tor)

        fq_name = ['default-domain', 'default-project', 'vmi1']
        vmi = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        li_tor = LogicalInterface('li_tor', parent_obj = pi_tor)
        li_tor.set_virtual_machine_interface(vmi)
        li_tor_id = self._vnc_lib.logical_interface_create(li_tor)
        vmi = self._vnc_lib.virtual_machine_interface_read(vmi.get_fq_name())

        ip_obj1 = InstanceIp(name='inst-ip-1')
        ip_obj1.set_virtual_machine_interface(vmi)
        ip_obj1.set_virtual_network(vn1_obj)
        ip_id1 = self._vnc_lib.instance_ip_create(ip_obj1)
        ip_obj1 = self._vnc_lib.instance_ip_read(id=ip_id1)
        ip_addr1 = ip_obj1.get_instance_ip_address()
        vn2_name = 'vn-public'
        vn2_obj = VirtualNetwork(vn2_name)
        vn2_obj.set_router_external(True)
        ipam2_obj = NetworkIpam('ipam2')
        self._vnc_lib.network_ipam_create(ipam2_obj)
        vn2_obj.add_network_ipam(ipam2_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)
        fip_pool_name = 'vn_public_fip_pool'
        fip_pool = FloatingIpPool(fip_pool_name, vn2_obj)
        self._vnc_lib.floating_ip_pool_create(fip_pool)
        fip_obj = FloatingIp("fip1", fip_pool)
        fip_obj.set_virtual_machine_interface(vmi)
        default_project = self._vnc_lib.project_read(fq_name=[u'default-domain', u'default-project'])
        fip_obj.set_project(default_project)
        fip_uuid = self._vnc_lib.floating_ip_create(fip_obj)

        for obj in [vn1_obj, vn2_obj]:
            self.assertTill(self.ifmap_has_ident, obj=obj)

        xml_config_str = '<config xmlns:xc="urn:ietf:params:xml:ns:netconf:base:1.0"><configuration><groups operation="replace"><name>__contrail__</name><protocols><bgp><group operation="replace"><name>__contrail__</name><type>internal</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group><group operation="replace"><name>__contrail_external__</name><type>external</type><multihop/><local-address>1.1.1.1</local-address><family><route-target/><inet-vpn><unicast/></inet-vpn><evpn><signaling/></evpn><inet6-vpn><unicast/></inet6-vpn></family><keep>all</keep></group></bgp></protocols><routing-options><route-distinguisher-id/><autonomous-system>64512</autonomous-system></routing-options><routing-instances><instance operation="replace"><name>__contrail__default-domain_default-project_vn-private</name><instance-type>vrf</instance-type><vrf-import>__contrail__default-domain_default-project_vn-private-import</vrf-import><vrf-export>__contrail__default-domain_default-project_vn-private-export</vrf-export><vrf-table-label/><routing-options><static><route><name>10.0.0.0/24</name><discard/></route></static><auto-export><family><inet><unicast/></inet></family></auto-export></routing-options></instance><instance operation="replace"><name>__contrail__default-domain_default-project_vn-private_public_nat</name><instance-type>vrf</instance-type><interface><name>ge-0/0/0.0</name></interface><interface><name>ge-0/0/0.1</name></interface><vrf-import>__contrail__default-domain_default-project_vn-private_public_nat-import</vrf-import><vrf-export>__contrail__default-domain_default-project_vn-private_public_nat-export</vrf-export><vrf-table-label/><routing-options><static><route><name>0.0.0.0/0</name><next-hop>ge-0/0/0.0</next-hop></route></static></routing-options></instance><instance><name>__contrail__default-domain_default-project_vn-public</name><routing-options><static><route><name>192.168.7.252/32</name><next-hop>ge-0/0/0.1</next-hop></route></static></routing-options></instance></routing-instances><interfaces><interface><name>ge-0/0/0</name><unit><name>0</name><family><inet/></family><service-domain>inside</service-domain></unit><unit><name>1</name><family><inet/></family><service-domain>outside</service-domain></unit></interface></interfaces><services><service-set><name>__contrail__default-domain_default-project_vn-private_public_nat-fip-nat</name><nat-rules><name>__contrail__default-domain_default-project_vn-private_public_nat-fip-nat-snat-rule-0</name></nat-rules><nat-rules><name>__contrail__default-domain_default-project_vn-private_public_nat-fip-nat-dnat-rule-0</name></nat-rules><next-hop-service><inside-service-interface>ge-0/0/0.0</inside-service-interface><outside-service-interface>ge-0/0/0.1</outside-service-interface></next-hop-service></service-set><nat><rule><name>__contrail__default-domain_default-project_vn-private_public_nat-fip-nat-snat-rule-0</name><match-condition>input</match-condition><term><name>t1</name><from><source-address><name>10.0.0.252/32</name></source-address></from><then><translated><source-prefix>192.168.7.252/32</source-prefix><translation-type><basic-nat44/></translation-type></translated></then></term></rule><rule><name>__contrail__default-domain_default-project_vn-private_public_nat-fip-nat-dnat-rule-0</name><match-condition>output</match-condition><term><name>t1</name><from><destination-address><name>192.168.7.252/32</name></destination-address></from><then><translated><destination-prefix>10.0.0.252/32</destination-prefix><translation-type><dnat-44/></translation-type></translated></then></term></rule></nat></services><policy-options><policy-statement><name>__contrail__default-domain_default-project_vn-private-export</name><term><name>t1</name><then><community><add/><community-name>target_64512_8000001</community-name></community><accept/></then></term></policy-statement><policy-statement><name>__contrail__default-domain_default-project_vn-private-import</name><term><name>t1</name><from><community>target_64512_8000001</community></from><then><accept/></then></term><then><reject/></then></policy-statement><policy-statement><name>__contrail__default-domain_default-project_vn-private_public_nat-export</name><term><name>t1</name><then><community><add/><community-name>target_64512_8000001</community-name></community><accept/></then></term></policy-statement><policy-statement><name>__contrail__default-domain_default-project_vn-private_public_nat-import</name><term><name>t1</name><from><community>target_64512_8000001</community></from><then><accept/></then></term><then><reject/></then></policy-statement><community><name>target_64512_8000001</name><members>target:64512:8000001</members></community></policy-options></groups><apply-groups operation="replace">__contrail__</apply-groups></configuration></config>'
        self.check_netconf_config_mesg('1.1.1.1', xml_config_str)
#end
