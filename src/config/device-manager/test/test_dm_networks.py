#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
from builtins import str
from vnc_api.vnc_api import *
from device_api.juniper_common_xsd import *
from device_manager.dm_utils import *
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from cfgm_common.exceptions import BadRequest
from .test_dm_common import *
from .test_dm_utils import FakeDeviceConnect, FakeNetconfManager
from unittest import skip

#
# All Networks related DM test cases should go here
#
class TestNetworkDM(TestCommonDM):

    def __init__(self, *args, **kwargs):
        self.product = "mx"
        super(TestNetworkDM, self).__init__(*args, **kwargs)

    @retries(5, hook=retry_exc_handler)
    def check_interface_ip_config(self, if_name='', ri_name='',
                     ip_check='', ip_type='v4', network_id='', is_free=False):
        check = False
        if is_free:
            check = True

        config = FakeDeviceConnect.get_xml_config()
        ifl_num = DMUtils.compute_lo0_unit_number(network_id)
        intfs = self.get_interfaces(config, if_name)
        found = False
        for intf in intfs or []:
            ips = self.get_ip_list(intf, ip_type, ifl_num)
            if ip_check in ips:
                found = True
                break

        if not found:
            self.assertTrue(check)

        ris = self.get_routing_instances(config, ri_name)
        if not ris:
            self.assertTrue(check)
            return

        ri = ris[0]
        intfs = ri.get_interface() or []
        found = False
        for intf in intfs:
            if intf.get_name() == if_name + "." + ifl_num:
                found = True
                break
        if not found:
            self.assertTrue(check)
        return
    # end check_interface_ip_config


    def get_subnets(self, ipam_refs=[]):
        subnets = []
        for ipam_ref in ipam_refs or []:
            for ipam_subnet in ipam_ref['attr'].get_ipam_subnets() or []:
                if ipam_subnet.subnet:
                    subnet = ipam_subnet.subnet
                    subnets.append([subnet.ip_prefix,
                                    subnet.ip_prefix_len])
        return subnets
    # end get_subnets

    @retries(5, hook=retry_exc_handler)
    def check_dci_ipam(self):
        # make sure network ipam created
        ipam = NetworkIpam('default-dci-lo0-network-ipam')
        self._vnc_lib.network_ipam_read(fq_name=ipam.get_fq_name())
    # end check_dci_ipam

    @retries(5, hook=retry_exc_handler)
    def check_dci_lo_network(self, check_subnet, should_exist=True):
        dci_vn_fq = ['default-domain', 'default-project', 'dci-network']
        vn = VirtualNetwork(dci_vn_fq[:-1])
        vn = self._vnc_lib.virtual_network_read(fq_name=dci_vn_fq)
        if not vn:
            raise Exception("DCI network not found: " + vn_name)
        ipam_refs = vn.get_network_ipam_refs()
        subnets = self.get_subnets(ipam_refs)
        if should_exist:
            if check_subnet not in subnets:
                raise Exception("Subnet not found")
        else:
            if check_subnet in subnets:
                raise Exception("Subnet still found")
    # end check_dci_lo_network

    # Skipping test case: This test case needs to be clean up all the resources
    # created (PR, DCI, LR, etc.). Leaving them is causing other test cases
    # to fail when the entire suite is run.
    @skip("requires cleanup")
    def test_dci_api(self):
        FakeNetconfManager.set_model('qfx10000')
        gs = GlobalSystemConfig(fq_name=["default-global-system-config"])
        subnets = SubnetListType([SubnetType("10.0.0.0", 24), SubnetType("20.0.0.0", 16)])
        gs.set_data_center_interconnect_loopback_namespace(subnets)
        self._vnc_lib.global_system_config_update(gs)
        self.check_dci_ipam()

        self.check_dci_lo_network(["10.0.0.0", 24])
        self.check_dci_lo_network(["20.0.0.0", 16])

        subnets = SubnetListType([SubnetType("30.0.0.0", 24)])
        gs.set_data_center_interconnect_loopback_namespace(subnets)
        self._vnc_lib.global_system_config_update(gs)
        self.check_dci_lo_network(["10.0.0.0", 24], False)
        self.check_dci_lo_network(["30.0.0.0", 24], True)

        gs.set_data_center_interconnect_loopback_namespace(None)
        self._vnc_lib.global_system_config_update(gs)
        self.check_dci_lo_network(["30.0.0.0", 24], False)

        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1', product="qfx10000", role="spine")

        fab1 = self._vnc_lib.fabric_create(Fabric('fab1'))
        fab1 = self._vnc_lib.fabric_read(id=fab1)
        pr.set_fabric(fab1)
        self._vnc_lib.physical_router_update(pr)

        lr = LogicalRouter("lr1")
        lr.set_physical_router(pr)
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        lr = self._vnc_lib.logical_router_read(id=lr_uuid)

        dci = DataCenterInterconnect("test-dci")
        dci.add_logical_router(lr)

        self._vnc_lib.data_center_interconnect_create(dci)

        # test lr connected two dcis
        dci2 = DataCenterInterconnect("test-dci-2")
        dci2.add_logical_router(lr)
        try:
            self._vnc_lib.data_center_interconnect_create(dci2)
            raise Exception("dci is not allowed to create with lr, which is part of another dci")
        except BadRequest as e:
            # lr can not be associated more than one dci
            pass

        # test lr connected to different fabric pr's, this lr can't be part of dci
        bgp_router2, pr2 = self.create_router('router-2' + self.id(), '2.1.1.1', product="qfx10000", role="spine")
        fab2 = self._vnc_lib.fabric_create(Fabric('fab2'))
        fab2 = self._vnc_lib.fabric_read(id=fab2)


        pr2.set_fabric(fab2)
        self._vnc_lib.physical_router_update(pr2)

        lr2 = LogicalRouter("lr2")
        lr2.add_physical_router(pr)
        lr2.add_physical_router(pr2)
        lr_uuid = self._vnc_lib.logical_router_create(lr2)
        lr = self._vnc_lib.logical_router_read(id=lr_uuid)

        dci2 = DataCenterInterconnect("test-dci-2")
        dci2.add_logical_router(lr)
        try:
            self._vnc_lib.data_center_interconnect_create(dci2)
            raise Exception("dci is not allowed to create with lr, lr has prs part of different fabrics")
        except BadRequest as e:
            # can't create dci with lr connected to two different fabrics
            pass

        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self._vnc_lib.data_center_interconnect_delete(fq_name=dci.fq_name)
        self._vnc_lib.data_center_interconnect_delete(fq_name=dci2.fq_name)
        self._vnc_lib.logical_router_delete(fq_name=lr.fq_name)
        self._vnc_lib.logical_router_delete(fq_name=lr2.fq_name)
        self.delete_routers(bgp_router, pr)
        self._vnc_lib.fabric_delete(fq_name=fab1.fq_name)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

    # end test_dci_api

    # test vn  flat subnet lo0 ip allocation
    def test_dm_lo0_flat_subnet_ip_alloc(self):
        vn1_name = 'vn1' + self.id()
        vn1_obj = VirtualNetwork(vn1_name, address_allocation_mode='flat-subnet-only')
        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_forwarding_mode('l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        ipam1_sn_v4 = IpamSubnetType(subnet=SubnetType('11.1.1.0', 28), alloc_unit=4)
        ipam2_sn_v4 = IpamSubnetType(subnet=SubnetType('12.1.1.0', 28), alloc_unit=4)

        ipam_subnets = IpamSubnets([ipam1_sn_v4, ipam2_sn_v4])

        ipam_obj = NetworkIpam('ipam-flat', ipam_subnet_method="flat-subnet", ipam_subnets=ipam_subnets)
        self._vnc_lib.network_ipam_create(ipam_obj)

        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType([]))
        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)

        bgp_router, pr = self.create_router('router1' + self.id(), '1.1.1.1')
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)
        vrf_name_l3 = DMUtils.make_vrf_name(vn1_obj.fq_name[-1], vn1_obj.virtual_network_network_id, 'l3')

        self.check_interface_ip_config('lo0', vrf_name_l3, '11.1.1.8/32', 'v4', vn1_obj.virtual_network_network_id)

        #detach vn from PR and check
        pr.del_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)

        self.check_interface_ip_config('lo0', vrf_name_l3, '11.1.1.8/32', 'v4', vn1_obj.virtual_network_network_id, True)

        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.fq_name)
        self._vnc_lib.network_ipam_delete(fq_name=ipam_obj.fq_name)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

    #end test_flat_subnet

    @retries(5, hook=retry_exc_handler)
    def check_auto_export_config(self, vrf_name_l3='', ip_type='v4', check=True):
        config = FakeDeviceConnect.get_xml_config()
        ri = self.get_routing_instances(config, vrf_name_l3)[0]
        ri_opts = ri.get_routing_options()
        auto_export = ri_opts.get_auto_export()
        family = auto_export.get_family()
        if ip_type == 'v4':
            if check:
                self.assertIsNotNone(family.get_inet().get_unicast())
            else:
                self.assertTrue(not family.get_inet() or
                                not family.get_inet().get_unicast())
        elif ip_type == 'v6':
            if check:
                self.assertIsNotNone(family.get_inet6().get_unicast())
            else:
                self.assertTrue(not family.get_inet6() or
                                not family.get_inet6().get_unicast())
    # end check_auto_export_config

    def test_dm_auto_export_config(self):
        vn1_name = 'vn1' + self.id()
        vn1_obj = VirtualNetwork(vn1_name)
        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_forwarding_mode('l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        ipam_obj = NetworkIpam('ipam1' + self.id())
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("10.0.0.0", 24)),
             IpamSubnetType(SubnetType("2001:db8:abcd:0012::0", 64))]))

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)

        bgp_router, pr = self.create_router('router1' + self.id(), '1.1.1.1')
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)
        vrf_name_l3 = DMUtils.make_vrf_name(vn1_obj.fq_name[-1],
                                            vn1_obj.virtual_network_network_id, 'l3')
        self.check_auto_export_config(vrf_name_l3, 'v4', check=True)
        self.check_auto_export_config(vrf_name_l3, 'v6', check=True)

        vn1_obj.set_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("2001:ab8:abcd:0012::0", 64))]))
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_auto_export_config(vrf_name_l3, 'v4', check=False)
        self.check_auto_export_config(vrf_name_l3, 'v6', check=True)

        vn1_obj.set_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("11.0.0.0", 24))]))
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_auto_export_config(vrf_name_l3, 'v4', check=True)
        self.check_auto_export_config(vrf_name_l3, 'v6', check=False)

        # cleanup
        pr.del_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)

        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.fq_name)
        self._vnc_lib.network_ipam_delete(fq_name=ipam_obj.fq_name)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)
    # end test_dm_auto_export_config

    # test vn  lo0 ip allocation
    def test_dm_lo0_ip_alloc(self):
        vn1_name = 'vn1' + self.id()
        vn1_obj = VirtualNetwork(vn1_name)
        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_forwarding_mode('l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        ipam_obj = NetworkIpam('ipam1' + self.id())
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("10.0.0.0", 24)), IpamSubnetType(SubnetType("20.0.0.0", 16))]))

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)

        bgp_router, pr = self.create_router('router1' + self.id(), '1.1.1.1')
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)
        vrf_name_l3 = DMUtils.make_vrf_name(vn1_obj.fq_name[-1], vn1_obj.virtual_network_network_id, 'l3')

        self.check_interface_ip_config('lo0', vrf_name_l3, '10.0.0.252/32', 'v4', vn1_obj.virtual_network_network_id)
        self.check_interface_ip_config('lo0', vrf_name_l3, '20.0.255.252/32', 'v4', vn1_obj.virtual_network_network_id)

        #set fwd mode l2 and check lo0 ip alloc, should not be allocated
        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_forwarding_mode('l2')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)
        self._vnc_lib.virtual_network_update(vn1_obj)

        self.check_interface_ip_config('lo0', vrf_name_l3, '10.0.0.252/32', 'v4', vn1_obj.virtual_network_network_id, True)
        self.check_interface_ip_config('lo0', vrf_name_l3, '20.0.255.252/32', 'v4', vn1_obj.virtual_network_network_id, True)

        #set fwd mode l2_l3 and check lo0 ip alloc
        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)
        self._vnc_lib.virtual_network_update(vn1_obj)

        self.check_interface_ip_config('lo0', vrf_name_l3, '10.0.0.252/32', 'v4', vn1_obj.virtual_network_network_id, True)
        self.check_interface_ip_config('lo0', vrf_name_l3, '20.0.255.252/32', 'v4', vn1_obj.virtual_network_network_id, True)

        #detach vn from PR and check
        pr.del_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)

        self.check_interface_ip_config('lo0', vrf_name_l3, '10.0.0.252/32', 'v4', vn1_obj.virtual_network_network_id, True)
        self.check_interface_ip_config('lo0', vrf_name_l3, '20.0.255.252/32', 'v4', vn1_obj.virtual_network_network_id, True)

        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.fq_name)
        self._vnc_lib.network_ipam_delete(fq_name=ipam_obj.fq_name)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

    #end check_interface_ip_config

    @retries(5, hook=retry_exc_handler)
    def check_evpn_config(self, global_encap, vn_obj, interfaces=[]):
        vrf_name_l2 = DMUtils.make_vrf_name(vn_obj.fq_name[-1], vn_obj.virtual_network_network_id, 'l2')
        network_id = vn_obj.get_virtual_network_network_id()
        vn_obj_properties = vn_obj.get_virtual_network_properties()
        vxlan_id = vn_obj_properties.get_vxlan_network_identifier()
        fwd_mode = vn_obj_properties.get_forwarding_mode()

        config = FakeDeviceConnect.get_xml_config()
        ri = self.get_routing_instances(config, vrf_name_l2)[0]
        ri_intf = "irb." + str(network_id) if fwd_mode == 'l2_l3' else None
        protocols = ri.get_protocols()
        intfs = []
        if global_encap in ['MPLSoGRE', 'MPLSoUDP']:
            self.assertEqual(ri.get_instance_type(), 'evpn')
            self.assertEqual(ri.get_vlan_id(), 'none')
            self.assertEqual(ri.get_routing_interface(), ri_intf)
            intfs = protocols.get_evpn().get_interface() if interfaces else []

        if global_encap == 'VXLAN':
            self.assertEqual(protocols.get_evpn().get_encapsulation(), 'vxlan')
            bd = ri.get_bridge_domains().get_domain()[0]
            self.assertEqual(ri.get_instance_type(),  'virtual-switch')
            self.assertEqual(bd.name, "bd-" + str(vxlan_id))
            self.assertEqual(bd.get_vlan_id(), 'none')
            self.assertEqual(bd.get_routing_interface(), ri_intf)
            intfs = bd.get_interface() or []

        ifnames = [intf.name for intf in intfs]
        self.assertTrue(set(interfaces or []) <= set(ifnames))
        return

    def set_global_vrouter_config(self, encap_priority_list = []):
        create = False
        try:
            gv = self._vnc_lib.global_vrouter_config_read(fq_name=["default-global-system-config", "default-global-vrouter-config"])
        except NoIdError:
            create = True
            gv = GlobalVrouterConfig(fq_name=["default-global-system-config", "default-global-vrouter-config"])
        encaps = EncapsulationPrioritiesType()
        encaps.set_encapsulation(encap_priority_list)
        gv.set_encapsulation_priorities(encaps)
        if create:
            self._vnc_lib.global_vrouter_config_create(gv)
        else:
            self._vnc_lib.global_vrouter_config_update(gv)

    def test_evpn_config(self):
        self.set_global_vrouter_config(["MPLSoGRE", "VXLAN"])
        vn1_name = 'vn1' + self.id()
        vn1_obj = VirtualNetwork(vn1_name)
        ipam_obj = NetworkIpam('ipam1' + self.id())
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))

        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_vxlan_network_identifier(2006)
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)

        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1')
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)

        pi = PhysicalInterface('pi' + self.id(), parent_obj = pr)
        pi_id = self._vnc_lib.physical_interface_create(pi)

        fq_name = ['default-domain', 'default-project', 'vmi1' + self.id()]
        vmi1 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi1.set_virtual_network(vn1_obj)
        vmi1.set_virtual_machine_interface_device_owner("PhysicalRouter")
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        fq_name = ['default-domain', 'default-project', 'vmi2' + self.id()]
        vmi2 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi2.set_virtual_network(vn1_obj)
        vmi2.set_virtual_machine_interface_device_owner("PhysicalRouter")
        self._vnc_lib.virtual_machine_interface_create(vmi2)

        li1 = LogicalInterface('li.1' + self.id(), parent_obj = pi)
        li1.set_virtual_machine_interface(vmi1)
        li1_id = self._vnc_lib.logical_interface_create(li1)

        li2 = LogicalInterface('li.2' + self.id(), parent_obj = pi)
        li2.set_virtual_machine_interface(vmi2)
        li2_id = self._vnc_lib.logical_interface_create(li2)

        self.set_global_vrouter_config(["VXLAN", "MPLSoGRE"])
        self.check_evpn_config("VXLAN", vn1_obj, ["li.1" + self.id(), "li.2" + self.id()])

        self.set_global_vrouter_config(["MPLSoGRE", "VXLAN"])
        self.check_evpn_config("MPLSoGRE", vn1_obj, ["li.1" + self.id(), "li.2" + self.id()])

        self.set_global_vrouter_config(["MPLSoUDP", "VXLAN", "MPLSoGRE"])
        self.check_evpn_config("MPLSoUDP", vn1_obj, ["li.1" + self.id(), "li.2" + self.id()])

        self.set_global_vrouter_config([])
        # DM defaults to MPLSoGRE
        self.check_evpn_config("MPLSoGRE", vn1_obj, ["li.1" + self.id(), "li.2" + self.id()])

        # cleanup
        pr.del_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)

        self._vnc_lib.logical_interface_delete(fq_name=li2.get_fq_name())
        self._vnc_lib.logical_interface_delete(fq_name=li1.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi2.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi1.get_fq_name())
        self._vnc_lib.physical_interface_delete(fq_name=pi.get_fq_name())

        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.fq_name)
        self._vnc_lib.network_ipam_delete(fq_name=ipam_obj.fq_name)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

# end TestNetworkDM

