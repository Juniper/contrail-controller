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
    def check_interface_ip_config(self, if_name='', ri_name='',  ip_check='', ip_type='v4', network_id='', is_free=False):
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
                 
        ris = self.get_routing_instances(config, ri_name) or self.assertTrue(False)
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
        self.check_interface_ip_config('lo0', vrf_name_l3, '10.0.0.252/32', 'v4', vn1_obj.virtual_network_network_id)
        self.check_interface_ip_config('lo0', vrf_name_l3, '20.0.255.252/32', 'v4', vn1_obj.virtual_network_network_id)

        #detach vn from PR and check
        pr.del_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)
        gevent.sleep(2)

        self.check_interface_ip_config('lo0', vrf_name_l3, '10.0.0.252/32', 'v4', vn1_obj.virtual_network_network_id, True)
        self.check_interface_ip_config('lo0', vrf_name_l3, '20.0.255.252/32', 'v4', vn1_obj.virtual_network_network_id, True)

    #end check_interface_ip_config

# end TestNetworkDM

