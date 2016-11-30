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

#
# All Networks related DM test cases should go here
#
class TestNetworkDM(TestCommonDM):

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

    # test vn  lo0 ip allocation
    def test_dm_lo0_ip_alloc(self):
        vn1_name = 'vn1'
        vn1_obj = VirtualNetwork(vn1_name)
        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_forwarding_mode('l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        ipam_obj = NetworkIpam('ipam1')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("10.0.0.0", 24)), IpamSubnetType(SubnetType("20.0.0.0", 16))]))

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)

        bgp_router, pr = self.create_router('router1', '1.1.1.1')
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)
        vrf_name_l3 = DMUtils.make_vrf_name(vn1_obj.fq_name[-1], vn1_obj.virtual_network_network_id, 'l3')

        gevent.sleep(2)
        self.check_interface_ip_config('lo0', vrf_name_l3, '10.0.0.252/32', 'v4', vn1_obj.virtual_network_network_id)
        self.check_interface_ip_config('lo0', vrf_name_l3, '20.0.255.252/32', 'v4', vn1_obj.virtual_network_network_id)

        #set fwd mode l2 and check lo0 ip alloc, should not be allocated
        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_forwarding_mode('l2')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)
        self._vnc_lib.virtual_network_update(vn1_obj)

        gevent.sleep(2)
        self.check_interface_ip_config('lo0', vrf_name_l3, '10.0.0.252/32', 'v4', vn1_obj.virtual_network_network_id, True)
        self.check_interface_ip_config('lo0', vrf_name_l3, '20.0.255.252/32', 'v4', vn1_obj.virtual_network_network_id, True)

        #set fwd mode l2_l3 and check lo0 ip alloc
        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)
        self._vnc_lib.virtual_network_update(vn1_obj)

        gevent.sleep(2)
        self.check_interface_ip_config('lo0', vrf_name_l3, '10.0.0.252/32', 'v4', vn1_obj.virtual_network_network_id, True)
        self.check_interface_ip_config('lo0', vrf_name_l3, '20.0.255.252/32', 'v4', vn1_obj.virtual_network_network_id, True)

        #detach vn from PR and check
        pr.del_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)
        gevent.sleep(2)

        self.check_interface_ip_config('lo0', vrf_name_l3, '10.0.0.252/32', 'v4', vn1_obj.virtual_network_network_id, True)
        self.check_interface_ip_config('lo0', vrf_name_l3, '20.0.255.252/32', 'v4', vn1_obj.virtual_network_network_id, True)

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
        ri_intf = None
        if fwd_mode == 'l2_l3':
            ri_intf = "irb." + str(network_id)
        protocols = ri.get_protocols()
        intfs = []
        if global_encap in ['MPLSoGRE', 'MPLSoUDP']:
            if (ri.get_instance_type() != 'evpn' or
                ri.get_vlan_id() != 'none' or \
                ri.get_routing_interface() != ri_intf):
                self.assertTrue(False)
            if interfaces:
                intfs = protocols.get_evpn().get_interface() or []

        if global_encap == 'VXLAN':
            if protocols.get_evpn().get_encapsulation() != 'vxlan':
                assertTrue(False)
            bd = ri.get_bridge_domains().get_domain()[0]
            if (ri.get_instance_type() != 'virtual-switch' or \
                 bd.name != "bd-" + str(vxlan_id) or \
                 bd.get_vlan_id() != 'none' or \
                 bd.get_routing_interface() != ri_intf):
                self.assertTrue(False)
            intfs = bd.get_interface() or []

        ifnames = [intf.name for intf in intfs]
        for ifc in interfaces or []:
            if ifc not in ifnames:
                self.assertTrue(False)

        self.assertTrue(True)
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
        vn1_name = 'vn1'
        vn1_obj = VirtualNetwork(vn1_name)
        ipam_obj = NetworkIpam('ipam1')
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))

        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_vxlan_network_identifier(2000)
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)

        bgp_router, pr = self.create_router('router10', '1.1.1.1')
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)

        pi = PhysicalInterface('pi1', parent_obj = pr)
        pi_id = self._vnc_lib.physical_interface_create(pi)

        fq_name = ['default-domain', 'default-project', 'vmi1']
        vmi1 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi1.set_virtual_network(vn1_obj)
        vmi1.set_virtual_machine_interface_device_owner("PhysicalRouter")
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        fq_name = ['default-domain', 'default-project', 'vmi2']
        vmi2 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi2.set_virtual_network(vn1_obj)
        vmi2.set_virtual_machine_interface_device_owner("PhysicalRouter")
        self._vnc_lib.virtual_machine_interface_create(vmi2)

        li1 = LogicalInterface('li.1', parent_obj = pi)
        li1.set_virtual_machine_interface(vmi1)
        li1_id = self._vnc_lib.logical_interface_create(li1)

        li2 = LogicalInterface('li.2', parent_obj = pi)
        li2.set_virtual_machine_interface(vmi2)
        li2_id = self._vnc_lib.logical_interface_create(li2)

        self.set_global_vrouter_config(["VXLAN", "MPLSoGRE"])
        gevent.sleep(2)
        self.check_evpn_config("VXLAN", vn1_obj, ["li.1", "li.2"])

        self.set_global_vrouter_config(["MPLSoGRE", "VXLAN"])
        gevent.sleep(2)
        self.check_evpn_config("MPLSoGRE", vn1_obj, ["li.1", "li.2"])

        self.set_global_vrouter_config(["MPLSoUDP", "VXLAN", "MPLSoGRE"])
        gevent.sleep(2)
        self.check_evpn_config("MPLSoUDP", vn1_obj, ["li.1", "li.2"])

        self.set_global_vrouter_config([])
        gevent.sleep(2)
        # DM defaults to VXLAN
        self.check_evpn_config("VXLAN", vn1_obj, ["li.1", "li.2"])

# end TestNetworkDM

