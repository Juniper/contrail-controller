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
import to_bgp

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


class TestPolicy(test_case.STTestCase):

    @retries(5, hook=retry_exc_handler)
    def check_service_chain_prefix_match(self, fq_name, prefix):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        sci = ri.get_service_chain_information()
        if sci is None:
            print "retrying ... ", test_common.lineno()
            raise NoIdError
        self.assertEqual(sci.prefix[0], prefix)

    @retries(5, hook=retry_exc_handler)
    def check_ri_rt_state_vn_policy(self, fq_name, to_fq_name, expect_to_find):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        rt_refs = ri.get_route_target_refs()
        if not rt_refs:
            print "retrying ... ", test_common.lineno()
            raise NoIdError

        found = False
        for rt_ref in rt_refs:
            rt_obj = self._vnc_lib.route_target_read(id=rt_ref['uuid'])
            ri_refs = rt_obj.get_routing_instance_back_refs()
            for ri_ref in ri_refs:
                if ri_ref['to'] == to_fq_name:
                    found = True
                    break
            if found == True:
                break
        self.assertTrue(found == expect_to_find)

    @retries(5, hook=retry_exc_handler)
    def check_ri_state_vn_policy(self, fq_name, to_fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        ri_refs = ri.get_routing_instance_refs()
        if not ri_refs:
            print "retrying ... ", test_common.lineno()
            raise NoIdError
        self.assertEqual(ri_refs[0]['to'], to_fq_name)

    @retries(5, hook=retry_exc_handler)
    def check_ri_refs_are_deleted(self, fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        ri_refs = ri.get_routing_instance_refs()
        if ri_refs:
            print "retrying ... ", test_common.lineno()
            raise Exception

    @retries(5, hook=retry_exc_handler)
    def check_vn_is_deleted(self, uuid):
        try:
            self._vnc_lib.virtual_network_read(id=uuid)
            print "retrying ... ", test_common.lineno()
            raise Exception
        except NoIdError:
            print 'vn deleted'

    @retries(5, hook=retry_exc_handler)
    def check_ri_is_deleted(self, fq_name):
        try:
            self._vnc_lib.routing_instance_read(fq_name)
            print "retrying ... ", test_common.lineno()
            raise Exception
        except NoIdError:
            print 'ri deleted'

    @retries(5, hook=retry_exc_handler)
    def check_ri_is_present(self, fq_name):
        self._vnc_lib.routing_instance_read(fq_name)

    @retries(5, hook=retry_exc_handler)
    def check_link_in_ifmap_graph(self, fq_name_str, links):
        self._vnc_lib.routing_instance_read(fq_name)

    @retries(5, hook=retry_exc_handler)
    def wait_to_get_sc(self):
        sc = [x for x in to_bgp.ServiceChain]
        if len(sc) == 0:
            print "retrying ... ", test_common.lineno()
            raise Exception
        return sc

    @retries(5, hook=retry_exc_handler)
    def check_acl_match_dst_cidr(self, fq_name, ip_prefix, ip_len):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.get_access_control_list_entries().get_acl_rule():
            if rule.match_condition.dst_address.subnet.ip_prefix == ip_prefix:
                if rule.match_condition.dst_address.subnet.ip_prefix_len == ip_len:
                    return
        raise Exception

    def test_basic_policy(self):
        vn1_name = 'vn1'
        vn2_name = 'vn2'
        vn1_obj = VirtualNetwork(vn1_name)
        vn2_obj = VirtualNetwork(vn2_name)

        np = self.create_network_policy(vn1_obj, vn2_obj)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)

        for obj in [vn1_obj, vn2_obj]:
            ident_name = self.get_obj_imid(obj)
            gevent.sleep(2)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        try:
            self.check_ri_state_vn_policy(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'],
                                       to_fq_name=[u'default-domain', u'default-project', u'vn2', u'vn2'])
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        try:
            self.check_ri_state_vn_policy(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'],
                                       to_fq_name=[u'default-domain', u'default-project', u'vn1', u'vn1'])
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        try:
            self.check_ri_refs_are_deleted(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])

        except Exception, e:
            print "failed : ri refs are still present in routing instance [vn2]... ", test_common.lineno()
            self.assertTrue(False)

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        try:
            self.check_vn_is_deleted(uuid=vn1_obj.uuid)

        except Exception, e:
            print "failed : vn1 is still present in api server ... ", test_common.lineno()
            self.assertTrue(False)

        try:
            self.check_ri_is_deleted(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])

        except Exception, e:
            print "failed : ri1 is still present in api server ... ", test_common.lineno()
            self.assertTrue(False)
    # end test_basic_policy

    def test_multiple_policy(self):
        vn1_name = 'vn1'
        vn2_name = 'vn2'
        vn1_obj = VirtualNetwork(vn1_name)
        vn2_obj = VirtualNetwork(vn2_name)

        np1 = self.create_network_policy(vn1_obj, vn2_obj)
        np2 = self.create_network_policy(vn2_obj, vn1_obj)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np1, vnp)
        vn2_obj.set_network_policy(np2, vnp)
        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)

        try:
            self.check_ri_state_vn_policy(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'],
                                       to_fq_name=[u'default-domain', u'default-project', u'vn2', u'vn2'])
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        try:
            self.check_ri_state_vn_policy(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'],
                                       to_fq_name=[u'default-domain', u'default-project', u'vn1', u'vn1'])
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        np1.network_policy_entries.policy_rule[0].action_list.simple_action = 'deny'
        np1.set_network_policy_entries(np1.network_policy_entries)
        self._vnc_lib.network_policy_update(np1)

        while True:
            gevent.sleep(2)
            if ('contrail:connection contrail:routing-instance:default-domain:default-project:vn2:vn2' in
                FakeIfmapClient._graph['contrail:routing-instance:default-domain:default-project:vn1:vn1']['links']):
                print "retrying ... ", test_common.lineno()
                continue
            break
        np1.network_policy_entries.policy_rule[0].action_list.simple_action = 'pass'
        np1.set_network_policy_entries(np1.network_policy_entries)
        self._vnc_lib.network_policy_update(np1)
        np2.network_policy_entries.policy_rule[0].action_list.simple_action = 'deny'
        np2.set_network_policy_entries(np2.network_policy_entries)
        self._vnc_lib.network_policy_update(np2)

        while True:
            gevent.sleep(2)
            if ('contrail:connection contrail:routing-instance:default-domain:default-project:vn1:vn1' in
                FakeIfmapClient._graph['contrail:routing-instance:default-domain:default-project:vn2:vn2']['links']):
                print "retrying ... ", test_common.lineno()
                continue
            break
        vn1_obj.del_network_policy(np1)
        vn2_obj.del_network_policy(np2)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        try:
            self.check_ri_refs_are_deleted(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])

        except Exception, e:
            print "failed : ri refs are still present in routing instance [vn2]... ", test_common.lineno()
            self.assertTrue(False)

        self.delete_network_policy(np1)
        self.delete_network_policy(np2)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        try:
            self.check_vn_is_deleted(uuid=vn1_obj.uuid)

        except Exception, e:
            print "failed : vn1 is still present in api server ... ", test_common.lineno()
            self.assertTrue(False)
    # end test_multiple_policy

    def test_policy_in_policy(self):
        vn1_name = 'vn1'
        vn2_name = 'vn2'
        vn3_name = 'vn3'
        vn1_obj = VirtualNetwork(vn1_name)
        vn2_obj = VirtualNetwork(vn2_name)

        np1 = self.create_network_policy(vn1_obj, vn2_obj)
        np2 = self.create_network_policy(vn2_obj, vn1_obj)

        np1.network_policy_entries.policy_rule[0].dst_addresses[0].virtual_network = None
        np1.network_policy_entries.policy_rule[0].dst_addresses[0].network_policy = np2.get_fq_name_str()
        np1.set_network_policy_entries(np1.network_policy_entries)
        self._vnc_lib.network_policy_update(np1)

        np2.network_policy_entries.policy_rule[0].src_addresses[0].virtual_network = 'local'
        np2.set_network_policy_entries(np1.network_policy_entries)
        self._vnc_lib.network_policy_update(np2)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np1, vnp)
        vn2_obj.set_network_policy(np2, vnp)
        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)

        try:
            self.check_ri_state_vn_policy(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'],
                                       to_fq_name=[u'default-domain', u'default-project', u'vn2', u'vn2'])
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        try:
            self.check_ri_state_vn_policy(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'],
                                       to_fq_name=[u'default-domain', u'default-project', u'vn1', u'vn1'])
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        vn3_obj = VirtualNetwork(vn3_name)
        vn3_obj.set_network_policy(np2, vnp)
        vn3_uuid = self._vnc_lib.virtual_network_create(vn3_obj)

        try:
            self.check_ri_state_vn_policy(fq_name=[u'default-domain', u'default-project', 'vn3', 'vn3'],
                                       to_fq_name=[u'default-domain', u'default-project', u'vn1', u'vn1'])
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        vn3_obj.del_network_policy(np2)
        self._vnc_lib.virtual_network_update(vn3_obj)

        while True:
            try:
                acl = self._vnc_lib.access_control_list_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn1', 'vn1'])
            except NoIdError:
                gevent.sleep(2)
                print "retrying ... ", test_common.lineno()
                continue
            found = False
            for rule in acl.get_access_control_list_entries().get_acl_rule():
                if rule.match_condition.dst_address.virtual_network == vn3_obj.get_fq_name_str():
                    gevent.sleep(1)
                    print "retrying ... ", test_common.lineno()
                    found = True
                    break
            if not found:
                break
        # end while True


        vn1_obj.del_network_policy(np1)
        vn2_obj.del_network_policy(np2)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self.delete_network_policy(np1)
        self.delete_network_policy(np2)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn3_obj.get_fq_name())

        try:
            self.check_vn_is_deleted(uuid=vn1_obj.uuid)

        except Exception, e:
            print "failed : vn1 is still present in api server ... ", test_common.lineno()
            self.assertTrue(False)
    # end test_multiple_policy

    def test_service_policy(self):
        # create  vn1
        vn1_obj = VirtualNetwork('vn1')
        ipam_obj = NetworkIpam('ipam1')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("10.0.0.0", 24))]))
        self._vnc_lib.virtual_network_create(vn1_obj)

        # create vn2
        vn2_obj = VirtualNetwork('vn2')
        ipam_obj = NetworkIpam('ipam2')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn2_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("20.0.0.0", 24))]))
        self._vnc_lib.virtual_network_create(vn2_obj)

        np = self.create_network_policy(vn1_obj, vn2_obj, ["s1"])
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.clear_pending_updates()
        vn2_obj.clear_pending_updates()
        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        try:
            sc = self.wait_to_get_sc()
            sc_ri_name = 'service-'+sc[0]+'-default-domain_default-project_s1'
        except Exception, e:
            print "failed: unable to fetch to_bgp.service_chain"
            self.assertTrue(False)

        try:
            self.check_ri_rt_state_vn_policy(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'],
                                       to_fq_name=[u'default-domain', u'default-project', u'vn1', sc_ri_name], expect_to_find=True)
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        try:
            self.check_ri_rt_state_vn_policy(fq_name=[u'default-domain', u'default-project', 'vn2', sc_ri_name],
                                       to_fq_name=[u'default-domain', u'default-project', u'vn2', u'vn2'], expect_to_find=True)
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        try:
            self.check_service_chain_prefix_match(fq_name=[u'default-domain', u'default-project', 'vn2', sc_ri_name],
                                       prefix='10.0.0.0/24')
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        try:
            self.check_ri_refs_are_deleted(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'])

        except Exception, e:
            print "failed : ri refs are still present in routing instance [vn2]... ", test_common.lineno()
            self.assertTrue(False)

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        try:
            self.check_vn_is_deleted(uuid=vn1_obj.uuid)

        except Exception, e:
            print "failed : vn1 is still present in api server ... ", test_common.lineno()
            self.assertTrue(False)

        try:
            self.check_ri_is_deleted(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])

        except Exception, e:
            print "failed : ri1 is still present in api server ... ", test_common.lineno()
            self.assertTrue(False)

    # end test_service_policy
# end class TestPolicy

#class TestRouteTable(test_case.STTestCase):
    def test_add_delete_route(self):
        lvn = self.create_virtual_network("lvn", "10.0.0.0/24")
        rvn = self.create_virtual_network("rvn", "20.0.0.0/24")
        np = self.create_network_policy(lvn, rvn, ["s1"], "in-network")

        vn = self.create_virtual_network("vn100", "1.0.0.0/24")
        rt = RouteTable("rt1")
        self._vnc_lib.route_table_create(rt)
        vn.add_route_table(rt)
        self._vnc_lib.virtual_network_update(vn)
        routes = RouteTableType()
        route = RouteType(
            prefix="0.0.0.0/0", next_hop="default-domain:default-project:s1")
        routes.add_route(route)
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        while 1:
            gevent.sleep(2)
            lvn = self._vnc_lib.virtual_network_read(id=lvn.uuid)
            try:
                sc = [x for x in to_bgp.ServiceChain]
                if len(sc) == 0:
                    print "retrying ... ", test_common.lineno()
                    continue

                sc_ri_name = 'service-'+sc[0]+'-default-domain_default-project_s1'
                lri = self._vnc_lib.routing_instance_read(
                    fq_name=['default-domain', 'default-project', 'lvn', sc_ri_name])
                sr = lri.get_static_route_entries()
                if sr is None:
                    print "retrying ... ", test_common.lineno()
                    continue
                route = sr.route[0]
                self.assertEqual(route.prefix, "0.0.0.0/0")
                self.assertEqual(route.next_hop, "10.0.0.253")
            except NoIdError:
                print "retrying ... ", test_common.lineno()
                continue

            try:
                ri100 = self._vnc_lib.routing_instance_read(
                    fq_name=[
                        'default-domain', 'default-project', 'vn100', 'vn100'])
                rt100 = ri100.get_route_target_refs()[0]['to']
                found = False
                for rt_ref in lri.get_route_target_refs() or []:
                    if rt100 == rt_ref['to']:
                        found = True
                        break
                self.assertEqual(found, True)
            except NoIdError:
                print "retrying ... ", test_common.lineno()
                continue
            break
        # end while

        routes.set_route([])
        rt.set_routes(route)
        self._vnc_lib.route_table_update(rt)

        while 1:
            lri = self._vnc_lib.routing_instance_read(
                fq_name=['default-domain', 'default-project', 'lvn', sc_ri_name])
            sr = lri.get_static_route_entries()
            if sr and sr.route:
                gevent.sleep(2)
                print "retrying ... ", test_common.lineno()
                continue
            ri = self._vnc_lib.routing_instance_read(
                fq_name=['default-domain', 'default-project', 'lvn', 'lvn'])
            rt_refs = ri.get_route_target_refs()
            for rt_ref in ri.get_route_target_refs() or []:
                if rt100 == rt_ref['to']:
                    print "retrying ... ", test_common.lineno()
                    continue
            break
        # end while

        self._vnc_lib.virtual_network_delete(
            fq_name=['default-domain', 'default-project', 'vn100'])
        self.delete_network_policy(np, auto_policy=True)
        gevent.sleep(2)
        self._vnc_lib.virtual_network_delete(
            fq_name=['default-domain', 'default-project', 'lvn'])
        self._vnc_lib.virtual_network_delete(
            fq_name=['default-domain', 'default-project', 'rvn'])
    # test_add_delete_route

    def test_vn_delete(self):
        vn = self.create_virtual_network("vn", "10.1.1.0/24")
        gevent.sleep(2)
        for obj in [vn]:
            ident_name = self.get_obj_imid(obj)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        try:
            self.check_vn_ri_state(fq_name=[u'default-domain', u'default-project', 'vn', 'vn'])

        except NoIdError, e:
            print "failed : routing instance state is not created ... ", test_common.lineno()
            self.assertTrue(False)

        # stop st
        self._st_greenlet.kill()
        gevent.sleep(5)

        # delete vn in api server
        self._vnc_lib.virtual_network_delete(
            fq_name=['default-domain', 'default-project', 'vn'])

        # start st on a free port
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self._api_server_ip, self._api_server_port)
        gevent.sleep(2)

        # check if vn is deleted
        try:
            self.check_vn_is_deleted(uuid=vn.uuid)

        except Exception, e:
            print "failed : vn is still present in api server ... ", test_common.lineno()
            self.assertTrue(False)

        # check if ri is deleted
        try:
            self.check_ri_is_deleted(fq_name=[u'default-domain', u'default-project', 'vn', 'vn'])

        except Exception, e:
            print "failed : routing instance is still present in api server ... ", test_common.lineno()
            self.assertTrue(False)

    # test_vn_delete

    @retries(5, hook=retry_exc_handler)
    def check_vn_ri_state(self, fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)

    def test_policy_with_cidr(self):
        vn1 = self.create_virtual_network("vn1", "10.1.1.0/24")
        vn2 = self.create_virtual_network("vn2", "10.2.1.0/24")
        rules = []
        rule1 = { "protocol": "icmp",
                  "direction": "<>",
                  "src-port": "any",
                  "src": {"type": "vn", "value": vn1},
                  "dst": {"type": "cidr", "value": "10.2.1.2/32"},
                  "dst-port": "any",
                  "action": "deny"
                 }
        rules.append(rule1)

        np = self.create_network_policy_with_multiple_rules(rules)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1)

        for obj in [vn1]:
            ident_name = self.get_obj_imid(obj)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        try:
            self.check_vn_ri_state(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'])

        except NoIdError, e:
            print "failed : Routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        try:
            self.check_acl_match_dst_cidr(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'], ip_prefix="10.2.1.2", ip_len=32)
        except NoIdError, e:
            print "failed : acl match cidr... ", test_common.lineno()
            self.assertTrue(False)
        except Exception, e:
            print "failed : acl match cidr... ", test_common.lineno()
            self.assertTrue(False)

        #cleanup
        self.delete_network_policy(np, auto_policy=True)
        self._vnc_lib.virtual_network_delete(
            fq_name=['default-domain', 'default-project', 'vn1'])

    # test st restart while service chain is configured
    def test_st_restart_service_chain_delete(self):
        # create  vn1
        vn1_obj = VirtualNetwork('vn1')
        ipam_obj = NetworkIpam('ipam1')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("10.0.0.0", 24))]))
        self._vnc_lib.virtual_network_create(vn1_obj)

        # create vn2
        vn2_obj = VirtualNetwork('vn2')
        ipam_obj = NetworkIpam('ipam2')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn2_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("20.0.0.0", 24))]))
        self._vnc_lib.virtual_network_create(vn2_obj)

        np = self.create_network_policy(vn1_obj, vn2_obj, ["s1"])
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.clear_pending_updates()
        vn2_obj.clear_pending_updates()
        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        try:
            sc = self.wait_to_get_sc()
            sc_ri_name = 'service-'+sc[0]+'-default-domain_default-project_s1'
        except Exception, e:
            print "failed: unable to fetch to_bgp.service_chain"
            self.assertTrue(False)

        try:
            self.check_ri_rt_state_vn_policy(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'],
                                       to_fq_name=[u'default-domain', u'default-project', u'vn1', sc_ri_name], expect_to_find=True)
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        try:
            self.check_ri_rt_state_vn_policy(fq_name=[u'default-domain', u'default-project', 'vn2', sc_ri_name],
                                       to_fq_name=[u'default-domain', u'default-project', u'vn2', u'vn2'], expect_to_find=True)
        except NoIdError, e:
            print "failed : routing instance state is not correct... ", test_common.lineno()
            self.assertTrue(False)

        # stop st
        self._st_greenlet.kill()
        gevent.sleep(5)

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        try:
            self.check_ri_refs_are_deleted(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'])

        except Exception, e:
            print "failed : ri refs are still present in routing instance [vn1]... ", test_common.lineno()
            self.assertTrue(False)

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        try:
            self.check_vn_is_deleted(uuid=vn1_obj.uuid)

        except Exception, e:
            print "failed : vn1 is still present in api server ... ", test_common.lineno()
            self.assertTrue(False)

        # start st on a free port
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self._api_server_ip, self._api_server_port)
        gevent.sleep(4)

        #check if all ri's  are deleted
        try:
            self.check_ri_is_deleted(fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'])
            self.check_ri_is_deleted(fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])
            self.check_ri_is_deleted(fq_name=[u'default-domain', u'default-project', 'vn1', sc_ri_name])
            self.check_ri_is_deleted(fq_name=[u'default-domain', u'default-project', 'vn2', sc_ri_name])
        except Exception, e:
            print "failed : ri instances are still present in api server ... ", test_common.lineno()
            self.assertTrue(False)
    #end

# end class TestRouteTable
