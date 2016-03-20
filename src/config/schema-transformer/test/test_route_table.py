#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import gevent

try:
    import to_bgp
except ImportError:
    from schema_transformer import to_bgp
from vnc_api.vnc_api import (RouteTargetList, RouteTable, RouteTableType,
        RouteType, CommunityAttributes)

from test_case import STTestCase, retries
from test_policy import VerifyPolicy


class VerifyRouteTable(VerifyPolicy):
    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib


class TestRouteTable(STTestCase, VerifyRouteTable):
    def test_add_delete_route(self):
        lvn_name = self.id() + 'lvn'
        rvn_name = self.id() + 'rvn'
        lvn = self.create_virtual_network(lvn_name, "10.0.0.0/24")
        rvn = self.create_virtual_network(rvn_name, "20.0.0.0/24")

        service_name = self.id() + 's1'
        np = self.create_network_policy(lvn, rvn, [service_name], service_mode="in-network-nat")

        vn_name = self.id() + 'vn100'
        vn = self.create_virtual_network(vn_name, "1.0.0.0/24")
        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        vn.set_route_target_list(rtgt_list)
        exp_rtgt_list = RouteTargetList(route_target=['target:2:1'])
        vn.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list = RouteTargetList(route_target=['target:3:1'])
        vn.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn)
        rt = RouteTable("rt1")
        self._vnc_lib.route_table_create(rt)
        vn.add_route_table(rt)
        self._vnc_lib.virtual_network_update(vn)
        routes = RouteTableType()
        route = RouteType(prefix="0.0.0.0/0",
                          next_hop="default-domain:default-project:"+service_name)
        routes.add_route(route)
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        @retries(5)
        def _match_route_table(rtgt_list):
            lri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(lvn))
            sr = lri.get_static_route_entries()
            if sr is None:
                raise Exception("sr is None")
            route = sr.route[0]
            self.assertEqual(route.prefix, "0.0.0.0/0")
            self.assertEqual(route.next_hop, "10.0.0.252")
            for rtgt in rtgt_list:
                self.assertIn(rtgt, route.route_target)
            ri100 = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(vn))
            rt100 = set(ref['to'][0] for ref in ri100.get_route_target_refs())
            lrt = set(ref['to'][0] for ref in lri.get_route_target_refs() or [])
            if rt100 & lrt:
                return (rt100 & lrt)
            raise Exception("rt100 route-target ref not found")

        rt100 = _match_route_table(rtgt_list.get_route_target() +
                                   imp_rtgt_list.get_route_target())

        rtgt_list.add_route_target('target:1:2')
        vn.set_route_target_list(rtgt_list)
        exp_rtgt_list.add_route_target('target:2:2')
        vn.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list.add_route_target('target:3:2')
        vn.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn)
        _match_route_table(rtgt_list.get_route_target())

        rtgt_list.delete_route_target('target:1:1')
        vn.set_route_target_list(rtgt_list)
        exp_rtgt_list.delete_route_target('target:2:1')
        vn.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list.delete_route_target('target:3:1')
        vn.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn)
        _match_route_table(rtgt_list.get_route_target())

        routes.set_route([])
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        @retries(5)
        def _match_route_table_cleanup(rt100):
            lri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(lvn))
            sr = lri.get_static_route_entries()
            if sr and sr.route:
                raise Exception("sr has route")
            ri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(lvn))
            rt_refs = ri.get_route_target_refs()
            rt_set = set(ref['to'][0] for ref in ri.get_route_target_refs() or [])
            if rt100 & rt_set:
                raise Exception("route-target ref still found: %s" % (rt100 & rt_set))

        _match_route_table_cleanup(rt100)

        # add the route again, then delete the network without deleting the
        # link to route table
        route = RouteType(prefix="0.0.0.0/0",
                          next_hop="default-domain:default-project:"+service_name)
        routes.add_route(route)
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)
        _match_route_table(rtgt_list.get_route_target())
        self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())
        _match_route_table_cleanup(rt100)

        self._vnc_lib.route_table_delete(fq_name=rt.get_fq_name())
        self.delete_network_policy(np, auto_policy=True)
        gevent.sleep(1)
        self._vnc_lib.virtual_network_delete(fq_name=lvn.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=rvn.get_fq_name())
        # check if vn is deleted
        self.check_vn_is_deleted(uuid=lvn.uuid)
        self.check_vn_is_deleted(uuid=rvn.uuid)
        self.check_vn_is_deleted(uuid=vn.uuid)
        self.check_ri_is_deleted(fq_name=lvn.fq_name+[lvn.name])
        self.check_ri_is_deleted(fq_name=rvn.fq_name+[rvn.name])
        self.check_ri_is_deleted(fq_name=vn.fq_name+[vn.name])
    # test_add_delete_route

    def test_add_delete_static_route(self):

        vn1_name = 'vn1'
        vn2_name = 'vn2'
        vn1 = self.create_virtual_network(vn1_name, "1.0.0.0/24")
        vn2 = self.create_virtual_network(vn2_name, "2.0.0.0/24")

        rt = RouteTable("rt1")
        self._vnc_lib.route_table_create(rt)
        vn1.add_route_table(rt)
        self._vnc_lib.virtual_network_update(vn1)
        comm_attr = CommunityAttributes(community_attribute=['1:1'])
        routes = RouteTableType()
        route = RouteType(prefix="1.1.1.1/0",
                          next_hop="10.10.10.10", next_hop_type="ip-address",
                          community_attributes=comm_attr)
        routes.add_route(route)
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        @retries(5)
        def _match_route_table(vn, prefix, next_hop, communities,
                               should_be_present=True):
            ri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(vn))
            sr_list = ri.get_static_route_entries()
            if sr_list is None:
                if should_be_present:
                    raise Exception("sr is None")
                else:
                    return
            found = False
            for sr in sr_list.get_route() or []:
                if (sr.prefix == prefix and sr.next_hop == next_hop and
                        sr.community == communities):
                    found = True
                    break
            if found != should_be_present:
                raise Exception("route " + prefix + "" + next_hop + "not found")
            return

        _match_route_table(vn1, "1.1.1.1/0", "10.10.10.10", ['1:1'])

        route.community_attributes.community_attribute.append('1:2')
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)
        _match_route_table(vn1, "1.1.1.1/0", "10.10.10.10", ['1:1', '1:2'])

        route = RouteType(prefix="2.2.2.2/0",
                          next_hop="20.20.20.20", next_hop_type="ip-address")
        routes.add_route(route)
        rt.set_routes(routes)

        self._vnc_lib.route_table_update(rt)
        _match_route_table(vn1, "1.1.1.1/0", "10.10.10.10", ['1:1', '1:2'])
        _match_route_table(vn1, "2.2.2.2/0", "20.20.20.20", [])

        vn2.add_route_table(rt)
        self._vnc_lib.virtual_network_update(vn2)

        _match_route_table(vn1, "1.1.1.1/0", "10.10.10.10", ['1:1', '1:2'])
        _match_route_table(vn1, "2.2.2.2/0", "20.20.20.20", [])
        _match_route_table(vn2, "1.1.1.1/0", "10.10.10.10", ['1:1', '1:2'])
        _match_route_table(vn2, "2.2.2.2/0", "20.20.20.20", [])

        # delete second route and check vn ri sr entries
        routes.delete_route(route)
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        _match_route_table(vn1, "2.2.2.2/0", "20.20.20.20", [], False)
        _match_route_table(vn2, "2.2.2.2/0", "20.20.20.20", [], False)

        @retries(5)
        def _match_route_table_cleanup(vn):
            ri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(vn))
            sr = ri.get_static_route_entries()
            if sr and sr.route:
                raise Exception("sr has route")

        vn2.del_route_table(rt)
        self._vnc_lib.virtual_network_update(vn2)
        _match_route_table(vn1, "1.1.1.1/0", "10.10.10.10", ['1:1', '1:2'])
        _match_route_table_cleanup(vn2)

        # delete first route and check vn ri sr entries
        rt.set_routes(None)
        self._vnc_lib.route_table_update(rt)

        _match_route_table(vn1, "1.1.1.1/0", "10.10.10.10", [], False)

        vn1.del_route_table(rt)
        self._vnc_lib.virtual_network_update(vn1)
        _match_route_table_cleanup(vn1)

        vn1.add_route_table(rt)
        self._vnc_lib.virtual_network_update(vn1)

        vn2.add_route_table(rt)
        self._vnc_lib.virtual_network_update(vn2)

        routes.set_route([])
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        _match_route_table_cleanup(vn1)
        _match_route_table_cleanup(vn2)

        self._vnc_lib.virtual_network_delete(fq_name=vn1.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2.get_fq_name())
        gevent.sleep(2)
        self._vnc_lib.route_table_delete(fq_name=rt.get_fq_name())
    # test_add_delete_static_route
# end class TestRouteTable
