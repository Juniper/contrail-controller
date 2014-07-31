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

class TestPolicy(test_case.STTestCase):
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
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        while True:
            gevent.sleep(2)
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'])
            except NoIdError:
                print "retrying ... ", test_common.lineno()
                continue

            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn2', u'vn2'])
                break
                print "retrying ... ", test_common.lineno()
        # end while True

        while True:
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn2', 'vn2'])
            except NoIdError:
                gevent.sleep(2)
                print "retrying ... ", test_common.lineno()
                continue

            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn1', u'vn1'])
                break
            print "retrying ... ", test_common.lineno()
            gevent.sleep(2)
        # end while True

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        while True:
            ri = self._vnc_lib.routing_instance_read(
                fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                gevent.sleep(2)
            else:
                break
            print "retrying ... ", test_common.lineno()
        # end while True

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        while True:
            try:
                self._vnc_lib.virtual_network_read(id=vn1_obj.uuid)
                print "retrying ... ", test_common.lineno()
                gevent.sleep(2)
                continue
            except NoIdError:
                print 'vn1 deleted'
            try:
                self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn2', 'vn2'])
                print "retrying ... ", test_common.lineno()
                gevent.sleep(2)
                continue
            except NoIdError:
                print 'ri2 deleted'
            break

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

        while True:
            gevent.sleep(2)
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'])
            except NoIdError:
                print "retrying ... ", test_common.lineno()
                continue

            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn2', u'vn2'])
                break
                print "retrying ... ", test_common.lineno()
        # end while True

        while True:
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn2', 'vn2'])
            except NoIdError:
                gevent.sleep(2)
                print "retrying ... ", test_common.lineno()
                continue

            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn1', u'vn1'])
                break
            print "retrying ... ", test_common.lineno()
            gevent.sleep(2)
        # end while True

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
            if ('contrail:connection contrail:routing-instance:default-domain:default-project:vn2:vn2' in
                FakeIfmapClient._graph['contrail:routing-instance:default-domain:default-project:vn1:vn1']['links']):
                print "retrying ... ", test_common.lineno()
                continue
            break
        vn1_obj.del_network_policy(np1)
        vn2_obj.del_network_policy(np2)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        while True:
            ri = self._vnc_lib.routing_instance_read(
                fq_name=[u'default-domain', u'default-project', 'vn2', 'vn2'])
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                gevent.sleep(2)
            else:
                break
            print "retrying ... ", test_common.lineno()
        # end while True

        self.delete_network_policy(np1)
        self.delete_network_policy(np2)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        while True:
            try:
                self._vnc_lib.virtual_network_read(id=vn1_obj.uuid)
                print "retrying ... ", test_common.lineno()
                gevent.sleep(2)
                continue
            except NoIdError:
                print 'vn1 deleted'
                break
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

        while True:
            gevent.sleep(2)
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project', 'vn1', 'vn1'])
            except NoIdError:
                print "retrying ... ", test_common.lineno()
                continue

            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn2', u'vn2'])
                break
                print "retrying ... ", test_common.lineno()
        # end while True

        while True:
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn2', 'vn2'])
            except NoIdError:
                gevent.sleep(2)
                print "retrying ... ", test_common.lineno()
                continue

            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn1', u'vn1'])
                break
            print "retrying ... ", test_common.lineno()
            gevent.sleep(2)
        # end while True

        vn3_obj = VirtualNetwork(vn3_name)
        vn3_obj.set_network_policy(np2, vnp)
        vn3_uuid = self._vnc_lib.virtual_network_create(vn3_obj)

        while True:
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn3', 'vn3'])
            except NoIdError:
                gevent.sleep(2)
                print "retrying ... ", test_common.lineno()
                continue

            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn1', u'vn1'])
                break
            print "retrying ... ", test_common.lineno()
            gevent.sleep(2)
        # end while True

        vn1_obj.del_network_policy(np1)
        vn2_obj.del_network_policy(np2)
        vn3_obj.del_network_policy(np2)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self._vnc_lib.virtual_network_update(vn3_obj)
        self.delete_network_policy(np1)
        self.delete_network_policy(np2)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn3_obj.get_fq_name())

        while True:
            try:
                self._vnc_lib.virtual_network_read(id=vn1_obj.uuid)
                print "retrying ... ", test_common.lineno()
                gevent.sleep(2)
                continue
            except NoIdError:
                print 'vn1 deleted'
                break
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
        while True:
            gevent.sleep(2)
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn1', 'vn1'])
            except NoIdError:
                print "retrying ... ", test_common.lineno()
                continue
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                sc = [x for x in to_bgp.ServiceChain]
                sc_ri_name = 'service-'+sc[0]+'-default-domain_default-project_s1'
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn1', sc_ri_name])
                break
            print "retrying ... ", test_common.lineno()
        # end while True

        while True:
            try:
                test_common.FakeApiConfigLog._print()
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project', u'vn2', sc_ri_name])
            except NoIdError:
                gevent.sleep(2)
                print "retrying ... ", test_common.lineno()
                continue
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs:
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn2', u'vn2'])
                sci = ri.get_service_chain_information()
                if sci is None:
                    print "retrying ... ", test_common.lineno()
                    gevent.sleep(2)
                    continue
                self.assertEqual(sci.prefix[0], '10.0.0.0/24')
                break
            print "retrying ... ", test_common.lineno()
            gevent.sleep(2)
        # end while True

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        while True:
            gevent.sleep(2)
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn1', 'vn1'])
            except NoIdError:
                print "retrying ... ", test_common.lineno()
                continue
            ri_refs = ri.get_routing_instance_refs()
            if ri_refs is None:
                break
            print "retrying ... ", test_common.lineno()
        # end while True
        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        while True:
            try:
                self._vnc_lib.virtual_network_read(id=vn1_obj.uuid)
                gevent.sleep(2)
                print "retrying ... ", test_common.lineno()
                continue
            except NoIdError:
                print 'vn1 deleted'
            try:
                self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project',
                             'vn2', 'vn2'])
                print "retrying ... ", test_common.lineno()
                gevent.sleep(2)
                continue
            except NoIdError:
                print 'ri2 deleted'
            break

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
# end class TestRouteTable
