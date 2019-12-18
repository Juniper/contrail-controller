#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from __future__ import absolute_import
import gevent
from unittest import skip
from attrdict import AttrDict
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from vnc_api.vnc_api import *
from .test_dm_ansible_common import TestAnsibleCommonDM
from .test_dm_utils import FakeJobHandler


class TestAnsibleDM(TestAnsibleCommonDM):

    @retries(5, hook=retry_exc_handler)
    def check_li(self):
        ac = FakeJobHandler.get_job_input()
        self.assertIsNotNone(ac)
        fc = ac.get('device_abstract_config').get('features').get('l2-gateway')
        pi_name = 'xe-0/0/1'
        li_name = pi_name + '.101'
        pi = self.get_phy_interfaces(fc, name=pi_name)
        li = self.get_logical_interface(pi, name=li_name)

        self.assertEqual(li.get('vlan_tag'), '101')
        self.assertTrue(li.get('is_tagged'))

        pi_name = 'xe-0/0/2'
        li_name = pi_name + '.0'
        pi = self.get_phy_interfaces(fc, name=pi_name)
        li = self.get_logical_interface(pi, name=li_name)

        self.assertEqual(li.get('vlan_tag'), '102')
        self.assertFalse(li.get('is_tagged'))

    def test_dm_ansible_config_push(self):
        jt = self.create_job_template('job-template-1')
        fabric = self.create_fabric('test_fabric')
        np, rc = self.create_node_profile('node-profile-1',
            device_family='junos-qfx', job_template=jt)
        _, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                   role='leaf', ignore_bgp=True, fabric=fabric,
                                   node_profile=np)
        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_ansible_config_push()
        pr_fq = pr.get_fq_name()
        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr_fq)

        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())
    # end test_dm_ansible_config_push

    @skip("Timing failures")
    def test_erb_config_push(self):
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        project = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)

        self.create_features(['underlay-ip-clos', 'overlay-bgp', 'l2-gateway',
                              'l3-gateway', 'vn-interconnect'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['erb-ucast-gateway', 'crb-mcast-gateway'])
        self.create_role_definitions([
            AttrDict({
                'name': 'erb@leaf',
                'physical_role': 'leaf',
                'overlay_role': 'erb-ucast-gateway',
                'features': ['underlay-ip-clos', 'overlay-bgp', 'l2-gateway',
                             'l3-gateway', 'vn-interconnect'],
                'feature_configs': {'l3_gateway': {'use_gateway_ip': 'True'}}
            })
        ])

        jt = self.create_job_template('job-template-1')
        fabric = self.create_fabric('test-fabric')
        np, rc = self.create_node_profile('node-profile-1',
            device_family='junos-qfx',
            role_mappings=[
                AttrDict(
                    {'physical_role': 'leaf',
                    'rb_roles': ['erb-ucast-gateway']}
                )],
            job_template=jt)

        vn1_obj = self.create_vn('1', '1.1.1.0')
        vn2_obj = self.create_vn('2', '2.2.2.0')

        bgp_router, pr = self.create_router('router' + self.id(), '1.1.1.1',
            product='qfx5110', family='junos-qfx',
            role='leaf', rb_roles=['erb-ucast-gateway'],
            physical_role=self.physical_roles['leaf'],
            overlay_role=self.overlay_roles['erb-ucast-gateway'], fabric=fabric,
            node_profile=np)
        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)

        vmi1, vm1, pi1 = self.attach_vmi('1', ['xe-0/0/1'], [pr], vn1_obj, None, fabric, 101)
        vmi2, vm2, pi2 = self.attach_vmi('2', ['xe-0/0/2'], [pr], vn2_obj, None, fabric, None, 102)

        lr_fq_name = ['default-domain', 'default-project', 'lr-' + self.id()]
        lr = LogicalRouter(fq_name=lr_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing',
                           vxlan_network_identifier='3000')
        lr.set_physical_router(pr)

        fq_name = ['default-domain', 'default-project', 'vmi3-' + self.id()]
        vmi3 = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        vmi3.set_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi3)

        fq_name = ['default-domain', 'default-project', 'vmi4-' + self.id()]
        vmi4 = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        vmi4.set_virtual_network(vn2_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi4)

        lr.add_virtual_machine_interface(vmi3)
        lr.add_virtual_machine_interface(vmi4)

        lr_uuid = self._vnc_lib.logical_router_create(lr)
        lr = self._vnc_lib.logical_router_read(id=lr_uuid)

        self.check_li()

        self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi3.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi4.get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi1.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm1.get_fq_name())
        self._vnc_lib.physical_interface_delete(fq_name=pi1[0].get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi2.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm2.get_fq_name())
        self._vnc_lib.physical_interface_delete(fq_name=pi2[0].get_fq_name())


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
    # end test_erb_config_push


    def test_ar_config_push(self):
        self.create_features(['underlay-ip-clos', 'overlay-bgp', 'l2-gateway',
                              'l3-gateway', 'vn-interconnect', 'assisted-replicator'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['ar-client', 'ar-replicator'])
        self.create_role_definitions([
            AttrDict({
                'name': 'ar-replicator-spine',
                'physical_role': 'spine',
                'overlay_role': 'ar-replicator',
                'features': ['assisted-replicator'],
                'feature_configs': None
            })
        ])


        jt = self.create_job_template('job-template-2')
        fabric = self.create_fabric('fabric-ar')
        np, rc = self.create_node_profile('node-profile-ar',
                                          device_family='junos-qfx',
                                          role_mappings=[
                                              AttrDict(
                                                  {'physical_role': 'spine',
                                                   'rb_roles': [
                                                       'ar-replicator']}
                                              )],
                                          job_template=jt)

        _, pr = self.create_router('router' + self.id(), '2.2.2.2',
                                   product='qfx10008', family='junos-qfx',
                                   role='spine', rb_roles=['ar-replicator'],
                                   physical_role=self.physical_roles['spine'],
                                   overlay_role=self.overlay_roles[
                                       'ar-replicator'], fabric=fabric,
                                   node_profile=np)

        pr.set_physical_router_loopback_ip('20.20.0.1')
        pr.set_physical_router_replicator_loopback_ip('20.20.0.2')
        self._vnc_lib.physical_router_update(pr)

        gevent.sleep(1)
        self.check_dm_ansible_config_push()

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())

        self._vnc_lib.role_config_delete(fq_name=rc.get_fq_name())
        self._vnc_lib.node_profile_delete(fq_name=np.get_fq_name())
        self._vnc_lib.fabric_delete(fq_name=fabric.get_fq_name())
        self._vnc_lib.job_template_delete(fq_name=jt.get_fq_name())

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
    # end test_ar_config_push
# end TestAnsibleDM
