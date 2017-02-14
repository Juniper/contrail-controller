#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import gevent
from testtools.matchers import Contains, Not

try:
    import config_db
except ImportError:
    from schema_transformer import config_db
from vnc_api.vnc_api import (BgpRouterParams, VirtualMachineInterface,
        BgpRouter, LogicalRouter, RouteTargetList, InstanceIp, BgpAsAService,
        NoIdError)

from test_case import STTestCase, retries
from test_route_target import VerifyRouteTarget

sys.path.append("../common/tests")
import test_common


class VerifyBgp(VerifyRouteTarget):
    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib

    @retries(5)
    def check_ri_asn(self, fq_name, rt_target):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        rt_refs = ri.get_route_target_refs()
        if not rt_refs:
            print "retrying ... ", test_common.lineno()
            raise Exception('ri_refs is None for %s' % fq_name)
        for rt_ref in rt_refs:
            if rt_ref['to'][0] == rt_target:
                return
        raise Exception('rt_target %s not found in ri %s' % (rt_target, fq_name))

    @retries(5)
    def check_bgp_asn(self, fq_name, asn):
        router = self._vnc_lib.bgp_router_read(fq_name)
        params = router.get_bgp_router_parameters()
        if not params:
            print "retrying ... ", test_common.lineno()
            raise Exception('bgp params is None for %s' % fq_name)
        self.assertEqual(params.get_autonomous_system(), asn)

    @retries(5)
    def check_lr_asn(self, fq_name, rt_target):
        router = self._vnc_lib.logical_router_read(fq_name)
        rt_refs = router.get_route_target_refs()
        if not rt_refs:
            print "retrying ... ", test_common.lineno()
            raise Exception('ri_refs is None for %s' % fq_name)
        self.assertEqual(rt_refs[0]['to'][0], rt_target)

    @retries(5)
    def check_lr_is_deleted(self, uuid):
        try:
            self._vnc_lib.logical_router_read(id=uuid)
            print "retrying ... ", test_common.lineno()
            raise Exception('logical router %s still exists' % uuid)
        except NoIdError:
            print 'lr deleted'

    @retries(5)
    def check_bgp_peering(self, router1, router2, length):
        r1 = self._vnc_lib.bgp_router_read(fq_name=router1.get_fq_name())
        ref_names = [ref['to'] for ref in r1.get_bgp_router_refs() or []]
        self.assertEqual(len(ref_names), length)
        self.assertThat(ref_names, Contains(router2.get_fq_name()))

    def create_bgp_router(self, name, vendor, asn=None):
        ip_fabric_ri = self._vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project', 'ip-fabric', '__default__'])
        router = BgpRouter(name, parent_obj=ip_fabric_ri)
        params = BgpRouterParams()
        params.vendor = 'contrail'
        params.autonomous_system = asn
        router.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_create(router)
        return router

    @retries(5)
    def check_bgp_no_peering(self, router1, router2):
        r1 = self._vnc_lib.bgp_router_read(fq_name=router1.get_fq_name())
        ref_names = [ref['to'] for ref in r1.get_bgp_router_refs() or []]
        self.assertThat(ref_names, Not(Contains(router2.get_fq_name())))

    @retries(5)
    def check_bgp_router_ip(self, router_name, ip):
        router_obj = self._vnc_lib.bgp_router_read(fq_name_str=router_name)
        self.assertEqual(router_obj.get_bgp_router_parameters().address,
                         ip)
    @retries(5)
    def check_bgp_router_identifier(self, router_name, ip):
        router_obj = self._vnc_lib.bgp_router_read(fq_name_str=router_name)
        self.assertEqual(router_obj.get_bgp_router_parameters().identifier,
                         ip)

    @retries(5)
    def check_v4_bgp_gateway(self, router_name, gateway):
        bgpaas_client_obj = self._vnc_lib.bgp_router_read(
                fq_name_str=router_name)
        self.assertEqual(
                bgpaas_client_obj.get_bgp_router_parameters().gateway_address,
                gateway)

    @retries(5)
    def check_v6_bgp_gateway(self, router_name, gateway):
        bgpaas_client_obj = self._vnc_lib.bgp_router_read(
                fq_name_str=router_name)
        self.assertEqual(
                bgpaas_client_obj.get_bgp_router_parameters().ipv6_gateway_address,
                gateway)


class TestBgp(STTestCase, VerifyBgp):
    # test logical router functionality
    def test_logical_router(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create virtual machine interface
        vmi_name = self.id() + 'vmi1'
        vmi = VirtualMachineInterface(vmi_name, parent_type='project', fq_name=['default-domain', 'default-project', vmi_name])
        vmi.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        # create logical router
        lr_name = self.id() + 'lr1'
        lr = LogicalRouter(lr_name)
        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        lr.set_configured_route_target_list(rtgt_list)
        lr.add_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_create(lr)

        ri_name = self.get_ri_name(vn1_obj)
        self.check_route_target_in_routing_instance(ri_name,rtgt_list.get_route_target())

        rtgt_list.add_route_target('target:1:2')
        lr.set_configured_route_target_list(rtgt_list)
        self._vnc_lib.logical_router_update(lr)
        self.check_route_target_in_routing_instance(ri_name, rtgt_list.get_route_target())

        rtgt_list.delete_route_target('target:1:1')
        lr.set_configured_route_target_list(rtgt_list)
        self._vnc_lib.logical_router_update(lr)
        self.check_route_target_in_routing_instance(ri_name, rtgt_list.get_route_target())

        lr.del_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_update(lr)
        self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self._vnc_lib.logical_router_delete(id=lr.uuid)
        self.check_lr_is_deleted(uuid=lr.uuid)
        self.check_rt_is_deleted(name='target:64512:8000002')

    def test_ibgp_auto_mesh(self):
        # create router1
        r1_name = self.id() + 'router1'
        router1 = self.create_bgp_router(r1_name, 'contrail')

        # create router2
        r2_name = self.id() + 'router2'
        router2 = self.create_bgp_router(r2_name, 'contrail')

        self.check_bgp_peering(router1, router2, 1)

        r3_name = self.id() + 'router3'
        router3 = self.create_bgp_router(r3_name, 'juniper', 1)

        self.check_bgp_peering(router1, router2, 1)

        params = router3.get_bgp_router_parameters()
        params.autonomous_system = 64512
        router3.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_update(router3)

        self.check_bgp_peering(router1, router3, 2)

        r4_name = self.id() + 'router4'
        router4 = self.create_bgp_router(r4_name, 'juniper', 1)

        gsc = self._vnc_lib.global_system_config_read(
            fq_name=['default-global-system-config'])

        gsc.set_autonomous_system(1)
        self.check_bgp_peering(router1, router4, 3)

        self._vnc_lib.bgp_router_delete(id=router1.uuid)
        self._vnc_lib.bgp_router_delete(id=router2.uuid)
        self._vnc_lib.bgp_router_delete(id=router3.uuid)
        self._vnc_lib.bgp_router_delete(id=router4.uuid)
        gevent.sleep(1)

    def test_asn(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')
        self.assertTill(self.vnc_db_has_ident, obj=vn1_obj)

        self.check_ri_asn(self.get_ri_name(vn1_obj), 'target:64512:8000001')

        # create router1
        r1_name = self.id() + 'router1'
        router1 = self.create_bgp_router(r1_name, 'contrail')
        self.check_bgp_asn(router1.get_fq_name(), 64512)

        # create virtual machine interface
        vmi_name = self.id() + 'vmi1'
        vmi = VirtualMachineInterface(vmi_name, parent_type='project',
                                      fq_name=['default-domain',
                                               'default-project', vmi_name])
        vmi.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        # create logical router
        lr_name = self.id() + 'lr1'
        lr = LogicalRouter(lr_name)
        lr.add_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_create(lr)
        self.check_lr_asn(lr.get_fq_name(), 'target:64512:8000002')

        #update global system config but dont change asn value for equality path
        gs = self._vnc_lib.global_system_config_read(
            fq_name=['default-global-system-config'])
        gs.set_autonomous_system(64512)
        self._vnc_lib.global_system_config_update(gs)

        # check route targets
        self.check_ri_asn(self.get_ri_name(vn1_obj), 'target:64512:8000001')
        self.check_bgp_asn(router1.get_fq_name(), 64512)
        self.check_lr_asn(lr.get_fq_name(), 'target:64512:8000002')

        #update ASN value
        gs = self._vnc_lib.global_system_config_read(
            fq_name=[u'default-global-system-config'])
        gs.set_autonomous_system(50000)
        self._vnc_lib.global_system_config_update(gs)

        # check new route targets
        self.check_ri_asn(self.get_ri_name(vn1_obj), 'target:50000:8000001')
        self.check_bgp_asn(router1.get_fq_name(), 50000)
        self.check_lr_asn(lr.get_fq_name(), 'target:50000:8000002')

        self._vnc_lib.logical_router_delete(id=lr.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=vn1_obj.fq_name+[vn1_obj.name])
        self._vnc_lib.bgp_router_delete(id=router1.uuid)
    #end test_asn

    def test_bgpaas(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name,
                                              ['10.0.0.0/24', '1000::/16'])

        project_name = ['default-domain', 'default-project']
        project_obj = self._vnc_lib.project_read(fq_name=project_name)
        port_name = self.id() + 'p1'
        port_obj = VirtualMachineInterface(port_name, parent_obj=project_obj)
        port_obj.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(port_obj)

        v6_obj = InstanceIp(name=port_name+'-v6')
        v6_obj.set_virtual_machine_interface(port_obj)
        v6_obj.set_virtual_network(vn1_obj)
        v6_obj.set_instance_ip_family('v6')
        self._vnc_lib.instance_ip_create(v6_obj)

        v4_obj = InstanceIp(name=port_name+'-v4')
        v4_obj.set_virtual_machine_interface(port_obj)
        v4_obj.set_virtual_network(vn1_obj)
        v4_obj.set_instance_ip_family('v4')
        self._vnc_lib.instance_ip_create(v4_obj)

        bgpaas_name = self.id() + 'bgp1'
        bgpaas = BgpAsAService(bgpaas_name, parent_obj=project_obj,
                               autonomous_system=64512)
        bgpaas.add_virtual_machine_interface(port_obj)
        self._vnc_lib.bgp_as_a_service_create(bgpaas)

        router1_name = vn1_obj.get_fq_name_str() + ':' + vn1_name + ':' + port_name
        self.wait_to_get_object(config_db.BgpAsAServiceST,
                                bgpaas.get_fq_name_str())
        self.wait_to_get_object(config_db.BgpRouterST, router1_name)
        server_fq_name = ':'.join(self.get_ri_name(vn1_obj)) + ':bgpaas-server'
        self.wait_to_get_object(config_db.BgpRouterST, server_fq_name)
        server_router_obj = self._vnc_lib.bgp_router_read(fq_name_str=server_fq_name)

        mx_bgp_router = self.create_bgp_router("mx-bgp-router", "contrail")
        mx_bgp_router_name = mx_bgp_router.get_fq_name_str()
        self.wait_to_get_object(config_db.BgpRouterST, mx_bgp_router_name)
        mx_bgp_router = self._vnc_lib.bgp_router_read(fq_name_str=mx_bgp_router_name)
        self.check_bgp_no_peering(server_router_obj, mx_bgp_router)

        router1_obj = self._vnc_lib.bgp_router_read(fq_name_str=router1_name)
        self.assertEqual(router1_obj.get_bgp_router_parameters().address,
                         '10.0.0.252')
        self.assertEqual(router1_obj.get_bgp_router_parameters().identifier,
                         '10.0.0.252')

        self.check_bgp_peering(server_router_obj, router1_obj, 1)
        self.check_v4_bgp_gateway(router1_name, '10.0.0.254')
        self.check_v6_bgp_gateway(router1_name,
            '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffe')
        # Set suppress_route_advertisement; and expect the gateways
        # to be reset to None in bgpaas-client router
        bgpaas.set_bgpaas_suppress_route_advertisement(True)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        self.check_v4_bgp_gateway(router1_name, None)
        self.check_v6_bgp_gateway(router1_name, None)
        # Unset suppress_route_advertisement; and expect the gateways
        # to be set to gateway addresses in bgpaas-client router
        bgpaas.set_bgpaas_suppress_route_advertisement(False)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        self.check_v4_bgp_gateway(router1_name, '10.0.0.254')
        self.check_v6_bgp_gateway(router1_name,
            '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffe')
        # Set bgpaas_ipv4_mapped_ipv6_nexthop and expect the
        # ipv4-mapped ipv6 address is set as gateway
        bgpaas.set_bgpaas_ipv4_mapped_ipv6_nexthop(True)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        self.check_v6_bgp_gateway(router1_name, '::ffff:10.0.0.254')
        # unset bgpaas_ipv4_mapped_ipv6_nexthop and expect the
        # subnets ipv6 gateway address is set as gateway
        bgpaas.set_bgpaas_ipv4_mapped_ipv6_nexthop(False)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        self.check_v6_bgp_gateway(router1_name,
            '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffe')

        self._vnc_lib.instance_ip_delete(id=v4_obj.uuid)
        v4_obj = InstanceIp(name=port_name+'-v4')
        v4_obj.set_virtual_machine_interface(port_obj)
        v4_obj.set_virtual_network(vn1_obj)
        v4_obj.set_instance_ip_family('v4')
        v4_obj.set_instance_ip_address('10.0.0.60')
        self._vnc_lib.instance_ip_create(v4_obj)

        self.check_bgp_router_ip(router1_name, '10.0.0.60')
        self.check_bgp_router_identifier(router1_name, '10.0.0.60')

        bgpaas.set_bgpaas_ip_address('10.0.0.70')
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        self.check_bgp_router_ip(router1_name, '10.0.0.70')
        v4_obj.del_virtual_machine_interface(port_obj)
        v4_obj.del_virtual_network(vn1_obj)
        self._vnc_lib.instance_ip_delete(id=v4_obj.uuid)
        self.check_bgp_router_ip(router1_name, '10.0.0.70')
        self.check_bgp_router_identifier(router1_name, '10.0.0.70')

        port2_name = self.id() + 'p2'
        port2_obj = VirtualMachineInterface(port2_name, parent_obj=project_obj)
        port2_obj.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(port2_obj)
        bgpaas.add_virtual_machine_interface(port2_obj)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        router2_name = vn1_obj.get_fq_name_str() + ':' + vn1_name + ':' + port2_name
        self.wait_to_get_object(config_db.BgpRouterST, router2_name)

        router2_obj = self._vnc_lib.bgp_router_read(fq_name_str=router2_name)
        self.check_bgp_peering(server_router_obj, router2_obj, 2)
        self.check_bgp_peering(server_router_obj, router1_obj, 2)

        bgpaas.del_virtual_machine_interface(port_obj)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        self.wait_to_delete_object(config_db.BgpRouterST, router1_name)
        self._vnc_lib.bgp_as_a_service_delete(id=bgpaas.uuid)
        self.wait_to_delete_object(config_db.BgpRouterST, router2_name)

        self._vnc_lib.instance_ip_delete(id=v6_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port2_obj.uuid)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self.check_ri_is_deleted(vn1_obj.fq_name+[vn1_obj.name])
        self._vnc_lib.bgp_router_delete(id=mx_bgp_router.uuid)
    # end test_bgpaas
