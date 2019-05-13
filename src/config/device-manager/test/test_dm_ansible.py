#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
import sys
sys.path.append("../common/tests")

import gevent
import json
from attrdict import AttrDict
from device_manager.device_manager import DeviceManager
from test_common import retries
from test_common import retry_exc_handler
from test_dm_ansible_common import TestAnsibleCommonDM
from test_dm_utils import FakeJobHandler
from vnc_api.vnc_api import *


class TestAnsibleDM(TestAnsibleCommonDM):
    @retries(5, hook=retry_exc_handler)
    def check_dm_ansible_config_push(self):
        job_input = FakeJobHandler.get_job_input()
        self.assertIsNotNone(job_input)
        DeviceManager.get_instance().logger.debug("Job Input: %s" % json.dumps(job_input, indent=4))
    # end check_dm_ansible_config_push

    def test_dm_ansible_config_push(self):
        fabric = self.create_fabric('test_fabric')
        _, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                   role='leaf', ignore_bgp=True, fabric=fabric)
        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)
        self.check_dm_ansible_config_push()
        pr_fq = pr.get_fq_name()
        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr_fq)
    # end test_dm_ansible_config_push

    def test_erb_config_push(self):
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        project = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)

        self.create_features(['underlay_ip_clos', 'overlay_bgp', 'l2_gateway',
                              'l3_gateway', 'vn_interconnect'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['erb-ucast-gateway', 'crb-mcast-gateway'])
        self.create_role_definitions([
            AttrDict({
                'name': 'erb@leaf',
                'physical_role': 'leaf',
                'overlay_role': 'erb-ucast-gateway',
                'features': ['underlay_ip_clos', 'overlay_bgp', 'l2_gateway',
                             'l3_gateway', 'vn_interconnect'],
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

        _, pr = self.create_router('router' + self.id(), '1.1.1.1',
            product='qfx5110', family='junos-qfx',
            role='leaf', rb_roles=['erb-ucast-gateway'],
            physical_role=self.physical_roles['leaf'],
            overlay_role=self.overlay_roles['erb-ucast-gateway'], fabric=fabric)
        pr.set_physical_router_loopback_ip('10.10.0.1')
        self._vnc_lib.physical_router_update(pr)

        vmi1, vm1, pi1 = self.attach_vmi('1', 'xe-0/0/1', pr, vn1_obj, fabric)
        vmi2, vm2, pi2 = self.attach_vmi('2', 'xe-0/0/2', pr, vn2_obj, fabric)

        lr_fq_name = ['default-domain', 'default-project', 'lr-' + self.id()]
        lr = LogicalRouter(fq_name=lr_fq_name, parent_type='project',
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

        gevent.sleep(1)
        self.check_dm_ansible_config_push()

        self._vnc_lib.logical_router_delete(fq_name=lr.get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi1.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm1.get_fq_name())
        self._vnc_lib.physical_interface_delete(fq_name=pi1.get_fq_name())

        self._vnc_lib.virtual_machine_interface_delete(fq_name=vmi2.get_fq_name())
        self._vnc_lib.virtual_machine_delete(fq_name=vm2.get_fq_name())
        self._vnc_lib.physical_interface_delete(fq_name=pi2.get_fq_name())

        self.delete_routers(None, pr)
        self.wait_for_routers_delete(None, pr.get_fq_name())
        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()
    # end test_erb_config_push

    def test_ar_config_push(self):
        self.create_features(['underlay_ip_clos', 'overlay_bgp', 'l2_gateway',
                              'l3_gateway', 'vn_interconnect', 'assisted_replicator'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['ar-client', 'ar-replicator'])
        self.create_role_definitions([
            AttrDict({
                'name': 'ar@spine',
                'physical_role': 'spine',
                'overlay_role': 'ar-replicator',
                'features': ['underlay_ip_clos', 'overlay_bgp', 'l2_gateway',
                             'l3_gateway', 'vn_interconnect', 'assisted_replicator'],
                # 'feature_configs': {'assisted_replicator': {'replicator-activation-delay': '30'}}
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

        _, pr = self.create_router('router' + self.id(), '1.1.1.1',
                                   product='qfx10008', family='junos-qfx',
                                   role='spine', rb_roles=['ar-replicator'],
                                   physical_role=self.physical_roles['spine'],
                                   overlay_role=self.overlay_roles[
                                       'ar-replicator'], fabric=fabric)
        pr.set_physical_router_loopback_ip('10.10.0.1')
        pr.set_physical_router_replicator_loopback_ip('10.10.0.2')
        self._vnc_lib.physical_router_update(pr)

        gevent.sleep(1)
        self.check_dm_ansible_config_push()
    # end test_ar_config_push
# end TestAnsibleDM
