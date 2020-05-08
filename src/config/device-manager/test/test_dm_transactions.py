#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#
from __future__ import absolute_import
import time
from unittest import skip
from attrdict import AttrDict
from cfgm_common.tests.test_common import retries
from cfgm_common.tests.test_common import retry_exc_handler
from vnc_api.vnc_api import *
from vnc_api.gen.resource_client import *
from .test_dm_ansible_common import TestAnsibleCommonDM
from .test_dm_utils import FakeJobHandler

class TestTransactionsDM(TestAnsibleCommonDM):

    def setUp(self, extra_config_knobs=None):
        super(TestTransactionsDM, self).setUp(extra_config_knobs=extra_config_knobs)
        self.create_all()

    def tearDown(self):
        self.delete_all()
        super(TestTransactionsDM, self).tearDown()

    @retries(5, hook=retry_exc_handler)
    def chk_trans_info(self, trans_id=None, pr_name=None, trans_descr=None):
        if pr_name:
            t_id, t_descr = FakeJobHandler.get_dev_transaction_info(pr_name)
        else:
            t_id, t_descr = FakeJobHandler.get_transaction_info()
        self.assertEqual(trans_descr, t_descr)
        if trans_id:
            self.assertEqual(trans_id, t_id)
        print ("TRANSACTION: {}".format(trans_descr))
        return t_id, t_descr

    def check_trans_info(self, obj_type=None, oper=None, obj_name=None,
                         trans_id=None, pr_name=None, trans_descr=None):
        trans_descr = trans_descr or \
                      "{} '{}' {}".format(obj_type, obj_name, oper)
        time.sleep(1)
        return self.chk_trans_info(trans_id=trans_id, pr_name=pr_name,
                                   trans_descr=trans_descr)

    def test_update_bgp_router(self):
        self.bgp_router1.set_display_name("foo")
        self._vnc_lib.bgp_router_update(self.bgp_router1)
        self.check_trans_info('Bgp Router', 'Update', self.bgp_router1.name,
                              pr_name=self.bgp_router1.name)

    def test_create_logical_router(self):
        lr_name = 'lr-' + self.id()
        lr_fq_name = ['default-domain', 'default-project', lr_name]
        lr = LogicalRouter(fq_name=lr_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing',
                           vxlan_network_identifier='5000')
        lr.set_physical_router(self.pr1)
        self._vnc_lib.logical_router_create(lr)
        self.check_trans_info('Logical Router', 'Create', lr_name,
                              pr_name=self.pr1.name)

        lr.add_virtual_machine_interface(self.vmi)

        self._vnc_lib.logical_router_update(lr)
        self.check_trans_info('Logical Router', 'Update', lr_name,
                              pr_name=self.pr1.name)

        self._vnc_lib.logical_router_delete(id=lr.uuid)
        self.check_trans_info('Logical Router', 'Delete', lr_name,
                              pr_name=self.pr1.name)

    @skip("timing issues")
    def test_create_vpg(self):
        device_name = self.pr1.get_fq_name()[-1]
        fabric_name = self.fabric.get_fq_name()[-1]
        phy_int_name = self.pi1_0.get_fq_name()[-1]

        vpg_name = "vpg-" + self.id()
        vlan_tag = 10

        vmi = VirtualMachineInterface(
            vpg_name + "-tagged-" + str(vlan_tag),
            parent_type='project',
            fq_name = ["default-domain", "default-project",
                       vpg_name + "-tagged-" + str(vlan_tag)])

        vmi_profile = \
            "{\"local_link_information\":[{\"switch_id\":\"%s\",\"port_id\":\"%s\",\"switch_info\":\"%s\",\"fabric\":\"%s\"}]}" % \
            (phy_int_name, phy_int_name, device_name, fabric_name)

        vmi_bindings = {
            "key_value_pair": [{
                "key": "vnic_type",
                "value": "baremetal"
            }, {
                "key": "vif_type",
                "value": "vrouter"
            }, {
                "key": "vpg",
                "value": vpg_name
            }, {
                "key": "profile",
                "value": vmi_profile
            }]
        }

        vmi.set_virtual_machine_interface_bindings(vmi_bindings)

        vmi_properties = {
                "sub_interface_vlan_tag": vlan_tag
            }
        vmi.set_virtual_machine_interface_properties(vmi_properties)

        vmi.set_virtual_network(self.vn1)

        sg_name = 'sg-1' + self.id()
        project = self._vnc_lib.project_read(
            ['default-domain', 'default-project'])
        sg = SecurityGroup(name=sg_name, parent_obj=project)
        self._vnc_lib.security_group_create(sg)

        # now create a VPG
        vpg = VirtualPortGroup(vpg_name, parent_obj=self.fabric)
        vpg.set_physical_interface(self.pi1_0)
        vpg.set_security_group(sg)
        self._vnc_lib.virtual_port_group_create(vpg)
        self._vnc_lib.virtual_machine_interface_create(vmi)
        self.check_trans_info('Virtual Port Group', 'Create', vpg_name)

        # Update VPG
        vpg.set_virtual_machine_interface_list([{'uuid': vmi.get_uuid()}])
        self._vnc_lib.virtual_port_group_update(vpg)
        self.check_trans_info('Virtual Port Group', 'Update', vpg_name)

        # Update Security Group
        sg = self._vnc_lib.security_group_read(sg.get_fq_name())
        rule1 = self.build_acl_rule(0, 65535, 'egress', 'icmp', 'IPv4')
        sg_rule1 = self._security_group_rule_build(rule1,
                                                   sg.get_fq_name_str())
        self._security_group_rule_append(sg, sg_rule1)
        self._vnc_lib.security_group_update(sg)
        self.check_trans_info('Security Group', 'Update', sg_name)

        self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
        self.check_trans_info('Virtual Port Group', 'Delete', vpg_name)

        self._vnc_lib.virtual_port_group_delete(id=vpg.uuid)

    def test_create_dci(self):
        dci_name = "test-dci" + self.id()
        dci = DataCenterInterconnect(dci_name)
        dci.add_logical_router(self.lr1)
        self._vnc_lib.data_center_interconnect_create(dci)
        self.check_trans_info('DCI', 'Create', dci_name,
                              pr_name=self.pr1.name)

        dci.del_logical_router(self.lr1)
        self._vnc_lib.data_center_interconnect_update(dci)
        self.check_trans_info('DCI', 'Update', dci_name,
                              pr_name=self.pr1.name)

        dci.add_logical_router(self.lr1)
        self._vnc_lib.data_center_interconnect_update(dci)
        self._vnc_lib.data_center_interconnect_delete(fq_name=dci.fq_name)
        self.check_trans_info('DCI', 'Delete', dci_name,
                              pr_name=self.pr1.name)

    def test_create_service_instance(self):
        sas_name = 'sas-1' + self.id()
        sas_fq_name = ['default-global-system-config', sas_name]
        sas = ServiceApplianceSet(fq_name=sas_fq_name,
                                  parent_type='global-system-config',
                                  service_appliance_set_virtualization_type='physical-device')
        self._vnc_lib.service_appliance_set_create(sas)

        sa_name = 'sa-1' + self.id()
        sa_fq_name = ['default-global-system-config', sas_name, sa_name]
        sa = ServiceAppliance(fq_name=sa_fq_name,
                               parent_type='service-appliance-set',
                               service_appliance_virtualization_type='physical-device')

        sa.set_service_appliance_properties(KeyValuePairs([
            KeyValuePair(key='left-attachment-point', value=self.pi1_0_fq),
            KeyValuePair(key='right-attachment-point', value=self.pi1_1_fq)
        ]))
        attr = ServiceApplianceInterfaceType(interface_type='left')
        sa.add_physical_interface(self.pi2_0, attr)
        attr = ServiceApplianceInterfaceType(interface_type='right')
        sa.add_physical_interface(self.pi2_1, attr)
        self._vnc_lib.service_appliance_create(sa)
        tid, td = self.check_trans_info(
            'Service Appliance', 'Create', sa_name, pr_name=self.pr1.name)

        st_name = 'st-1' + self.id()
        st_fq_name = ['default-domain', st_name]
        st = ServiceTemplate(fq_name=st_fq_name)
        st.set_service_appliance_set(sas)
        st.set_service_config_managed(False)
        svc_properties = ServiceTemplateType()
        svc_properties.set_service_virtualization_type('physical-device')
        if_type = ServiceTemplateInterfaceType(interface_type='left')
        svc_properties.add_interface_type(if_type)
        if_type = ServiceTemplateInterfaceType(interface_type='right')
        svc_properties.add_interface_type(if_type)
        st.set_service_template_properties(svc_properties)
        self._vnc_lib.service_template_create(st)
        self.check_trans_info(
            trans_id=tid, trans_descr=td, pr_name=self.pr1.name)

        si_name = 'si-' + self.id()
        si_fqn = ['default-domain', 'default-project', si_name]
        si = ServiceInstance(fq_name=si_fqn)
        si.fq_name = si_fqn
        si.add_service_template(st)
        kvp_array = []
        kvp = KeyValuePair("left-svc-vlan", "100")
        kvp_array.append(kvp)
        kvp = KeyValuePair("right-svc-vlan", "101")
        kvp_array.append(kvp)
        kvp = KeyValuePair("left-svc-asns", "66000,66001")
        kvp_array.append(kvp)
        kvp = KeyValuePair("right-svc-asns", "66000,66002")
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        si.set_annotations(kvps)
        props = ServiceInstanceType()
        props.set_service_virtualization_type('physical-device')
        props.set_ha_mode("active-standby")
        si.set_service_instance_properties(props)
        self._vnc_lib.service_instance_create(si)
        self.check_trans_info(
            trans_id=tid, trans_descr=td, pr_name=self.pr1.name)

        pt_name = 'pt-' + self.id()
        pt = PortTuple(pt_name, parent_obj=si)
        pt.add_logical_router(self.lr1)
        pt.add_logical_router(self.lr2)
        kvp_array = []
        kvp = KeyValuePair("left-lr", self.lr1.uuid)
        kvp_array.append(kvp)
        kvp = KeyValuePair("right-lr", self.lr2.uuid)
        kvp_array.append(kvp)
        kvps = KeyValuePairs()
        kvps.set_key_value_pair(kvp_array)
        pt.set_annotations(kvps)
        self._vnc_lib.port_tuple_create(pt)
        self.check_trans_info('Service Instance', 'Create', si_name,
                              pr_name=self.pr1.name)

        self._vnc_lib.port_tuple_delete(id=pt.uuid)
        self.check_trans_info('Service Instance', 'Delete', si_name,
                              pr_name=self.pr1.name)

        self._vnc_lib.service_appliance_delete(fq_name=sa.fq_name)
        self.check_trans_info('Service Appliance', 'Delete', sa_name,
                              pr_name=self.pr1.name)

    def create_all(self):
        self.create_features(['underlay-ip-clos', 'overlay-bgp', 'l2-gateway',
                              'l3-gateway', 'vn-interconnect'])
        self.create_physical_roles(['leaf', 'spine', 'pnf'])
        self.create_overlay_roles(['crb-gateway', 'pnf-servicechain'])
        self.create_role_definitions([
            AttrDict(
                {
                    'name': 'crb-gateway-spine',
                    'physical_role': 'spine',
                    'overlay_role': 'crb-gateway',
                    'features': ['l3-gateway', 'vn-interconnect'],
                    'feature_configs': None
                }),
            AttrDict(
                {
                    'name': 'pnf-service-chain',
                    'physical_role': 'pnf',
                    'overlay_role': 'pnf-servicechain',
                    'features': ['l3-gateway', 'vn-interconnect'],
                    'feature_configs': None
                })
        ])

        self.jt = self.create_job_template('job-template-1' + self.id())
        self.fabric = self.create_fabric('fabric-1' + self.id())
        self.np1, self.rc1 = self.create_node_profile(
            'node-profile-1' + self.id(),
            device_family='junos-qfx',
            role_mappings=[
                AttrDict(
                    {
                        'physical_role': 'spine',
                        'rb_roles': ['crb-gateway']
                    }
                )
            ],
            job_template=self.jt
        )
        self.np2, self.rc2 = self.create_node_profile(
            'node-profile-2' + self.id(),
            device_family='junos-srx',
            role_mappings=[
                AttrDict(
                    {
                        'physical_role': 'pnf',
                        'rb_roles': ['pnf-servicechain']
                    }
                )
            ],
            job_template=self.jt
        )

        rtr1_name = 'router1' + self.id()
        self.bgp_router1, self.pr1 = self.create_router(rtr1_name, '1.1.1.1',
                                   product='qfx10008', family='junos-qfx',
                                   role='spine',
                                   rb_roles=['crb-gateway', 'DCI-Gateway'],
                                   physical_role=self.physical_roles['spine'],
                                   overlay_role=self.overlay_roles[
                                       'crb-gateway'], fabric=self.fabric,
                                   node_profile=self.np1)
        rtr2_name = 'router2' + self.id()
        self.bgp_router2, self.pr2 = self.create_router(rtr2_name, '1.1.1.2',
                                   product='qfx10008', family='junos-qfx',
                                   role='pnf', rb_roles=['PNF-Servicechain'],
                                   physical_role=self.physical_roles['pnf'],
                                   overlay_role=self.overlay_roles[
                                       'pnf-servicechain'], fabric=self.fabric,
                                   node_profile=self.np2)

        pi1_0_name = "xe-0/0/0"
        self.pi1_0 = PhysicalInterface(pi1_0_name, parent_obj=self.pr1)
        self.pi1_0_fq = ':'.join(self.pi1_0.fq_name)
        self._vnc_lib.physical_interface_create(self.pi1_0)

        pi1_1_name = "xe-0/0/1"
        self.pi1_1 = PhysicalInterface(pi1_1_name, parent_obj=self.pr1)
        self.pi1_1_fq = ':'.join(self.pi1_1.fq_name)
        self._vnc_lib.physical_interface_create(self.pi1_1)

        pi2_0_name = "xe-0/0/0"
        self.pi2_0 = PhysicalInterface(pi2_0_name, parent_obj=self.pr2)
        self._vnc_lib.physical_interface_create(self.pi2_0)

        pi2_1_name = "xe-0/0/1"
        self.pi2_1 = PhysicalInterface(pi2_1_name, parent_obj=self.pr2)
        self._vnc_lib.physical_interface_create(self.pi2_1)

        self.vn1 = self.create_vn('1', '1.1.1.0')

        lr1_name = 'lr1-' + self.id()
        lr1_fq_name = ['default-domain', 'default-project', lr1_name]
        self.lr1 = LogicalRouter(fq_name=lr1_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing',
                           vxlan_network_identifier='3000')
        self.lr1.set_physical_router(self.pr1)
        self._vnc_lib.logical_router_create(self.lr1)

        lr2_name = 'lr2-' + self.id()
        lr2_fq_name = ['default-domain', 'default-project', lr2_name]
        self.lr2 = LogicalRouter(fq_name=lr2_fq_name, parent_type='project',
                           logical_router_type='vxlan-routing',
                           vxlan_network_identifier='4000')
        self.lr2.set_physical_router(self.pr2)
        self._vnc_lib.logical_router_create(self.lr2)

        fq_name = ['default-domain', 'default-project', 'vmi-' + self.id()]
        self.vmi = VirtualMachineInterface(fq_name=fq_name, parent_type='project')
        self.vmi.set_virtual_network(self.vn1)
        self._vnc_lib.virtual_machine_interface_create(self.vmi)

        self.lr1.add_virtual_machine_interface(self.vmi)
        self._vnc_lib.logical_router_update(self.lr1)

    def delete_all(self):
        self._vnc_lib.logical_router_delete(id=self.lr1.uuid)
        self._vnc_lib.logical_router_delete(id=self.lr2.uuid)

        self._vnc_lib.virtual_machine_interface_delete(id=self.vmi.uuid)

        self._vnc_lib.physical_interface_delete(id=self.pi1_0.uuid)
        self._vnc_lib.physical_interface_delete(id=self.pi1_1.uuid)
        self._vnc_lib.physical_interface_delete(id=self.pi2_0.uuid)
        self._vnc_lib.physical_interface_delete(id=self.pi2_1.uuid)

        self.delete_routers(None, self.pr1)
        self.wait_for_routers_delete(None, self.pr1.get_fq_name())
        self._vnc_lib.bgp_router_delete(id=self.bgp_router1.uuid)

        self.delete_routers(None, self.pr2)
        self.wait_for_routers_delete(None, self.pr2.get_fq_name())
        self._vnc_lib.bgp_router_delete(id=self.bgp_router2.uuid)

        self._vnc_lib.virtual_network_delete(id=self.vn1.uuid)
        self._vnc_lib.role_config_delete(id=self.rc1.uuid)
        self._vnc_lib.role_config_delete(id=self.rc2.uuid)
        self._vnc_lib.node_profile_delete(id=self.np1.uuid)
        self._vnc_lib.node_profile_delete(id=self.np2.uuid)
        self._vnc_lib.fabric_delete(id=self.fabric.uuid)
        self._vnc_lib.job_template_delete(id=self.jt.uuid)

        self.delete_role_definitions()
        self.delete_overlay_roles()
        self.delete_physical_roles()
        self.delete_features()
        self.wait_for_features_delete()

    def build_acl_rule(self, pmin, pmax, direction, proto, etype):
        rule = {}
        rule['port_min'] = pmin
        rule['port_max'] = pmax
        rule['direction'] = direction
        rule['ip_prefix'] = None
        rule['protocol'] = proto
        rule['ether_type'] = etype
        return rule
