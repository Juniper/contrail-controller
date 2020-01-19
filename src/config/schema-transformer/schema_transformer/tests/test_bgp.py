#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from __future__ import print_function

from builtins import range
from unittest import skip

from cfgm_common import BGP_RTGT_ALLOC_PATH_TYPE0
from cfgm_common import BGP_RTGT_ALLOC_PATH_TYPE1_2
from cfgm_common import get_lr_internal_vn_name
from cfgm_common.exceptions import BadRequest
from cfgm_common.tests import test_common
import gevent
from testtools.matchers import Contains, Not
from vnc_api.gen.resource_client import Fabric
from vnc_api.vnc_api import AddressFamilies
from vnc_api.vnc_api import BgpAsAService, BgpRouter, BgpRouterParams
from vnc_api.vnc_api import InstanceIp, LogicalRouter
from vnc_api.vnc_api import NoIdError, RouteTargetList
from vnc_api.vnc_api import PhysicalRouter
from vnc_api.vnc_api import SubCluster, UserCredentials
from vnc_api.vnc_api import VirtualMachineInterface

from schema_transformer import to_bgp
from schema_transformer.resources.bgp_as_a_service import BgpAsAServiceST
from schema_transformer.resources.bgp_router import BgpRouterST
from schema_transformer.resources.global_system_config \
    import GlobalSystemConfigST
from schema_transformer.resources.virtual_machine import VirtualMachineST
from .test_case import retries, STTestCase
from .test_route_target import VerifyRouteTarget


class VerifyBgp(VerifyRouteTarget):
    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib

    def get_lr_internal_vn(self, lr_fq_name, err_on_fail=True):
        intvns = []
        lr = self._vnc_lib.logical_router_read(lr_fq_name)
        for vn_ref in lr.get_virtual_network_refs() or []:
            if ('logical_router_virtual_network_type' in vn_ref[
                    'attr'].attr_fields and
                    vn_ref['attr'].logical_router_virtual_network_type ==
                    'InternalVirtualNetwork'):
                intvns.append(vn_ref)
        if len(intvns) != 1 and err_on_fail:
            print("Expecting only one internalVN connected to LR")
            raise Exception("Internal VN attached to LR (%s) == %s" % (lr.uuid,
                                                                       intvns))
        return intvns

    @retries(5)
    def check_no_si_in_lr(self, lr_fq_name):
        lr = self._vnc_lib.logical_router_read(lr_fq_name)
        si_refs = lr.get_service_instance_refs()
        if si_refs:
            print('Service Instance Refs: \n\n%s\n' % si_refs)
            raise Exception("Service Instance Refs found "
                            "while its not expected")

    @retries(8, 2)
    def check_4byteASN_ri_target(self, fq_name, rt_target=None):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        rt_refs = ri.get_route_target_refs()
        if not rt_refs:
            print("retrying ... ", test_common.lineno())
            raise Exception('ri_refs is None for %s' % fq_name)
        if not rt_target:
            return rt_refs[0]['to'][0]
        for rt_ref in rt_refs:
            if rt_target in rt_ref['to'][0]:
                return rt_ref['to'][0]
        raise Exception('rt_target prefix %s not found in ri %s' % (rt_target,
                                                                    fq_name))

    @retries(5)
    def check_ri_target(self, fq_name, rt_target=None):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        rt_refs = ri.get_route_target_refs()
        if not rt_refs:
            print("retrying ... ", test_common.lineno())
            raise Exception('ri_refs is None for %s' % fq_name)
        if not rt_target:
            return rt_refs[0]['to'][0]
        for rt_ref in rt_refs:
            if rt_ref['to'][0] == rt_target:
                return rt_target
            if (rt_target.rsplit(':', 1)[1] > 0xFFFF):
                if rt_target in rt_ref['to'][0]:
                    return rt_target
        raise Exception('rt_target %s not found in ri %s' % (rt_target,
                                                             fq_name))

    @retries(5)
    def check_bgp_asn(self, fq_name, asn):
        router = self._vnc_lib.bgp_router_read(fq_name)
        params = router.get_bgp_router_parameters()
        if not params:
            print("retrying ... ", test_common.lineno())
            raise Exception('bgp params is None for %s' % fq_name)
        self.assertEqual(params.get_autonomous_system(), asn)

    @retries(5)
    def check_lr_target(self, fq_name, rt_target=None):
        router = self._vnc_lib.logical_router_read(fq_name)
        rt_refs = router.get_route_target_refs()
        if not rt_refs:
            print("retrying ... ", test_common.lineno())
            raise Exception('ri_refs is None for %s' % fq_name)
        if rt_target:
            self.assertEqual(rt_refs[0]['to'][0], rt_target)
        return rt_refs[0]['to'][0]

    @retries(5)
    def check_lr_is_deleted(self, uuid):
        try:
            self._vnc_lib.logical_router_read(id=uuid)
            print("retrying ... ", test_common.lineno())
            raise Exception('logical router %s still exists' % uuid)
        except NoIdError:
            print('lr deleted')

    @retries(5)
    def check_bgp_peering(self, router1, router2, length):
        r1 = self._vnc_lib.bgp_router_read(fq_name=router1.get_fq_name())
        ref_names = [ref['to'] for ref in r1.get_bgp_router_refs() or []]
        self.assertEqual(len(ref_names), length)
        self.assertThat(ref_names, Contains(router2.get_fq_name()))

    def create_bgp_router(self, name, vendor, asn=None, cluster_id=None,
                          router_type=None):
        ip_fabric_ri = self._vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        router = BgpRouter(name, parent_obj=ip_fabric_ri)
        params = BgpRouterParams()
        params.vendor = 'contrail'
        params.autonomous_system = asn
        if cluster_id:
            params.cluster_id = cluster_id
        if router_type:
            params.router_type = router_type
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
            bgpaas_client_obj.get_bgp_router_parameters(
            ).ipv6_gateway_address,
            gateway)


class TestVxLan(STTestCase, VerifyBgp):

    def test_vn_internal_lr(self):
        proj_obj = self._vnc_lib.project_read(
            fq_name=['default-domain', 'default-project'])
        lr_name = self.id() + '_logicalrouter'
        lr = LogicalRouter(lr_name)
        lr.set_logical_router_type('vxlan-routing')
        rtgt_list = RouteTargetList(route_target=['target:3:1'])
        lr.set_configured_route_target_list(rtgt_list)
        self._vnc_lib.logical_router_create(lr)
        lr_read = self._vnc_lib.logical_router_read(fq_name=lr.get_fq_name())
        _ = self.check_lr_target(lr_read.get_fq_name())
        ivn_name = get_lr_internal_vn_name(lr_read.uuid)
        lr_ivn_read = self._vnc_lib.virtual_network_read(
            fq_name=proj_obj.get_fq_name() + [ivn_name])
        ri_name = self.get_ri_name(lr_ivn_read)
        self.check_route_target_in_routing_instance(
            ri_name, rtgt_list.get_route_target())
        # change RT and see it is getting updated
        rtgt_list.delete_route_target('target:3:1')
        rtgt_list.add_route_target('target:4:1')
        lr.set_configured_route_target_list(rtgt_list)
        self._vnc_lib.logical_router_update(lr)
        self.check_route_target_in_routing_instance(
            ri_name, rtgt_list.get_route_target())
        # cleanup
        self._vnc_lib.logical_router_delete(id=lr.uuid)
        self.check_lr_is_deleted(uuid=lr.uuid)
        self.check_vn_is_deleted(uuid=lr_ivn_read.uuid)
    # end test_vn_internal_lr


class TestBgp(STTestCase, VerifyBgp):

    @skip("CEM-7696-Skip these until we figure out the reason for flakiness")
    def test_vxlan_routing(self):
        # create  vn1
        vn1_name = self.id() + '_vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create virtual machine interface
        vmi_name = self.id() + '_vmi1'
        vmi = VirtualMachineInterface(vmi_name,
                                      parent_type='project',
                                      fq_name=['default-domain',
                                               'default-project',
                                               vmi_name])
        vmi.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        # Enable vxlan_routing
        # and verify RT not attached to LR
        ri_name = self.get_ri_name(vn1_obj)
        ri_obj = self._vnc_lib.routing_instance_read(fq_name=ri_name)
        vn_rt_refs = set([ref['to'][0]
                         for ref in ri_obj.get_route_target_refs() or []])

        # create logical router with RT
        lr_name = self.id() + '_lr1'
        lr = LogicalRouter(lr_name)
        lr.set_logical_router_type('vxlan-routing')
        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        lr.set_configured_route_target_list(rtgt_list)
        lr.add_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_create(lr)
        lr = self._vnc_lib.logical_router_read(fq_name=lr.get_fq_name())
        _ = self.check_lr_target(lr.get_fq_name())
        ri_obj = self._vnc_lib.routing_instance_read(fq_name=ri_name)
        vn_rt_refs_with_lr = set([ref['to'][0]
                                 for ref in ri_obj.get_route_target_refs() or
                                  []])
        # ensure no RT from LR is attached to VN
        self.assertTrue(vn_rt_refs_with_lr == vn_rt_refs,
                        msg='RT attached to VN is different after LR creation')

        # configure a new route target after
        # LR is created
        rtgt_list.add_route_target('target:1:2')
        lr.set_configured_route_target_list(rtgt_list)
        self._vnc_lib.logical_router_update(lr)
        lr = self._vnc_lib.logical_router_read(fq_name=lr.get_fq_name())
        _ = self.check_lr_target(lr.get_fq_name())
        ri_obj = self._vnc_lib.routing_instance_read(fq_name=ri_name)
        vn_rt_refs_with_lr = set([ref['to'][0]
                                 for ref in ri_obj.get_route_target_refs() or
                                  []])
        # ensure no RT from LR is attached to VN
        self.assertTrue(vn_rt_refs_with_lr == vn_rt_refs,
                        msg='RT attached to VN is different after LR creation')

        # check no service instance generated
        self.check_no_si_in_lr(lr.get_fq_name())

        # cleanup
        lr.del_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_update(lr)
        self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self._vnc_lib.logical_router_delete(id=lr.uuid)
        self.check_lr_is_deleted(uuid=lr.uuid)
    # end test_vxlan_routing

    # test logical router functionality
    def test_logical_router(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create virtual machine interface
        vmi_name = self.id() + 'vmi1'
        vmi = VirtualMachineInterface(vmi_name, parent_type='project',
                                      fq_name=['default-domain',
                                               'default-project',
                                               vmi_name])
        vmi.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        # create logical router
        lr_name = self.id() + 'lr1'
        lr = LogicalRouter(lr_name)
        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        lr.set_configured_route_target_list(rtgt_list)
        lr.add_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_create(lr)
        lr_target = self.check_lr_target(lr.get_fq_name())
        ri_name = self.get_ri_name(vn1_obj)
        self.check_route_target_in_routing_instance(
            ri_name,
            rtgt_list.get_route_target())

        rtgt_list.add_route_target('target:1:2')
        lr.set_configured_route_target_list(rtgt_list)
        self._vnc_lib.logical_router_update(lr)
        self.check_route_target_in_routing_instance(
            ri_name,
            rtgt_list.get_route_target())

        rtgt_list.delete_route_target('target:1:1')
        lr.set_configured_route_target_list(rtgt_list)
        self._vnc_lib.logical_router_update(lr)
        self.check_route_target_in_routing_instance(
            ri_name,
            rtgt_list.get_route_target())

        lr.del_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_update(lr)
        self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self._vnc_lib.logical_router_delete(id=lr.uuid)
        self.check_lr_is_deleted(uuid=lr.uuid)
        self.check_rt_is_deleted(name=lr_target)

    # test logical router delete failure reverts
    # internalVN deletion
    def test_logical_router_delete(self):
        # configure vxlan routing
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

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
        lr.set_logical_router_type('vxlan-routing')
        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        lr.set_configured_route_target_list(rtgt_list)
        lr.add_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_create(lr)
        lr = self._vnc_lib.logical_router_read(id=lr.uuid)
        lr_target = self.check_lr_target(lr.get_fq_name())
        intvns = self.get_lr_internal_vn(lr.get_fq_name())

        # Deletion
        lr.del_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_update(lr)
        self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)

        # internalVN should be restored when LR delete fails
        try:
            # override libs
            org_dbe_delete = \
                self._server_info['api_server']._db_conn.dbe_delete

            def tmp_dbe_delete(obj_type, id, read_result):
                if obj_type == 'logical_router':
                    return False, (400, 'Fake LR delete failure')
                else:
                    return org_dbe_delete(obj_type, id, read_result)
            self._server_info['api_server']._db_conn.dbe_delete = \
                tmp_dbe_delete

            try:
                self._vnc_lib.logical_router_delete(id=lr.uuid)
            except BadRequest:
                lr = self._vnc_lib.logical_router_read(id=lr.uuid)
                self.get_lr_internal_vn(lr.get_fq_name())
            else:
                raise Exception('UT: Fake LR delete didnt '
                                'create BadRequest exception')
        finally:
            self._server_info['api_server']._db_conn.dbe_delete = \
                org_dbe_delete

        # internalVN should be removed when LR is deleted successfully
        self._vnc_lib.logical_router_delete(id=lr.uuid)
        self.check_lr_is_deleted(uuid=lr.uuid)
        self.assertRaises(NoIdError, self._vnc_lib.virtual_network_read,
                          intvns[0]['uuid'])
        self.check_rt_is_deleted(name=lr_target)

    def create_bgp_router_sub(self, name, vendor='contrail', asn=None,
                              router_type=None, sub_cluster=None):
        ip_fabric_ri = self._vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])
        router = BgpRouter(name, parent_obj=ip_fabric_ri)
        params = BgpRouterParams(router_type=router_type)
        params.vendor = 'unknown'
        params.autonomous_system = asn
        router.set_bgp_router_parameters(params)
        if sub_cluster:
            router.add_sub_cluster(sub_cluster)
        self._vnc_lib.bgp_router_create(router)
        return router

    def _get_ip_fabric_ri_obj(self):
        # TODO pick fqname hardcode from common
        rt_inst_obj = self._vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project',
                     'ip-fabric', '__default__'])

        return rt_inst_obj
    # end _get_ip_fabric_ri_obj

    def create_router(self, name, mgmt_ip, vendor='juniper', product='mx',
                      ignore_pr=False, role=None, cluster_id=None,
                      ignore_bgp=False, fabric=None, asn=64512):
        bgp_router, pr = None, None

        if not ignore_bgp:
            bgp_router = BgpRouter(name,
                                   display_name=name + "-bgp",
                                   parent_obj=self._get_ip_fabric_ri_obj())
            params = BgpRouterParams()
            params.address = mgmt_ip
            params.identifier = mgmt_ip
            params.address_families = AddressFamilies(['route-target',
                                                       'inet-vpn',
                                                       'e-vpn',
                                                       'inet6-vpn'])
            params.vendor = vendor
            params.autonomous_system = asn
            params.cluster_id = cluster_id
            bgp_router.set_bgp_router_parameters(params)
            self._vnc_lib.bgp_router_create(bgp_router)
            gevent.sleep(1)

        if not ignore_pr:
            pr = PhysicalRouter(name, display_name=name)
            pr.physical_router_management_ip = mgmt_ip
            pr.physical_router_vendor_name = vendor
            pr.physical_router_product_name = product
            pr.physical_router_vnc_managed = True
            if role:
                pr.physical_router_role = role
            uc = UserCredentials('user', 'pw')
            pr.set_physical_router_user_credentials(uc)
            pr.set_fabric(fabric)
            if not ignore_bgp:
                pr.set_bgp_router(bgp_router)
            self._vnc_lib.physical_router_create(pr)
            gevent.sleep(1)

        return bgp_router, pr
    # end create_router

    def delete_routers(self, bgp_router=None, pr=None):
        if pr:
            self._vnc_lib.physical_router_delete(fq_name=pr.get_fq_name())
        if bgp_router:
            self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())
        return
    # end delete_routers

    def delete_fabric(self, fab=None):
        if fab:
            self._vnc_lib.fabric_delete(fq_name=fab.get_fq_name())
    # end delete_fabric

    def test_ibgp_auto_mesh_sub(self):
        GlobalSystemConfigST.ibgp_auto_mesh = True
        self.assertEqual(GlobalSystemConfigST.get_ibgp_auto_mesh(),
                         True, "ibgp_auto_mesh_toggle_test")
        # create subcluster
        sub_cluster_obj = SubCluster('test-host', sub_cluster_asn=64513)

        self._vnc_lib.sub_cluster_create(sub_cluster_obj)
        sub_cluster_obj = self._vnc_lib.sub_cluster_read(
            fq_name=sub_cluster_obj.get_fq_name())
        # create router1
        r1_name = self.id() + 'router1'
        router1 = self.create_bgp_router_sub(
            r1_name, 'contrail', asn=64513,
            router_type='external-control-node',
            sub_cluster=sub_cluster_obj)
        # create router2
        r2_name = self.id() + 'router2'
        router2 = self.create_bgp_router_sub(
            r2_name, 'contrail', asn=64513,
            router_type='external-control-node',
            sub_cluster=sub_cluster_obj)
        # create router3
        r3_name = self.id() + 'router3'
        router3 = self.create_bgp_router(r3_name, 'contrail')
        # create router4
        r4_name = self.id() + 'router4'
        router4 = self.create_bgp_router(r4_name, 'contrail')

        self.check_bgp_peering(router3, router4, 1)
        self.check_bgp_peering(router1, router2, 1)

        self._vnc_lib.bgp_router_delete(id=router1.uuid)
        self._vnc_lib.bgp_router_delete(id=router2.uuid)
        self._vnc_lib.bgp_router_delete(id=router3.uuid)
        self._vnc_lib.bgp_router_delete(id=router4.uuid)
        self._vnc_lib.sub_cluster_delete(id=sub_cluster_obj.uuid)
        gevent.sleep(1)
    # end test_ibgp_auto_mesh_sub

    def test_ibgp_auto_mesh_fabric_phy_rtr_to_bgp_rtr_dependency(self):
        GlobalSystemConfigST.ibgp_auto_mesh = True
        self.assertEqual(GlobalSystemConfigST.get_ibgp_auto_mesh(),
                         True, "ibgp_auto_mesh_toggle_test")

        # Fabric 1 has 1 RR,spine + 1 leafs
        fab1 = self._vnc_lib.fabric_create(Fabric('fab1'))
        fab1 = self._vnc_lib.fabric_read(id=fab1)

        bgp_router1_fab1, pr1_fab1 = self.create_router(
            'router1_fab1' + self.id(), '1.1.1.1',
            product="qfx1000", role="spine", cluster_id=1000, fabric=fab1)
        bgp_router2_fab1, ignore = self.create_router(
            'router2_fab1' + self.id(), '1.1.1.2',
            product="qfx1000", ignore_pr=True, role="leaf", fabric=fab1)
        ignore, pr2_fab1 = self.create_router(
            'router2_fab1' + self.id(), '1.1.1.2',
            product="qfx1000", ignore_bgp=True, role="leaf", fabric=fab1)

        # Fabric 2 has 1 RR, spine + 2 leafs
        fab2 = self._vnc_lib.fabric_create(Fabric('fab2'))
        fab2 = self._vnc_lib.fabric_read(id=fab2)

        bgp_router1_fab2, pr1_fab2 = self.create_router(
            'router1_fab2' + self.id(), '2.1.1.1',
            product="qfx1000", role="spine", cluster_id=2000, fabric=fab2)

        bgp_router2_fab2, ignore = self.create_router(
            'router2_fab2' + self.id(), '2.1.1.2',
            product="qfx1000", ignore_pr=True, role="leaf", fabric=fab2)
        ignore, pr2_fab2 = self.create_router(
            'router2_fab2' + self.id(), '1.1.1.2',
            product="qfx1000", ignore_bgp=True, role="leaf", fabric=fab2)

        pr2_fab1.set_bgp_router(bgp_router2_fab1)
        self._vnc_lib.physical_router_update(pr2_fab1)
        gevent.sleep(1)
        pr2_fab2.set_bgp_router(bgp_router2_fab2)
        self._vnc_lib.physical_router_update(pr2_fab2)
        gevent.sleep(1)

        bgp_router1_fab1 = self._vnc_lib.bgp_router_read(
            fq_name=bgp_router1_fab1.fq_name)
        bgp_router2_fab1 = self._vnc_lib.bgp_router_read(
            fq_name=bgp_router2_fab1.fq_name)
        bgp_router1_fab2 = self._vnc_lib.bgp_router_read(
            fq_name=bgp_router1_fab2.fq_name)
        bgp_router2_fab2 = self._vnc_lib.bgp_router_read(
            fq_name=bgp_router2_fab2.fq_name)

        self.check_bgp_peering(bgp_router2_fab1, bgp_router1_fab1, 1)
        self.check_bgp_peering(bgp_router1_fab1, bgp_router2_fab1, 1)

        self.check_bgp_peering(bgp_router2_fab2, bgp_router1_fab2, 1)
        self.check_bgp_peering(bgp_router1_fab2, bgp_router2_fab2, 1)

        self.delete_routers(bgp_router1_fab1, pr1_fab1)
        self.delete_routers(bgp_router2_fab1, pr2_fab1)
        self.delete_routers(bgp_router1_fab2, pr1_fab2)
        self.delete_routers(bgp_router2_fab2, pr2_fab2)
        self.delete_fabric(fab1)
        self.delete_fabric(fab2)

        gevent.sleep(1)
    # end test_ibgp_auto_mesh_fabric_phy_rtr_to_bgp_rtr_dependency

    def test_ibgp_auto_mesh_fabric_with_different_asn(self):
        # global asn = 64512
        GlobalSystemConfigST.ibgp_auto_mesh = True
        self.assertEqual(GlobalSystemConfigST.get_ibgp_auto_mesh(),
                         True, "ibgp_auto_mesh_toggle_test")

        # Fabric 1 has 1 RR,spine + 2 leafs, asn = 64000
        fab1 = self._vnc_lib.fabric_create(Fabric('fab1'))
        fab1 = self._vnc_lib.fabric_read(id=fab1)

        bgp_router1_fab1, pr1_fab1 = self.create_router(
            'router1_fab1' + self.id(), '1.1.1.1',
            product="qfx1000", role="spine", cluster_id=1000,
            fabric=fab1, asn=64000)

        bgp_router2_fab1, pr2_fab1 = self.create_router(
            'router2_fab1' + self.id(), '1.1.1.2',
            product="qfx1000", role="leaf", fabric=fab1,
            asn=64000)

        bgp_router3_fab1, pr3_fab1 = self.create_router(
            'router3_fab1' + self.id(), '1.1.1.3',
            product="qfx1000", role="leaf", fabric=fab1,
            asn=64000)

        # Fabric 2 has 1 RR, spine + 2 leafs, asn = 64001
        fab2 = self._vnc_lib.fabric_create(Fabric('fab2'))
        fab2 = self._vnc_lib.fabric_read(id=fab2)

        bgp_router1_fab2, pr1_fab2 = self.create_router(
            'router1_fab2' + self.id(), '2.1.1.1',
            product="qfx1000", role="spine", cluster_id=2000,
            fabric=fab2, asn=64001)

        bgp_router2_fab2, pr2_fab2 = self.create_router(
            'router2_fab2' + self.id(), '2.1.1.2',
            product="qfx1000", role="leaf", fabric=fab2,
            asn=64001)

        bgp_router3_fab2, pr3_fab2 = self.create_router(
            'router3_fab2' + self.id(), '2.1.1.3',
            product="qfx1000", role="leaf", fabric=fab2,
            asn=64001)

        # router1 and router2 should not be connected, both of them should be
        # connected to router3
        self.check_bgp_peering(bgp_router2_fab1, bgp_router1_fab1, 1)
        self.check_bgp_peering(bgp_router3_fab1, bgp_router1_fab1, 1)
        self.check_bgp_peering(bgp_router1_fab1, bgp_router2_fab1, 2)
        self.check_bgp_peering(bgp_router1_fab1, bgp_router3_fab1, 2)

        self.check_bgp_peering(bgp_router2_fab2, bgp_router1_fab2, 1)
        self.check_bgp_peering(bgp_router3_fab2, bgp_router1_fab2, 1)
        self.check_bgp_peering(bgp_router1_fab2, bgp_router2_fab2, 2)
        self.check_bgp_peering(bgp_router1_fab2, bgp_router3_fab2, 2)

        self.delete_routers(bgp_router1_fab1, pr1_fab1)
        self.delete_routers(bgp_router2_fab1, pr2_fab1)
        self.delete_routers(bgp_router3_fab1, pr3_fab1)
        self.delete_routers(bgp_router1_fab2, pr1_fab2)
        self.delete_routers(bgp_router2_fab2, pr2_fab2)
        self.delete_routers(bgp_router3_fab2, pr3_fab2)
        self.delete_fabric(fab1)
        self.delete_fabric(fab2)

        gevent.sleep(1)
    # end test_ibgp_auto_mesh_fab

    def test_ibgp_auto_mesh_fab_with_control_node(self):
        GlobalSystemConfigST.ibgp_auto_mesh = False
        self.assertEqual(GlobalSystemConfigST.get_ibgp_auto_mesh(),
                         False, "ibgp_auto_mesh_toggle_test")

        # Fabric 1 has 1 RR,spine + 2 leafs
        fab1 = self._vnc_lib.fabric_create(Fabric('fab1'))
        fab1 = self._vnc_lib.fabric_read(id=fab1)

        bgp_router1_fab1, pr1_fab1 = self.create_router(
            'router1_fab1' + self.id(), '1.1.1.1',
            product="qfx1000", role="spine", cluster_id=1000, fabric=fab1)

        bgp_router2_fab1, pr2_fab1 = self.create_router(
            'router2_fab1' + self.id(), '1.1.1.2',
            product="qfx1000", role="leaf", fabric=fab1)
        bgp_router3_fab1, pr3_fab1 = self.create_router(
            'router3_fab1' + self.id(), '1.1.1.3',
            product="qfx1000", role="leaf", fabric=fab1)

        params_bgp_router2 = bgp_router2_fab1.get_bgp_router_parameters()
        params_bgp_router2.router_type = 'control-node'
        bgp_router2_fab1.set_bgp_router_parameters(params_bgp_router2)
        self._vnc_lib.bgp_router_update(bgp_router2_fab1)
        gevent.sleep(1)

        # Fabric 2 has 1 RR, spine + 2 leafs
        fab2 = self._vnc_lib.fabric_create(Fabric('fab2'))
        fab2 = self._vnc_lib.fabric_read(id=fab2)

        bgp_router1_fab2, pr1_fab2 = self.create_router(
            'router1_fab2' + self.id(), '2.1.1.1',
            product="qfx1000", role="spine", cluster_id=2000, fabric=fab2)

        bgp_router2_fab2, pr2_fab2 = self.create_router(
            'router2_fab2' + self.id(), '2.1.1.2',
            product="qfx1000", role="leaf", fabric=fab2)
        bgp_router3_fab2, pr3_fab2 = self.create_router(
            'router3_fab2' + self.id(), '2.1.1.3',
            product="qfx1000", role="leaf", fabric=fab2)

        # Enable auto-mesh and then trigger update peering
        GlobalSystemConfigST.ibgp_auto_mesh = True
        self.assertEqual(GlobalSystemConfigST.get_ibgp_auto_mesh(),
                         True, "ibgp_auto_mesh_toggle_test")

        for router in list(BgpRouterST._dict.values()):
            router.update()

        for router in list(BgpRouterST._dict.values()):
            router.update_peering()

        # router1 and router2 should not be connected, both of them should be
        # connected to router3
        self.check_bgp_peering(bgp_router2_fab1, bgp_router1_fab1, 1)
        self.check_bgp_peering(bgp_router3_fab1, bgp_router1_fab1, 1)
        self.check_bgp_peering(bgp_router1_fab1, bgp_router2_fab1, 2)
        self.check_bgp_peering(bgp_router1_fab1, bgp_router3_fab1, 2)

        self.check_bgp_peering(bgp_router2_fab2, bgp_router1_fab2, 1)
        self.check_bgp_peering(bgp_router3_fab2, bgp_router1_fab2, 1)
        self.check_bgp_peering(bgp_router1_fab2, bgp_router2_fab2, 2)
        self.check_bgp_peering(bgp_router1_fab2, bgp_router3_fab2, 2)

        self.delete_routers(bgp_router1_fab1, pr1_fab1)
        self.delete_routers(bgp_router2_fab1, pr2_fab1)
        self.delete_routers(bgp_router3_fab1, pr3_fab1)
        self.delete_routers(bgp_router1_fab2, pr1_fab2)
        self.delete_routers(bgp_router2_fab2, pr2_fab2)
        self.delete_routers(bgp_router3_fab2, pr3_fab2)
        self.delete_fabric(fab1)
        self.delete_fabric(fab2)

        gevent.sleep(1)
    # end test_ibgp_auto_mesh_fab

    def test_ibgp_auto_route_reflector(self):
        GlobalSystemConfigST.ibgp_auto_mesh = True
        self.assertEqual(GlobalSystemConfigST.get_ibgp_auto_mesh(),
                         True, "ibgp_auto_mesh_toggle_test")

        # create route reflector
        r3_name = self.id() + 'router3'
        router3 = self.create_bgp_router(r3_name, 'contrail', None,
                                         cluster_id=1000)

        # create router1
        r1_name = self.id() + 'router1'
        router1 = self.create_bgp_router(r1_name, 'contrail')

        # create router2
        r2_name = self.id() + 'router2'
        router2 = self.create_bgp_router(r2_name, 'contrail')

        # router1 and router2 should not be connected, both of them should be
        # connected to router3
        self.check_bgp_peering(router1, router3, 1)
        self.check_bgp_peering(router2, router3, 1)
        self.check_bgp_peering(router3, router1, 2)
        self.check_bgp_peering(router3, router2, 2)

        self._vnc_lib.bgp_router_delete(id=router1.uuid)
        self._vnc_lib.bgp_router_delete(id=router2.uuid)
        self._vnc_lib.bgp_router_delete(id=router3.uuid)
        gevent.sleep(1)

    def test_ibgp_auto_mesh(self):
        GlobalSystemConfigST.ibgp_auto_mesh = True
        self.assertEqual(GlobalSystemConfigST.get_ibgp_auto_mesh(),
                         True, "ibgp_auto_mesh_toggle_test")

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

    def test_ibgp_full_mesh_to_route_reflector(self):
        GlobalSystemConfigST.ibgp_auto_mesh = True

        # create router1
        r1_name = self.id() + 'router1'
        router1 = self.create_bgp_router(r1_name, 'juniper')
        # create router2
        r2_name = self.id() + 'router2'
        router2 = self.create_bgp_router(r2_name, 'juniper')
        # create router3
        r3_name = self.id() + 'router3'
        router3 = self.create_bgp_router(r3_name, 'juniper')
        gevent.sleep(1)

        # verify full mesh created
        self.check_bgp_peering(router1, router2, 2)
        self.check_bgp_peering(router1, router3, 2)
        self.check_bgp_peering(router2, router1, 2)
        self.check_bgp_peering(router2, router3, 2)
        self.check_bgp_peering(router3, router1, 2)
        self.check_bgp_peering(router3, router2, 2)

        params = router1.get_bgp_router_parameters()
        params.cluster_id = 100
        router1.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_update(router1)
        gevent.sleep(1)

        # verify full mesh reduced to route reflector
        self.check_bgp_peering(router1, router2, 2)
        self.check_bgp_peering(router1, router3, 2)
        self.check_bgp_peering(router2, router1, 1)
        self.check_bgp_peering(router3, router1, 1)
        self.check_bgp_no_peering(router2, router3)
        self.check_bgp_no_peering(router3, router2)

        # make routers as contrail controllers and route reflector is external
        # router, contrail controllers should still create full mesh
        params2 = router2.get_bgp_router_parameters()
        params2.router_type = 'control-node'
        router2.set_bgp_router_parameters(params2)
        self._vnc_lib.bgp_router_update(router2)
        params3 = router2.get_bgp_router_parameters()
        params3.router_type = 'control-node'
        router3.set_bgp_router_parameters(params3)
        self._vnc_lib.bgp_router_update(router3)
        gevent.sleep(1)

        # verify full mesh created
        self.check_bgp_peering(router1, router2, 2)
        self.check_bgp_peering(router1, router3, 2)
        self.check_bgp_peering(router2, router1, 2)

        # reset the cluster id so that there is no rr any more
        params.cluster_id = None
        router1.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_update(router1)
        gevent.sleep(1)

        # verify full mesh created
        self.check_bgp_peering(router1, router2, 2)
        self.check_bgp_peering(router1, router3, 2)
        self.check_bgp_peering(router2, router1, 2)
        self.check_bgp_peering(router2, router3, 2)
        self.check_bgp_peering(router3, router1, 2)
        self.check_bgp_peering(router3, router2, 2)

        self._vnc_lib.bgp_router_delete(id=router1.uuid)
        self._vnc_lib.bgp_router_delete(id=router2.uuid)
        self._vnc_lib.bgp_router_delete(id=router3.uuid)
        gevent.sleep(1)

    def test_ibgp_full_mesh_with_route_reflector_change(self):
        GlobalSystemConfigST.ibgp_auto_mesh = True

        # create router1
        r1_name = self.id() + 'router1'
        router1 = self.create_bgp_router(r1_name, 'juniper')
        # create router2
        r2_name = self.id() + 'router2'
        router2 = self.create_bgp_router(r2_name, 'juniper')
        # create router3
        r3_name = self.id() + 'router3'
        router3 = self.create_bgp_router(r3_name, 'juniper')
        gevent.sleep(1)

        # verify full mesh created
        self.check_bgp_peering(router1, router2, 2)
        self.check_bgp_peering(router1, router3, 2)
        self.check_bgp_peering(router2, router1, 2)
        self.check_bgp_peering(router2, router3, 2)
        self.check_bgp_peering(router3, router1, 2)
        self.check_bgp_peering(router3, router2, 2)

        params = router1.get_bgp_router_parameters()
        params.cluster_id = 100
        router1.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_update(router1)
        gevent.sleep(1)

        # verify full mesh reduced to route reflector
        self.check_bgp_peering(router1, router2, 2)
        self.check_bgp_peering(router1, router3, 2)
        self.check_bgp_peering(router2, router1, 1)
        self.check_bgp_peering(router3, router1, 1)
        self.check_bgp_no_peering(router2, router3)
        self.check_bgp_no_peering(router3, router2)

        # reset the cluster id so that there is no rr any more
        params = router1.get_bgp_router_parameters()
        params.cluster_id = None
        router1.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_update(router1)
        gevent.sleep(1)

        params = router2.get_bgp_router_parameters()
        params.cluster_id = 200
        router2.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_update(router2)
        gevent.sleep(1)

        # verify full mesh reduced to new route reflector
        self.check_bgp_peering(router2, router1, 2)
        self.check_bgp_peering(router2, router3, 2)
        self.check_bgp_peering(router1, router2, 1)
        self.check_bgp_peering(router3, router2, 1)
        self.check_bgp_no_peering(router1, router3)
        self.check_bgp_no_peering(router3, router1)

        # reset the cluster id so that there is no rr any more
        params = router2.get_bgp_router_parameters()
        params.cluster_id = None
        router2.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_update(router2)
        gevent.sleep(1)

        # verify full mesh created
        self.check_bgp_peering(router1, router2, 2)
        self.check_bgp_peering(router1, router3, 2)
        self.check_bgp_peering(router2, router1, 2)
        self.check_bgp_peering(router2, router3, 2)
        self.check_bgp_peering(router3, router1, 2)
        self.check_bgp_peering(router3, router2, 2)

        self._vnc_lib.bgp_router_delete(id=router1.uuid)
        self._vnc_lib.bgp_router_delete(id=router2.uuid)
        self._vnc_lib.bgp_router_delete(id=router3.uuid)
        gevent.sleep(1)
    # end test_ibgp_full_mesh_with_route_reflector_change

    def test_ibgp_auto_mesh_redundant_route_reflector(self):
        GlobalSystemConfigST.ibgp_auto_mesh = True
        self.assertEqual(GlobalSystemConfigST.get_ibgp_auto_mesh(),
                         True, "ibgp_auto_mesh_test")

        # create route reflector
        r3_name = self.id() + 'router3'
        router3 = self.create_bgp_router(r3_name, 'contrail', None,
                                         cluster_id=1000)

        # create HA route reflector
        r4_name = self.id() + 'router4'
        router4 = self.create_bgp_router(r4_name, 'contrail', None,
                                         cluster_id=1000)

        # create router1
        r1_name = self.id() + 'router1'
        router1 = self.create_bgp_router(r1_name, 'contrail')

        # create router2
        r2_name = self.id() + 'router2'
        router2 = self.create_bgp_router(r2_name, 'contrail')

        # router1 and router2 should not be connected, both of them should be
        # connected to router3 and router4.
        # router3 and router4 should be connected with each other.
        self.check_bgp_peering(router1, router3, 2)
        self.check_bgp_peering(router2, router3, 2)
        self.check_bgp_peering(router1, router4, 2)
        self.check_bgp_peering(router2, router4, 2)
        self.check_bgp_peering(router3, router1, 3)
        self.check_bgp_peering(router3, router2, 3)
        self.check_bgp_peering(router4, router1, 3)
        self.check_bgp_peering(router4, router2, 3)
        self.check_bgp_peering(router3, router4, 3)
        self.check_bgp_peering(router4, router3, 3)

        # Cleanup
        self._vnc_lib.bgp_router_delete(id=router1.uuid)
        self._vnc_lib.bgp_router_delete(id=router2.uuid)
        self._vnc_lib.bgp_router_delete(id=router3.uuid)
        self._vnc_lib.bgp_router_delete(id=router4.uuid)
        gevent.sleep(1)
    # test_ibgp_auto_mesh_redundant_route_reflector

    def test_asn_4byte(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')
        self.assertTill(self.vnc_db_has_ident, obj=vn1_obj)

        ri_target = self.check_ri_target(self.get_ri_name(vn1_obj))

        gs = self._vnc_lib.global_system_config_read(
            fq_name=['default-global-system-config'])

        # first, enable 4 byte ASN flag
        gs.enable_4byte_as = True
        self._vnc_lib.global_system_config_update(gs)

        # change ASN, but to a new 2 byte ASN.
        gs.set_autonomous_system(5000)
        self._vnc_lib.global_system_config_update(gs)

        # check route targets
        self.check_4byteASN_ri_target(self.get_ri_name(vn1_obj), ri_target)

        # update ASN value
        gs = self._vnc_lib.global_system_config_read(
            fq_name=[u'default-global-system-config'])
        gs.set_autonomous_system(700000)
        self._vnc_lib.global_system_config_update(gs)

        # check new route targets
        ri_target_for_4byte_asn = 'target:700000'
        ri_target_in_db = self.check_4byteASN_ri_target(
            self.get_ri_name(vn1_obj),
            ri_target_for_4byte_asn)

        # Verify in ZK that old path for type0 RT doesn't exist
        zk_value = self._api_server._db_conn._zk_db._zk_client.read_node(
            '%s%s000%s' % (self._api_server._db_conn._zk_db._zk_path_pfx,
                           BGP_RTGT_ALLOC_PATH_TYPE0,
                           ri_target.rsplit(':')[2]))
        self.assertEqual(zk_value, None)

        # Delete the virtual_network now
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)

        # sleep for ST to take over processing
        gevent.sleep(3)

        # Verify in ZK that the path doesn't exist anymore.
        # It wll be in the format of /id/bgp/route-targets/type1_2/0000008001
        zk_value = self._api_server._db_conn._zk_db._zk_client.read_node(
            '%s%s000000%s' % (self._api_server._db_conn._zk_db._zk_path_pfx,
                              BGP_RTGT_ALLOC_PATH_TYPE1_2,
                              ri_target_in_db.rsplit(':')[2]))
        self.assertEqual(zk_value, None)

    def test_asn(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')
        self.assertTill(self.vnc_db_has_ident, obj=vn1_obj)

        ri_target = self.check_ri_target(self.get_ri_name(vn1_obj))

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
        lr_target = self.check_lr_target(lr.get_fq_name())

        # update global system config but dont change
        # asn value for equality path
        gs = self._vnc_lib.global_system_config_read(
            fq_name=['default-global-system-config'])
        gs.set_autonomous_system(64512)
        self._vnc_lib.global_system_config_update(gs)

        # check route targets
        self.check_ri_target(self.get_ri_name(vn1_obj), ri_target)
        self.check_bgp_asn(router1.get_fq_name(), 64512)
        self.check_lr_target(lr.get_fq_name(), lr_target)

        # update ASN value
        gs = self._vnc_lib.global_system_config_read(
            fq_name=[u'default-global-system-config'])
        gs.set_autonomous_system(50000)
        self._vnc_lib.global_system_config_update(gs)

        # check new route targets
        self.check_ri_target(self.get_ri_name(vn1_obj),
                             ri_target.replace('64512', '50000'))
        self.check_bgp_asn(router1.get_fq_name(), 50000)
        self.check_lr_target(lr.get_fq_name(),
                             lr_target.replace('64512', '50000'))

        self._vnc_lib.logical_router_delete(id=lr.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=vn1_obj.fq_name + [vn1_obj.name])
        self._vnc_lib.bgp_router_delete(id=router1.uuid)
    # end test_asn

    def test_bgpaas_shared(self):
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name,
                                              ['10.0.0.0/24', '1000::/16'])

        project_name = ['default-domain', 'default-project']
        project_obj = self._vnc_lib.project_read(fq_name=project_name)
        port_name1 = self.id() + 'p1'
        port_obj1 = VirtualMachineInterface(port_name1, parent_obj=project_obj)
        port_obj1.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(port_obj1)
        port_name2 = self.id() + 'p2'
        port_obj2 = VirtualMachineInterface(port_name2, parent_obj=project_obj)
        port_obj2.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(port_obj2)

        # Create a shared BGPaaS server. It means BGP Router object
        # does not get created for VMI on the network.
        bgpaas_name = self.id() + 'bgp1'
        bgpaas = BgpAsAService(bgpaas_name, parent_obj=project_obj,
                               autonomous_system=64512, bgpaas_shared=True,
                               bgpaas_ip_address='1.1.1.1')

        bgpaas.add_virtual_machine_interface(port_obj1)
        self._vnc_lib.bgp_as_a_service_create(bgpaas)
        self.wait_to_get_object(BgpAsAServiceST,
                                bgpaas.get_fq_name_str())

        router1_name = vn1_obj.get_fq_name_str() + ':' + \
            vn1_name + ':' + bgpaas_name
        router2_name = vn1_obj.get_fq_name_str() + ':' + \
            vn1_name + ':' + 'bgpaas-server'

        # Check for two BGP routers - one with the BGPaaS name and
        # not with Port name
        self.wait_to_get_object(BgpRouterST, router1_name)
        self.wait_to_get_object(BgpRouterST, router2_name)

        # check whether shared IP address is assigned to the BGP rotuer
        self.check_bgp_router_ip(router1_name, '1.1.1.1')

        # Add a new VMI to the BGPaaS. This should not create a new BGP router
        bgpaas = self._vnc_lib.bgp_as_a_service_read(
            fq_name_str=bgpaas.get_fq_name_str())
        bgpaas.add_virtual_machine_interface(port_obj2)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)

        # Check for the absence of BGP router along with the new VMI.
        router3_name = vn1_obj.get_fq_name_str() + ':' + \
            vn1_name + ':' + port_name2
        try:
            self._vnc_lib.bgp_router_read(fq_name_str=router3_name)
        except NoIdError:
            print('Second BGP Router not created for second port - PASS')
        else:
            assert(True)

        # Check vmi->bgp-router link exists
        for i in range(3):
            port_obj_updated = self._vnc_lib.virtual_machine_interface_read(
                id=port_obj2.uuid)
            refs = port_obj_updated.get_bgp_router_refs()
            if refs:
                break
            gevent.sleep(1)
        self.assertEqual(port_obj_updated.bgp_router_refs[0]['to'],
                         router1_name.split(':'))

        bgpaas.del_virtual_machine_interface(port_obj1)
        bgpaas.del_virtual_machine_interface(port_obj2)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        self._vnc_lib.bgp_as_a_service_delete(id=bgpaas.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj1.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj2.uuid)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self.check_ri_is_deleted(vn1_obj.fq_name + [vn1_obj.name])
    # end test_bgpaas_shared

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

        v6_obj = InstanceIp(name=port_name + '-v6')
        v6_obj.set_virtual_machine_interface(port_obj)
        v6_obj.set_virtual_network(vn1_obj)
        v6_obj.set_instance_ip_family('v6')
        self._vnc_lib.instance_ip_create(v6_obj)

        v4_obj = InstanceIp(name=port_name + '-v4')
        v4_obj.set_virtual_machine_interface(port_obj)
        v4_obj.set_virtual_network(vn1_obj)
        v4_obj.set_instance_ip_family('v4')
        self._vnc_lib.instance_ip_create(v4_obj)

        bgpaas_name = self.id() + 'bgp1'
        bgpaas = BgpAsAService(bgpaas_name, parent_obj=project_obj,
                               autonomous_system=64512)
        bgpaas.add_virtual_machine_interface(port_obj)
        self._vnc_lib.bgp_as_a_service_create(bgpaas)

        router1_name = vn1_obj.get_fq_name_str() + ':' \
            + vn1_name + ':' + port_name
        self.wait_to_get_object(BgpAsAServiceST,
                                bgpaas.get_fq_name_str())
        self.wait_to_get_object(BgpRouterST, router1_name)
        server_fq_name = ':'.join(self.get_ri_name(vn1_obj)) + ':bgpaas-server'
        self.wait_to_get_object(BgpRouterST, server_fq_name)
        server_router_obj = self._vnc_lib.bgp_router_read(
            fq_name_str=server_fq_name)

        router1_name = vn1_obj.get_fq_name_str() + ':' + \
            vn1_name + ':' + port_name
        mx_bgp_router = self.create_bgp_router("mx-bgp-router", "contrail")
        mx_bgp_router_name = mx_bgp_router.get_fq_name_str()
        self.wait_to_get_object(BgpRouterST, mx_bgp_router_name)
        mx_bgp_router = self._vnc_lib.bgp_router_read(
            fq_name_str=mx_bgp_router_name)
        self.check_bgp_no_peering(server_router_obj, mx_bgp_router)

        for i in range(5):
            router1_obj = self._vnc_lib.bgp_router_read(
                fq_name_str=router1_name)
            params = router1_obj.get_bgp_router_parameters()
            if not params:
                gevent.sleep(1)
                continue
            self.assertEqual(router1_obj.get_bgp_router_parameters().address,
                             '10.0.0.252')
            self.assertEqual(router1_obj.get_bgp_router_parameters().
                             identifier, '10.0.0.252')
        self.assertNotEqual(params, None)

        # verify ref from vmi to bgp-router is created
        port_obj_updated = \
            self._vnc_lib.virtual_machine_interface_read(id=port_obj.uuid)
        self.assertEqual(port_obj_updated.bgp_router_refs[0]['to'],
                         router1_obj.fq_name)

        # remove bgp-router ref from vmi
        self._vnc_lib.ref_update(obj_uuid=port_obj_updated.uuid,
                                 obj_type='virtual_machine_interface',
                                 ref_uuid=router1_obj.uuid,
                                 ref_fq_name=router1_obj.fq_name,
                                 ref_type='bgp-router',
                                 operation='DELETE')
        port_obj_updated = \
            self._vnc_lib.virtual_machine_interface_read(id=port_obj.uuid)
        self.assertIsNone(port_obj_updated.get_bgp_router_refs())

        # check bgp-router ref in vmi is restored during reinit
        VirtualMachineST._dict = {}
        BgpRouterST._dict = {}
        BgpAsAServiceST._dict = {}
        to_bgp.transformer.reinit()
        gevent.sleep(1)

        port_obj_updated = \
            self._vnc_lib.virtual_machine_interface_read(id=port_obj.uuid)
        refs = port_obj_updated.get_bgp_router_refs()
        self.assertNotEqual(refs, None)
        self.assertEqual(refs[0]['to'], router1_obj.fq_name)

        # remove VMI from bgpaas and ensure
        bgpaas = self._vnc_lib.bgp_as_a_service_read(fq_name=bgpaas.fq_name)
        port_obj_updated = \
            self._vnc_lib.virtual_machine_interface_read(id=port_obj.uuid)
        bgpaas.del_virtual_machine_interface(port_obj_updated)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        gevent.sleep(1)

        bgpaas = self._vnc_lib.bgp_as_a_service_read(fq_name=bgpaas.fq_name)
        port_obj_updated = \
            self._vnc_lib.virtual_machine_interface_read(id=port_obj.uuid)

        #   ensure vmi is not attached to bpgaas
        self.assertEqual(bgpaas.get_virtual_machine_interface_refs(), None)
        #   vmi loses vmi->bgp-router ref
        self.assertEqual(port_obj_updated.get_bgp_router_refs(), None)
        #   ensure VMI has no bgpaas ref
        self.assertEqual(port_obj_updated.get_bgp_as_a_service_back_refs(),
                         None)

        # add VMI back to bgpaas
        bgpaas = self._vnc_lib.bgp_as_a_service_read(fq_name=bgpaas.fq_name)
        port_obj_updated = \
            self._vnc_lib.virtual_machine_interface_read(id=port_obj.uuid)
        bgpaas.add_virtual_machine_interface(port_obj_updated)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        bgpaas = self._vnc_lib.bgp_as_a_service_read(fq_name=bgpaas.fq_name)
        port_obj_updated = \
            self._vnc_lib.virtual_machine_interface_read(id=port_obj.uuid)

        #   ensure vmi is attached to bgpaas
        self.assertNotEqual(bgpaas.get_virtual_machine_interface_refs(), None)
        #   VMI has bgpaas ref
        self.assertNotEqual(port_obj_updated.get_bgp_as_a_service_back_refs(),
                            None)
        #   vmi got vmi->bgp-router ref
        for i in range(10):
            port_obj_updated = \
                self._vnc_lib.virtual_machine_interface_read(id=port_obj.uuid)
            if not port_obj_updated.get_bgp_router_refs():
                gevent.sleep(1)
                print('retrying...')
        self.assertNotEqual(port_obj_updated.get_bgp_router_refs(), None,
                            msg="vmi did not get ref to bgp-router")

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
        v4_obj = InstanceIp(name=port_name + '-v4')
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
        router2_name = vn1_obj.get_fq_name_str() + ':' + \
            vn1_name + ':' + port2_name
        self.wait_to_get_object(BgpRouterST, router2_name)

        router2_obj = self._vnc_lib.bgp_router_read(fq_name_str=router2_name)
        self.check_bgp_peering(server_router_obj, router2_obj, 2)
        self.check_bgp_peering(server_router_obj, router1_obj, 2)

        bgpaas.del_virtual_machine_interface(port_obj)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        self.wait_to_delete_object(BgpRouterST, router1_name)
        self._vnc_lib.bgp_as_a_service_delete(id=bgpaas.uuid)
        self.wait_to_delete_object(BgpRouterST, router2_name)

        self._vnc_lib.instance_ip_delete(id=v6_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port2_obj.uuid)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self.check_ri_is_deleted(vn1_obj.fq_name + [vn1_obj.name])
        self._vnc_lib.bgp_router_delete(id=mx_bgp_router.uuid)
    # end test_bgpaas
