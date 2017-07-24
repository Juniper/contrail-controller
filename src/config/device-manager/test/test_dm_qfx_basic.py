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
# All QFX DM  Basic Test Cases should go here
#
class TestQfxBasicDM(TestCommonDM):

    def __init__(self, *args, **kwargs):
        self.product = "qfx5110"
        super(TestQfxBasicDM, self).__init__(*args, **kwargs)

    @retries(5, hook=retry_exc_handler)
    def check_switch_options_config(self, vn_obj, vn_mode):
        vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1], vn_obj.virtual_network_network_id, vn_mode)

        config = FakeDeviceConnect.get_xml_config()
        switch_opts = config.get_switch_options()
        self.assertIsNotNone(switch_opts)
        self.assertEqual(switch_opts.get_vtep_source_interface(), "lo0.0")
        import_name = DMUtils.make_import_name(vrf_name)
        imports = switch_opts.get_vrf_import() or []
        self.assertIn(import_name, imports)
        export_name = DMUtils.make_export_name(vrf_name)
        exports = switch_opts.get_vrf_export() or []
        self.assertIn(export_name, exports)
    # end check_switch_options_config

    @retries(5, hook=retry_exc_handler)
    def check_ri_vlans_config(self, vn_obj, vni, vn_mode='l3'):
        vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                  vn_obj.virtual_network_network_id, vn_mode)
        config = FakeDeviceConnect.get_xml_config()
        ris = self.get_routing_instances(config, ri_name)
        if not ris:
            self.assertTrue(check)
        ri = ris[0]
        vlans = ri.get_vlans() or []
        vlans = vlans.get_vlan() or []
        vlan_name = vrf_name[1:]
        for vlan in vlans:
            if vlan.get_name() == vlan_name and vlan.get_vlan_id() == str(vni):
                return
        raise Exception("RI Vlan Config Not Found: %s"%(vlan_name))

    @retries(5, hook=retry_exc_handler)
    def check_vlans_config(self, vn_obj, vni, vn_mode):
        vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1], vn_obj.virtual_network_network_id, vn_mode)
        config = FakeDeviceConnect.get_xml_config()
        vlans = config.get_vlans()
        self.assertIsNotNone(vlans)
        vlans = vlans.get_vlan() or []
        vlan_name = vrf_name[1:]
        for vlan in vlans:
            if vlan.get_name() == vlan_name and vlan.get_vlan_id() == str(vni):
                return
        raise Exception("Vlan Config Not Found: %s"%(vlan_name))

    @retries(5, hook=retry_exc_handler)
    def check_lr_internal_vn_state(self, lr_obj):
        internal_vn_name = DMUtils.get_lr_internal_vn_name(lr_obj.uuid)
        vn_fq = lr_obj.get_fq_name()[:-1] + [internal_vn_name]
        vn_obj = None
        try:
            vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq)
        except NoIdError:
            raise
        vn_obj_properties = vn_obj.get_virtual_network_properties()
        if not vn_obj_properties:
            raise Exception("LR Internal VN properties are not set")
        fwd_mode = vn_obj_properties.get_forwarding_mode()
        if fwd_mode != 'l3':
            raise Exception("LR Internal VN Forwarding mode is not set to L3")
        return vn_obj
    # end check_lr_internal_vn_state

    def create_global_vrouter_config(self):
        fq = ["default-global-system-config", "default-global-vrouter-config"]
        gv = None
        try:
            gv = self._vnc_lib.global_vrouter_config_read(fq_name=fq)
        except NoIdError:
            gv = GlobalVrouterConfig(fq_name=fq)
            self._vnc_lib.global_vrouter_config_create(gv)
        return gv
    # end create_global_vrouter_config

    @retries(5, hook=retry_exc_handler)
    def check_esi_config(self, pi_name, esi_value, should_present=True):
        config = FakeDeviceConnect.get_xml_config()
        if should_present:
            interfaces = self.get_interfaces(config, pi_name)
            if not interfaces:
                raise Exception("No Interface Config generated")
            intf = interfaces[0]
            self.assertIsNotNone(intf.get_esi())
            self.assertIsNotNone(intf.get_esi().get_all_active())
            self.assertEqual(intf.get_esi().get_identifier(), esi_value)
        else:
            interfaces = self.get_interfaces(config, pi_name)
            if not interfaces:
                return
            intf = interfaces[0]
            if intf.get_esi():
                raise Exception("ESI Config still exist")
    # end check_esi_config

    def test_esi_config(self):
        FakeNetconfManager.set_model('qfx5110')
        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1', product="qfx5110")
        pr.set_physical_router_role("leaf")
        self._vnc_lib.physical_router_update(pr)
        pi = PhysicalInterface('pi1', parent_obj = pr)
        esi_value = "33:33:33:33:33:33:33:33:33:33"
        pi.set_ethernet_segment_identifier(esi_value)
        pi_id = self._vnc_lib.physical_interface_create(pi)
        self.check_esi_config('pi1', esi_value)
        pi.set_ethernet_segment_identifier(None)
        self._vnc_lib.physical_interface_update(pi)
        self.check_esi_config('pi1', esi_value, False)
    # end test_esi_config

    # check qfx switch options
    def verify_dm_qfx_switch_options(self, product):
        # check basic valid vendor, product plugin
        FakeNetconfManager.set_model(product)

        # create global vrouter config object
        self.create_global_vrouter_config()
        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1', product=product)

        # enable vxlan routing on project
        proj = self._vnc_lib.project_read(fq_name=["default-domain", "default-project"])
        proj.set_vxlan_routing(True)
        self._vnc_lib.project_update(proj)

        # create customer network, logical router and link to logical router
        vn1_name = 'vn1' + self.id() + "-" + product
        vn1_obj = VirtualNetwork(vn1_name)
        ipam_obj = NetworkIpam('ipam1' + self.id() + "-" + product)
        self._vnc_lib.network_ipam_create(ipam_obj)
        vn1_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))

        vn1_obj_properties = VirtualNetworkType()
        vn1_obj_properties.set_vxlan_network_identifier(2000)
        vn1_obj_properties.set_forwarding_mode('l2_l3')
        vn1_obj.set_virtual_network_properties(vn1_obj_properties)

        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn1_obj = self._vnc_lib.virtual_network_read(id=vn1_uuid)

        fq_name = ['default-domain', 'default-project', 'vmi1' + self.id() + "-" + product]
        vmi1 = VirtualMachineInterface(fq_name=fq_name, parent_type = 'project')
        vmi1.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        fq_name = ['default-domain', 'default-project', 'lr1' + self.id() + "-" + product]
        lr = LogicalRouter(fq_name=fq_name, parent_type = 'project')
        lr.set_physical_router(pr)
        lr.add_virtual_machine_interface(vmi1)
        lr_uuid = self._vnc_lib.logical_router_create(lr)

        # make sure internal is created
        int_vn = self.check_lr_internal_vn_state(lr)

        # verify generated switch options config
        self.check_switch_options_config(vn1_obj, "l2")
        # verify internal vn's config
        if 'qfx5' not in self.product:
            self.check_switch_options_config(int_vn, "l3")

        # check vlans config
        self.check_vlans_config(vn1_obj,
                 vn1_obj_properties.get_vxlan_network_identifier(), 'l2')
        if 'qfx10' in self.product:
            self.check_ri_vlans_config(vn1_obj,
                 vn1_obj_properties.get_vxlan_network_identifier(), 'l3')

        # cleanup
        pr = self._vnc_lib.physical_router_read(fq_name=pr.get_fq_name())
        lr = self._vnc_lib.logical_router_read(fq_name=lr.get_fq_name())
        lr.del_physical_router(pr)
        self._vnc_lib.logical_router_update(lr)

        bgp_router_fq = bgp_router.get_fq_name()
        pr_fq = pr.get_fq_name()
        self.delete_routers(bgp_router, pr)
        self.wait_for_routers_delete(bgp_router_fq, pr_fq)

    # end verify_dm_qfx_switch_options

    def test_dm_qfx_switch_options(self):
        self.verify_dm_qfx_switch_options('qfx5110')
        self.verify_dm_qfx_switch_options('qfx10000')
    # end test_dm_qfx_switch_options
