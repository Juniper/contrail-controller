#
# Copyright (c) 2017 Juniper Infra, Inc. All rights reserved.
#
import sys
import gevent
from time import sleep
sys.path.append("../common/tests")
from test_utils import *
from vnc_api.vnc_api import *
from cfgm_common.vnc_db import DBBase
from device_api.juniper_common_xsd import *
from device_manager.dm_utils import *
from gevent import monkey
monkey.patch_all()
from device_manager.db import DMCassandraDB
from device_manager.db import DBBaseDM
from device_manager.device_manager import DeviceManager
from test_common import *
from test_dm_common import *
from test_case import DMTestCase
from test_dm_utils import FakeDeviceConnect
from test_dm_utils import FakeNetconfManager

#
# All Infra related DM test cases should go here
#
class TestQfxBasicDM(TestCommonDM):

    def __init__(self, *args, **kwargs):
        self.product = "qfx5110"
        super(TestQfxBasicDM, self).__init__(*args, **kwargs)

    @retries(5, hook=retry_exc_handler)
    def check_switch_options_config(self, vn_obj):
        vrf_name_l2 = DMUtils.make_vrf_name(vn_obj.fq_name[-1], vn_obj.virtual_network_network_id, 'l2')
        network_id = vn_obj.get_virtual_network_network_id()
        vn_obj_properties = vn_obj.get_virtual_network_properties()
        vxlan_id = vn_obj_properties.get_vxlan_network_identifier()
        fwd_mode = vn_obj_properties.get_forwarding_mode()

        config = FakeDeviceConnect.get_xml_config()
        switch_opts = config.get_switch_options()
        self.assertIsNotNone(switch_opts)
        self.assertEqual(switch_opts.get_vtep_source_interface(), "lo0.0")
        if not switch_opts.get_vrf_import():
            self.assertTrue(False)
        if not switch_opts.get_vrf_export():
            self.assertTrue(False)
    # end check_switch_options_config

    # check qfx switch options
    def test_dm_qfx_switch_options(self):
        # check basic valid vendor, product plugin
        FakeNetconfManager.set_model('qfx5110')
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

        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1', product="qfx5110")
        pr.set_virtual_network(vn1_obj)
        self._vnc_lib.physical_router_update(pr)

        self.check_switch_options_config(vn1_obj)

        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

    # end test_dm_qfx_switch_options
