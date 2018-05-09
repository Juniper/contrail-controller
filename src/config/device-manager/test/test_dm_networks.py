#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
import gevent
from time import sleep
sys.path.append("../common/tests")
from test_utils import *
from vnc_api.vnc_api import *
from device_api.juniper_common_xsd import *
from device_manager.dm_utils import *
from test_common import *
from test_dm_common import *
from test_dm_utils import FakeDeviceConnect

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

    @retries(5, hook=retry_exc_handler)
    def check_fip_config(self, vn1, vn2, pr, fip_obj):
        config = FakeDeviceConnect.get_xml_config()
        ris = self.get_routing_instances(config, ri_name='')
        self.assertEqual(len(ris), 4)
        for ri in ris:
            instance_type = ri.get_instance_type()
            instance_name = ri.get_name()
            if instance_type:
                vrf_import=instance_name + "-import"
                vrf_export=instance_name + "-export"
                self.assertEqual(ri.get_vrf_import(), vrf_import)
                self.assertEqual(ri.get_vrf_export(), vrf_export)
                if instance_type == 'evpn':
                    self.assertEqual(instance_name, "_contrail_vn-private-l2-4")
                    intf_name = "irb." + instance_name[-1]
                    ri_intf=ri.get_routing_interface()
                    self.assertEqual(ri_intf, intf_name)
                if instance_type == 'vrf':
                    ri_intf=ri.get_interface()
                    self.assertEqual(len(ri_intf), 1)
                    ri_intf_name = ri_intf[0].get_name()
                    ri_opts = ri.get_routing_options()
                    route=ri_opts.get_static().get_route()
                    self.assertEqual(len(route), 1)
                    route_name = route[0].get_name()
                    if 'nat' in instance_name:
                        self.assertEqual(instance_name,
                            "_contrail_vn-private-l3-4-nat")
                        ri_intf_name = ri_intf_name[:-2]
                        self.assertEqual(ri_intf_name, "ge-0/0/0")
                        next_hop = route[0].get_next_hop()
                        self.assertEqual(route_name, "0.0.0.0/0")
                        self.assertEqual(next_hop, "ge-0/0/0.7")

                    else:
                        self.assertEqual(instance_name,
                            "_contrail_vn-private-l3-4")
                        intf_name = "irb." + instance_name[-1]
                        self.assertEqual(ri_intf_name, intf_name)
                        network_ipam=vn1.get_network_ipam_refs()[0]
                        ipam_subnet = network_ipam['attr'].get_ipam_subnets()[0]
                        subnet = ipam_subnet.get_subnet()
                        ip_prefix = subnet.get_ip_prefix()
                        prefix_len = subnet.get_ip_prefix_len()
                        subnet_name = ip_prefix +'/' + str(prefix_len)
                        self.assertEqual(route_name, subnet_name)

            else:
                self.assertEqual(instance_name, "_contrail_vn-public-l3-5")
                ri_opts = ri.get_routing_options()
                route=ri_opts.get_static().get_route()
                self.assertEqual(len(route), 1)
                name = route[0].get_name()
                next_hop = route[0].get_next_hop()
                fip_addr = fip_obj.get_floating_ip_address()
                fip_addr = fip_addr + "/32"
                self.assertEqual(fip_addr, name)
                junos_service_port=pr.get_physical_router_junos_service_ports()
                service_ports = junos_service_port.get_service_port()
                self.assertEqual(len(service_ports), 1)
                ser_port = service_ports[0]
                self.assertEqual(ser_port, "ge-0/0/0")
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
        vn1_obj_properties.set_vxlan_network_identifier(2000)
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
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)


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
        vmi.virtual_machine_interface_device_owner = 'physical-router'
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
        self._vnc_lib.physical_router_update(pr_mx)

        default_project = self._vnc_lib.project_read(fq_name=[u'default-domain', u'default-project'])
        fip_obj.set_project(default_project)
        fip_uuid = self._vnc_lib.floating_ip_create(fip_obj)
        fip_obj1 = self._vnc_lib.floating_ip_read(id=fip_uuid)
        self.check_fip_config(vn1_obj, vn2_obj, pr_mx, fip_obj1)
# end TestNetworkDM

