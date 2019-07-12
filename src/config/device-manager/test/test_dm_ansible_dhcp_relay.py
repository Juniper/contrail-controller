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
from vnc_api.gen.resource_client import *


class TestAnsibleDhcpRelayDM(TestAnsibleCommonDM):

    def test_dhcp_relay_config_push(self):
        self.set_encapsulation_priorities(['VXLAN', 'MPLSoUDP'])
        project = self._vnc_lib.project_read(fq_name=['default-domain', 'default-project'])
        project.set_vxlan_routing(True)
        self._vnc_lib.project_update(project)

        self.create_features(['underlay-ip-clos', 'overlay-bgp', 'l2-gateway',
                              'l3-gateway', 'vn-interconnect', 'dhcp-relay'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['crb-gateway'])
        self.create_role_definitions([
            AttrDict({
                'name': 'crb-gateway-spine',
                'physical_role': 'spine',
                'overlay_role': 'crb-gateway',
                'features': ['vn-interconnect', 'dhcp-relay'],
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

        _, pr = self.create_router('router' + self.id(), '1.1.1.1',
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
                           vxlan_network_identifier='3000')
        ip = IpAddressesType(["1.1.1.30, 1.1.1.20"])
        lr.set_logical_router_dhcp_relay_server(ip)
        lr.set_logical_router_type('vxlan-routing')
        lr.set_physical_router(pr)
        lr.add_virtual_network(vn1_obj)
        lr.add_virtual_network(vn2_obj)

        lr_uuid = self._vnc_lib.logical_router_create(lr)
        lr = self._vnc_lib.logical_router_read(id=lr_uuid)

        gevent.sleep(1)
        self.check_dm_ansible_config_push()
    # end test_dhcp_relay_config_push

# end TestAnsibleDhcpRelayDM