#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from __future__ import absolute_import
import gevent
import mock
from attrdict import AttrDict
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from .test_dm_ansible_common import TestAnsibleCommonDM
from vnc_api.vnc_api import *
from vnc_api.gen.resource_client import *


class RPTerm:
    def __init__(self, name, protocols=[], prefixs=[], prefixtypes=[],
                 extcommunity_list=[], extcommunity_match_all=None,
                 community_match_all=None, action="",
                 local_pref=None, med=None, asn_list=[]):
        self.name = name
        self.protocols = protocols
        self.prefixs = prefixs
        self.prefixtypes = prefixtypes
        self.extcommunity_list = extcommunity_list
        self.extcommunity_match_all = extcommunity_match_all
        self.community_match_all = community_match_all
        self.action = action
        self.local_pref = local_pref
        self.med = med
        self.asn_list = asn_list

# end RPTerm

class BgpRoutedParam:
    def __init__(self, peer_ip, peer_asn, local_asn, hold_time,
                 auth_type, auth_key, auth_key_id, routed_ip,
                 bfd_time, bfd_multiplier, rp_params):
        self.peer_ip = peer_ip
        self.peer_asn = peer_asn
        self.local_asn = local_asn
        self.hold_time = hold_time
        self.auth_type = auth_type
        self.auth_key = auth_key
        self.auth_key_id = auth_key_id
        self.routed_ip = routed_ip
        self.bfd_time = bfd_time
        self.bfd_multiplier = bfd_multiplier
        self.rp_params = rp_params

    def create_bgp_routed_properties(self, pr_uuid):
        bgp_params = BgpParameters(
            peer_autonomous_system=self.peer_asn,
            peer_ip_address=self.peer_ip,
            auth_data=AuthenticationData(
                key_type=self.auth_type, key_items=[
                    AuthenticationKeyItem(key_id=self.auth_key_id,
                                          key=self.auth_key)]),
            local_autonomous_system=self.local_asn, hold_time=self.hold_time)

        return RoutedProperties(
            physical_router_uuid=pr_uuid,
            routed_interface_ip_address=self.routed_ip,
            routing_protocol='bgp',
            bgp_params=bgp_params,
            bfd_params=BfdParameters(
                time_interval=self.bfd_time,
                detection_time_multiplier=self.bfd_multiplier),
            static_route_params=None,
            routing_policy_params=self.rp_params)
    # end create_bgp_routed_properties
# end BgpRoutedParam

class TestAnsibleRoutedVNDM(TestAnsibleCommonDM):

    def setUp(self, extra_config_knobs=None):
        super(TestAnsibleRoutedVNDM, self).setUp(
            extra_config_knobs=extra_config_knobs)
        self.idle_patch = mock.patch('gevent.idle')
        self.idle_mock = self.idle_patch.start()

    def tearDown(self):
        self.idle_patch.stop()
        super(TestAnsibleRoutedVNDM, self).tearDown()

    def _delete_objects(self):
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
    # end _delete_objects

    @retries(2, hook=retry_exc_handler)
    def _verify_abstract_config_static_routes(self, cpr, prnew_name, ri_name,
                                              irt_next_hopes, irt_prefixs):
        cpr.set_physical_router_product_name(prnew_name)
        self._vnc_lib.physical_router_update(cpr)
        gevent.sleep(2)

        ac1 = self.check_dm_ansible_config_push()
        dac = ac1.get('device_abstract_config')
        ri = self.get_routing_instance_from_description(dac, ri_name)
        irt_prefixs_cpy = irt_prefixs[:]
        ri_static_routes = ri.get('static_routes', None)
        self.assertIsNotNone(ri_static_routes)
        for ri in ri_static_routes:
            self.assertIn(ri.get('next_hop'), irt_next_hopes)
            ri_prefix = ri.get('prefix')
            self.assertIn(ri_prefix, irt_prefixs_cpy)
            irt_prefixs_cpy.remove(ri_prefix)
    # end _verify_abstract_config_static_routes

    @retries(2, hook=retry_exc_handler)
    def _verify_abstract_config_rp_and_bgp(self, cpr, prnew_name, ri_name,
                                           vn_id, import_rp_name,
                                           export_rp_name, rp_inputdict,
                                           bgp_param):
        cpr.set_physical_router_product_name(prnew_name)
        self._vnc_lib.physical_router_update(cpr)
        gevent.sleep(2)

        ac1 = self.check_dm_ansible_config_push()
        dac = ac1.get('device_abstract_config')
        ri = self.get_routing_instance_from_description(dac, ri_name)

        ri_protocols = ri.get('protocols', None)
        self.assertIsNotNone(ri_protocols)
        import_rp_name_cpy = import_rp_name[:]
        export_rp_name_cpy = export_rp_name[:]
        for proto in ri_protocols:
            bgps = proto.get('bgp', None)
            self.assertIsNotNone(bgps)
            for bgp in bgps:
                self.assertEqual(bgp.get('name'),
                                 'vn' + vn_id + '-' + self.id() + '_bgp')
                bfdp = bgp.get('bfd', None)
                self.assertIsNotNone(bfdp)
                self.assertEqual(bfdp.get('rx_tx_interval'),
                                 bgp_param.bfd_time)
                self.assertEqual(bfdp.get('detection_time_multiplier'),
                                 bgp_param.bfd_multiplier)
                rp_cfg = bgp.get('routing_policies', None)
                self.assertIsNotNone(rp_cfg)
                for v in rp_cfg.get('import_routing_policies'):
                    self.assertIn(v, import_rp_name)
                    import_rp_name_cpy.remove(v)
                for v in rp_cfg.get('export_routing_policies'):
                    self.assertIn(v, export_rp_name)
                    export_rp_name_cpy.remove(v)
                peers = bgp.get('peers', None)
                self.assertIsNotNone(peers)
                for peer in peers:
                    self.assertEqual(peer.get('ip_address'),
                                     bgp_param.peer_ip)
                    self.assertEqual(peer.get('autonomous_system'),
                                     bgp_param.peer_asn)
                # NOTE: auth key special character gets wrapped in quotes in
                # db.py for ansible config
                auth_key = bgp_param.auth_key
                if auth_key.lower().startswith(('$9$', '$1$', '$5$', '$6$')):
                    auth_key = '"%s"' % auth_key
                self.assertEqual(bgp.get('authentication_key'), auth_key)
                self.assertEqual(bgp.get('autonomous_system'),
                                 bgp_param.local_asn)
        rp_abstract = dac.get('routing_policies', None)
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
                self.assertIsNotNone(tm);
                self.assertIsNotNone(ta)
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
    # end _verify_abstract_config_rp_and_bgp

    def _create_route_props_static_route(self, irt_uuids, irt_next_hopes, pr,
                                         interface_ip='41.1.1.10'):
        static_route_params = StaticRouteParameters(
            interface_route_table_uuid=irt_uuids,
            next_hop_ip_address=irt_next_hopes
        )
        return RoutedProperties(
            physical_router_uuid=pr.get_uuid(),
            routed_interface_ip_address=interface_ip,
            routing_protocol='static-routes',
            bgp_params=None,
            static_route_params=static_route_params,
            routing_policy_params=None)
    # end _create_route_props_static_route

    def _create_fabrics_two_pr(self, name, two_fabric=False):
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
    # end _create_fabrics_two_pr

    def _create_and_validate_routed_vn(self, vn_id, two_fabric=False,
                                      test_static_route=True, test_bgp=True):
        _, _, pr1, pr2 = self._create_fabrics_two_pr('lr', two_fabric)
        # create 1 routed VN with Static Routes with interface_route_table
        # for PR1 and bgp with routing policys for PR2
        vn_obj1 = self.create_vn(vn_id, '41.1.1.0')
        irt_obj1 = None
        rp_obj_dic = {}
        bgp_routed_props = None
        static_routed_props = None
        vn_routed_props = VirtualNetworkRoutedPropertiesType()
        if test_static_route == True:
            irt_prefix1 = ['41.1.1.0/24', '41.2.2.0/24']
            irt_obj1 = self.create_or_update_irt(vn_id,
                                                 prefix_list=irt_prefix1)
            irt_next_hopes = ['41.1.1.20']
            static_routed_props = self._create_route_props_static_route(
                [irt_obj1.get_uuid()], irt_next_hopes, pr1, '41.1.1.10')
            vn_routed_props.add_routed_properties(static_routed_props)

        if test_bgp == True:
            rp_inputdict = {
                'PR-BGP-ACCEPT-COMMUNITY' : [
                    RPTerm('PR-BGP-ACCEPT-COMMUNITY', protocols=["bgp"],
                           prefixs=["0.0.0.0/0"], prefixtypes=["orlonger"],
                           extcommunity_list=["64112:11114", "origin:64511:3",
                                              "origin:1.1.1.1:3",
                                              "target:64511:4",
                                              "target:1.1.1.1:4",
                                              "65...:50203"], action="accept",
                           local_pref=100, asn_list=[65401] ) ],
                'PR-STATIC-ACCEPT': [
                    RPTerm('PR-STATIC-ACCEPT', protocols=["static"],
                           prefixs=["0.0.0.0/0"], prefixtypes=["orlonger"],
                           extcommunity_list=[], action="accept") ],
                'PR-BGP-PERMIT-2-TERMS': [
                    RPTerm('PR-BGP-PERMIT-2-TERMS', protocols=["interface"],
                           prefixs=["1.1.1.1/24"], prefixtypes=["exact"],
                           extcommunity_list=[], action="accept"),
                    RPTerm('PR-BGP-PERMIT-2-TERMS', protocols=["interface"],
                           prefixs=["2.2.2.2/32"], prefixtypes=["orlonger"],
                           action="reject") ],
                'PR-STATIC-REJECT': [
                    RPTerm('PR-STATIC-REJECT', protocols=["static"],
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
            bgp_param = BgpRoutedParam(
                peer_ip="30.30.30.30", peer_asn=7000, local_asn=7001,
                hold_time=90, auth_type="md5", auth_key="99493939393",
                auth_key_id=0, routed_ip='41.1.1.11', bfd_time=30,
                bfd_multiplier=100, rp_params=rp_parameters)
            bgp_routed_props = bgp_param.create_bgp_routed_properties(
                pr2.get_uuid())
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
        ri_name = '__contrail_' + lr_name + '_' + lr_uuid
        if test_static_route == True:
            # for pr1 - verify static routes
            self._verify_abstract_config_static_routes(
                pr1, 'qfx10008', ri_name, irt_next_hopes, irt_prefix1)
        if test_bgp == True:
            # for pr2 - verify ri's bgp and all routing policies
            self._verify_abstract_config_rp_and_bgp(
                pr2, 'qfx10008', ri_name, vn_id, import_rp_name,
                export_rp_name, rp_inputdict, bgp_param)

        if test_static_route == True:
            # change irt prefix and verify pr1 abstract cfg updated
            irt_prefix1_changed = ['43.1.1.0/24', '41.2.2.0/24']
            irt_obj1 = self.create_or_update_irt(
                vn_id, prefix_list=irt_prefix1_changed, irt_obj=irt_obj1)

            self._verify_abstract_config_static_routes(
                pr1, 'qfx10002', ri_name, irt_next_hopes, irt_prefix1_changed)

            # change routed VN static routes next hops and verify abstactCfg
            irt_next_hopes_chng = ['41.1.1.15']
            static_routed_props = self._create_route_props_static_route(
                [irt_obj1.get_uuid()], irt_next_hopes_chng, pr1, '41.1.1.10')
            route_list = []
            route_list.append(static_routed_props)
            if test_bgp == True:
                route_list.append(bgp_routed_props)
            vn_routed_props.set_routed_properties(route_list)

            vn_obj1.set_virtual_network_routed_properties(vn_routed_props)
            self._vnc_lib.virtual_network_update(vn_obj1)

            self._verify_abstract_config_static_routes(
                pr1, 'qfx10008', ri_name, irt_next_hopes_chng,
                irt_prefix1_changed)

        if test_bgp == True:
            # change rp terms and verify pr2 abstract cfg updated.
            rp_inputdict['PR-STATIC-ACCEPT'] = [
                RPTerm('PR-STATIC-ACCEPT', protocols=["static"],
                       prefixs=["20.0.0.0/27"], prefixtypes=["exact"],
                       extcommunity_list=[], action="accept") ]
            t = rp_inputdict['PR-STATIC-ACCEPT'][0]
            term_list = []
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
            tmp_rp_obj = rp_obj_dic['PR-STATIC-ACCEPT']
            tmp_rp_obj.set_routing_policy_entries(PolicyStatementType(
                term=term_list))
            self._vnc_lib.routing_policy_update(tmp_rp_obj)
            rp_obj_dic['PR-STATIC-ACCEPT'] = \
                self._vnc_lib.routing_policy_read(id=tmp_rp_obj.get_uuid())
            
            self._verify_abstract_config_rp_and_bgp(
                pr2, 'qfx10002', ri_name, vn_id, import_rp_name,
                export_rp_name, rp_inputdict, bgp_param)

            # change routed bgp values and verify abstract updated
            bgp_param_change = BgpRoutedParam(
                peer_ip="23.30.30.30", peer_asn=8000, local_asn=8001,
                hold_time=80, auth_type="md5", auth_key="$9$1234555555",
                auth_key_id=0, routed_ip='42.1.1.12', bfd_time=40,
                bfd_multiplier=50, rp_params=rp_parameters)
            bgp_routed_props = bgp_param_change.create_bgp_routed_properties(
                pr2.get_uuid())
            route_list = []
            if test_static_route == True:
                route_list.append(static_routed_props)
            route_list.append(bgp_routed_props)
            vn_routed_props.set_routed_properties(route_list)

            vn_obj1.set_virtual_network_routed_properties(vn_routed_props)
            self._vnc_lib.virtual_network_update(vn_obj1)

            self._verify_abstract_config_rp_and_bgp(
                pr2, 'qfx10008', ri_name, vn_id, import_rp_name,
                export_rp_name, rp_inputdict, bgp_param_change)

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
        self._delete_objects()
    # end _create_and_validate_routed_vn

    def test_routed_vn_single_fabric(self):
        self._create_and_validate_routed_vn(vn_id='10', two_fabric=False)
    # end test_routed_vn_single_fabric

    def test_routed_vn_two_fabric(self):
        self._create_and_validate_routed_vn(vn_id='11', two_fabric=True)
    # end test_routed_vn_two_fabric

# end TestAnsibleDM
