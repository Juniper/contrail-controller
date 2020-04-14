#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from __future__ import absolute_import
import gevent
import mock
from attrdict import AttrDict
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from .test_dm_ansible_common import TestAnsibleCommonDM, RPTerm
from vnc_api.vnc_api import *
from vnc_api.gen.resource_client import *


class PRExpectedACfg(object):
    def __init__(self, dciname):
        self.dciname = dciname
        self.rib_groups = {}
        self.routing_policies = {}
        self.routing_instances = {}

    def set_rib_groups(self, name, import_rib=[], import_policy=[],
                       interface_routes=None, bgp=None, static=None):
        if name not in self.rib_groups:
            self.rib_groups[name] = {
                "interface_routes": True, "bgp": False,
                "static": False, "import_rib": [], "import_policy": []
            }
        if interface_routes is not None:
            self.rib_groups[name]["interface_routes"] = interface_routes
        if bgp is not None:
            self.rib_groups[name]["bgp"] = bgp
        if static is not None:
            self.rib_groups[name]["static"] = static
        if len(import_rib) > 0:
            self.rib_groups[name]["import_rib"] = import_rib
        if len(import_policy) > 0:
            self.rib_groups[name]["import_policy"] = import_policy
    # end set_rib_groups

    def set_routing_policies(self, name, term_type=None, terms=None):
        if name not in self.routing_policies:
            self.routing_policies[name] = []
        if terms is not None:
            self.routing_policies[name] = terms
    # end set_routing_policies

    def set_routing_instances(self, riname, rib_group=None,
                              vrf_export=[], vrf_import=[]):
        if riname not in self.routing_instances:
            self.routing_instances[riname] = {
                "rib_group": None, "vrf_export": [], "vrf_import": []
            }
        if rib_group is not None:
            self.routing_instances[riname]["rib_group"] = rib_group
        if len(vrf_export) > 0:
            self.routing_instances[riname]["vrf_export"] = vrf_export
        if len(vrf_import) > 0:
            self.routing_instances[riname]["vrf_import"] = vrf_import
    # end set_routing_instances

    def verify_abstract_config(self, cself, a_cfg):
        for riname in self.routing_instances.keys():
            a_ri = cself.get_routing_instance_from_description(
                a_cfg, riname)
            if not a_ri:
                continue
            rib_group = self.routing_instances[riname]["rib_group"]
            vrf_export = self.routing_instances[riname]["vrf_export"]
            vrf_import = self.routing_instances[riname]["vrf_import"]
            if rib_group is not None:
                cself.assertEqual(a_ri.get('rib_group'), rib_group)
            if len(vrf_export) > 0:
                a_vrf_exports = a_ri.get('vrf_export', None)
                if a_vrf_exports:
                    cself.assertIsNotNone(a_vrf_exports)
                    for a_vrf_export in a_vrf_exports:
                        cself.assertIn(a_vrf_export, vrf_export)
                else:
                    continue
            if len(vrf_import) > 0:
                a_vrf_imports = a_ri.get('vrf_import', None)
                if a_vrf_imports:
                    cself.assertIsNotNone(a_vrf_imports)
                    for a_vrf_import in a_vrf_imports:
                        cself.assertIn(a_vrf_import, vrf_import)
                else:
                    continue
        if len(self.rib_groups) > 0:
            a_ribs = a_cfg.get('rib_groups', None)
            if a_ribs:
                cself.assertIsNotNone(a_ribs)
                c_verified = 0
                for a_rib in a_ribs:
                    a_ribname = a_rib.get('name')
                    cself.assertIsNotNone(a_ribname)
                    if a_ribname not in self.rib_groups.keys():
                        continue
                    c_verified += 1
                    cself.assertIn(a_ribname, self.rib_groups.keys())
                    for key in ['interface_routes', 'bgp', 'static']:
                        cself.assertEqual( a_rib.get(key),
                                           self.rib_groups[a_ribname][key])
                    for key in ['import_rib', 'import_policy']:
                        a_keylist = a_rib.get(key)
                        for a_key in a_keylist:
                            cself.assertIn(a_key,
                                           self.rib_groups[a_ribname][key])
                cself.assertEqual(c_verified, len(self.rib_groups))
        self._verify_routing_policy_in_abstract_cfg(cself, a_cfg,
                                                    self.routing_policies )

    def _verify_routing_policy_in_abstract_cfg(self, cself, abstract_cfg,
                                               rp_inputdict):
        if len(rp_inputdict) == 0:
            return
        rp_abstract = abstract_cfg.get('routing_policies', None)
        if rp_abstract is None:
            return
        cself.assertIsNotNone(rp_abstract)
        c_verified = 0
        for rp_abs in rp_abstract:
            rpname = rp_abs.get('name')
            cself.assertIsNotNone(rpname)
            if rpname not in rp_inputdict:
                continue
            c_verified += 1
            cself.assertIn(rpname, rp_inputdict)
            rpterms = rp_abs.get('routing_policy_entries', None)
            cself.assertIsNotNone(rpterms)
            termlist = rpterms.get('terms', None)
            cself.assertIsNotNone(termlist)
            cself.verify_rpterms_in_abstract_cfg(rpname, termlist,
                                                 rp_inputdict)
        # cself.assertEqual(c_verified, len(rp_inputdict))
    # end verify_abstract_config
# class PRExpectedACfg

class TestAnsibleDciIntraFabric(TestAnsibleCommonDM):

    def setUp(self, extra_config_knobs=None):
        super(TestAnsibleDciIntraFabric, self).setUp(
            extra_config_knobs=extra_config_knobs)
        self.idle_patch = mock.patch('gevent.idle')
        self.idle_mock = self.idle_patch.start()

    def tearDown(self):
        self.idle_patch.stop()
        super(TestAnsibleDciIntraFabric, self).tearDown()

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

    # @retries(2, hook=retry_exc_handler)
    def verify_intrafabric_dci(self, prname, probj, dict_pr_acfg):
        if prname not in dict_pr_acfg or len(dict_pr_acfg[prname]) == 0:
            return
        prnew_name = 'qfx10002'
        if probj.get_physical_router_product_name() == 'qfx10002':
            prnew_name = 'qfx10008'
        probj.set_physical_router_product_name(prnew_name)
        self._vnc_lib.physical_router_update(probj)
        gevent.sleep(2)

        ac1 = self.check_dm_ansible_config_push()
        dac = ac1.get('device_abstract_config')
        for pr_cfg in dict_pr_acfg[prname]:
            pr_cfg.verify_abstract_config(self, dac)
    # end verify_intrafabric_dci

    def _create_fabrics_prs(self, dict_prs, name="DCI"):
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
        ov_roles = ['crb-gateway', 'erb-ucast-gateway',
                    'dci-gateway', 'crb-mcast-gateway']
        self.create_physical_roles(['spine'])
        self.create_overlay_roles(ov_roles)
        jt = self.create_job_template('job-template-' + name + self.id())
        self.job_templates.append(jt)
        fabric = self.create_fabric('fab-' + name + self.id())
        self.fabrics.append(fabric)

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
        num = 22
        for prname in dict_prs.keys():
            br, pr = self.create_router(
                prname + self.id(), '7.7.7.7', product='qfx10002',
                family='junos-qfx', role='spine',
                rb_roles=[spine_rb_role],
                physical_role=self.physical_roles[role],
                overlay_role=self.overlay_roles[spine_rb_role],
                fabric=fabric, node_profile=np)
            pr.set_physical_router_loopback_ip('30.30.0.%s' % num)
            num += 1
            self._vnc_lib.physical_router_update(pr)

            self.physical_routers.append(pr)
            self.bgp_routers.append(br)
            dict_prs[prname] = pr
        return fabric
    # end _create_fabrics_prs

    def make_vn_name(self, subnet):
        return "VN_%s" % subnet

    def make_rp_name(self, subnet):
        return "RP_%s" % subnet

    def make_lr_name(self, subnet1, subnet2=None, subnet3=None):
        if subnet3 is not None:
            return "LR_%s_%s_%s" % (subnet1, subnet2, subnet3)
        if subnet2 is not None:
            return "LR_%s_%s" % (subnet1, subnet2)
        return "LR_%s" % subnet1

    def make_ri_name(self, lrname, lr_uuid):
        return "__contrail_%s_%s" % (lrname, lr_uuid)

    def make_rp_rib_vn_name(self, dciname):
        return "_contrail_rp_rib_%s" % dciname

    def make_rp_vrf_vn_name(self, dciname):
        return "_contrail_rp_vrf_%s" % dciname

    def create_vn_ipam(self, id):
        ipam1_obj = NetworkIpam('ipam' + '-' + id)
        ipam1_uuid = self._vnc_lib.network_ipam_create(ipam1_obj)
        return self._vnc_lib.network_ipam_read(id=ipam1_uuid)
    # end create_vn_ipam

    def create_vn_with_subnets(self, id, vn_name, ipam_obj, subnet,
                               subnetmask=24):
        #vn_name = 'vn' + vn_id + '_' + self.id()
        vn_obj = VirtualNetwork(vn_name)
        vn_obj_properties = VirtualNetworkType()
        vn_obj_properties.set_vxlan_network_identifier(2000 + id)
        vn_obj_properties.set_forwarding_mode('l2_l3')
        vn_obj.set_virtual_network_properties(vn_obj_properties)
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType(subnet, subnetmask))]))
        vn_uuid = self._vnc_lib.virtual_network_create(vn_obj)
        return self._vnc_lib.virtual_network_read(id=vn_uuid)
    # end create_vn_with_subnets

    def create_lr(self, lrname, vns, prs, vmis):
        lr_fq_name = ['default-domain', 'default-project', lrname]
        lr = LogicalRouter(fq_name=lr_fq_name, parent_type='project',
                            logical_router_type='vxlan-routing')
        for pr in prs:
            probj = self._vnc_lib.physical_router_read(id=pr.get_uuid())
            lr.add_physical_router(probj)
        for vn in vns:
            vminame = 'vmi-lr-to-vn' + vn.get_display_name()
            fq_name1 = ['default-domain', 'default-project', vminame]
            vmi = VirtualMachineInterface(fq_name=fq_name1,
                                          parent_type='project')
            vmi.set_virtual_network(vn)
            self._vnc_lib.virtual_machine_interface_create(vmi)
            vmis[vminame] = vmi
            lr.add_virtual_machine_interface(vmi)
        lr_uuid = self._vnc_lib.logical_router_create(lr)
        return lr, self._vnc_lib.logical_router_read(id=lr_uuid)
    # end create_lr

    # @retries(2, hook=retry_exc_handler)
    def get_coumminty_from_lr(self, dciname, srclrname, dict_lrs, lrobj):
        community_member = ""
        rt_ref = dict_lrs[srclrname].get_route_target_refs() or []
        if len(rt_ref) == 0:
            dict_lrs[srclrname] = self._vnc_lib.logical_router_read(
                id=dict_lrs[srclrname].get_uuid())
            rt_ref = dict_lrs[srclrname].get_route_target_refs() or []
        displayname = dict_lrs[srclrname].display_name
        retry_cnt = 0
        while len(rt_ref) == 0 and retry_cnt < 3:
            retry_cnt += 1
            dict_lrs[srclrname].set_display_name(
                '%s_changed%s' % (displayname, retry_cnt))
            self._vnc_lib.logical_router_update(dict_lrs[srclrname])
            gevent.sleep(2)
            dict_lrs[srclrname] = self._vnc_lib.logical_router_read(
                id=dict_lrs[srclrname].get_uuid())
            rt_ref = dict_lrs[srclrname].get_route_target_refs() or []
        for rt in rt_ref:
            community_member = rt.get('to')[0]
            break
        community_name = "_contrail_rp_inter_%s" % dciname
        return community_name, community_member

    def create_intrafabric_dci(self, dciname, src_lrname, srcvn_names,
                               srcrp_names, dstlrs_pr_names_map, dict_lrs,
                               dict_vns, dict_rps_created, dict_prs,
                               srclrs_pr_map, dict_pr_acfg, dict_rps,
                               community_name = '', community_member = []):
        dci_fq_name = ["default-global-system-config", dciname]
        dci = DataCenterInterconnect(
            fq_name=dci_fq_name, parent_type='global-system-config',
            data_center_interconnect_type='intra_fabric')
        dci.add_logical_router(dict_lrs[src_lrname])
        for vnname in srcvn_names:
            dci.add_virtual_network(dict_vns[vnname])
        for rpname in srcrp_names:
            dci.add_routing_policy(dict_rps_created[rpname])
        lrpr_list_obj = LogicalRouterPRListType()
        for dstlrname, prnames in dstlrs_pr_names_map.items():
            dci.add_logical_router(dict_lrs[dstlrname])
            dstlr_uuid = dict_lrs[dstlrname].get_uuid()
            dst_pr_list = []
            for prname in prnames:
                pruuid = dict_prs[prname].get_uuid()
                dst_pr_list.append(pruuid)
            lrpr_list_obj.add_logical_router_list(
                LogicalRouterPRListParams(
                    logical_router_uuid=dstlr_uuid,
                    physical_router_uuid_list=dst_pr_list))
        dci.set_destination_physical_router_list(lrpr_list_obj)
        # update refs for src lr
        for lrref in dci.logical_router_refs or []:
            if lrref['uuid'] == dict_lrs[src_lrname].get_uuid():
                lrref['attr'] = 'source'
                break
        dci_uuid = self._vnc_lib.data_center_interconnect_create(dci)

        # now update dict_pr_acfg
        if "_RIB_VRF_" in dciname:
            return dci, self._vnc_lib.data_center_interconnect_read(
                id=dci_uuid)
        if "_VRF_" in dciname:
            vrf_rp_name = self.make_rp_vrf_vn_name(dciname)
            srcrp_term = [
                RPTerm(vrf_rp_name, action='accept',
                       tcommunity_list=community_member,
                       tcommunity_add=[community_name])
                ]
            src_ri = self.make_ri_name(src_lrname,
                                       dict_lrs[src_lrname].get_uuid())
            for srcpr in srclrs_pr_map[src_lrname]:
                pr_acfg = PRExpectedACfg(dciname)
                pr_acfg.set_routing_instances(
                    riname=src_ri, vrf_export=[vrf_rp_name])
                pr_acfg.set_routing_policies(
                    vrf_rp_name, term_type='network-device',
                    terms=srcrp_term)
                dict_pr_acfg[srcpr].append(pr_acfg)

            dstr_vn_term = []
            if "_VN_" in dciname:
                dstr_vn_term = [
                    RPTerm(vrf_rp_name, action='accept',
                           fcommunity_list=community_member,
                           fcommunity=community_name)
                    ]

            for dstlrname, prnames in dstlrs_pr_names_map.items():
                dst_ri = self.make_ri_name(dstlrname,
                                           dict_lrs[dstlrname].get_uuid())

                for prname in prnames:
                    pr_acfg = PRExpectedACfg(dciname)
                    if "_VN_" in dciname:
                        pr_acfg.set_routing_instances(
                            riname=dst_ri, vrf_import=[vrf_rp_name])
                        pr_acfg.set_routing_policies(
                            vrf_rp_name, term_type='network-device',
                            terms=dstr_vn_term)
                    else:
                        pr_acfg.set_routing_instances(
                            riname=dst_ri, vrf_import=srcrp_names)
                        for rpname in srcrp_names:
                            pr_acfg.set_routing_policies(
                                name=rpname, term_type='network-device',
                                terms=dict_rps[rpname])
                    dict_pr_acfg[prname].append(pr_acfg)
            # Create vrf_export new policy for src lr
            # create seperate vrf_import new policy for dest LR if its VN
            # else if its RP then for dst PR, use RPs at is it.
            return dci, self._vnc_lib.data_center_interconnect_read(
                id=dci_uuid)
        if "_RIB_" in dciname:
            for srcpr in srclrs_pr_map[src_lrname]:
                pr_acfg= PRExpectedACfg(dciname)
                ribgroup_name = "_contrail_rib_%s_%s" % (dciname, dci_uuid)
                src_ri = self.make_ri_name(src_lrname,
                                           dict_lrs[src_lrname].get_uuid())
                import_rib = [src_ri];
                pr_acfg.set_routing_instances(
                    riname=src_ri, rib_group=ribgroup_name)
                for dstlrname, dstprnames in dstlrs_pr_names_map.items():
                    riname = self.make_ri_name(dstlrname,
                                               dict_lrs[dstlrname].get_uuid())
                    for prname in dstprnames:
                        if prname == srcpr:
                            import_rib.append(riname)
                import_policy = []
                if "_VN_" in dciname:
                    vn_rp_name = self.make_rp_rib_vn_name(dciname)
                    import_policy.append(vn_rp_name)
                else:
                    for rpname in srcrp_names:
                        import_policy.append(rpname)
                pr_acfg.set_rib_groups(
                    name=ribgroup_name,
                    import_rib=import_rib, import_policy=import_policy,
                    interface_routes=True)
                for rpname in import_policy:
                    pr_acfg.set_routing_policies(
                        name=rpname, term_type='network-device',
                        terms=dict_rps[rpname])
                dict_pr_acfg[srcpr].append(pr_acfg)
        return dci, self._vnc_lib.data_center_interconnect_read(id=dci_uuid)
    # end create_intrafabric_dci

    def create_rps(self, dict_rps, rp_names, dict_rps_created):
        for rp_name in rp_names:
            term_list = []
            for t in dict_rps[rp_name]:
                term_list.append(
                    self.create_routing_policy_term(
                        protocols=t.protocols, prefixs=t.prefixs,
                        prefixtypes=t.prefixtypes,
                        extcommunity_list=t.extcommunity_list,
                        extcommunity_match_all=t.extcommunity_match_all,
                        community_match_all=t.community_match_all,
                        action=t.action, local_pref=t.local_pref,
                        med=t.med, asn_list=t.asn_list,
                        routes=t.routes, route_types=t.route_types,
                        route_values=t.route_values)
                )
            if len(term_list) == 0:
                continue
            dict_rps_created[rp_name] = \
                self.create_routing_policy(rp_name=rp_name,
                                           term_list=term_list,
                                           termtype='network-device')
    # end create_rps

    def _create_and_validate_dci_intrafabric(self):
        # prepare all dictionary for input params
        # dict_xx key: name, value is either db object or class object
        dict_rps = {}
        dict_rps_created = {}
        dict_vns = {}
        dict_lrs = {}
        dict_vmis = {}
        dict_prs = {"PR1": None, "PR2": None, "PR3": None, "PR4": None}
        dict_pr_acfg = {"PR1": [], "PR2": [], "PR3": [], "PR4": []}

        dict_dcis = {"DCI_RIB_VN_1": None, "DCI_RIB_RP_1": None,
                     "DCI_VRF_VN_1": None, "DCI_VRF_RP_1": None,
                     "DCI_RIB_VRF_VN_1": None, "DCI_RIB_VRF_RP_1": None}

        # dict_dcis = {"DCI_VRF_VN_1": None}
        ipam_obj = self.create_vn_ipam(self.id())
        subnetmask = 24
        vn_starting_index = 21
        # create 4 PR and single fabric
        fabric = self._create_fabrics_prs(dict_prs)

        # create all 28 VN, 16 (6 src LR with PR1, PR2 PR, 10 dst LR) LRs
        for i in range(vn_starting_index, vn_starting_index + 29):
            subnet = "%s.0.0.0" % i; vn_name = self.make_vn_name(i)
            dict_vns[vn_name] = self.create_vn_with_subnets(
                i, vn_name, ipam_obj, subnet, subnetmask)

        parag_lr_src_name = ''; parag_lr_obj = None
        vn_num = vn_starting_index
        for k in dict_dcis.keys():
            srclrs_pr_map = {}
            dstlrs_pr_map = {}
            if "_RIB_VRF_" in k:
                continue
            if "_VRF_" in k:
                # create src lr
                src_lrname = self.make_lr_name(vn_num, vn_num + 1, vn_num + 2)
                srcvns = [dict_vns[self.make_vn_name(vn_num)],
                          dict_vns[self.make_vn_name(vn_num + 1)],
                          dict_vns[self.make_vn_name(vn_num + 2)]]
                lrobj, dict_lrs[src_lrname] = self.create_lr(
                    src_lrname, srcvns, [dict_prs["PR1"]], dict_vmis)
                community_name, c_member = \
                    self.get_coumminty_from_lr(k, src_lrname, dict_lrs, lrobj)
                community_member = []
                if len(c_member) > 0:
                    community_member = [c_member]
                parag_lr_src_name = src_lrname; parag_lr_obj = lrobj

                srcrp_names = []
                srcvn_names = []
                if "_VN_" in k:
                    srcvn_names = [self.make_vn_name(vn_num),
                                   self.make_vn_name(vn_num + 2)]
                else:
                    srcrp_names = [self.make_rp_name(vn_num),
                                   self.make_rp_name(vn_num + 2)]
                    rpname = self.make_rp_name(vn_num)
                    dict_rps[rpname] = [
                        RPTerm(rpname, action='accept',
                               routes=["%s.0.0.0" % (vn_num)],
                               route_types=["upto"],
                               route_values=["/%s" % subnetmask],
                               fcommunity_list=community_member,
                               fcommunity=community_name)
                    ]
                    rpname = self.make_rp_name(vn_num + 2)
                    dict_rps[rpname] = [
                        RPTerm(rpname, action='accept',
                               routes=["%s.0.0.0/%s" % (vn_num + 2, subnetmask)],
                               route_types=["exact"],
                               route_values=[None],
                               fcommunity_list=community_member,
                               fcommunity=community_name)
                    ]
                    self.create_rps(dict_rps, srcrp_names, dict_rps_created)
                srclrs_pr_map[src_lrname] = ["PR1"]

                # create 2 dst lrs
                vn_num += 3
                dst_lrname1 = self.make_lr_name(vn_num)
                dstvns1 = [dict_vns[self.make_vn_name(vn_num)]]
                _, dict_lrs[dst_lrname1] = self.create_lr(
                    dst_lrname1, dstvns1, [dict_prs["PR2"]], dict_vmis)
                vn_num += 1
                dst_lrname2 = self.make_lr_name(vn_num)
                dstvns2 = [dict_vns[self.make_vn_name(vn_num)]]
                _, dict_lrs[dst_lrname2] = self.create_lr(
                    dst_lrname2, dstvns2, [dict_prs["PR3"]], dict_vmis)
                vn_num += 1

                dstlrs_pr_map[dst_lrname1] = ["PR2"]
                dstlrs_pr_map[dst_lrname2] = ["PR3"]

                dci, dict_dcis[k] = self.create_intrafabric_dci(
                    k, src_lrname, srcvn_names, srcrp_names, dstlrs_pr_map,
                    dict_lrs, dict_vns, dict_rps_created, dict_prs,
                    srclrs_pr_map, dict_pr_acfg, dict_rps, community_name,
                    community_member)
                continue

            if "_RIB_" in k:
                # create src lr
                src_lrname = self.make_lr_name(vn_num, vn_num+1, vn_num+2)
                srcvn_names = []
                srcrp_names = []
                if "_VN_" in k:
                    srcvn_names = [self.make_vn_name(vn_num),
                                   self.make_vn_name(vn_num+2)]
                    vn_rp_name = self.make_rp_rib_vn_name(k)
                    dict_rps[vn_rp_name] = [
                        RPTerm(vn_rp_name, action='accept',
                               routes=[
                                   "%s.0.0.0/%s" % (vn_num, subnetmask),
                                   "%s.0.0.0/%s" % (vn_num+2, subnetmask)],
                               route_types=["exact", "exact"],
                               route_values=[None, None]),
                        RPTerm(vn_rp_name, action='reject')
                    ]
                else:
                    srcrp_names = [self.make_rp_name(vn_num),
                                   self.make_rp_name(vn_num+2)]
                    rpname = self.make_rp_name(vn_num)
                    dict_rps[rpname] = [
                        RPTerm(rpname, action='accept',
                               routes=["%s.0.0.0" % (vn_num)],
                               route_types=["upto"],
                               route_values=["/%s" % subnetmask])
                    ]
                    rpname = self.make_rp_name(vn_num+2)
                    dict_rps[rpname] = [
                        RPTerm(rpname, action='accept',
                               routes=["%s.0.0.0/%s" % (vn_num+2,subnetmask)],
                               route_types=["exact"],
                               route_values=[None])
                    ]
                    self.create_rps(dict_rps, srcrp_names, dict_rps_created)
                srcvns = [dict_vns[self.make_vn_name(vn_num)],
                          dict_vns[self.make_vn_name(vn_num + 1)],
                          dict_vns[self.make_vn_name(vn_num + 2)]]
                srclrs_pr_map[src_lrname] = ["PR1"]
                _, dict_lrs[src_lrname] = self.create_lr(
                    src_lrname, srcvns, [dict_prs["PR1"]], dict_vmis)
                # create 2 dst lrs
                vn_num += 3
                dst_lrname1 = self.make_lr_name(vn_num)
                dstvns1 = [dict_vns[self.make_vn_name(vn_num)]]
                _, dict_lrs[dst_lrname1] = self.create_lr(
                    dst_lrname1, dstvns1, [dict_prs["PR1"]], dict_vmis)
                vn_num += 1
                dst_lrname2 = self.make_lr_name(vn_num)
                dstvns2 = [dict_vns[self.make_vn_name(vn_num)]]
                _, dict_lrs[dst_lrname2] = self.create_lr(
                    dst_lrname2, dstvns2, [dict_prs["PR1"]], dict_vmis)
                vn_num += 1

                dstlrs_pr_map[dst_lrname1] = ["PR1"]
                dstlrs_pr_map[dst_lrname2] = ["PR1"]

                dci, dict_dcis[k] = self.create_intrafabric_dci(
                    k, src_lrname, srcvn_names, srcrp_names, dstlrs_pr_map,
                    dict_lrs, dict_vns, dict_rps_created, dict_prs,
                    srclrs_pr_map, dict_pr_acfg, dict_rps)
                continue

        # validate one PR ansible cfg at a time
        for prname in dict_pr_acfg.keys():
            self.verify_intrafabric_dci(prname, dict_prs[prname],
                                        dict_pr_acfg)

        # cleanup
        for dciname, dciobj in dict_dcis.items():
            if dciobj is None:
                continue
            self._vnc_lib.data_center_interconnect_delete(
                id=dciobj.get_uuid())
        for lrname, lrobj in dict_lrs.items():
            self._vnc_lib.logical_router_delete(id=lrobj.get_uuid())
        for vminame, vmiobj in dict_vmis.items():
            self._vnc_lib.virtual_machine_interface_delete(
                id=vmiobj.get_uuid())
        for vnname, vnobj in dict_vns.items():
            self._vnc_lib.virtual_network_delete(
                fq_name=vnobj.get_fq_name())
        self._vnc_lib.network_ipam_delete(id=ipam_obj.uuid)
        for rpname, rpobj in dict_rps_created.items():
            self._vnc_lib.routing_policy_delete(id=rpobj.get_uuid())
        self._delete_objects()
    # end _create_and_validate_dci_intrafabric

    def test_dci_intrafabric_lr_interconnect(self):
        self._create_and_validate_dci_intrafabric()
    # end test_dci_intrafabric_lr_interconnect

# end TestAnsibleDciIntraFabric
