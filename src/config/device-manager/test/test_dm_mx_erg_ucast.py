#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
from builtins import str
import gevent
import json
from attrdict import AttrDict
from device_manager.device_manager import DeviceManager
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from .test_dm_ansible_common import TestAnsibleCommonDM
from .test_dm_utils import FakeJobHandler
from vnc_api.vnc_api import *


class TestMXERBUCATGW(TestAnsibleCommonDM):

    def test_erb_ucast_gw_on_mx(self):
        # create objects
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoGRE', 'MPLSoUDP'])
        jt = self.create_job_template('job-template-lag')
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['erb-ucast-gateway'])
        fabric = self.create_fabric('test-fabric-vpg')
        np, rc = self.create_node_profile('node-profile-lag',
                                          device_family='junos',
                                          role_mappings=[
                                              AttrDict(
                                                  {'physical_role': 'leaf',
                                                   'rb_roles': [
                                                       'erb-ucast-gateway']}
                                              )],
                                          job_template=jt)
        erb_cast_gw_role_obj = self.overlay_roles['erb-ucast-gateway']
        bgp_router, pr = self.create_router('router' + self.id(), '2.2.2.2',
                                            product='mx480', family='junos',
                                            role='leaf',
                                            rb_roles=['erb-ucast-gateway'],
                                            fabric=fabric,node_profile=np,
                                            physical_role=
                                            self.physical_roles['leaf'],
                                            overlay_role=erb_cast_gw_role_obj
                                            )

        pr.set_physical_router_loopback_ip('20.20.0.1')
        self._vnc_lib.physical_router_update(pr)

        vxlan_id = 3
        vn_obj = self.create_vn(str(vxlan_id), '3.3.3.0')
        vlan_tag = 100

        vmi, vm, pi_list = self.attach_vmi(str(vxlan_id),
                                           ['xe-0/0/1', 'xe-0/0/2'],
                                           [pr, pr], vn_obj, None, fabric,
                                           vlan_tag)
        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        device_abstract_config = abstract_config.get(
            'device_abstract_config', {})
        rb_roles = device_abstract_config.get('system', {}).get(
            'routing_bridging_roles', [])
        if 'erb-ucast-gateway' in rb_roles:
            self.assertTrue(True)

        self.assertEqual('mx480', device_abstract_config.get('system').get(
            'product_name'))

        phy_intf = self.get_phy_interfaces(
            abstract_config.get('device_abstract_config'), name='ae0')
        self.assertEqual(phy_intf.get('interface_type'), 'lag')

        log_intf = self.get_logical_interface(phy_intf,
                                              name='ae0.' + str(vlan_tag))
        self.assertEqual(log_intf.get('vlan_tag'), str(vlan_tag))
        self.assertEqual(log_intf.get('unit'), str(vlan_tag))
        self.assertTrue(log_intf.get('is_tagged'))

        link_members = self.get_lag_members(phy_intf)
        self.assertEqual(len(link_members), 2)
        if 'xe-0/0/1' and 'xe-0/0/2' not in link_members:
            self.assertTrue(False)

        name = str(vxlan_id + 2000)
        vlan = self.get_vlans(
            abstract_config.get('device_abstract_config'),
            name='bd-' + name)
        self.assertEqual(vlan.get('interfaces')[0].get('name'),
                         'ae0.' + str(vlan_tag))
        self.assertEqual(vlan.get('vxlan_id'), vxlan_id + 2000)

        self._vnc_lib.virtual_machine_interface_delete(
            fq_name=vmi.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(
                fq_name=pi_list[idx].get_fq_name())

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())

        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn_obj.get_fq_name())

        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())
