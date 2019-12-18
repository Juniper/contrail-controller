#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
import mock
from attrdict import AttrDict
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from vnc_api.vnc_api import *
from .test_dm_ansible_common import TestAnsibleCommonDM
from .test_dm_utils import FakeJobHandler


class TestAnsibleRRRedundancy(TestAnsibleCommonDM):

    def setUp(self, extra_config_knobs=None):
        super(TestAnsibleRRRedundancy, self).setUp(extra_config_knobs=extra_config_knobs)
        self.create_feature_objects_and_params()
        self.idle_patch = mock.patch('gevent.idle')
        self.idle_mock = self.idle_patch.start()

    def tearDown(self):
        self.delete_objects()
        self.idle_patch.stop()
        super(TestAnsibleRRRedundancy, self).tearDown()

    @retries(5, hook=retry_exc_handler)
    def check_pr(self, pr, peer_a, peer_b, check_feature=False):
        abstract_config = FakeJobHandler.get_dev_job_input(pr.name)
        self.assertIsNotNone(abstract_config)
        device_abstract_config = abstract_config.get('device_abstract_config')

        if check_feature:
            overlay_bgp_feature = device_abstract_config.get('features', {}).get(
                'overlay-bgp')
            self.assertIsNotNone(overlay_bgp_feature)

        bgp_grps = device_abstract_config.get('bgp')
        self.assertEqual(len(bgp_grps), 1)

        bgp_grp = bgp_grps[0]
        self.assertEqual(bgp_grp.get('name'), '_contrail_asn-64512')
        self.assertEqual(bgp_grp.get('ip_address'),
                         pr.get_physical_router_management_ip())
        peers = bgp_grp.get('peers')
        self.assertEqual(len(peers), 2)

        for peer in peers:
            self.assertIn(peer.get('ip_address'),
                          [peer_a.get_physical_router_management_ip(),
                           peer_b.get_physical_router_management_ip()])

    @retries(5, hook=retry_exc_handler)
    def check_pr_aux(self, pr, peer_a, peer_b):
        abstract_config = FakeJobHandler.get_dev_job_input(pr.name)
        self.assertIsNotNone(abstract_config)
        device_abstract_config = abstract_config.get('device_abstract_config')
        bgp_grps = device_abstract_config.get('bgp')
        self.assertEqual(len(bgp_grps), 2)

        for bgp_grp in bgp_grps:
            self.assertEqual(bgp_grp.get('ip_address'),
                             pr.get_physical_router_management_ip())
            peers = bgp_grp.get('peers')
            self.assertEqual(len(peers), 1)
            peer = peers[0]
            peer_ip = peer.get('ip_address')
            self.assertIn(peer_ip,
                          [peer_a.get_physical_router_management_ip(),
                           peer_b.get_physical_router_management_ip()])
            if peer_ip == peer_a.get_physical_router_management_ip():
                self.assertEqual(bgp_grp.get('name'), '_contrail_asn-64512')
            else:
                self.assertEqual(bgp_grp.get('name'), '_contrail_asn-64512-rr')


    def test_01_2_leaf_1_spine(self):

        pr1, pr2, pr3 = self.create_rr_dependencies(
            'leaf', 'leaf', 'CRB-Access', 'ERB-UCAST-Gateway'
        )

        # update each PR separately to get the abstract config
        # corresponding to that PR
        pr1.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr1)
        self.check_pr(pr1, pr2, pr3)

        pr2.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr2)
        self.check_pr(pr2, pr1, pr3, check_feature=True)

        pr3.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr3)
        self.check_pr(pr3, pr1, pr2)

    def test_02_1_leaf_2_spine_old_way(self):

        pr1, pr2, pr3 = self.create_rr_dependencies(
            'leaf', 'spine', 'CRB-Access', 'Route-Reflector'
        )

        # update each PR separately to get the abstract config
        # corresponding to that PR
        pr1.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr1)
        self.check_pr(pr1, pr2, pr3)

        pr2.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr2)
        self.check_pr_aux(pr2, pr1, pr3)

        pr3.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr3)
        self.check_pr_aux(pr3, pr1, pr2)

    def test_03_1_leaf_2_spine_new_way(self):

        pr1, pr2, pr3 = self.create_rr_dependencies(
            'leaf', 'spine', 'ERB-UCAST-Gateway', 'Route-Reflector'
        )

        # update each PR separately to get the abstract config
        # corresponding to that PR
        pr1.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr1)
        self.check_pr(pr1, pr2, pr3, check_feature=True)

        pr2.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr2)
        self.check_pr_aux(pr2, pr1, pr3)

        pr3.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr3)
        self.check_pr_aux(pr3, pr1, pr2)

    def test_04_leaf_rr(self):

        pr1, pr2, pr3 = self.create_rr_dependencies(
            'leaf', 'leaf', 'ERB-UCAST-Gateway', 'Route-Reflector'
        )

        # update each PR separately to get the abstract config
        # corresponding to that PR
        pr1.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr1)
        self.check_pr(pr1, pr2, pr3, check_feature=True)

        pr2.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr2)
        self.check_pr_aux(pr2, pr1, pr3)

        pr3.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr3)
        self.check_pr_aux(pr3, pr1, pr2)

    def test_05_1_leaf_2_spine_revert(self):

        pr1, pr2, pr3 = self.create_rr_dependencies(
            'leaf', 'spine', 'CRB-Access', 'Route-Reflector'
        )

        # update each PR separately to get the abstract config
        # corresponding to that PR
        pr1.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr1)
        self.check_pr(pr1, pr2, pr3)

        pr2.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr2)
        self.check_pr_aux(pr2, pr1, pr3)

        pr3.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr3)
        self.check_pr_aux(pr3, pr1, pr2)

        # now update this to a 1 RR, 2 non RR setup and check to see
        # if rr specific grouping is deleted.
        pr2.set_physical_router_role('leaf')
        pr2.set_routing_bridging_roles(RoutingBridgingRolesType(rb_roles=['CRB-Access']))
        self._vnc_lib.physical_router_update(pr2)

        pr1.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr1)
        self.check_pr(pr1, pr2, pr3)

        pr2.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr2)
        self.check_pr(pr2, pr1, pr3)

        pr3.set_physical_router_product_name('juniper-qfx5110-32q')
        self._vnc_lib.physical_router_update(pr3)
        self.check_pr(pr3, pr1, pr2)

    def create_rr_dependencies(self, phy_role1, phy_role2,
                               rb_role1, rb_role2):

        jt = self.create_job_template('job-template-sf' + self.id())

        fabric = self.create_fabric('fab-sf' + self.id())

        np, rc = self.create_node_profile('node-profile-sf' + self.id(),
                                          device_family='junos-qfx',
                                          role_mappings=[
                                              AttrDict(
                                                  {
                                                      'physical_role': 'leaf',
                                                      'rb_roles': ['CRB-Access', 'ERB-UCAST-Gateway']
                                                  }
                                              ),
                                              AttrDict(
                                                  {
                                                      'physical_role': 'spine',
                                                      'rb_roles': ['Route-Reflector']
                                                  }
                                              )
                                          ],
                                          job_template=jt)

        bgp_router1, pr1 = self.create_router('device-1' + self.id(),
                                              '3.3.3.3',
                                              product='qfx5110-48s-4c', family='junos-qfx',
                                              role=phy_role1, rb_roles=[rb_role1],
                                              physical_role=self.physical_roles[phy_role1],
                                              overlay_role=self.overlay_roles[
                                                  rb_role1.lower()], fabric=fabric,
                                              node_profile=np)
        pr1.set_physical_router_loopback_ip('30.30.0.1')
        self._vnc_lib.physical_router_update(pr1)

        bgp_router2, pr2 = self.create_router('device-2' + self.id(),
                                              '3.3.3.4',
                                              product='qfx5110-48s-4c', family='junos-qfx',
                                              role=phy_role2, rb_roles=[rb_role2],
                                              physical_role=self.physical_roles[phy_role2],
                                              overlay_role=self.overlay_roles[
                                                  rb_role2.lower()], fabric=fabric,
                                              node_profile=np)
        pr2.set_physical_router_loopback_ip('30.30.0.2')
        self._vnc_lib.physical_router_update(pr2)

        bgp_router3, pr3 = self.create_router('device-3' + self.id(),
                                              '3.3.3.5',
                                              product='qfx5110-48s-4c', family='junos-qfx',
                                              role='spine', rb_roles=['Route-Reflector'],
                                              physical_role=self.physical_roles['spine'],
                                              overlay_role=None, fabric=fabric,
                                              node_profile=np)
        pr3.set_physical_router_loopback_ip('30.30.0.3')
        self._vnc_lib.physical_router_update(pr3)

        return pr1, pr2, pr3

    def create_feature_objects_and_params(self):
        self.create_features(['overlay-bgp'])
        self.create_physical_roles(['leaf', 'spine'])
        self.create_overlay_roles(['erb-ucast-gateway', 'route-reflector', 'crb-access'])
        self.create_role_definitions([
            AttrDict({
                'name': 'overlay-bgp-role',
                'physical_role': 'leaf',
                'overlay_role': 'erb-ucast-gateway',
                'features': ['overlay-bgp'],
                'feature_configs': None
            })
        ])

    def delete_objects(self):

        pr_list = self._vnc_lib.physical_routers_list().get('physical-routers')
        for pr in pr_list:
            self._vnc_lib.physical_router_delete(id=pr['uuid'])

        bgp_router_list = self._vnc_lib.bgp_routers_list().get('bgp-routers')
        for bgp_router in bgp_router_list:
            self._vnc_lib.bgp_router_delete(id=bgp_router['uuid'])

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

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()

