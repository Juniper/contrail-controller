#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import gevent
import json
from attrdict import AttrDict
from device_manager.device_manager import DeviceManager
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from test_dm_ansible_common import TestAnsibleCommonDM
from test_dm_utils import FakeJobHandler
from vnc_api.vnc_api import *


class TestAnsibleVirtualPortGroupDM(TestAnsibleCommonDM):

    def test_lag_config_push(self):
        # create objects
        try:
            self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
            self.create_features(['overlay-bgp', 'l2-gateway', 'virtual-port-group'])
            self.create_physical_roles(['leaf'])
            self.create_overlay_roles(['crb-access'])
            self.create_role_definitions([
                AttrDict({
                    'name': 'crb-access@leaf',
                    'physical_role': 'leaf',
                    'overlay_role': 'crb-access',
                    'features': ['overlay-bgp', 'l2-gateway', 'virtual-port-group'],
                    'feature_configs': {}
                })
            ])
            jt = self.create_job_template('job-template-lag')
            fabric = self.create_fabric('test-fabric-vpg', fabric_enterprise_style=False)
            np, rc = self.create_node_profile('node-profile-lag',
                                              device_family='junos-qfx',
                                              role_mappings=[
                                                  AttrDict(
                                                      {'physical_role': 'leaf',
                                                       'rb_roles': ['crb-access']}
                                                  )],
                                              job_template=jt)

            bgp_router, pr = self.create_router('router' + self.id(), '2.2.2.2',
                                                product='qfx5110', family='junos-qfx',
                                                role='leaf', rb_roles=['crb-access'],
                                                physical_role=self.physical_roles['leaf'],
                                                overlay_role=self.overlay_roles['crb-access'], fabric=fabric,
                                                node_profile=np)

            pr.set_physical_router_loopback_ip('20.20.0.1')
            self._vnc_lib.physical_router_update(pr)

            vxlan_id = 3
            vn_obj = self.create_vn(str(vxlan_id), '3.3.3.0')
            vlan_tag = 100

            pi_name1 = "xe-0/0/0"
            pi_obj_1 = PhysicalInterface(pi_name1, parent_obj=pr)
            self._vnc_lib.physical_interface_create(pi_obj_1)

            pi_name2 = "xe-0/0/1"
            pi_obj_2 = PhysicalInterface(pi_name2, parent_obj=pr)
            self._vnc_lib.physical_interface_create(pi_obj_2)

            vpg_name = "vpg-lag-1" + self.id()

            vmi_obj_1 = VirtualMachineInterface(vpg_name + "-tagged-" + str(vlan_tag),
                                                parent_type='project',
                                                fq_name=["default-domain", "default-project",
                                                         vpg_name + "-tagged-" + str(vlan_tag)])

            device_name = pr.get_fq_name()[-1]
            fabric_name = fabric.get_fq_name()[-1]
            phy_int_name = pi_obj_1.get_fq_name()[-1]

            phy_int_name_2 = pi_obj_2.get_fq_name()[-1]
            vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"},{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (
                phy_int_name, phy_int_name, device_name, fabric_name, phy_int_name_2, phy_int_name_2,
                device_name, fabric_name)

            vmi_bindings = {
                "key_value_pair": [{
                    "key": "vnic_type",
                    "value": "baremetal"
                }, {
                    "key": "vif_type",
                    "value": "vrouter"
                }, {
                    "key": "profile",
                    "value": vmi_profile
                }]
            }

            vmi_obj_1.set_virtual_machine_interface_bindings(vmi_bindings)

            vmi_properties = {
                "sub_interface_vlan_tag": vlan_tag
            }
            vmi_obj_1.set_virtual_machine_interface_properties(vmi_properties)

            vmi_obj_1.set_virtual_network(vn_obj)
            self._vnc_lib.virtual_machine_interface_create(vmi_obj_1)
            vpg_obj = VirtualPortGroup(vpg_name, parent_obj=fabric)
            vpg_obj.set_physical_interface_list([
                pi_obj_2.get_fq_name(),
                pi_obj_1.get_fq_name()],
                [{'ae_num': 0},
                 {'ae_num': 0}])

            self._vnc_lib.virtual_port_group_create(vpg_obj)
            vpg_obj.set_virtual_machine_interface_list([{'uuid': vmi_obj_1.get_uuid()}])
            self._vnc_lib.virtual_port_group_update(vpg_obj)

            gevent.sleep(1)
            abstract_config = self.check_dm_ansible_config_push()

            phy_intf = self.get_phy_interfaces(
                abstract_config.get('device_abstract_config').get('features').get('l2-gateway'), name='ae0')

            log_intf = self.get_logical_interface(phy_intf, name='ae0.' + str(vlan_tag))
            self.assertEqual(log_intf.get('vlan_tag'), str(vlan_tag))
            self.assertEqual(log_intf.get('unit'), str(vlan_tag))
            self.assertTrue(log_intf.get('is_tagged'))

            phy_intf = self.get_phy_interfaces(
                abstract_config.get('device_abstract_config').get('features').get('virtual-port-group'), name='ae0')
            self.assertEqual(phy_intf.get('interface_type'), 'lag')
            link_members = self.get_lag_members(phy_intf)
            self.assertEqual(len(link_members), 2)
            if 'xe-0/0/0' and 'xe-0/0/1' not in link_members:
                self.assertTrue(False)

            name = str(vxlan_id + 2000)
            vlan = self.get_vlans(abstract_config.get('device_abstract_config').get('features').get('l2-gateway'),
                                  name='bd-' + name)
            self.assertEqual(vlan.get('interfaces')[0].get('name'), 'ae0.' + str(vlan_tag))
            self.assertEqual(vlan.get('vxlan_id'), vxlan_id + 2000)

        finally:
            self.delete_objects()

    def test_mh_config_push(self):
        # create objects
        try:
            self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
            self.create_features(['overlay-bgp', 'l2-gateway', 'virtual-port-group'])
            self.create_physical_roles(['leaf'])
            self.create_overlay_roles(['crb-access'])
            self.create_role_definitions([
                AttrDict({
                    'name': 'crb-access@leaf',
                    'physical_role': 'leaf',
                    'overlay_role': 'crb-access',
                    'features': ['overlay-bgp', 'l2-gateway', 'virtual-port-group'],
                    'feature_configs': {}
                })
            ])
            jt = self.create_job_template('job-template-mh')
            fabric = self.create_fabric('test-fabric-mh')
            np, rc = self.create_node_profile('node-profile-mh',
                                              device_family='junos-qfx',
                                              role_mappings=[
                                                  AttrDict(
                                                      {'physical_role': 'leaf',
                                                       'rb_roles': ['crb-access']}
                                                  )],
                                              job_template=jt)

            bgp_router1, pr1 = self.create_router('router1' + self.id(), '3.3.3.3',
                                                  product='qfx5110', family='junos-qfx',
                                                  role='leaf', rb_roles=['crb-access'], fabric=fabric,
                                                  physical_role=self.physical_roles['leaf'],
                                                  overlay_role=self.overlay_roles['crb-access'],
                                                  node_profile=np)
            pr1.set_physical_router_loopback_ip('30.30.0.1')
            self._vnc_lib.physical_router_update(pr1)

            bgp_router2, pr2 = self.create_router('router2' + self.id(), '4.4.4.4',
                                                  product='qfx5110', family='junos-qfx',
                                                  role='leaf', rb_roles=['crb-access'], fabric=fabric,
                                                  physical_role=self.physical_roles['leaf'],
                                                  overlay_role=self.overlay_roles['crb-access'],
                                                  node_profile=np)
            pr2.set_physical_router_loopback_ip('40.40.0.1')
            self._vnc_lib.physical_router_update(pr2)

            vxlan_id = 4
            vn_obj = self.create_vn(str(vxlan_id), '4.4.4.0')
            vlan_tag = 100

            pi_name1 = "xe-0/0/0"
            pi_obj_1 = PhysicalInterface(pi_name1, parent_obj=pr1)
            self._vnc_lib.physical_interface_create(pi_obj_1)

            pi_name2 = "xe-0/0/1"
            pi_obj_2 = PhysicalInterface(pi_name2, parent_obj=pr2)
            self._vnc_lib.physical_interface_create(pi_obj_2)

            vpg_name = "vpg-lag-1" + self.id()

            vmi_obj_1 = VirtualMachineInterface(vpg_name + "-tagged-" + str(vlan_tag),
                                                parent_type='project',
                                                fq_name=["default-domain", "default-project",
                                                         vpg_name + "-tagged-" + str(vlan_tag)])

            device_name_1 = pr1.get_fq_name()[-1]
            fabric_name = fabric.get_fq_name()[-1]

            device_name_2 = pr2.get_fq_name()[-1]

            vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"},{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (
                pi_name1, pi_name1, device_name_1, fabric_name, pi_name2, pi_name2,
                device_name_2, fabric_name)

            vmi_bindings = {
                "key_value_pair": [{
                    "key": "vnic_type",
                    "value": "baremetal"
                }, {
                    "key": "vif_type",
                    "value": "vrouter"
                }, {
                    "key": "profile",
                    "value": vmi_profile
                }]
            }

            vmi_obj_1.set_virtual_machine_interface_bindings(vmi_bindings)

            vmi_properties = {
                "sub_interface_vlan_tag": vlan_tag
            }
            vmi_obj_1.set_virtual_machine_interface_properties(vmi_properties)

            vmi_obj_1.set_virtual_network(vn_obj)
            self._vnc_lib.virtual_machine_interface_create(vmi_obj_1)
            vpg_obj = VirtualPortGroup(vpg_name, parent_obj=fabric)
            vpg_obj.set_physical_interface_list([
                pi_obj_2.get_fq_name(),
                pi_obj_1.get_fq_name()],
                [{'ae_num': 0},
                 {'ae_num': 0}])

            self._vnc_lib.virtual_port_group_create(vpg_obj)
            vpg_obj.set_virtual_machine_interface_list([{'uuid': vmi_obj_1.get_uuid()}])
            self._vnc_lib.virtual_port_group_update(vpg_obj)

            gevent.sleep(1)
            abstract_config = self.check_dm_ansible_config_push()

            phy_intf = self.get_phy_interfaces(
                abstract_config.get('device_abstract_config').get('features').get('l2-gateway'), name='ae0')
            log_intf = self.get_logical_interface(phy_intf, name='ae0.' + str(vlan_tag))
            self.assertEqual(log_intf.get('vlan_tag'), str(vlan_tag))
            self.assertEqual(log_intf.get('unit'), str(vlan_tag))
            self.assertTrue(log_intf.get('is_tagged'))

            phy_intf = self.get_phy_interfaces(
                abstract_config.get('device_abstract_config').get('features').get('virtual-port-group'), name='ae0')
            self.assertEqual(phy_intf.get('interface_type'), 'lag')
            self.assertIsNotNone(phy_intf.get('ethernet_segment_identifier'))

            link_members = self.get_lag_members(phy_intf)
            self.assertEqual(len(link_members), 1)
            if abstract_config.get('management_ip') == '3.3.3.3' and 'xe-0/0/0' not in link_members:
                self.assertTrue(False)
            if abstract_config.get('management_ip') == '4.4.4.4' and 'xe-0/0/1' not in link_members:
                self.assertTrue(False)

            name = str(vxlan_id + 2000)
            vlan = self.get_vlans(abstract_config.get('device_abstract_config').get('features').get('l2-gateway'),
                                  name='bd-' + name)
            self.assertEqual(vlan.get('interfaces')[0].get('name'), 'ae0.' + str(vlan_tag))
            self.assertEqual(vlan.get('vxlan_id'), vxlan_id + 2000)

        finally:
            self.delete_objects()

    def test_lag_config_native_vlan_push(self):
        # create objects
        try:
            self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
            self.create_features(['overlay-bgp', 'l2-gateway', 'virtual-port-group'])
            self.create_physical_roles(['leaf'])
            self.create_overlay_roles(['crb-access'])
            self.create_role_definitions([
                AttrDict({
                    'name': 'crb-access@leaf',
                    'physical_role': 'leaf',
                    'overlay_role': 'crb-access',
                    'features': ['overlay-bgp', 'l2-gateway', 'virtual-port-group'],
                    'feature_configs': {}
                })
            ])
            jt = self.create_job_template('job-template-lag2')
            fabric = self.create_fabric('test-fabric-vpg-native')
            np, rc = self.create_node_profile('node-profile-lag2',
                                              device_family='junos-qfx',
                                              role_mappings=[
                                                  AttrDict(
                                                      {'physical_role': 'leaf',
                                                       'rb_roles': ['crb-access'],
                                                       'feature_configs': {}}
                                                  )],
                                              job_template=jt)

            bgp_router, pr = self.create_router('router3' + self.id(), '2.2.2.2',
                                                product='qfx5110', family='junos-qfx',
                                                role='leaf', rb_roles=['crb-access'], fabric=fabric,
                                                node_profile=np, overlay_role=self.overlay_roles['crb-access'],
                                                physical_role=self.physical_roles['leaf'])

            pr.set_physical_router_loopback_ip('20.20.0.1')
            self._vnc_lib.physical_router_update(pr)

            vxlan_id = 5
            vn_obj = self.create_vn(str(vxlan_id), '5.5.5.0')
            port_vlan_tag = 50
            vlan_tag = 0

            pi_name1 = "xe-0/0/0"
            pi_obj_1 = PhysicalInterface(pi_name1, parent_obj=pr)
            self._vnc_lib.physical_interface_create(pi_obj_1)

            pi_name2 = "xe-0/0/1"
            pi_obj_2 = PhysicalInterface(pi_name2, parent_obj=pr)
            self._vnc_lib.physical_interface_create(pi_obj_2)

            vpg_name = "vpg-native-vlan-1" + self.id()

            vmi_obj_1 = VirtualMachineInterface(vpg_name + "-untagged-" + str(port_vlan_tag),
                                                parent_type='project',
                                                fq_name=["default-domain", "default-project",
                                                         vpg_name + "-untagged-" + str(port_vlan_tag)])

            device_name = pr.get_fq_name()[-1]
            fabric_name = fabric.get_fq_name()[-1]

            vmi_profile = "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"},{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % (
                pi_name1, pi_name1, device_name, fabric_name, pi_name2, pi_name2,
                device_name, fabric_name)

            vmi_bindings = {
                "key_value_pair": [{
                    "key": "vnic_type",
                    "value": "baremetal"
                }, {
                    "key": "vif_type",
                    "value": "vrouter"
                }, {
                    "key": "profile",
                    "value": vmi_profile
                }]
            }

            if not vlan_tag:
                ll = {
                    "key": "tor_port_vlan_id",
                    "value": str(port_vlan_tag)
                }
                vmi_bindings['key_value_pair'].append(ll)

            else:
                vmi_properties = {
                    "sub_interface_vlan_tag": vlan_tag
                }
                vmi_obj_1.set_virtual_machine_interface_properties(vmi_properties)

            vmi_obj_1.set_virtual_machine_interface_bindings(vmi_bindings)

            vmi_obj_1.set_virtual_network(vn_obj)
            self._vnc_lib.virtual_machine_interface_create(vmi_obj_1)
            vpg_obj = VirtualPortGroup(vpg_name, parent_obj=fabric)
            vpg_obj.set_physical_interface_list([
                pi_obj_2.get_fq_name(),
                pi_obj_1.get_fq_name()],
                [{'ae_num': 0},
                 {'ae_num': 0}])

            self._vnc_lib.virtual_port_group_create(vpg_obj)
            vpg_obj.set_virtual_machine_interface_list([{'uuid': vmi_obj_1.get_uuid()}])
            self._vnc_lib.virtual_port_group_update(vpg_obj)

            gevent.sleep(1)
            abstract_config = self.check_dm_ansible_config_push()

            phy_intf = self.get_phy_interfaces(abstract_config.get('device_abstract_config').get('features').get('virtual-port-group'), name='ae0')
            self.assertEqual(phy_intf.get('interface_type'), 'lag')
            link_members = self.get_lag_members(phy_intf)
            self.assertEqual(len(link_members), 2)
            if 'xe-0/0/0' and 'xe-0/0/1' not in link_members:
                self.assertTrue(False)

            name = str(vxlan_id + 2000)
            vlan = self.get_vlans(abstract_config.get('device_abstract_config').get('features').get('l2-gateway'), name='bd-' + name)
            self.assertEqual(vlan.get('interfaces')[0].get('name'), 'ae0.' + str(vlan_tag))
            self.assertEqual(vlan.get('vxlan_id'), vxlan_id + 2000)
        finally:
            self.delete_objects()

    def test_mh_config_native_vlan_push(self):
        # create objects
        try:
            self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
            self.create_features(['overlay-bgp', 'l2-gateway', 'virtual-port-group'])
            self.create_physical_roles(['leaf'])
            self.create_overlay_roles(['crb-access'])
            self.create_role_definitions([
                AttrDict({
                    'name': 'crb-access@leaf',
                    'physical_role': 'leaf',
                    'overlay_role': 'crb-access',
                    'features': ['overlay-bgp', 'l2-gateway', 'virtual-port-group'],
                    'feature_configs': {}
                })
            ])
            jt = self.create_job_template('job-template-mh2')
            fabric = self.create_fabric('test-fabric-mh-native')
            np, rc = self.create_node_profile('node-profile-mh2',
                                              device_family='junos-qfx',
                                              role_mappings=[
                                                  AttrDict(
                                                      {'physical_role': 'leaf',
                                                       'rb_roles': ['crb-access']}
                                                  )],
                                              job_template=jt)

            bgp_router1, pr1 = self.create_router('router4' + self.id(), '3.3.3.3',
                                                  product='qfx5110', family='junos-qfx',
                                                  role='leaf', rb_roles=['crb-access'], fabric=fabric,
                                                  node_profile=np, overlay_role=self.overlay_roles['crb-access'],
                                                  physical_role=self.physical_roles['leaf'])
            pr1.set_physical_router_loopback_ip('30.30.0.1')
            self._vnc_lib.physical_router_update(pr1)

            bgp_router2, pr2 = self.create_router('router5' + self.id(), '4.4.4.4',
                                                  product='qfx5110', family='junos-qfx',
                                                  role='leaf', rb_roles=['crb-access'], fabric=fabric,
                                                  node_profile=np, overlay_role=self.overlay_roles['crb-access'],
                                                  physical_role=self.physical_roles['leaf'])
            pr2.set_physical_router_loopback_ip('40.40.0.1')
            self._vnc_lib.physical_router_update(pr2)

            vxlan_id = 6
            vn_obj = self.create_vn(str(vxlan_id), '6.6.6.0')
            port_vlan_tag = 60
            vlan_tag = 0
            vmi, vm, pi_list = self.attach_vmi(str(vxlan_id), ['xe-0/0/1', 'xe-0/0/2'], [pr1, pr2], vn_obj, None, fabric,
                                               None, port_vlan_tag)

            gevent.sleep(1)
            abstract_config = self.check_dm_ansible_config_push()

            phy_intf = self.get_phy_interfaces(abstract_config.get('device_abstract_config').get('features').get('l2-gateway'), name='ae0')
            log_intf = self.get_logical_interface(phy_intf, name='ae0.' + str(vlan_tag))
            self.assertEqual(log_intf.get('vlan_tag'), str(port_vlan_tag))
            self.assertEqual(log_intf.get('unit'), str(vlan_tag))
            self.assertFalse(log_intf.get('is_tagged'))

            phy_intf = self.get_phy_interfaces(abstract_config.get('device_abstract_config').get('features').get('virtual-port-group'), name='ae0')
            self.assertEqual(phy_intf.get('interface_type'), 'lag')
            self.assertIsNotNone(phy_intf.get('ethernet_segment_identifier'))
            link_members = self.get_lag_members(phy_intf)
            self.assertEqual(len(link_members), 1)
            if abstract_config.get('management_ip') == '3.3.3.3' and 'xe-0/0/1' not in link_members:
                self.assertTrue(False)
            if abstract_config.get('management_ip') == '4.4.4.4' and 'xe-0/0/2' not in link_members:
                self.assertTrue(False)

            name = str(vxlan_id + 2000)
            vlan = self.get_vlans(abstract_config.get('device_abstract_config').get('features').get('l2-gateway'), name='bd-' + name)
            self.assertEqual(vlan.get('interfaces')[0].get('name'), 'ae0.' + str(vlan_tag))
            self.assertEqual(vlan.get('vxlan_id'), vxlan_id + 2000)
        finally:
            self.delete_objects()

    def test_lag_sg_config_push(self):
        # create objects
        try:
            self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
            self.create_features(['overlay-bgp', 'l2-gateway', 'virtual-port-group'])
            self.create_physical_roles(['leaf'])
            self.create_overlay_roles(['crb-access'])
            self.create_role_definitions([
                AttrDict({
                    'name': 'crb-access@leaf',
                    'physical_role': 'leaf',
                    'overlay_role': 'crb-access',
                    'features': ['overlay-bgp', 'l2-gateway', 'virtual-port-group'],
                    'feature_configs': {}
                })
            ])
            jt = self.create_job_template('job-template-lag')
            fabric = self.create_fabric('test-fabric-vpg')
            np, rc = self.create_node_profile('node-profile-lag',
                                              device_family='junos-qfx',
                                              role_mappings=[
                                                  AttrDict(
                                                      {'physical_role': 'leaf',
                                                       'rb_roles': ['crb-access']}
                                                  )],
                                              job_template=jt)

            bgp_router, pr = self.create_router('router' + self.id(), '2.2.2.2',
                                                product='qfx5110', family='junos-qfx',
                                                role='leaf', rb_roles=['crb-access'], fabric=fabric,
                                                node_profile=np, overlay_role=self.overlay_roles['crb-access'],
                                                physical_role=self.physical_roles['leaf'])

            pr.set_physical_router_loopback_ip('20.20.0.1')
            self._vnc_lib.physical_router_update(pr)

            vxlan_id = 3
            vn_obj = self.create_vn(str(vxlan_id), '3.3.3.0')
            sg_obj = self.create_sg('SG1')
            vlan_tag = 100

            vmi, vm, pi_list = self.attach_vmi(str(vxlan_id), ['xe-0/0/1', 'xe-0/0/2'], [pr, pr], vn_obj, sg_obj, fabric,
                                               vlan_tag)

            gevent.sleep(1)
            abstract_config = self.check_dm_ansible_config_push()

            phy_intf = self.get_phy_interfaces(abstract_config.get('device_abstract_config').get('features').get('l2-gateway'), name='ae0')
            log_intf = self.get_logical_interface(phy_intf, name='ae0.' + str(vlan_tag))
            self.assertEqual(log_intf.get('vlan_tag'), str(vlan_tag))
            self.assertEqual(log_intf.get('unit'), str(vlan_tag))
            self.assertTrue(log_intf.get('is_tagged'))

            phy_intf = self.get_phy_interfaces(abstract_config.get('device_abstract_config').get('features').get('virtual-port-group'), name='ae0')
            self.assertEqual(phy_intf.get('interface_type'), 'lag')
            link_members = self.get_lag_members(phy_intf)
            self.assertEqual(len(link_members), 2)
            if 'xe-0/0/1' and 'xe-0/0/2' not in link_members:
                self.assertTrue(False)

            name = str(vxlan_id + 2000)
            vlan = self.get_vlans(abstract_config.get('device_abstract_config').get('features').get('l2-gateway'), name='bd-' + name)
            self.assertEqual(vlan.get('interfaces')[0].get('name'), 'ae0.' + str(vlan_tag))
            self.assertEqual(vlan.get('vxlan_id'), vxlan_id + 2000)

            fw = self.get_firewalls(abstract_config.get('device_abstract_config'))
            if not fw:
                self.assertTrue(False)

        finally:
            self.delete_objects()

    def test_tagged_and_untagged_vpg_enterprise_style(self):
        try:
            self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
            self.create_features(['overlay-bgp', 'l2-gateway', 'virtual-port-group'])
            self.create_physical_roles(['leaf'])
            self.create_overlay_roles(['crb-access'])
            self.create_role_definitions([
                AttrDict({
                    'name': 'crb-access@leaf',
                    'physical_role': 'leaf',
                    'overlay_role': 'crb-access',
                    'features': ['overlay-bgp', 'l2-gateway', 'virtual-port-group'],
                    'feature_configs': {}
                })
            ])

            jt = self.create_job_template('job-template-1')
            fabric = self.create_fabric('test-fabric',
                                        fabric_enterprise_style=True)
            np, rc = self.create_node_profile('node-profile-1',
                                              device_family='junos-qfx',
                                              role_mappings=[
                                                  AttrDict(
                                                      {'physical_role': 'leaf',
                                                       'rb_roles': ['crb-access']}
                                                  )],
                                              job_template=jt)

            vn1_obj = self.create_vn('1', '1.1.1.0')

            bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                                product='qfx5110', family='junos-qfx',
                                                role='leaf', rb_roles=['crb-access'],
                                                physical_role=self.physical_roles['leaf'],
                                                overlay_role=self.overlay_roles['crb-access'], fabric=fabric,
                                                node_profile=np)
            pr.set_physical_router_loopback_ip('10.10.0.1')
            self._vnc_lib.physical_router_update(pr)

            vmi1, vm1, pi1 = self.attach_vmi('1', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, 101)
            vmi2, vm2, _ = self.attach_vmi('2', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, None, 101)

            gevent.sleep(1)
            ac = self.check_dm_ansible_config_push()
            fc = ac.get('device_abstract_config').get('features').get('l2-gateway')

            self.assertIsNone(self.get_phy_interfaces(fc, name='xe-0/0/1'))
            self.assertIsNone(self.get_phy_interfaces(fc, name='xe-0/0/2'))
        finally:
            self.delete_objects()

    def test_untagged_and_tagged_vpg_enterprise_style(self):
        try:
            self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
            self.create_features(['overlay-bgp', 'l2-gateway', 'virtual-port-group'])
            self.create_physical_roles(['leaf'])
            self.create_overlay_roles(['crb-access'])
            self.create_role_definitions([
                AttrDict({
                    'name': 'crb-access@leaf',
                    'physical_role': 'leaf',
                    'overlay_role': 'crb-access',
                    'features': ['overlay-bgp', 'l2-gateway', 'virtual-port-group'],
                    'feature_configs': {}
                })
            ])

            jt = self.create_job_template('job-template-1')
            fabric = self.create_fabric('test-fabric',
                                        fabric_enterprise_style=True)
            np, rc = self.create_node_profile('node-profile-1',
                                              device_family='junos-qfx',
                                              role_mappings=[
                                                  AttrDict(
                                                      {'physical_role': 'leaf',
                                                       'rb_roles': ['crb-access']}
                                                  )],
                                              job_template=jt)

            vn1_obj = self.create_vn('1', '1.1.1.0')

            bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                                product='qfx5110', family='junos-qfx',
                                                role='leaf', rb_roles=['crb-access'],
                                                physical_role=self.physical_roles['leaf'],
                                                overlay_role=self.overlay_roles['crb-access'], fabric=fabric,
                                                node_profile=np)
            pr.set_physical_router_loopback_ip('10.10.0.1')
            self._vnc_lib.physical_router_update(pr)

            vmi1, vm1, pi1 = self.attach_vmi('11', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, None, 101)
            vmi2, vm2, _ = self.attach_vmi('12', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, 101)

            gevent.sleep(1)
            ac = self.check_dm_ansible_config_push()
            fc = ac.get('device_abstract_config').get('features').get('l2-gateway')

            self.assertIsNone(self.get_phy_interfaces(fc, name='xe-0/0/1'))
            self.assertIsNone(self.get_phy_interfaces(fc, name='xe-0/0/2'))
        finally:
            self.delete_objects()

    def test_tagged_and_untagged_vpg_sp_style(self):
        try:
            self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
            self.create_features(['overlay-bgp', 'l2-gateway', 'virtual-port-group'])
            self.create_physical_roles(['leaf'])
            self.create_overlay_roles(['crb-access'])
            self.create_role_definitions([
                AttrDict({
                    'name': 'crb-access@leaf',
                    'physical_role': 'leaf',
                    'overlay_role': 'crb-access',
                    'features': ['overlay-bgp', 'l2-gateway', 'virtual-port-group'],
                    'feature_configs': {}
                })
            ])

            jt = self.create_job_template('job-template-1')
            fabric = self.create_fabric('test-fabric',
                                        fabric_enterprise_style=False)
            np, rc = self.create_node_profile('node-profile-1',
                                              device_family='junos-qfx',
                                              role_mappings=[
                                                  AttrDict(
                                                      {'physical_role': 'leaf',
                                                       'rb_roles': ['crb-access']}
                                                  )],
                                              job_template=jt)

            vn1_obj = self.create_vn('1', '1.1.1.0')

            bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                                product='qfx5110', family='junos-qfx',
                                                role='leaf', rb_roles=['crb-access'],
                                                physical_role=self.physical_roles['leaf'],
                                                overlay_role=self.overlay_roles['crb-access'], fabric=fabric,
                                                node_profile=np)
            pr.set_physical_router_loopback_ip('10.10.0.1')
            self._vnc_lib.physical_router_update(pr)

            vmi1, vm1, pi1 = self.attach_vmi('21', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, 101)
            vmi2, vm2, _ = self.attach_vmi('22', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, None, 102)

            gevent.sleep(1)
            ac = self.check_dm_ansible_config_push()
            fc = ac.get('device_abstract_config').get('features').get('l2-gateway')

            pi_name = 'xe-0/0/1'
            li_name = pi_name + '.101'
            pi = self.get_phy_interfaces(fc, name=pi_name)
            li = self.get_logical_interface(pi, name=li_name)

            self.assertEqual(li.get('vlan_tag'), '101')
            self.assertTrue(li.get('is_tagged'))

            pi_name = 'xe-0/0/1'
            li_name = pi_name + '.0'
            pi = self.get_phy_interfaces(fc, name=pi_name)
            li = self.get_logical_interface(pi, name=li_name)

            self.assertEqual(li.get('vlan_tag'), '102')
            self.assertFalse(li.get('is_tagged'))
        finally:
            self.delete_objects()

    def test_untagged_and_tagged_vpg_sp_style(self):
        try:
            self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
            self.create_features(['overlay-bgp', 'l2-gateway', 'virtual-port-group'])
            self.create_physical_roles(['leaf'])
            self.create_overlay_roles(['crb-access'])
            self.create_role_definitions([
                AttrDict({
                    'name': 'crb-access@leaf',
                    'physical_role': 'leaf',
                    'overlay_role': 'crb-access',
                    'features': ['overlay-bgp', 'l2-gateway', 'virtual-port-group'],
                    'feature_configs': {}
                })
            ])

            jt = self.create_job_template('job-template-1')
            fabric = self.create_fabric('test-fabric',
                                        fabric_enterprise_style=False)
            np, rc = self.create_node_profile('node-profile-1',
                                              device_family='junos-qfx',
                                              role_mappings=[
                                                  AttrDict(
                                                      {'physical_role': 'leaf',
                                                       'rb_roles': ['crb-access']}
                                                  )],
                                              job_template=jt)

            vn1_obj = self.create_vn('1', '1.1.1.0')

            bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                                product='qfx5110', family='junos-qfx',
                                                role='leaf', rb_roles=['crb-access'],
                                                physical_role=self.physical_roles['leaf'],
                                                overlay_role=self.overlay_roles['crb-access'], fabric=fabric,
                                                node_profile=np)
            pr.set_physical_router_loopback_ip('10.10.0.1')
            self._vnc_lib.physical_router_update(pr)

            vmi1, vm1, pi1 = self.attach_vmi('31', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, None, 101)
            vmi2, vm2, _ = self.attach_vmi('32', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, 102)

            gevent.sleep(1)
            ac = self.check_dm_ansible_config_push()
            fc = ac.get('device_abstract_config').get('features').get('l2-gateway')

            pi_name = 'xe-0/0/1'
            li_name = pi_name + '.0'
            pi = self.get_phy_interfaces(fc, name=pi_name)
            li = self.get_logical_interface(pi, name=li_name)

            self.assertEqual(li.get('vlan_tag'), '101')
            self.assertFalse(li.get('is_tagged'))

            pi_name = 'xe-0/0/1'
            li_name = pi_name + '.102'
            pi = self.get_phy_interfaces(fc, name=pi_name)
            li = self.get_logical_interface(pi, name=li_name)

            self.assertEqual(li.get('vlan_tag'), '102')
            self.assertTrue(li.get('is_tagged'))

        finally:
            self.delete_objects()

    def delete_objects(self):

        vpg_list = self._vnc_lib.virtual_port_groups_list().get('virtual-port-groups')
        for vpg in vpg_list:
            vpg_obj = self._vnc_lib.virtual_port_group_read(id=vpg['uuid'])
            vpg_obj.set_virtual_machine_interface_list([])
            vpg_obj.set_physical_interface_list([])
            self._vnc_lib.virtual_port_group_update(vpg_obj)

        vmi_list = self._vnc_lib.virtual_machine_interfaces_list().get(
            'virtual-machine-interfaces')
        for vmi in vmi_list:
            self._vnc_lib.virtual_machine_interface_delete(id=vmi['uuid'])

        pi_list = self._vnc_lib.physical_interfaces_list().get('physical-interfaces')
        for pi in pi_list:
            self._vnc_lib.physical_interface_delete(id=pi['uuid'])

        for vpg in vpg_list:
            self._vnc_lib.virtual_port_group_delete(id=vpg['uuid'])

        pr_list = self._vnc_lib.physical_routers_list().get('physical-routers')
        for pr in pr_list:
            self._vnc_lib.physical_router_delete(id=pr['uuid'])

        vn_list = self._vnc_lib.virtual_networks_list().get('virtual-networks')
        for vn in vn_list:
            self._vnc_lib.virtual_network_delete(id=vn['uuid'])

        rc_list = self._vnc_lib.role_configs_list().get('role-configs')
        for rc in rc_list:
            self._vnc_lib.role_config_delete(id=rc['uuid'])

        np_list = self._vnc_lib.node_profiles_list().get('node-profiles')
        for np in np_list:
            self._vnc_lib.node_profile_delete(id=np['uuid'])

        fab_list = self._vnc_lib.fabrics_list().get('fabrics')
        for fab in fab_list:
            self._vnc_lib.fabric_delete(id=fab['uuid'])

        jt_list = self._vnc_lib.job_templates_list().get('job-templates')
        for jt in jt_list:
            self._vnc_lib.job_template_delete(id=jt['uuid'])

        pp_list = self._vnc_lib.port_profiles_list().get('port-profiles')
        for pp in pp_list:
            self._vnc_lib.port_profile_delete(id=pp['uuid'])

        sc_list = self._vnc_lib.storm_control_profiles_list().get('storm-control-profiles')
        for sc in sc_list:
            self._vnc_lib.storm_control_profile_delete(id=sc['uuid'])

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
