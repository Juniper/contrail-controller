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
        #import pdb; pdb.set_trace()
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
                self.assertEqual(
                    ri_refs[0]['to'],
                    [u'default-domain', u'default-project', u'vn1',
                     u'service-default-domain_default-project_vn1-default'
                     '-domain_default-project_vn2-default'
                     '-domain_default-project_s1'])
                break
            print "retrying ... ", test_common.lineno()
        # end while True

        while True:
            try:
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[u'default-domain', u'default-project', u'vn2',
                             u'service-default-domain_default'
                             '-project_vn2-default-domain_default'
                             '-project_vn1-default-domain_default-project_s1'])
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
                self.assertEqual(sci.prefix[0], '10.0.0.1/24')
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
    def _test_add_delete_route(self):
        import pdb; pdb.set_trace()
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
                lri = self._vnc_lib.routing_instance_read(
                    fq_name=['default-domain', 'default-project', 'lvn',
                             'service-default-domain_default-'
                             'project_lvn-default-domain_default'
                             '-project_rvn-default-domain_default-project_s1'])
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
                ri = self._vnc_lib.routing_instance_read(
                    fq_name=[
                        'default-domain', 'default-project', 'lvn', 'lvn'])
                rt_refs = ri.get_route_target_refs()
                found = False
                for rt_ref in ri.get_route_target_refs() or []:
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
                fq_name=['default-domain', 'default-project', 'lvn',
                         'service-default-domain_default-project_lvn-default'
                         '-domain_default-project_rvn-default-domain_default'
                         '-project_s1'])
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
        self.delete_network_policy(np)
        gevent.sleep(2)
        self._vnc_lib.virtual_network_delete(
            fq_name=['default-domain', 'default-project', 'lvn'])
        self._vnc_lib.virtual_network_delete(
            fq_name=['default-domain', 'default-project', 'rvn'])
    # test_add_delete_route
# end class TestRouteTable
