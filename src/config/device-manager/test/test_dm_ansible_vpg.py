#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
from builtins import str
from builtins import range
import gevent
from attrdict import AttrDict
from .test_dm_ansible_common import TestAnsibleCommonDM
from vnc_api.vnc_api import *


class TestAnsibleVpgDM(TestAnsibleCommonDM):

    def test_lag_config_push(self):
        #create objects
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
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
            node_profile=np)

        pr.set_physical_router_loopback_ip('20.20.0.1')
        self._vnc_lib.physical_router_update(pr)

        vxlan_id = 3
        vn_obj = self.create_vn(str(vxlan_id), '3.3.3.0')
        vlan_tag = 100

        vmi, vm, pi_list = self.attach_vmi(str(vxlan_id), ['xe-0/0/1', 'xe-0/0/2'], [pr, pr], vn_obj, None, fabric, vlan_tag)


        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()


        phy_intf = self.get_phy_interfaces(abstract_config.get('device_abstract_config'), name='ae0')
        self.assertEqual(phy_intf.get('interface_type'), 'lag')

        log_intf = self.get_logical_interface(phy_intf, name='ae0.'+str(vlan_tag))
        self.assertEqual(log_intf.get('vlan_tag'), str(vlan_tag))
        self.assertEqual(log_intf.get('unit'), str(vlan_tag))
        self.assertTrue(log_intf.get('is_tagged'))

        link_members = self.get_lag_members(phy_intf)
        self.assertEqual(len(link_members), 2)
        if 'xe-0/0/1' and 'xe-0/0/2' not in link_members:
            self.assertTrue(False)

        name = str(vxlan_id+2000)
        vlan = self.get_vlans(abstract_config.get('device_abstract_config'), name='bd-'+name)
        self.assertEqual(vlan.get('interfaces')[0].get('name'), 'ae0.'+str(vlan_tag))
        self.assertEqual(vlan.get('vxlan_id'), vxlan_id+2000)

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(fq_name=pi_list[idx].get_fq_name())

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())

        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn_obj.get_fq_name())


        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())


    def test_mh_config_push(self):
        #create objects
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
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
            node_profile=np)
        pr1.set_physical_router_loopback_ip('30.30.0.1')
        self._vnc_lib.physical_router_update(pr1)

        bgp_router2, pr2 = self.create_router('router2' + self.id(), '4.4.4.4',
            product='qfx5110', family='junos-qfx',
            role='leaf', rb_roles=['crb-access'], fabric=fabric,
            node_profile=np)
        pr2.set_physical_router_loopback_ip('40.40.0.1')
        self._vnc_lib.physical_router_update(pr2)

        vxlan_id = 4
        vn_obj = self.create_vn(str(vxlan_id), '4.4.4.0')
        vlan_tag = 100
        vmi, vm, pi_list = self.attach_vmi(str(vxlan_id), ['xe-0/0/1', 'xe-0/0/2'], [pr1, pr2], vn_obj, None, fabric, vlan_tag)


        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()


        phy_intf = self.get_phy_interfaces(abstract_config.get('device_abstract_config'), name='ae0')
        self.assertEqual(phy_intf.get('interface_type'), 'lag')
        self.assertIsNotNone(phy_intf.get('ethernet_segment_identifier'))

        log_intf = self.get_logical_interface(phy_intf, name='ae0.'+str(vlan_tag))
        self.assertEqual(log_intf.get('vlan_tag'), str(vlan_tag))
        self.assertEqual(log_intf.get('unit'), str(vlan_tag))
        self.assertTrue(log_intf.get('is_tagged'))

        link_members = self.get_lag_members(phy_intf)
        self.assertEqual(len(link_members), 1)
        if abstract_config.get('management_ip') == '3.3.3.3' and 'xe-0/0/1' not in link_members:
            self.assertTrue(False)
        if abstract_config.get('management_ip') == '4.4.4.4' and 'xe-0/0/2' not in link_members:
            self.assertTrue(False)

        name = str(vxlan_id+2000)
        vlan = self.get_vlans(abstract_config.get('device_abstract_config'), name='bd-'+name)
        self.assertEqual(vlan.get('interfaces')[0].get('name'), 'ae0.'+str(vlan_tag))
        self.assertEqual(vlan.get('vxlan_id'), vxlan_id+2000)


        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(fq_name=pi_list[idx].get_fq_name())

        self.delete_routers(None, pr1)
        self.wait_for_routers_delete(None, pr1.get_fq_name())
        self.delete_routers(None, pr2)
        self.wait_for_routers_delete(None, pr2.get_fq_name())

        self._vnc_lib.bgp_router_delete(fq_name=bgp_router1.get_fq_name())
        self._vnc_lib.bgp_router_delete(fq_name=bgp_router2.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn_obj.get_fq_name())


        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

    def test_lag_config_native_vlan_push(self):
        #create objects
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        jt = self.create_job_template('job-template-lag2')
        fabric = self.create_fabric('test-fabric-vpg-native')
        np, rc = self.create_node_profile('node-profile-lag2',
            device_family='junos-qfx',
            role_mappings=[
                AttrDict(
                    {'physical_role': 'leaf',
                    'rb_roles': ['crb-access']}
                )],
            job_template=jt)

        bgp_router, pr = self.create_router('router3' + self.id(), '2.2.2.2',
            product='qfx5110', family='junos-qfx',
            role='leaf', rb_roles=['crb-access'], fabric=fabric,
            node_profile=np)

        pr.set_physical_router_loopback_ip('20.20.0.1')
        self._vnc_lib.physical_router_update(pr)

        vxlan_id = 5
        vn_obj = self.create_vn(str(vxlan_id), '5.5.5.0')
        port_vlan_tag = 50
        vlan_tag = 0
        vmi, vm, pi_list = self.attach_vmi(str(vxlan_id), ['xe-0/0/1', 'xe-0/0/2'], [pr, pr], vn_obj, None, fabric, None, port_vlan_tag)


        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()


        phy_intf = self.get_phy_interfaces(abstract_config.get('device_abstract_config'), name='ae0')
        self.assertEqual(phy_intf.get('interface_type'), 'lag')

        log_intf = self.get_logical_interface(phy_intf, name='ae0.'+str(vlan_tag))
        self.assertEqual(log_intf.get('vlan_tag'), str(port_vlan_tag))
        self.assertEqual(log_intf.get('unit'), str(vlan_tag))
        self.assertFalse(log_intf.get('is_tagged'))

        link_members = self.get_lag_members(phy_intf)
        self.assertEqual(len(link_members), 2)
        if 'xe-0/0/1' and 'xe-0/0/2' not in link_members:
            self.assertTrue(False)

        name = str(vxlan_id+2000)
        vlan = self.get_vlans(abstract_config.get('device_abstract_config'), name='bd-'+name)
        self.assertEqual(vlan.get('interfaces')[0].get('name'), 'ae0.'+str(vlan_tag))
        self.assertEqual(vlan.get('vxlan_id'), vxlan_id+2000)

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(fq_name=pi_list[idx].get_fq_name())

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())

        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn_obj.get_fq_name())


        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

    def test_mh_config_native_vlan_push(self):
        #create objects
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
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
            node_profile=np)
        pr1.set_physical_router_loopback_ip('30.30.0.1')
        self._vnc_lib.physical_router_update(pr1)

        bgp_router2, pr2 = self.create_router('router5' + self.id(), '4.4.4.4',
            product='qfx5110', family='junos-qfx',
            role='leaf', rb_roles=['crb-access'], fabric=fabric,
            node_profile=np)
        pr2.set_physical_router_loopback_ip('40.40.0.1')
        self._vnc_lib.physical_router_update(pr2)


        vxlan_id = 6
        vn_obj = self.create_vn(str(vxlan_id), '6.6.6.0')
        port_vlan_tag = 60
        vlan_tag = 0
        vmi, vm, pi_list = self.attach_vmi(str(vxlan_id), ['xe-0/0/1', 'xe-0/0/2'], [pr1, pr2], vn_obj, None, fabric, None, port_vlan_tag)

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()


        phy_intf = self.get_phy_interfaces(abstract_config.get('device_abstract_config'), name='ae0')
        self.assertEqual(phy_intf.get('interface_type'), 'lag')
        self.assertIsNotNone(phy_intf.get('ethernet_segment_identifier'))

        log_intf = self.get_logical_interface(phy_intf, name='ae0.'+str(vlan_tag))
        self.assertEqual(log_intf.get('vlan_tag'), str(port_vlan_tag))
        self.assertEqual(log_intf.get('unit'), str(vlan_tag))
        self.assertFalse(log_intf.get('is_tagged'))

        link_members = self.get_lag_members(phy_intf)
        self.assertEqual(len(link_members), 1)
        if abstract_config.get('management_ip') == '3.3.3.3' and 'xe-0/0/1' not in link_members:
            self.assertTrue(False)
        if abstract_config.get('management_ip') == '4.4.4.4' and 'xe-0/0/2' not in link_members:
            self.assertTrue(False)

        name = str(vxlan_id+2000)
        vlan = self.get_vlans(abstract_config.get('device_abstract_config'), name='bd-'+name)
        self.assertEqual(vlan.get('interfaces')[0].get('name'), 'ae0.'+str(vlan_tag))
        self.assertEqual(vlan.get('vxlan_id'), vxlan_id+2000)

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(fq_name=pi_list[idx].get_fq_name())

        self.delete_routers(None, pr1)
        self.wait_for_routers_delete(None, pr1.get_fq_name())
        self.delete_routers(None, pr2)
        self.wait_for_routers_delete(None, pr2.get_fq_name())

        self._vnc_lib.bgp_router_delete(fq_name=bgp_router1.get_fq_name())
        self._vnc_lib.bgp_router_delete(fq_name=bgp_router2.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn_obj.get_fq_name())


        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

    def test_lag_sg_config_push(self):
        #create objects
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
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
            node_profile=np)

        pr.set_physical_router_loopback_ip('20.20.0.1')
        self._vnc_lib.physical_router_update(pr)

        vxlan_id = 3
        vn_obj = self.create_vn(str(vxlan_id), '3.3.3.0')
        sg_obj = self.create_sg('SG1')
        vlan_tag = 100

        vmi, vm, pi_list = self.attach_vmi(str(vxlan_id), ['xe-0/0/1', 'xe-0/0/2'], [pr, pr], vn_obj, sg_obj, fabric, vlan_tag)


        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()


        phy_intf = self.get_phy_interfaces(abstract_config.get('device_abstract_config'), name='ae0')
        self.assertEqual(phy_intf.get('interface_type'), 'lag')

        log_intf = self.get_logical_interface(phy_intf, name='ae0.'+str(vlan_tag))
        self.assertEqual(log_intf.get('vlan_tag'), str(vlan_tag))
        self.assertEqual(log_intf.get('unit'), str(vlan_tag))
        self.assertTrue(log_intf.get('is_tagged'))

        link_members = self.get_lag_members(phy_intf)
        self.assertEqual(len(link_members), 2)
        if 'xe-0/0/1' and 'xe-0/0/2' not in link_members:
            self.assertTrue(False)

        name = str(vxlan_id+2000)
        vlan = self.get_vlans(abstract_config.get('device_abstract_config'), name='bd-'+name)
        self.assertEqual(vlan.get('interfaces')[0].get('name'), 'ae0.'+str(vlan_tag))
        self.assertEqual(vlan.get('vxlan_id'), vxlan_id+2000)

        fw = self.get_firewalls(abstract_config.get('device_abstract_config'))
        if not fw:
            self.assertTrue(False)

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(fq_name=pi_list[idx].get_fq_name())

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())

        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn_obj.get_fq_name())
        self._vnc_lib.security_group_delete(fq_name=sg_obj.get_fq_name())


        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

    def test_lag_sg_feature_based_config_push(self):
        #create objects
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoGRE', 'MPLSoUDP'])
        self.create_features(['overlay-bgp', 'l2-gateway'])
        self.create_physical_roles(['leaf'])
        self.create_overlay_roles(['crb-access'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-access@leaf',
                'physical_role': 'leaf',
                'overlay_role': 'crb-access',
                'features': ['overlay-bgp', 'l2-gateway'],
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

        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1',
            product='qfx5110', family='junos-qfx',
            role='leaf', rb_roles=['crb-access'],
            physical_role=self.physical_roles['leaf'],
            overlay_role=self.overlay_roles['crb-access'], fabric=fabric,
            node_profile=np)
        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)

        vxlan_id = 3
        vn_obj = self.create_vn(str(vxlan_id), '3.3.3.0')
        sg_obj = self.create_sg('SG1')
        port_vlan_tag = 1234
        vlan_tag = 0
        vmi, vm, pi_list = self.attach_vmi(str(vxlan_id), ['xe-0/0/1',
                                                           'xe-0/0/2'], [pr,
                                                                         pr], vn_obj, sg_obj, fabric, None, port_vlan_tag)

        gevent.sleep(1)
        abstract_config = self.check_dm_ansible_config_push()

        phy_intf = self.get_phy_interfaces(abstract_config.get(
            'device_abstract_config').get('features').get('l2-gateway'),
                                           name='ae0')

        log_intf = self.get_logical_interface(phy_intf, name='ae0.'+str(vlan_tag))
        self.assertEqual(log_intf.get('vlan_tag'), str(port_vlan_tag))
        self.assertEqual(log_intf.get('unit'), str(vlan_tag))
        self.assertFalse(log_intf.get('is_tagged'))

        name = str(vxlan_id+2000)
        vlan = self.get_vlans(abstract_config.get(
            'device_abstract_config').get('features').get('l2-gateway'),
                              name='bd-'+name)
        self.assertEqual(vlan.get('interfaces')[0].get('name'), 'ae0.'+str(vlan_tag))
        self.assertEqual(vlan.get('vxlan_id'), vxlan_id+2000)

        fw = self.get_firewalls(abstract_config.get('device_abstract_config'))
        if not fw:
            self.assertTrue(False)

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm.get_fq_name())

        for idx in range(len(pi_list)):
            self._vnc_lib.physical_interface_delete(fq_name=pi_list[idx].get_fq_name())

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())

        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn_obj.get_fq_name())
        self._vnc_lib.security_group_delete(fq_name=sg_obj.get_fq_name())

        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
        
    def test_tagged_and_untagged_vpg_enterprise_style(self):
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        self.create_features(['overlay-bgp', 'l2-gateway'])
        self.create_physical_roles(['leaf'])
        self.create_overlay_roles(['crb-access'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-access@leaf',
                'physical_role': 'leaf',
                'overlay_role': 'crb-access',
                'features': ['overlay-bgp', 'l2-gateway'],
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

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi1.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm1.get_fq_name())
        self._vnc_lib.physical_interface_delete(fq_name=pi1[0].get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi2.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm2.get_fq_name())

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())

        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
    # end test_tagged_and_untagged_vpg_enterprise_style

    def test_tagged_and_untagged_vpg_sp_style(self):
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        self.create_features(['overlay-bgp', 'l2-gateway'])
        self.create_physical_roles(['leaf'])
        self.create_overlay_roles(['crb-access'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-access@leaf',
                'physical_role': 'leaf',
                'overlay_role': 'crb-access',
                'features': ['overlay-bgp', 'l2-gateway'],
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

        vmi1, vm1, pi1 = self.attach_vmi('1', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, 101)
        vmi2, vm2, _ = self.attach_vmi('2', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, None, 102)

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

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi1.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm1.get_fq_name())
        self._vnc_lib.physical_interface_delete(fq_name=pi1[0].get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi2.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm2.get_fq_name())

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())

        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
    # end test_tagged_and_untagged_vpg_sp_style

    def test_untagged_and_tagged_vpg_sp_style(self):
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        self.create_features(['overlay-bgp', 'l2-gateway'])
        self.create_physical_roles(['leaf'])
        self.create_overlay_roles(['crb-access'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-access@leaf',
                'physical_role': 'leaf',
                'overlay_role': 'crb-access',
                'features': ['overlay-bgp', 'l2-gateway'],
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

        vmi1, vm1, pi1 = self.attach_vmi('1', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, None, 101)
        vmi2, vm2, _ = self.attach_vmi('2', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, 102)

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

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi1.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm1.get_fq_name())
        self._vnc_lib.physical_interface_delete(fq_name=pi1[0].get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi2.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm2.get_fq_name())

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())

        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
    # end test_tagged_and_untagged_vpg_sp_style
