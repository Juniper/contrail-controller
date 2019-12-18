#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from __future__ import absolute_import
import mock
from attrdict import AttrDict
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from vnc_api.vnc_api import *
from vnc_api.gen.resource_client import *
from .test_dm_ansible_common import TestAnsibleCommonDM
from .test_dm_utils import FakeJobHandler


class TestAnsibleDhcpRelayDM(TestAnsibleCommonDM):

    def setUp(self, extra_config_knobs=None):
        super(TestAnsibleDhcpRelayDM, self).setUp(extra_config_knobs=extra_config_knobs)
        self.idle_patch = mock.patch('gevent.idle')
        self.idle_mock = self.idle_patch.start()

    def tearDown(self):
        self.idle_patch.stop()
        super(TestAnsibleDhcpRelayDM, self).tearDown()

    @retries(5, hook=retry_exc_handler)
    def check_dhcp_info(self, lr_uuid, check_rib=True, in_network=True,
                        num_server_ip=1,
                        dhcp_server_addr='1.1.1.30',
                        dhcp_server_addr_2='2.2.2.30'):

        abs = FakeJobHandler.get_job_input()
        self.assertIsNotNone(abs)
        dhcp_relay_abs = abs.get('device_abstract_config').get('features').get(
            'vn-interconnect')
        dhcp_info_list = dhcp_relay_abs.get('routing_instances')[0].get(
            'forwarding_options').get('dhcp_relay')
        if check_rib:
            self.assertIsNotNone(dhcp_relay_abs.get('routing_instances')[0].\
                                 get('rib_group'))

        self.assertEqual(len(dhcp_info_list), 1)
        dhcp = dhcp_info_list[0]
        if in_network:
            self.assertTrue(dhcp.get('in_network'))
        else:
            self.assertFalse(dhcp.get('in_network'))
        self.assertEqual(len(dhcp.get('dhcp_server_ips')), num_server_ip)
        if num_server_ip == 1:
            self.assertEqual(dhcp.get('dhcp_server_ips')[0].get('address'),
                            dhcp_server_addr)
        else:
            self.assertTrue(any(dhcp['address'] ==
                                dhcp_server_addr for dhcp in dhcp.get(
                'dhcp_server_ips')))
            self.assertTrue(any(dhcp['address'] ==
                                dhcp_server_addr_2 for dhcp in dhcp.get(
                'dhcp_server_ips')))
        self.assertEqual(dhcp.get('dhcp_relay_group'),
                         'DHCP_RELAY_GRP_' + lr_uuid)

    @retries(5, hook=retry_exc_handler)
    def check_dhcp_info_aux(self, lr_uuid):
        abs = FakeJobHandler.get_job_input()
        self.assertIsNotNone(abs)
        dhcp_relay_abs = abs.get('device_abstract_config').get('features').get(
            'vn-interconnect')
        self.assertEqual(len(dhcp_relay_abs.get('routing_instances')), 2)

        for ri in dhcp_relay_abs.get('routing_instances'):
            if ri.get('vxlan_id') == 2001:
                self.assertIsNotNone(ri.get('forwarding_options'))

                dhcp_info_list = ri.get('forwarding_options').get('dhcp_relay')

                self.assertEqual(len(dhcp_info_list), 1)
                dhcp = dhcp_info_list[0]
                self.assertTrue(dhcp.get('in_network'))
                self.assertEqual(len(dhcp.get('dhcp_server_ips')), 1)
                self.assertEqual(dhcp.get('dhcp_server_ips')[0].get('address'),
                                 '20.20.20.30')
                self.assertEqual(dhcp.get('dhcp_relay_group'),
                                 'DHCP_RELAY_GRP_' + lr_uuid)
            if ri.get('vxlan_id') == 2020:
                self.assertIsNone(ri.get('forwarding_options'))

    def test_dhcp_relay_config_push_one_vn_in_network(self):
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        project = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)

        self.create_features(['underlay-ip-clos', 'overlay-bgp', 'l2-gateway',
                              'l3-gateway', 'vn-interconnect'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['crb-gateway'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-gateway-spine',
                'physical_role': 'spine',
                'overlay_role': 'crb-gateway',
                'features': ['l3-gateway', 'vn-interconnect'],
                'feature_configs': None
            })
        ])

        jt = self.create_job_template('job-template-1')
        fabric = self.create_fabric('fabric-dhcp-relay')
        np, rc = self.create_node_profile('node-profile-1',
                                          device_family='junos-qfx',
                                          role_mappings=[
                                              AttrDict(
                                                  {'physical_role': 'spine',
                                                   'rb_roles': [
                                                       'crb-gateway']}
                                              )],
                                          job_template=jt)

        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                   product='qfx10008', family='junos-qfx',
                                   role='spine', rb_roles=['crb-gateway'],
                                   physical_role=self.physical_roles['spine'],
                                   overlay_role=self.overlay_roles[
                                       'crb-gateway'], fabric=fabric,
                                   node_profile=np)

        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)

        vn_obj = self.create_vn('1', '1.1.1.0')

        lr_fq_name = ['default-domain', 'default-project', 'lr-' + self.id()]
        lr = LogicalRouter(fq_name=lr_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing',
                           vxlan_network_identifier='3000')
        ip = IpAddressesType(["1.1.1.30"])
        lr.set_logical_router_dhcp_relay_server(ip)
        lr.set_physical_router(pr)

        fq_name = ['default-domain', 'default-project', 'vmi-' + self.id()]
        vmi = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        vmi.set_virtual_network(vn_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)
        lr.add_virtual_machine_interface(vmi)

        lr_uuid = self._vnc_lib.logical_router_create(lr)

        self.check_dhcp_info(lr_uuid, check_rib=False)

        #deletes
        self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi.get_fq_name())
        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn_obj.get_fq_name())

        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
    # end test_dhcp_relay_config_push_one_vn_in_network


    def test_dhcp_relay_config_push_two_vns_in_network(self):
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        project = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)

        self.create_features(['underlay-ip-clos', 'overlay-bgp', 'l2-gateway',
                              'l3-gateway', 'vn-interconnect'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['crb-gateway'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-gateway-spine',
                'physical_role': 'spine',
                'overlay_role': 'crb-gateway',
                'features': ['l3-gateway', 'vn-interconnect'],
                'feature_configs': None
            })
        ])

        jt = self.create_job_template('job-template-1')
        fabric = self.create_fabric('fabric-dhcp-relay')
        np, rc = self.create_node_profile('node-profile-1',
                                          device_family='junos-qfx',
                                          role_mappings=[
                                              AttrDict(
                                                  {'physical_role': 'spine',
                                                   'rb_roles': [
                                                       'crb-gateway']}
                                              )],
                                          job_template=jt)

        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                   product='qfx10008', family='junos-qfx',
                                   role='spine', rb_roles=['crb-gateway'],
                                   physical_role=self.physical_roles['spine'],
                                   overlay_role=self.overlay_roles[
                                       'crb-gateway'], fabric=fabric,
                                   node_profile=np)

        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)

        vn1_obj = self.create_vn('1', '1.1.1.0')
        vn2_obj = self.create_vn('2', '2.2.2.0')

        lr_fq_name = ['default-domain', 'default-project', 'lr-' + self.id()]
        lr = LogicalRouter(fq_name=lr_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing',
                           vxlan_network_identifier='3000')
        ip = IpAddressesType(["1.1.1.30"])
        lr.set_logical_router_dhcp_relay_server(ip)
        lr.set_physical_router(pr)

        fq_name = ['default-domain', 'default-project', 'vmi1-' + self.id()]
        vmi1 = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        vmi1.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi1)
        lr.add_virtual_machine_interface(vmi1)

        fq_name = ['default-domain', 'default-project', 'vmi2-' + self.id()]
        vmi2 = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        vmi2.set_virtual_network(vn2_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi2)
        lr.add_virtual_machine_interface(vmi2)

        lr_uuid = self._vnc_lib.logical_router_create(lr)

        self.check_dhcp_info(lr_uuid)

        #deletes
        self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(
            fq_name=vmi1.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(
            fq_name=vmi2.get_fq_name())

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
    # end test_dhcp_relay_config_push_two_vns_in_network

    def test_dhcp_relay_config_push_one_vn_inet0(self):
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        project = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)

        self.create_features(['underlay-ip-clos', 'overlay-bgp', 'l2-gateway',
                              'l3-gateway', 'vn-interconnect'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['crb-gateway'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-gateway-spine',
                'physical_role': 'spine',
                'overlay_role': 'crb-gateway',
                'features': ['l3-gateway', 'vn-interconnect'],
                'feature_configs': None
            })
        ])

        jt = self.create_job_template('job-template-1')
        fabric = self.create_fabric('fabric-dhcp-relay')
        np, rc = self.create_node_profile('node-profile-1',
                                          device_family='junos-qfx',
                                          role_mappings=[
                                              AttrDict(
                                                  {'physical_role': 'spine',
                                                   'rb_roles': [
                                                       'crb-gateway']}
                                              )],
                                          job_template=jt)

        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                   product='qfx10008', family='junos-qfx',
                                   role='spine', rb_roles=['crb-gateway'],
                                   physical_role=self.physical_roles['spine'],
                                   overlay_role=self.overlay_roles[
                                       'crb-gateway'], fabric=fabric,
                                   node_profile=np)

        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)

        vn_obj = self.create_vn('1', '1.1.1.0')

        lr_fq_name = ['default-domain', 'default-project', 'lr-' + self.id()]
        lr = LogicalRouter(fq_name=lr_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing',
                           vxlan_network_identifier='3000')
        ip = IpAddressesType(["30.1.1.1"])
        lr.set_logical_router_dhcp_relay_server(ip)
        lr.set_physical_router(pr)

        fq_name = ['default-domain', 'default-project', 'vmi-' + self.id()]
        vmi = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        vmi.set_virtual_network(vn_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)
        lr.add_virtual_machine_interface(vmi)

        lr_uuid = self._vnc_lib.logical_router_create(lr)

        self.check_dhcp_info(lr_uuid, dhcp_server_addr='30.1.1.1',
                             in_network=False)

        #deletes
        self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi.get_fq_name())
        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn_obj.get_fq_name())

        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
    # end test_dhcp_relay_config_push_one_vn_inet0

    def test_dhcp_relay_config_push_one_vn_two_dhcp_servers(self):
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        project = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)

        self.create_features(['underlay-ip-clos', 'overlay-bgp', 'l2-gateway',
                              'l3-gateway', 'vn-interconnect'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['crb-gateway'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-gateway-spine',
                'physical_role': 'spine',
                'overlay_role': 'crb-gateway',
                'features': ['l3-gateway', 'vn-interconnect'],
                'feature_configs': None
            })
        ])

        jt = self.create_job_template('job-template-1')
        fabric = self.create_fabric('fabric-dhcp-relay')
        np, rc = self.create_node_profile('node-profile-1',
                                          device_family='junos-qfx',
                                          role_mappings=[
                                              AttrDict(
                                                  {'physical_role': 'spine',
                                                   'rb_roles': [
                                                       'crb-gateway']}
                                              )],
                                          job_template=jt)

        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                   product='qfx10008', family='junos-qfx',
                                   role='spine', rb_roles=['crb-gateway'],
                                   physical_role=self.physical_roles['spine'],
                                   overlay_role=self.overlay_roles[
                                       'crb-gateway'], fabric=fabric,
                                   node_profile=np)

        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)

        vn_obj = self.create_vn('1', '1.1.1.0')

        lr_fq_name = ['default-domain', 'default-project', 'lr-' + self.id()]
        lr = LogicalRouter(fq_name=lr_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing',
                           vxlan_network_identifier='3000')
        ip = IpAddressesType(["1.1.1.30", "2.2.2.30"])
        lr.set_logical_router_dhcp_relay_server(ip)
        lr.set_physical_router(pr)

        fq_name = ['default-domain', 'default-project', 'vmi-' + self.id()]
        vmi = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        vmi.set_virtual_network(vn_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)
        lr.add_virtual_machine_interface(vmi)

        lr_uuid = self._vnc_lib.logical_router_create(lr)

        self.check_dhcp_info(lr_uuid, num_server_ip=2)

        #deletes
        self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi.get_fq_name())
        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn_obj.get_fq_name())

        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
    # end test_dhcp_relay_config_push_one_vn_two_dhcp_servers

    def test_dhcp_relay_config_push_two_lrs_with_dhcp_in_one(self):
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        project = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)

        self.create_features(['underlay-ip-clos', 'overlay-bgp', 'l2-gateway',
                              'l3-gateway', 'vn-interconnect'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['crb-gateway'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-gateway-spine',
                'physical_role': 'spine',
                'overlay_role': 'crb-gateway',
                'features': ['l3-gateway', 'vn-interconnect'],
                'feature_configs': None
            })
        ])

        jt = self.create_job_template('job-template-1')
        fabric = self.create_fabric('fabric-dhcp-relay')
        np, rc = self.create_node_profile('node-profile-1',
                                          device_family='junos-qfx',
                                          role_mappings=[
                                              AttrDict(
                                                  {'physical_role': 'spine',
                                                   'rb_roles': [
                                                       'crb-gateway']}
                                              )],
                                          job_template=jt)

        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                   product='qfx10008', family='junos-qfx',
                                   role='spine', rb_roles=['crb-gateway'],
                                   physical_role=self.physical_roles['spine'],
                                   overlay_role=self.overlay_roles[
                                       'crb-gateway'], fabric=fabric,
                                   node_profile=np)

        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)

        vn_obj = self.create_vn('1', '1.1.1.0')

        lr_fq_name = ['default-domain', 'default-project', 'lr-' + self.id()]
        lr = LogicalRouter(fq_name=lr_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing',
                           vxlan_network_identifier='3000')
        ip = IpAddressesType(["20.20.20.30"])
        lr.set_logical_router_dhcp_relay_server(ip)
        lr.set_physical_router(pr)

        fq_name = ['default-domain', 'default-project', 'vmi-' + self.id()]
        vmi = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        vmi.set_virtual_network(vn_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)
        lr.add_virtual_machine_interface(vmi)

        lr_uuid = self._vnc_lib.logical_router_create(lr)


        vn2_obj = self.create_vn('20', '20.20.20.0')

        lr2_fq_name = ['default-domain', 'default-project', 'lr2-' + self.id()]
        lr2 = LogicalRouter(fq_name=lr2_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing',
                           vxlan_network_identifier='4000')
        lr2.set_physical_router(pr)

        fq_name = ['default-domain', 'default-project', 'vmi2-' + self.id()]
        vmi2 = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        vmi2.set_virtual_network(vn2_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi2)
        lr2.add_virtual_machine_interface(vmi2)

        lr2_uuid = self._vnc_lib.logical_router_create(lr2)

        self.check_dhcp_info_aux(lr_uuid)

        #deletes
        self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())
        self._vnc_lib.logical_router_delete(fq_name=lr2.get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(
            fq_name=vmi2.get_fq_name())

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())
        self._vnc_lib.bgp_router_delete(fq_name=bgp_router.get_fq_name())

        self._vnc_lib.virtual_network_delete(fq_name=vn_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())


        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
    # end test_dhcp_relay_config_push_two_lrs_with_dhcp_in_one
# end TestAnsibleDhcpRelayDM