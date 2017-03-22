#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import gevent

try:
    import to_bgp
except ImportError:
    from schema_transformer import to_bgp
from vnc_api.vnc_api import (RouteTargetList, RouteTable, RouteTableType,
        VirtualNetwork, VirtualMachineInterface, NetworkIpam, VnSubnetsType, IpamSubnetType,
        LogicalRouter, SubnetType, RouteType, CommunityAttributes)

from test_case import STTestCase, retries
from test_policy import VerifyPolicy


class VerifyRouteTable(VerifyPolicy):
    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib


class TestRouteTable(STTestCase, VerifyRouteTable):

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
                raise Exception("route %s %s not found" % (prefix, next_hop))
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
        self._vnc_lib.route_table_delete(fq_name=rt.get_fq_name())
    # test_add_delete_static_route

    def test_public_snat_routes(self):

        #create private vn
        vn_private_name = 'vn1'
        vn_private = self.create_virtual_network(vn_private_name, "1.0.0.0/24")

        # create virtual machine interface
        vmi_name = self.id() + 'vmi1'
        vmi = VirtualMachineInterface(vmi_name, parent_type='project',
                        fq_name=['default-domain', 'default-project', vmi_name])
        vmi.add_virtual_network(vn_private)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        #create public vn
        vn_public_name = 'vn-public'
        vn_public = VirtualNetwork(vn_public_name)
        vn_public.set_router_external(True)
        ipam_obj = NetworkIpam('ipam')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn_public.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))
        self._vnc_lib.virtual_network_create(vn_public)

        #create logical router, set route targets,
        #add private network and extend lr to public network
        lr_name = self.id() + 'lr1'
        lr = LogicalRouter(lr_name)
        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        lr.set_configured_route_target_list(rtgt_list)
        lr.add_virtual_machine_interface(vmi)
        lr.add_virtual_network(vn_public)
        self._vnc_lib.logical_router_create(lr)

        @retries(5)
        def _match_route_table(rtgt_list, ri_name):
            lri = self._vnc_lib.routing_instance_read(
                fq_name_str=ri_name)
            sr = lri.get_static_route_entries()
            if sr is None:
                raise Exception("sr is None")
            route = sr.route[0]
            self.assertEqual(route.prefix, "0.0.0.0/0")
            self.assertEqual(route.next_hop, "100.64.0.4")
            for rtgt in rtgt_list:
                self.assertIn(rtgt, route.route_target)

        @retries(5)
        def _wait_to_get_si():
            si_list = self._vnc_lib.service_instances_list()
            si = si_list.get("service-instances")[0]
            si = self._vnc_lib.service_instance_read(id=si.get("uuid"))
            return si

        @retries(5)
        def _wait_to_delete_si():
            si_list = self._vnc_lib.service_instances_list()
            try:
                si = si_list.get("service-instances")[0]
                si = self._vnc_lib.service_instance_read(id=si.get("uuid"))
                raise
            except:
                pass

        @retries(5)
        def _wait_to_delete_ip(vn_fq_name):
            vn = self._vnc_lib.virtual_network_read(fq_name=vn_fq_name)
            ip_refs = vn.get_instance_ip_back_refs()
            if ip_refs:
                raise
            return
        # end

        si = _wait_to_get_si()
        si_props = si.get_service_instance_properties().get_interface_list()[1]
        ri_name = si_props.virtual_network + ":" + si_props.virtual_network.split(':')[-1]
        _match_route_table(['target:1:1', 'target:64512:8000003'], ri_name)

        rtgt_list = RouteTargetList(route_target=['target:2:2'])
        lr.set_configured_route_target_list(rtgt_list)
        self._vnc_lib.logical_router_update(lr)
        _match_route_table(['target:2:2', 'target:64512:8000003'], ri_name)

        lr.del_virtual_network(vn_public)
        self._vnc_lib.logical_router_update(lr)
        _wait_to_delete_si()

        #cleanup
        self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi.get_fq_name())
        _wait_to_delete_ip(vn_private.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn_private.get_fq_name())
        _wait_to_delete_ip(vn_public.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn_public.get_fq_name())
    # end

# end class TestRouteTable
