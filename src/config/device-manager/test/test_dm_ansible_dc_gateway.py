#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

from __future__ import absolute_import

from attrdict import AttrDict
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
import gevent
import mock
from vnc_api.vnc_api import FloatingIp, FloatingIpPool, InstanceIp, \
    IpamSubnetType, JunosServicePorts, LogicalRouter, NetworkIpam, \
    PhysicalInterface, SubnetType, VirtualMachineInterface, VirtualNetwork, \
    VirtualNetworkType, VirtualPortGroup, VnSubnetsType

from .test_dm_ansible_common import TestAnsibleCommonDM
# from vnc_api.gen.resource_client import *


class TestAnsibleDcGateway(TestAnsibleCommonDM):

    def setUp(self, extra_config_knobs=None):
        super(TestAnsibleDcGateway, self).setUp(
            extra_config_knobs=extra_config_knobs)
        self.idle_patch = mock.patch('gevent.idle')
        self.idle_mock = self.idle_patch.start()

    def tearDown(self):
        self.idle_patch.stop()
        super(TestAnsibleDcGateway, self).tearDown()

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

    @retries(5, hook=retry_exc_handler)
    def check_lr_internal_vn_state(self, lr_obj):
        internal_vn_name = '__contrail_lr_internal_vn_' + lr_obj.uuid + '__'
        vn_fq = lr_obj.get_fq_name()[:-1] + [internal_vn_name]
        vn_obj = None
        vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq)
        vn_obj_properties = vn_obj.get_virtual_network_properties()
        if not vn_obj_properties:
            raise Exception("LR Internal VN properties are not set")
        fwd_mode = vn_obj_properties.get_forwarding_mode()
        if fwd_mode != 'l3':
            raise Exception("LR Internal VN Forwarding mode is not set to L3")
        return vn_obj
    # end check_lr_internal_vn_state

    def _init_fabric_prs(self):
        self.features = {}
        self.role_definitions = {}
        self.feature_configs = []

        self.job_templates = []
        self.fabrics = []
        self.bgp_routers = []
        self.node_profiles = []
        self.role_configs = []
        self.physical_routers = []
    # end _init_fabric_prs

    def _create_node_profile(self, name, device_family, role, rb_roles,
                             job_temp):
        np1, rc1 = self.create_node_profile(
            'node-profile-' + name,
            device_family=device_family,
            role_mappings=[
                AttrDict({
                    'physical_role': role,
                    'rb_roles': rb_roles
                })
            ],
            job_template=job_temp)
        self.node_profiles.append(np1)
        self.role_configs.append(rc1)
        return np1, rc1
    # end _create_node_profile

    def _create_fabrics_prs(self, dict_prs, name="DC"):
        self._init_fabric_prs()
        self.create_features(['dc-gateway'])
        self.create_physical_roles(['spine'])
        ov_roles = ['dc-gateway']

        self.create_overlay_roles(ov_roles)

        role = 'spine'
        spine_rb_role = 'dc-gateway'
        self.create_role_definitions([
            AttrDict({
                'name': 'dc-gateway@spine',
                'physical_role': 'spine',
                'overlay_role': spine_rb_role,
                'features': ['dc-gateway'],
                'feature_configs': {}
            })
        ])

        jt = self.create_job_template('job-template-' + name + self.id())
        self.job_templates.append(jt)
        fabric = self.create_fabric('fab-' + name + self.id())
        self.fabrics.append(fabric)

        np, rc = self._create_node_profile(name + self.id(),
                                           'junos-qfx', 'spine',
                                           ['dc-gateway'], jt)
        num = 22
        for prname in dict_prs.keys():
            br, pr = self.create_router(
                prname + self.id(), '7.7.7.7',
                product='qfx10002' if 'qfx' in prname else 'mx240',
                family='junos-qfx' if 'qfx' in prname else 'junos',
                role='spine',
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
        return
    # end _create_fabrics_prs

    def _create_fabrics_prs_fip_snat(self, dict_prs, qfx_pi_name="xe-0/0/0",
                                     name="DC"):
        self._init_fabric_prs()
        self.create_features(['dc-gateway'])
        self.create_physical_roles(['leaf', 'spine'])
        ov_roles = ['crb-access', 'dc-gateway']

        self.create_overlay_roles(ov_roles)

        self.create_role_definitions([
            AttrDict({
                'name': 'dc-gateway@spine',
                'physical_role': 'spine',
                'overlay_role': 'dc-gateway',
                'features': ['dc-gateway'],
                'feature_configs': {}
            }),
            AttrDict({
                'name': 'crb-access@leaf',
                'physical_role': 'leaf',
                'overlay_role': 'crb-access',
                'features': ['dc-gateway'],
                'feature_configs': {}
            })
        ])

        jt = self.create_job_template('job-template-' + name + self.id())
        self.job_templates.append(jt)
        fabric = self.create_fabric('fab-' + name + self.id())
        self.fabrics.append(fabric)
        np1, rc1 = self._create_node_profile(name + '-leaf' + self.id(),
                                             'junos-qfx', 'leaf',
                                             ['crb-access'], jt)
        np2, rc2 = self._create_node_profile(name + self.id(),
                                             'junos', 'spine',
                                             ['dc-gateway'], jt)
        num = 22
        for prname in dict_prs.keys():
            if 'qfx' in prname:
                br, pr = self.create_router(
                    prname + self.id(), '7.7.7.8',
                    product='qfx10002',
                    family='junos-qfx',
                    role='leaf',
                    rb_roles=['crb-access'],
                    physical_role=self.physical_roles['leaf'],
                    overlay_role=self.overlay_roles['crb-access'],
                    fabric=fabric, node_profile=np1)
            else:
                br, pr = self.create_router(
                    prname + self.id(), '7.7.7.7',
                    product='mx240',
                    family='junos',
                    role='spine',
                    rb_roles=['dc-gateway'],
                    physical_role=self.physical_roles['spine'],
                    overlay_role=self.overlay_roles['dc-gateway'],
                    fabric=fabric, node_profile=np2)
            pr.set_physical_router_loopback_ip('30.30.0.%s' % num)
            num += 1
            self._vnc_lib.physical_router_update(pr)
            if 'qfx' in prname:
                pi_name = qfx_pi_name
                qfx_pi_obj_1 = PhysicalInterface(pi_name, parent_obj=pr)
                self._vnc_lib.physical_interface_create(qfx_pi_obj_1)

            self.physical_routers.append(pr)
            self.bgp_routers.append(br)
            dict_prs[prname] = pr
        return fabric, qfx_pi_obj_1
    # end _create_fabrics_prs_fip_snat

    def create_vn_ipam(self, id):
        ipam1_obj = NetworkIpam('ipam' + '-' + id)
        ipam1_uuid = self._vnc_lib.network_ipam_create(ipam1_obj)
        return self._vnc_lib.network_ipam_read(id=ipam1_uuid)
    # end create_vn_ipam

    def create_vn_with_subnets(self, id, vn_name, ipam_obj, subnet,
                               subnetmask=24):
        vn_obj = VirtualNetwork(vn_name)
        vn_obj_properties = VirtualNetworkType()
        vn_obj_properties.set_vxlan_network_identifier(2000 + id)
        vn_obj_properties.set_forwarding_mode('l2_l3')

        vn_obj.set_virtual_network_properties(vn_obj_properties)
        vn_obj.add_network_ipam(ipam_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType(subnet, subnetmask))]))
        vn_uuid = self._vnc_lib.virtual_network_create(vn_obj)
        return vn_obj, self._vnc_lib.virtual_network_read(id=vn_uuid)
    # end create_vn_with_subnets

    def make_vn_name(self, subnet):
        return "VN_%s" % subnet

    def make_lr_name(self, subnet1, subnet2=None, subnet3=None):
        if subnet3 is not None:
            return "LR_%s_%s_%s" % (subnet1, subnet2, subnet3)
        if subnet2 is not None:
            return "LR_%s_%s" % (subnet1, subnet2)
        return "LR_%s" % subnet1

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
        lr.set_logical_router_type('vxlan-routing')
        lr_uuid = self._vnc_lib.logical_router_create(lr)

        # make sure internal is created
        try:
            self.check_lr_internal_vn_state(lr)
        except Exception:
            pass
        return lr, self._vnc_lib.logical_router_read(id=lr_uuid)
    # end create_lr

    def _make_ri_comments(self, vn_obj, vrf_mode, fwd_mode="L2-L3",
                          prefix=' Public'):
        return "/*%s Virtual Network: %s, UUID: %s, VRF Type: %s, " \
               "Forwarding Mode: %s */" % \
               (prefix, vn_obj.get_fq_name()[-1], vn_obj.get_uuid(),
                vrf_mode, fwd_mode)
    # end _make_ri_comments

    @retries(4, hook=retry_exc_handler)
    def _get_route_target(self, vn_obj):
        ri_list = vn_obj.get_routing_instances() or []
        if len(ri_list) == 0:
            print("RI of vn %s is empty!!" % vn_obj.get_fq_name()[-1])
            raise Exception("RI of vn %s is empty!!" %
                            vn_obj.get_fq_name()[-1])
        for ri in ri_list:
            ri_uuid = ri.get('uuid')
            riobj = self._vnc_lib.routing_instance_read(id=ri_uuid)
            if not riobj:
                continue
            rt_refs = riobj.get_route_target_refs() or []
            if len(rt_refs) == 0:
                print("RT of vn %s RI %s is empty!!" %
                      (vn_obj.get_fq_name()[-1], riobj.get_fq_name()[-1]))
                raise Exception("RT of vn %s RI %s is empty!!" %
                                (vn_obj.get_fq_name()[-1],
                                 riobj.get_fq_name()[-1]))
            for rt in rt_refs:
                return rt.get('to')[0]
        print("vn %s RT Empty!!" % (vn_obj.get_fq_name()[-1]))
        return ''
    # end _get_route_target

    def _get_vxlanid(self, vn_obj):
        vn_obj_properties = vn_obj.get_virtual_network_properties()
        if vn_obj_properties:
            return str(vn_obj_properties.get_vxlan_network_identifier())
        return 'None'
    # end _get_vxlanid

    def _make_abstract_cfg_ri(self, dict_vns, vn_prefixes, lr_obj):
        dict_eabs_ri = {}
        internal_vn_routing_inf = set()
        # Add lr_obj internal vn to dictionary temporary
        internal_vn_name = '__contrail_lr_internal_vn_' + lr_obj.get_uuid() +\
                           '__'
        vn_fq = lr_obj.get_fq_name()[:-1] + [internal_vn_name]
        i_vn_obj = self._vnc_lib.virtual_network_read(fq_name=vn_fq)
        dict_vns[internal_vn_name] = i_vn_obj
        ri_keys = ["comment", "export_targets", "virtual_network_mode",
                   "vxlan_id", "virtual_network_id", "import_targets",
                   "virtual_network_is_internal", "is_public_network",
                   "is_master", "routing_interfaces", "name"]
        for vn_name, vn_obj in dict_vns.items():
            name = vn_obj.get_fq_name()[-1]
            internal_vn = True if name.startswith(
                '__contrail_lr_internal_vn_') else False
            rt = self._get_route_target(vn_obj)
            vn_id = str(vn_obj.get_virtual_network_network_id())
            vxlan_id = self._get_vxlanid(vn_obj)
            l2_ri_name = "_contrail_%s-%s-%s" % (name, 'l2', vn_id)
            l3_ri_name = "_contrail_%s-%s-%s" % (name, 'l3', vn_id)
            irb_val = "irb.%s" % vn_id
            irb = {"name": irb_val}
            if internal_vn == False:
                l2_ri_values = [
                    self._make_ri_comments(vn_obj, 'L2'),
                    [rt], "l2-l3", vxlan_id, vn_id, [rt], internal_vn,
                    True, False, [irb], l2_ri_name]
                dict_eabs_ri[l2_ri_name] = {}
                for i in range(len(ri_keys)):
                    dict_eabs_ri[l2_ri_name][ri_keys[i]] = l2_ri_values[i]
                vn_mode = "l2-l3"
                vxlan_id = "None"
                ri_comments = self._make_ri_comments(vn_obj, 'L3')
                if irb_val not in internal_vn_routing_inf:
                    internal_vn_routing_inf.add(irb_val)
            else:
                vn_mode = "l3"
                ri_comments = self._make_ri_comments(vn_obj, 'L3', 'L3')

            l3_ri_values = [
                ri_comments, [rt], vn_mode, vxlan_id, vn_id, [rt],
                internal_vn, True, False, [irb], l3_ri_name]
            dict_eabs_ri[l3_ri_name] = {}
            for i in range(len(ri_keys)):
                if ri_keys[i] == 'routing_interfaces':
                    if l3_ri_name.startswith(
                            '_contrail___contrail_lr_internal_vn_'):
                        continue
                    else:
                        dict_eabs_ri[l3_ri_name]['interfaces'] = \
                            l3_ri_values[i]
                else:
                    dict_eabs_ri[l3_ri_name][ri_keys[i]] = l3_ri_values[i]
            dict_eabs_ri[l3_ri_name]["routing_instance_type"] = "vrf"
            dict_eabs_ri[l3_ri_name]["static_routes"] = vn_prefixes
            dict_eabs_ri[l3_ri_name]["prefixes"] = vn_prefixes
        for ri_name, ri_obj in dict_eabs_ri.items():
            if ri_name.startswith('_contrail___contrail_lr_internal_vn_'):
                del ri_obj["vxlan_id"]
                ri_obj['routing_interfaces'] = []
                for irb_vals in internal_vn_routing_inf:
                    ri_obj['routing_interfaces'].append({"name": irb_vals})
                break
        del dict_vns[internal_vn_name]
        return dict_eabs_ri
    # end _make_abstract_dc_gateway_ri

    def _get_ri_from_name(self, dc_gw, ri_name):
        for ri in dc_gw.get('routing_instances', []):
            if ri.get('name', '') == ri_name:
                return ri
        return None
    # end _get_ri_from_name

    def _get_rules_from_nat(self, ri_nat_rule, rule_name):
        for rule in ri_nat_rule.get('rules', []):
            if rule.get('name', '') == rule_name:
                return rule
        return None
    # end _get_ri_from_name

    def _get_firewall_filter_from_name(self, firewal_filters, ff_name):
        for ff in firewal_filters:
            if ff.get('name', '') == ff_name:
                return ff
        return None
    # end _get_firewall_filter_from_name

    def _get_firewall_terms_from_name(self, firewall_filter, t_name):
        for t in firewall_filter.get('terms', []):
            if t.get('name', '') == t_name:
                return t
        return None
    # end _get_firewall_terms_from_name

    def _match_prop(self, ri, e_ri, prop):
        e_val = e_ri.get(prop)
        val = ri.get(prop)
        # print('%s: %s: val : %s e_val: %s' %
        #      (ri.get('name', ' '), prop, val, e_val))
        if isinstance(val, list):
            for v in e_val:
                self.assertIn(v, val)
        else:
            self.assertEqual(val, e_val)
    # end _match_prop

    def _validate_abstract_cfg_dc_ri(self, e_abst_cfg_ri, dc_gw):
        for ri_name, e_ri in e_abst_cfg_ri.items():
            ri = self._get_ri_from_name(dc_gw, ri_name)
            self.assertIsNotNone(ri)
            for key, value in e_ri.items():
                self._match_prop(ri, e_ri, key)
    # end _validate_abstract_cfg_dc_ri

    @retries(4, hook=retry_exc_handler)
    def _validate_abstract_cfg_dc_gateway(self, pr_name, pr_obj,
                                          e_abst_cfg_ri, empty=True):
        pr_new_name = ''
        if 'qfx' in pr_name:
            pr_new_name = 'qfx10008' if \
                pr_obj.get_physical_router_product_name() == 'qfx10002' \
                else 'qfx10002'
        else:
            pr_new_name = 'mx240' if \
                pr_obj.get_physical_router_product_name() == 'mx80' \
                else 'mx80'

        pr_obj.set_physical_router_product_name(pr_new_name)
        self._vnc_lib.physical_router_update(pr_obj)
        gevent.sleep(2)

        ac1 = self.check_dm_ansible_config_push()
        dac = ac1.get('device_abstract_config')
        self.assertIsNotNone(dac.get('features'))
        dc_gw = dac.get('features').get('dc-gateway')
        self.assertIsNotNone(dc_gw)
        self.assertEqual(dc_gw.get('name'), "dc-gateway")
        if empty == True:
            self.assertIsNone(dc_gw.get('routing_instances'))
            self.assertIsNone(dc_gw.get('firewall'))
            self.assertIsNone(dc_gw.get('physical_interfaces'))
            return
        self._validate_abstract_cfg_dc_ri(e_abst_cfg_ri, dc_gw)
    # end _validate_abstract_cfg_dc_gateway

    def _create_and_validate_dc_lr(self):
        """Validate LR and public LR dc-gateway abstract config.

        It executes following steps in sequences
        - create single fabric with 2 PR, QFX and MX and marked both
        as a  DC-gateway.
        - create 3 VN with different subnets.
        - create LR with 3 VN and associate it with both PR.
        - validate both PR dc-gateway abstract config must be empty
        - mark LR as a public LR.
        - validate both PR has valid dc-gateway config.
        - delete LR.
        - delete 3 VN.
        - cleanup all config.
        - cleanup all config.
        : Args:
        : self: current instance of class
        : return: None
        :
        """
        dict_vns = {}
        dict_vmis = {}
        dict_prs = {"PR1_qfx": None, "PR2_mx": None}
        ipam_obj = self.create_vn_ipam(self.id())
        subnetmask = 24
        vn_starting_index = 67

        self._create_fabrics_prs(dict_prs)

        # create all 3 VN and single LRs
        vn_prefixes = []
        for i in range(vn_starting_index, vn_starting_index + 3):
            subnet = "%s.1.1.0" % i
            vn_name = self.make_vn_name(i)
            _, dict_vns[vn_name] = self.create_vn_with_subnets(
                i, vn_name, ipam_obj, subnet, subnetmask)
            vn_prefixes.append({'prefix': subnet, 'prefix_len': subnetmask})
        vn_num = vn_starting_index

        # create src lr
        src_lr_name = self.make_lr_name(vn_num, vn_num + 1, vn_num + 2)
        src_vns = [dict_vns[self.make_vn_name(vn_num)],
                   dict_vns[self.make_vn_name(vn_num + 1)],
                   dict_vns[self.make_vn_name(vn_num + 2)]]
        _, lr_obj = self.create_lr(
            src_lr_name, src_vns, [dict_prs["PR1_qfx"], dict_prs["PR2_mx"]],
            dict_vmis)

        lr_obj.set_logical_router_gateway_external(False)
        self._vnc_lib.logical_router_update(lr_obj)

        # validate abstract config of dc-gateway feature empty
        for pr_name in dict_prs.keys():
            print("Verifying Non Public LR Config on %s" % (pr_name))
            self._validate_abstract_cfg_dc_gateway(
                pr_name, dict_prs[pr_name], None, empty=True)

        # mark LR as Public LR and validate dc-gateway config
        lr_obj.set_logical_router_gateway_external(True)
        self._vnc_lib.logical_router_update(lr_obj)
        e_abst_cfg_ri = self._make_abstract_cfg_ri(dict_vns, vn_prefixes,
                                                   lr_obj)
        for pr_name in dict_prs.keys():
            print("Verifying Public LR Config on %s" % (pr_name))
            self._validate_abstract_cfg_dc_gateway(
                pr_name, dict_prs[pr_name], e_abst_cfg_ri, empty=False)
        # cleanup
        if lr_obj:
            self._vnc_lib.logical_router_delete(id=lr_obj.get_uuid())
        for vminame, vmiobj in dict_vmis.items():
            self._vnc_lib.virtual_machine_interface_delete(
                id=vmiobj.get_uuid())
        for vnname, vnobj in dict_vns.items():
            self._vnc_lib.virtual_network_delete(
                fq_name=vnobj.get_fq_name())
        self._vnc_lib.network_ipam_delete(id=ipam_obj.uuid)
        self._delete_objects()
    # end _create_and_validate_dc_lr

    def _create_fip_pool(self, name, vn_obj):
        fip_pool_obj = FloatingIpPool(name, vn_obj)
        uuid = self._vnc_lib.floating_ip_pool_create(fip_pool_obj)
        fip_pool_obj_rd = self._vnc_lib.floating_ip_pool_read(id=uuid)
        return fip_pool_obj, fip_pool_obj_rd
    # end _create_fip_pool

    def _create_vmi(self, name, vn_obj, sg_group_obj=None):
        fq_name = ['default-domain', 'default-project', name]
        vmi = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        vmi.set_display_name(name)
        vmi.name = name
        vmi.set_virtual_network(vn_obj)
        if sg_group_obj:
            vmi.set_security_group(sg_group_obj)
        uuid = self._vnc_lib.virtual_machine_interface_create(vmi)
        vmi_obj_rd = self._vnc_lib.virtual_machine_interface_read(id=uuid)
        return vmi, vmi_obj_rd
    # end _create_vmi

    def _create_instance_ip(self, name, inst_ip, vmi_obj, vn_obj):
        instance_ip_obj = InstanceIp(name=name)
        instance_ip_obj.set_instance_ip_address(inst_ip)
        instance_ip_obj.set_virtual_machine_interface(vmi_obj)
        instance_ip_obj.set_virtual_network(vn_obj)

        uuid = self._vnc_lib.instance_ip_create(instance_ip_obj)
        instance_ip_obj_rd = self._vnc_lib.instance_ip_read(id=uuid)
        return instance_ip_obj, instance_ip_obj_rd
    # end _create_instance_ip

    def _create_floating_ip(self, name, fip_pool, pub_vmi, pri_vmi, fip_ip):
        fip_obj = FloatingIp(name, fip_pool)
        if pri_vmi:
            fip_obj.add_virtual_machine_interface(pri_vmi)
        if pub_vmi:
            fip_obj.add_virtual_machine_interface(pub_vmi)
        fip_obj.set_floating_ip_address(fip_ip)
        default_project = self._vnc_lib.project_read(
            fq_name=[u'default-domain', u'default-project'])
        fip_obj.set_project(default_project)
        uuid = self._vnc_lib.floating_ip_create(fip_obj)
        fip_obj_rd = self._vnc_lib.floating_ip_read(id=uuid)
        return fip_obj, fip_obj_rd
    # end _create_floating_ip

    def _make_vmi_profile(self, vpg_name, phy_int_name, pr_obj, fabric_name):
        device_name = pr_obj.get_fq_name()[-1]
        return "{\"local_link_information\":[{\"vpg\":\"%s\",\"switch_id\":" \
               "\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\"" \
               ":\"%s\"}]}" % (vpg_name, phy_int_name, phy_int_name,
                               device_name, fabric_name)

    @retries(4, hook=retry_exc_handler)
    def _create_vpg(self, name, fabric, pi_obj, vlan_id, vport_vmi,
                    vmi_profile):

        vpg_obj = VirtualPortGroup(name, parent_obj=fabric)
        vpg_obj.name = name
        vpg_obj.set_display_name(name)
        uuid = self._vnc_lib.virtual_port_group_create(vpg_obj)
        vpg_obj_rd = self._vnc_lib.virtual_port_group_read(id=uuid)

        vpg_obj.add_physical_interface(pi_obj)
        vpg_obj.add_virtual_machine_interface(vport_vmi)
        self._vnc_lib.virtual_port_group_update(vpg_obj)

        # update vmi
        vmi_bindings = {
            "key_value_pair": [
                {"key": "vpg", "value": name},
                {"key": "vnic_type", "value": "baremetal"},
                {"key": "vif_type", "value": "vrouter"},
                {"key": "profile", "value": vmi_profile},
                {"key": "tor_port_vlan_id", "value": str(vlan_id)}]}
        vport_vmi.set_virtual_machine_interface_bindings(vmi_bindings)
        vmi_properties = {"sub_interface_vlan_tag": str(vlan_id)}
        vport_vmi.set_virtual_machine_interface_properties(vmi_properties)
        self._vnc_lib.virtual_machine_interface_update(vport_vmi)

        return vpg_obj, vpg_obj_rd
    # end _create_vpg

    @retries(4, hook=retry_exc_handler)
    def _validate_abstract_cfg_fip_snat(self, pr_obj, e_abst_cfg_ri,
                                        dict_nat_ri, dict_eabs_firewall):
        if pr_obj.get_display_name() == pr_obj.name:
            new_disp_name = "%s_changed" % pr_obj.name
        else:
            new_disp_name = pr_obj.name
        # print("PR name %s\n Display Name %s\n changing to %s" %
        #      (pr_obj.name, display_name, new_disp_name))
        pr_obj.set_display_name(new_disp_name)
        self._vnc_lib.physical_router_update(pr_obj)
        gevent.sleep(2)

        ac1 = self.check_dm_ansible_config_push()
        dac = ac1.get('device_abstract_config')
        self.assertIsNotNone(dac.get('features'))
        dc_gw = dac.get('features').get('dc-gateway')
        self.assertIsNotNone(dc_gw)
        self.assertEqual(dc_gw.get('name'), "dc-gateway")
        # validate routing_instances abstract config
        for ri_name, e_ri in e_abst_cfg_ri.items():
            ri = self._get_ri_from_name(dc_gw, ri_name)
            self.assertIsNotNone(ri)
            for key, value in e_ri.items():
                self._match_prop(ri, e_ri, key)

        for ri_name, e_ri in dict_nat_ri.items():
            ri = self._get_ri_from_name(dc_gw, ri_name)
            self.assertIsNotNone(ri)
            for key, value in e_ri.items():
                if key == "nat_rules":
                    nat_rule = ri.get('nat_rules')
                    self.assertIsNotNone(nat_rule)
                    e_nat_rule = e_ri.get(key) or {}
                    for nk, nv in e_nat_rule.items():
                        if nk == "rules":
                            for e_r in e_nat_rule.get(nk):
                                r_name = e_r.get('name')
                                rule = self._get_rules_from_nat(
                                    nat_rule, r_name)
                                self.assertIsNotNone(rule)
                                for rk, rv in e_r.items():
                                    self._match_prop(rule, e_r, rk)
                        else:
                            self._match_prop(nat_rule, e_nat_rule, nk)
                else:
                    self._match_prop(ri, e_ri, key)
        # validate firewall abstract config
        firewall = dc_gw.get('firewall')
        self.assertIsNotNone(firewall)
        self.assertEqual(firewall.get('comment'),
                         "/* Firewalls Configuration */")
        self.assertEqual(firewall.get('family'), "inet")
        firewal_filters = firewall.get('firewall_filters')
        self.assertIsNotNone(firewal_filters)
        for ff_name, e_ff in dict_eabs_firewall.items():
            ff = self._get_firewall_filter_from_name(firewal_filters, ff_name)
            self.assertIsNotNone(ff)
            for key, value in e_ff.items():
                if key == 'terms':
                    for e_t in value:
                        t_name = e_t.get('name')
                        term = self._get_firewall_terms_from_name(ff, t_name)
                        self.assertIsNotNone(term)
                        for tk, tv in e_t.items():
                            self._match_prop(term, e_t, tk)
                else:
                    self._match_prop(ff, e_ff, key)
    # end _validate_abstract_cfg_fip_snat

    def _make_address_prefixes(self, addr, subnet_len):
        return [{"prefix": addr, "prefix_len": subnet_len}]

    def _make_fip_snat_abstract_cfg(self, dict_vns, service_port, fip_ip,
                                    inst_ip_vpg):
        dict_eabs_ri = {}
        dict_nat_ri = {}
        dict_eabs_firewall = {}
        ri_keys = ["comment", "export_targets", "virtual_network_mode",
                   "vxlan_id", "virtual_network_id", "import_targets",
                   "virtual_network_is_internal", "is_public_network",
                   "is_master", "routing_interfaces", "name"]
        nat_rule_keys = ["comment", "outside_interface",
                         "inside_interface", "name",
                         "allow_overlapping_nat_pools"]
        rules_key = ["comment", "direction", "name",
                     "source_prefixes", "translation_type",
                     "source_addresses"]
        firewall_t_keys = ["name", "comment", "terms"]
        terms_keys = ["then", "name", "fromxx"]
        for vn_name, vn_obj in dict_vns.items():
            name = vn_obj.get_fq_name()[-1]
            private_vn = True if 'private' in name else False
            prefix = ' Private' if private_vn else ' Public'
            public_network = False if private_vn else True
            rt = self._get_route_target(vn_obj)
            vn_id = str(vn_obj.get_virtual_network_network_id())
            vxlan_id = self._get_vxlanid(vn_obj)
            l2_ri_name = "_contrail_%s-%s-%s" % (name, 'l2', vn_id)
            l3_ri_name = "_contrail_%s-%s-%s" % (name, 'l3', vn_id)
            irb_val = "irb.%s" % vn_id

            irb = {"name": irb_val}
            l2_ri_values = [
                self._make_ri_comments(vn_obj, 'L2', 'L2-L3', prefix),
                [rt], "l2-l3", vxlan_id, vn_id, [rt], False,
                public_network, False, [irb], l2_ri_name
            ]
            dict_eabs_ri[l2_ri_name] = {}
            for i in range(len(ri_keys)):
                if len(rt) == 0 and (ri_keys[i] == 'export_targets' or
                                     ri_keys[i] == 'import_targets'):
                    continue
                dict_eabs_ri[l2_ri_name][ri_keys[i]] = l2_ri_values[i]

            l3_ri_values = [
                self._make_ri_comments(vn_obj, 'L3', 'L2-L3', prefix),
                [rt], "l2-l3", "None", vn_id, [rt], False,
                public_network, False, [irb], l3_ri_name
            ]
            vn_prefix = [{'prefix': "66.0.0.0" if private_vn else "60.0.0.0",
                          'prefix_len': 24}]
            dict_eabs_ri[l3_ri_name] = {}
            for i in range(len(ri_keys)):
                if len(rt) == 0 and (ri_keys[i] == 'export_targets' or
                                     ri_keys[i] == 'import_targets'):
                    continue
                if ri_keys[i] == "routing_interfaces":
                    dict_eabs_ri[l3_ri_name]["interfaces"] = l3_ri_values[i]
                else:
                    dict_eabs_ri[l3_ri_name][ri_keys[i]] = l3_ri_values[i]

            dict_eabs_ri[l3_ri_name]['routing_instance_type'] = 'vrf'
            dict_eabs_ri[l3_ri_name]["static_routes"] = vn_prefix
            dict_eabs_ri[l3_ri_name]["prefixes"] = vn_prefix

            # Add NAT RI for private VN
            if private_vn:
                nat_ri_name = "%s-nat" % (l3_ri_name)
                i_interface = "%s.9" % service_port
                o_interface = "%s.10" % service_port
                nat_ri_values = [
                    self._make_ri_comments(vn_obj, 'L3 (NAT)', 'L3', ''),
                    [rt], "l3", "None", vn_id, [rt], False,
                    public_network, False, [irb], nat_ri_name
                ]
                dict_nat_ri[nat_ri_name] = {}
                for i in range(len(ri_keys)):
                    if ri_keys[i] == 'export_targets':
                        continue
                    if len(rt) == 0 and ri_keys[i] == 'import_targets':
                        continue
                    dict_nat_ri[nat_ri_name][ri_keys[i]] = nat_ri_values[i]

                dict_nat_ri[nat_ri_name]['routing_instance_type'] = 'vrf'
                dict_nat_ri[nat_ri_name]["ingress_interfaces"] = \
                    [{"name": i_interface}]
                dict_nat_ri[nat_ri_name]["egress_interfaces"] = \
                    [{"name": o_interface}]
                # dict_nat_ri[nat_ri_name]["interfaces"] = {"name": interface}
                nat_rule_values = [
                    "/* Virtual Network: %s, UUID: %s */" %
                    (vn_obj.get_fq_name()[-1], vn_obj.get_uuid()),
                    o_interface, i_interface, "sv-_contrail_vn_private", True
                ]
                dict_nat = {}
                for i in range(len(nat_rule_keys)):
                    dict_nat[nat_rule_keys[i]] = nat_rule_values[i]

                rules1_values = [
                    "/* Traffic Inbound Rule */", "input",
                    "sv-_contrail_vn_private-sn-rule",
                    self._make_address_prefixes(fip_ip, 32), "basic-nat44",
                    self._make_address_prefixes(inst_ip_vpg, 32)]
                rules2_values = [
                    "/* Traffic Outbound Rule */", "output",
                    "sv-_contrail_vn_private-dn-rule",
                    self._make_address_prefixes(inst_ip_vpg, 32), "dnat-44",
                    self._make_address_prefixes(fip_ip, 32)]

                dict_rules1 = {}
                dict_rules2 = {}
                for i in range(len(rules_key)):
                    dict_rules1[rules_key[i]] = rules1_values[i]
                    if rules_key[i].startswith('source_'):
                        outbound_key = rules_key[i].replace(
                            'source', 'destination')
                        dict_rules2[outbound_key] = rules2_values[i]
                    else:
                        dict_rules2[rules_key[i]] = rules2_values[i]
                dict_nat["rules"] = [dict_rules1, dict_rules2]
                dict_nat_ri[nat_ri_name]["nat_rules"] = dict_nat

        # make firewal for current vn
        if private_vn:
            fname = "redirect-to-_contrail_%s-%s-%s-nat-vrf" % \
                    (name, 'l3', vn_id)
            tname = "_contrail_%s-l3-5-nat" % name
            addr = [{'prefix': inst_ip_vpg, 'prefix_len': 32}]
            terms_val_1 = [{"routing_instance": [tname]},
                           "term-%s" % tname,
                           {"source_address": addr}]
            dict_terms_val_1 = {}
            for i in range(len(terms_keys)):
                dict_terms_val_1[terms_keys[i]] = terms_val_1[i]
            firewall_t_vals = [
                fname,
                "/* fip: Virtual Network: %s, UUID: %s, Filter Type: "
                "private */" % (name, vn_obj.get_uuid()),
                [dict_terms_val_1]
            ]
        else:
            fname = "_contrail_redirect-to-public-vrfs-inet4"
            tname = "_contrail_%s-l3-%s" % (name, vn_id)
            addr = [{'prefix': "60.0.0.0", 'prefix_len': 24}]
            terms_val_1 = [{"routing_instance": [tname]},
                           "term-%s" % tname,
                           {"destination_address": addr}]
            terms_val_2 = [{"accept_or_reject": True},
                           "default-term", ""]
            dict_terms_val_1 = {}
            dict_terms_val_2 = {}
            for i in range(len(terms_keys)):
                dict_terms_val_1[terms_keys[i]] = terms_val_1[i]
                if terms_keys[i] != 'fromxx':
                    dict_terms_val_2[terms_keys[i]] = terms_val_2[i]
            firewall_t_vals = [
                fname,
                "/* fip: Public VRF Filter for Floating IPs */",
                [dict_terms_val_1, dict_terms_val_2]
            ]
        dict_eabs_firewall[fname] = {}
        for i in range(len(firewall_t_keys)):
            dict_eabs_firewall[fname][firewall_t_keys[i]] =\
                firewall_t_vals[i]

        return dict_eabs_ri, dict_nat_ri, dict_eabs_firewall
    # end _make_fip_snat_abstract_cfg

    def _create_and_validate_dc_fip_snat(self):
        """Validate mx fip and snat dc-gateway abstract config.

        It executes following steps in sequences
        - create single fabric with 2 PR, QFX as leaf and MX as spine
        dcgateway.
        - create 2 VN with different subnets, one for private vn and
        second for public VN.
        - Create FIP pool.
        - create Security Group for NAT rules of Public network igress
        and egress rules.
        - Create VPG using private VN on QFX leaf device physical interface.
        - Edit Virtual Port of this VPG with Security Group, Floating public
        IP from FIP pool and Fixed IP from private VN.
        - on MX Device, set Service interface and associate Public VN.
        - Edit Public VN with Fip pool and extend it to MX PR Device.
        - Validate abstract config for dc-gateway on MX device.
        - delete 2 VN.
        - delete VPG and Virtual Port
        - cleanup all config.
        : Args:
        : self: current instance of class
        : return: None
        :
        """
        dict_vns = {}
        dict_prs = {"PR1_qfx": None, "PR2_mx": None}
        ipam_obj = self.create_vn_ipam(self.id())
        subnetmask = 24
        fip_ip = "60.0.0.4"
        inst_ip_fip = "60.0.0.5"  # Public VN subnet Fixed ip in virtual port
        inst_ip_vpg = "66.0.0.3"  # Private VN subnet Fixed ip in virtual port

        pi_name = "xe-0/0/0"
        fabric, qfx_pi_obj_1 = self._create_fabrics_prs_fip_snat(
            dict_prs, qfx_pi_name=pi_name)

        # create all 2 VN and single LRs
        vn_prefixes = []
        pri_vnname = 'vn_private_66'
        pri_vn_rd, dict_vns[pri_vnname] = self.create_vn_with_subnets(
            66, pri_vnname, ipam_obj, "66.0.0.0", subnetmask)
        vn_prefixes.append({'prefix': "66.0.0.0", 'prefix_len': subnetmask})

        pub_vnname = 'vn_public_60'
        pub_vn_rd, dict_vns[pub_vnname] = self.create_vn_with_subnets(
            60, pub_vnname, ipam_obj, "60.0.0.0", subnetmask)
        vn_prefixes.append({'prefix': "60.0.0.0", 'prefix_len': subnetmask})

        # create Fip pool
        fip_pool, fip_pool_rd = self._create_fip_pool(
            name='vn_public_fip_pool', vn_obj=dict_vns[pub_vnname])

        # for floatingIp, create 2 vim, one for pub fip and other for vpg
        pub_vmi, pub_vmi_rd = self._create_vmi('pub_vmi1',
                                               dict_vns[pub_vnname])
        pri_vmi, pri_vmi_rd = self._create_vmi('pri_vmi1',
                                               dict_vns[pri_vnname])

        # create 2 instance IP for public vn and private vn vpg
        pub_inst_ip, pub_inst_ip_rd = self._create_instance_ip(
            name='pub-instance-ip-1', inst_ip=inst_ip_fip, vmi_obj=pub_vmi,
            vn_obj=dict_vns[pub_vnname])
        pri_inst_ip, pri_inst_ip_rd = self._create_instance_ip(
            name='pri-instance-ip-1', inst_ip=inst_ip_vpg, vmi_obj=pri_vmi,
            vn_obj=dict_vns[pri_vnname])

        # create fip pool object
        fip_obj, fip_obj_rd = self._create_floating_ip(
            'fip1', fip_pool, pub_vmi, pri_vmi, fip_ip)

        # create VPG and its Virtual Port (VMI)
        vpg_vlan_id = 12
        vmi_profile = self._make_vmi_profile(
            "VPG1", pi_name, dict_prs["PR1_qfx"], 'fab-DC' + self.id())
        vpg_obj, vpg_obj_rd = self._create_vpg(
            "VPG1", fabric, qfx_pi_obj_1, vpg_vlan_id, pri_vmi, vmi_profile)

        # - on MX Device, set Service interface and associate Public VN
        service_port = "si-1/2/0"
        pr_mx = dict_prs["PR2_mx"]
        pr_mx.set_virtual_network(dict_vns[pub_vnname])
        junos_service_ports = JunosServicePorts()
        junos_service_ports.service_port.append(service_port)
        pr_mx.set_physical_router_junos_service_ports(junos_service_ports)

        self._vnc_lib.physical_router_update(pr_mx)

        # - Edit Public VN with Fip pool and extend it to MX PR Device
        dict_vns[pub_vnname].set_router_external(True)
        self._vnc_lib.virtual_network_update(dict_vns[pub_vnname])

        # validate abstract config of dc-gateway feature empty
        pr_name = "PR2_mx"
        print("Verifying fip and snat DC config on %s" % (pr_name))
        e_abst_cfg_ri, dict_nat_ri, dict_eabs_firewall = \
            self._make_fip_snat_abstract_cfg(dict_vns, service_port, fip_ip,
                                             inst_ip_vpg)
        self._validate_abstract_cfg_fip_snat(
            dict_prs[pr_name], e_abst_cfg_ri, dict_nat_ri, dict_eabs_firewall)

        # cleanup
        vpg_obj.set_physical_interface_list([])
        vpg_obj.set_virtual_machine_interface_list([])
        self._vnc_lib.virtual_port_group_update(vpg_obj)

        self._vnc_lib.floating_ip_delete(id=fip_obj.get_uuid())
        self._vnc_lib.instance_ip_delete(id=pub_inst_ip.get_uuid())
        self._vnc_lib.instance_ip_delete(id=pri_inst_ip.get_uuid())

        self._vnc_lib.virtual_machine_interface_delete(id=pub_vmi.get_uuid())
        self._vnc_lib.virtual_machine_interface_delete(id=pri_vmi.get_uuid())
        self._vnc_lib.floating_ip_pool_delete(id=fip_pool.get_uuid())

        self._vnc_lib.physical_interface_delete(id=qfx_pi_obj_1.get_uuid())
        self._vnc_lib.virtual_port_group_delete(id=vpg_obj.get_uuid())

        pr_mx.set_virtual_network_list([])
        self._vnc_lib.physical_router_update(pr_mx)

        for vnname, vnobj in dict_vns.items():
            self._vnc_lib.virtual_network_delete(
                id=vnobj.get_uuid())
        self._vnc_lib.network_ipam_delete(id=ipam_obj.uuid)
        self._delete_objects()
    # end _create_and_validate_dc_fip_snat

    def test_dc_gateway_public_lr(self):
        self._create_and_validate_dc_lr()
    # end test_dc_gateway_public_lr

    def test_dc_gateway_fip_snat(self):
        self._create_and_validate_dc_fip_snat()
    # end test_dc_gateway_fip_snat

# end TestAnsibleDcGateway
