#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from __future__ import absolute_import
import gevent
import json
import mock
from attrdict import AttrDict
from device_manager.device_manager import DeviceManager
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from .test_dm_ansible_common import TestAnsibleCommonDM
from .test_dm_utils import FakeJobHandler
from vnc_api.vnc_api import *
from vnc_api.gen.resource_client import *


class TestAnsibleRoutedVNDM(TestAnsibleCommonDM):

    def setUp(self, extra_config_knobs=None):
        super(TestAnsibleRoutedVNDM, self).setUp(
            extra_config_knobs=extra_config_knobs)
        self.idle_patch = mock.patch('gevent.idle')
        self.idle_mock = self.idle_patch.start()

    def tearDown(self):
        self.idle_patch.stop()
        super(TestAnsibleRoutedVNDM, self).tearDown()

    def delete_objects(self):
        for obj in self.physical_routers:
            self._vnc_lib.physical_router_delete(id=obj.get_uuid())
        for obj in self.bgp_routers:
            self._vnc_lib.bgp_router_delete(id=obj.get_uuid())
        for obj in self.role_configs:
            self._vnc_lib.role_config_delete(id=obj.get_uuid())
        for obj in self.node_profiles:
            self._vnc_lib.node_profile_delete(id=obj.get_uuid())
        for obj in self.fabrics:
            self._vnc_lib.fabric_delete(id=obj.get_uuid())
        for obj in self.job_templates:
            self._vnc_lib.job_template_delete(id=obj.get_uuid())

        self.delete_role_definitions()
        self.delete_features()
        self.delete_overlay_roles()
        self.delete_physical_roles()
    # end delete_objects

    def create_fabrics_two_pr(self, name, two_fabric=False):
        self.features = {}
        self.role_definitions = {}
        self.feature_configs = []

        self.job_templates = []
        self.fabrics = []
        self.bgp_routers = []
        self.node_profiles = []
        self.role_configs = []
        self.physical_routers = []
        self.bgp_routers = []

        self.create_physical_roles(['spine'])
        self.create_overlay_roles(['crb-gateway'])

        jt = self.create_job_template('job-template-' + name + self.id())
        self.job_templates.append(jt)

        fabric = self.create_fabric('fab-' + name + self.id())
        self.fabrics.append(fabric)

        fabric2 = fabric
        if two_fabric == True:
            fabric2 = self.create_fabric('fab-' + name + '2' + self.id())
            self.fabrics.append(fabric2)

        role = 'spine'
        spine_rb_role = 'crb-gateway'
        np, rc = self.create_node_profile(
            'node-profile-' + name + self.id(),
            device_family='junos-qfx',
            role_mappings=[
                AttrDict(
                    {
                        'physical_role': role,
                        'rb_roles': [spine_rb_role]
                    }
                )],
            job_template=jt)
        self.node_profiles.append(np)
        self.role_configs.append(rc)

        br1, pr1 = self.create_router(
            'device-1' + self.id(), '7.7.7.7', product='qfx10002',
            family='junos-qfx', role='spine',
            rb_roles=[spine_rb_role],
            physical_role=self.physical_roles[role],
            overlay_role=self.overlay_roles[spine_rb_role],
            fabric=fabric, node_profile=np)
        pr1.set_physical_router_loopback_ip('30.30.0.22')
        self._vnc_lib.physical_router_update(pr1)

        self.physical_routers.append(pr1)
        self.bgp_routers.append(br1)

        br2, pr2 = self.create_router(
            'device-2' + self.id(), '7.7.7.8', product='qfx10002',
            family='junos-qfx', role=role,
            rb_roles=[spine_rb_role],
            physical_role=self.physical_roles[role],
            overlay_role=self.overlay_roles[spine_rb_role],
            fabric=fabric2, node_profile=np)
        pr2.set_physical_router_loopback_ip('30.30.0.23')
        self._vnc_lib.physical_router_update(pr2)

        self.physical_routers.append(pr2)
        self.bgp_routers.append(br2)

        return fabric, fabric2, pr1, pr2
    # end create_fabrics_two_pr

    def test_routed_vn_single_fabric(self):
        self.create_and_validate_routed_vn(vn_id='10', two_fabric=False)
    # end test_routed_vn_single_fabric

    def test_routed_vn_two_fabric(self):
        self.create_and_validate_routed_vn(vn_id='11', two_fabric=True)
    # end test_routed_vn_two_fabric

    def create_and_validate_routed_vn(self, vn_id, two_fabric=False,
                                      test_static_route=True, test_bgp=True):
        _, _, pr1, pr2 = self.create_fabrics_two_pr('lr', two_fabric)
        # create 1 routed VN with Static Routes with interface_route_table
        # for PR1 and bgp with routing policys for PR2
        vn_obj1 = self.create_vn(vn_id, '41.1.1.0')
        irt_obj1 = None
        rp_obj_dic = {}
        vn_routed_props = VirtualNetworkRoutedPropertiesType()
        pr_used_count = 0

        if test_static_route == True:
            pr_used_count += 1
            irt_prefix1 = ['41.1.1.0/24', '41.2.2.0/24']
            irt_obj1 = self.create_irt(vn_id, prefix_list=irt_prefix1)
            # irt_obj2 = self.create_irt(vn_id, ['41.3.3.0/24'])

            irt_next_hopes = ['41.1.1.20']
            static_route_params = StaticRouteParameters(
                interface_route_table_uuid=[irt_obj1.get_uuid()],
                next_hop_ip_address=irt_next_hopes
            )
            static_routed_props = RoutedProperties(
                physical_router_uuid=pr1.get_uuid(),
                routed_interface_ip_address='41.1.1.10',
                routing_protocol='static-routes',
                bgp_params=None,
                static_route_params=static_route_params,
                routing_policy_params=None
            )
            vn_routed_props.add_routed_properties(static_routed_props)

        # use same routed VN for bgp for LR 1 for PR 2 with RPs
        class RP_terms:
            def __init__(self, name, protocols=[], prefixs=[], prefixtypes=[],
                         extcommunity_list=[], extcommunity_match_all=None,
                         community_match_all=None, action="",
                         local_pref=None, med=None, asn_list=[]):
                self.name = name; self.protocols = protocols
                self.prefixs = prefixs; self.prefixtypes = prefixtypes
                self.extcommunity_list = extcommunity_list
                self.extcommunity_match_all = extcommunity_match_all
                self.community_match_all = community_match_all
                self.action = action; self.local_pref = local_pref
                self.med = med; self.asn_list = asn_list

        if test_bgp == True:
            pr_used_count += 1
            rp_inputdict = {
                'PR-BGP-ACCEPT-COMMUNITY' : [
                    RP_terms('PR-BGP-ACCEPT-COMMUNITY', protocols=["bgp"],
                             prefixs=["0.0.0.0/0"], prefixtypes=["orlonger"],
                             extcommunity_list=["64112:11114", "origin:64511:3",
                                                 "origin:1.1.1.1:3",
                                                 "target:64511:4",
                                                 "target:1.1.1.1:4", "65...:50203"
                                                 ], action="accept",
                             local_pref=100, asn_list=[65401] ) ],
                'PR-STATIC-ACCEPT': [
                    RP_terms('PR-STATIC-ACCEPT', protocols=["static"],
                             prefixs=["0.0.0.0/0"], prefixtypes=["orlonger"],
                             extcommunity_list=[], action="accept") ],
                'PR-BGP-PERMIT-2-TERMS': [
                    RP_terms('PR-BGP-PERMIT-2-TERMS', protocols=["interface"],
                             prefixs=["1.1.1.1/24"], prefixtypes=["exact"],
                             extcommunity_list=[], action="accept"),
                    RP_terms('PR-BGP-PERMIT-2-TERMS', protocols=["interface"],
                             prefixs=["2.2.2.2/32"], prefixtypes=["orlonger"],
                             action="reject") ],
                'PR-STATIC-REJECT': [
                    RP_terms('PR-STATIC-REJECT', protocols=["static"],
                             prefixs=["0.0.0.0/0"], prefixtypes=["longer"],
                             action="reject") ]
                }
            for rp_name, terms in rp_inputdict.items():
                term_list = []
                for t in terms:
                    term_list.append(
                        self.create_routing_policy_term(
                            protocols=t.protocols, prefixs=t.prefixs,
                            prefixtypes=t.prefixtypes,
                            extcommunity_list=t.extcommunity_list,
                            extcommunity_match_all=t.extcommunity_match_all,
                            community_match_all=t.community_match_all,
                            action=t.action, local_pref=t.local_pref,
                            med=t.med, asn_list=t.asn_list)
                    )
                if len(term_list) == 0:
                    continue
                rp_obj_dic[rp_name] = \
                    self.create_routing_policy(rp_name=rp_name,
                                               term_list=term_list)
            i = 0; import_rp = []; export_rp = [];
            import_rp_name = []; export_rp_name = []
            for k,v in rp_obj_dic.items():
                if i < 2:
                    import_rp.append(v.get_uuid())
                    import_rp_name.append(k)
                else:
                    export_rp.append(v.get_uuid())
                    export_rp_name.append(k)
                i += 1
            rp_parameters = RoutingPolicyParameters(
                import_routing_policy_uuid=import_rp,
                export_routing_policy_uuid=export_rp)
            authkey_item = "99493939393"
            bgp_params = BgpParameters(
                peer_autonomous_system=7000, peer_ip_address="30.30.30.30",
                auth_data=AuthenticationData(key_type="md5",
                                             key_items=[AuthenticationKeyItem(
                                                 key_id=0, key=authkey_item)]),
                local_autonomous_system=7001, hold_time=90)
            bgp_routed_props = RoutedProperties(
                physical_router_uuid=pr2.get_uuid(),
                routed_interface_ip_address='41.1.1.11',
                routing_protocol='bgp',
                bgp_params=bgp_params,
                bfd_params=BfdParameters(
                    time_interval=30, detection_time_multiplier=100),
                static_route_params=None,
                routing_policy_params=rp_parameters
            )
            vn_routed_props.add_routed_properties(bgp_routed_props)

        vn_obj1.set_virtual_network_category('routed')
        vn_obj1.set_virtual_network_routed_properties(vn_routed_props)
        self._vnc_lib.virtual_network_update(vn_obj1)

        lr_name = 'lr-routed1-' + self.id()
        lr_fq_name1 = ['default-domain', 'default-project', lr_name]
        lr1 = LogicalRouter(fq_name=lr_fq_name1, parent_type='project',
                            logical_router_type='vxlan-routing')
        if test_static_route == True:
            lr1.add_physical_router(pr1)
        if test_bgp == True:
            lr1.add_physical_router(pr2)

        fq_name1 = ['default-domain', 'default-project',
                    'vmi-routed1-' + vn_id + '-' + self.id()]
        vmi1 = VirtualMachineInterface(fq_name=fq_name1, parent_type='project')
        vmi1.set_virtual_network(vn_obj1)
        self._vnc_lib.virtual_machine_interface_create(vmi1)

        lr1.add_virtual_machine_interface(vmi1)
        lr_uuid = self._vnc_lib.logical_router_create(lr1)
        gevent.sleep(3)

        # update each PR separately to get the abstract config
        # corresponding to that PR
        cpr = pr1
        for iloop in range(pr_used_count):
            cpr.set_physical_router_product_name('qfx10008')
            self._vnc_lib.physical_router_update(cpr)
            gevent.sleep(2)

            verify_static_route = False
            verify_bgp = False
            ac1 = self.check_dm_ansible_config_push()
            vi = ac1.get('device_abstract_config')
            pruuid = vi.get('system').get('uuid')
            if pruuid == pr1.get_uuid():
                cpr = pr2
                verify_static_route = True
            if pruuid == pr2.get_uuid():
                cpr = pr1
                verify_bgp = True
            ri_name = '__contrail_' + lr_name + '_' + lr_uuid
            ri = self.get_routing_instance_from_description(vi, ri_name)
            if test_static_route == True and verify_static_route == True:
                # for pr1 - verify static routes
                ri_static_routes = ri.get('static_routes', None)
                self.assertIsNotNone(ri_static_routes)
                for ri in ri_static_routes:
                    self.assertIn(ri.get('next_hop'), irt_next_hopes)
                    ri_prefix = ri.get('prefix')
                    self.assertIn(ri_prefix, irt_prefix1)
                    irt_prefix1.remove(ri_prefix)
            if test_bgp == True and verify_bgp == True:
                # for pr2 - verify routing instance's bgp and all routing policies
                ri_protocols = ri.get('protocols', None)
                self.assertIsNotNone(ri_protocols)
                for proto in ri_protocols:
                    bgps = proto.get('bgp', None)
                    self.assertIsNotNone(bgps)
                    for bgp in bgps:
                        self.assertEqual(bgp.get('name'),
                                         'vn' + vn_id + '-' + self.id() + '_bgp')
                        bfdp = bgp.get('bfd', None)
                        self.assertIsNotNone(bfdp)
                        self.assertEqual(bfdp.get('rx_tx_interval'), 30)
                        self.assertEqual(bfdp.get('detection_time_multiplier'), 100)
                        rp_cfg = bgp.get('routing_policies',None)
                        self.assertIsNotNone(rp_cfg)
                        for v in rp_cfg.get('import_routing_policies'):
                            self.assertIn(v, import_rp_name)
                            import_rp_name.remove(v)
                        for v in rp_cfg.get('export_routing_policies'):
                            self.assertIn(v, export_rp_name)
                            export_rp_name.remove(v)
                        peers = bgp.get('peers', None)
                        self.assertIsNotNone(peers)
                        for peer in peers:
                            self.assertEqual(peer.get('ip_address'), "30.30.30.30")
                            self.assertEqual(peer.get('autonomous_system'), 7000)
                        self.assertEqual(bgp.get('authentication_key'), authkey_item)
                        self.assertEqual(bgp.get('autonomous_system'), 7001)
                rp_abstract = vi.get('routing_policies', None)
                self.assertIsNotNone(rp_abstract)
                for rp_abs in rp_abstract:
                    rpname = rp_abs.get('name')
                    self.assertIsNotNone(rpname)
                    self.assertIn(rpname, rp_inputdict)
                    rpterms = rp_abs.get('routing_policy_entries', None)
                    self.assertIsNotNone(rpterms)
                    termlist = rpterms.get('terms', None)
                    self.assertIsNotNone(termlist)
                    i = 0
                    for t in termlist:
                        tm = t.get('term_match_condition', None)
                        ta = t.get('term_action_list', None)
                        self.assertIsNotNone(tm); self.assertIsNotNone(ta)
                        tme = tm.get('extcommunity_list', None)
                        tpassed = rp_inputdict[rpname][i]
                        for j in range(len(tpassed.extcommunity_list)):
                            self.assertEqual(tme[j], tpassed.extcommunity_list[j])
                        tprefix = tm.get('prefix', None)
                        for j in range(len(tpassed.prefixs)):
                            self.assertEqual(tprefix[j].get('prefix'),
                                             tpassed.prefixs[j])
                            self.assertEqual(tprefix[j].get('prefix_type'),
                                             tpassed.prefixtypes[j])
                        tproto = tm.get('protocol', None)
                        for j in range(len(tpassed.protocols)):
                            self.assertEqual(tproto[j], tpassed.protocols[j])
                        if len(tpassed.action) > 0:
                            self.assertEqual(ta.get('action'), tpassed.action)
                        tupdate = ta.get('update', None)
                        if tpassed.local_pref is not None:
                            self.assertEqual(tupdate.get('local_pref'),
                                             tpassed.local_pref)
                        if tpassed.med is not None:
                            self.assertEqual(tupdate.get('med'),
                                             tpassed.med)
                        if len(tpassed.asn_list) > 0:
                            tas = tupdate.get('as_path').get('expand').get('asn_list')
                            for j in range(len(tpassed.asn_list)):
                                self.assertEqual(tas[j], tpassed.asn_list[j])
                        i += 1
        # cleanup
        self._vnc_lib.logical_router_delete(fq_name=lr1.get_fq_name())
        self._vnc_lib.virtual_machine_interface_delete(
            fq_name=vmi1.get_fq_name())
        self._vnc_lib.virtual_network_delete(
            fq_name=vn_obj1.get_fq_name())
        if irt_obj1 is not None:
            self._vnc_lib.interface_route_table_delete(
                fq_name=irt_obj1.get_fq_name())
        for obj in rp_obj_dic.values():
            self._vnc_lib.routing_policy_delete(id=obj.get_uuid())
        self.delete_objects()
    # end create_and_validate_routed_vn

# end TestAnsibleDM
