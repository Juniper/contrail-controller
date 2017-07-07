#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

from gevent import sleep
import logging
from testtools import ExpectedException

from cfgm_common import exceptions
from cfgm_common import tag_dict
from cfgm_common import DEFAULT_MATCH_TAG_TYPE
from vnc_api.vnc_api import AddressGroup
from vnc_api.vnc_api import ActionListType
from vnc_api.vnc_api import ApplicationPolicySet
from vnc_api.vnc_api import FirewallGroup
from vnc_api.vnc_api import FirewallPolicy
from vnc_api.vnc_api import FirewallPolicyDirectionType
from vnc_api.vnc_api import FirewallRule
from vnc_api.vnc_api import FirewallRuleEndpointType
from vnc_api.vnc_api import FirewallRuleMatchTagsType
from vnc_api.vnc_api import FirewallSequence
from vnc_api.vnc_api import FirewallServiceGroupType
from vnc_api.vnc_api import FirewallServiceType
from vnc_api.vnc_api import PolicyManagement
from vnc_api.vnc_api import PortType
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import ServiceGroup
from vnc_api.vnc_api import SubnetListType
from vnc_api.vnc_api import SubnetType
from vnc_api.vnc_api import Tag
from vnc_api.vnc_api import VirtualMachineInterface
from vnc_api.vnc_api import VirtualNetwork

import test_case

logger = logging.getLogger(__name__)


class TestFWBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestFWBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestFWBase, cls).tearDownClass(*args, **kwargs)

    def _create_vn_collection(self, count, proj_obj=None):
        return self._create_test_objects(count=count, proj_obj=proj_obj)

    def _create_vmi_collection(self, count, vn):
        proj = self._vnc_lib.project_read(id=vn.parent_uuid)
        vmis = []
        for i in range(count):
            vmi = VirtualMachineInterface('vmi-%s-%d' % (self.id(), i),
                                          parent_obj=proj)
            vmi.add_virtual_network(vn)
            self._vnc_lib.virtual_machine_interface_create(vmi)
            vmis.append(vmi)
        return vmis

    def _create_fg_collection(self, count, proj=None, **kwargs):
        fgs = []
        for i in range(count):
            fg = FirewallGroup('fg-%s-%d' % (self.id(), i), parent_obj=proj,
                               **kwargs)
            self._vnc_lib.firewall_group_create(fg)
            fgs.append(fg)
        return fgs

    def _create_fp_collection(self, count, proj=None, **kwargs):
        fps = []
        for i in range(count):
            fp = FirewallPolicy('fp-%s-%d' % (self.id(), i), parent_obj=proj,
                                **kwargs)
            self._vnc_lib.firewall_policy_create(fp)
            fps.append(fp)
        return fps

    def _create_fr_collection(self, count, proj=None, **kwargs):
        frs = []
        for i in range(count):
            fr = FirewallRule('fp-%s-%d' % (self.id(), i), parent_obj=proj,
                              **kwargs)
            self._vnc_lib.firewall_rule_create(fr)
            frs.append(fr)
        return frs


class TestFirewallGroup(TestFWBase):
    def test_create_vmi_with_two_fg_associated(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        vn = self._create_vn_collection(1, proj)[0]
        vmi = VirtualMachineInterface('vmi-%s' % self.id(), parent_obj=proj)
        vmi.add_virtual_network(vn)
        for fg in fgs:
            vmi.add_firewall_group(fg)

        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.virtual_machine_interface_create(vmi)

    def test_update_vmi_with_two_fg_associated(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        vn = self._create_vn_collection(1, proj)[0]
        vmi = self._create_vmi_collection(1, vn)[0]

        for fg in fgs:
            vmi.add_firewall_group(fg)
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.virtual_machine_interface_update(vmi)

    def test_update_vmi_and_exceeded_firewall_group_associated(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        vn = self._create_vn_collection(1, proj)[0]
        vmi = self._create_vmi_collection(1, vn)[0]

        # Associate the first fwg to vmi success
        vmi.add_firewall_group(fgs[0])
        self._vnc_lib.virtual_machine_interface_update(vmi)

        # Associate the fwg to the same vmi do nothing and success
        vmi.add_firewall_group(fgs[0])
        self._vnc_lib.virtual_machine_interface_update(vmi)

        # Add a second one fwg to a vmi fails
        vmi.add_firewall_group(fgs[1])
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.virtual_machine_interface_update(vmi)

    def test_create_fp_association_both_direction_with_fg(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fg = self._create_fg_collection(1, proj)[0]
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=proj)

        fp.add_firewall_group(fg,
                              FirewallPolicyDirectionType(direction='both'))
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.firewall_policy_create(fp)

    def test_update_fp_association_both_direction_with_fg(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fg = self._create_fg_collection(1, proj)[0]

        fp = self._create_fp_collection(1, proj)[0]
        fp.add_firewall_group(fg,
                              FirewallPolicyDirectionType(direction='both'))
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.firewall_policy_update(fp)

    def test_create_fp_association_ingress_direction_with_fg(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        fp1 = self._create_fp_collection(1, proj)[0]
        for fg in fgs:
            fp1.add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='ingress'))
            self._vnc_lib.firewall_policy_update(fp1)

        for fg in fgs:
            fp2 = FirewallPolicy('fp-%s-1' % self.id(), parent_obj=proj)
            fp2.add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='ingress'))
            with ExpectedException(exceptions.BadRequest):
                self._vnc_lib.firewall_policy_create(fp2)

    def test_update_fp_association_ingress_direction_with_fg(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        fps = self._create_fp_collection(2, proj)
        for fg in fgs:
            fps[0].add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='ingress'))
        self._vnc_lib.firewall_policy_update(fps[0])

        for fg in fgs:
            fps[1].add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='ingress'))
            with ExpectedException(exceptions.BadRequest):
                self._vnc_lib.firewall_policy_update(fps[1])

    def test_create_fp_association_egress_direction_with_fg(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        fp1 = self._create_fp_collection(1, proj)[0]
        for fg in fgs:
            fp1.add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='egress'))
            self._vnc_lib.firewall_policy_update(fp1)

        for fg in fgs:
            fp2 = FirewallPolicy('fp-%s-1' % self.id(), parent_obj=proj)
            fp2.add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='egress'))
            with ExpectedException(exceptions.BadRequest):
                self._vnc_lib.firewall_policy_create(fp2)

    def test_update_fp_association_egress_direction_with_fg(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fgs = self._create_fg_collection(2, proj)
        fps = self._create_fp_collection(2, proj)
        for fg in fgs:
            fps[0].add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='egress'))
        self._vnc_lib.firewall_policy_update(fps[0])

        for fg in fgs:
            fps[1].add_firewall_group(
                fg, FirewallPolicyDirectionType(direction='egress'))
            with ExpectedException(exceptions.BadRequest):
                self._vnc_lib.firewall_policy_update(fps[1])


class TestFirewallPolicy(TestFWBase):
    def test_fp_audit_is_false_by_defaut(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fp = self._create_fp_collection(1, proj)[0]

        self.assertFalse(fp.audited)

    def test_fp_audited_set_to_false_if_asssociated_rule_changed(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fr = self._create_fr_collection(1, proj)[0]
        fp = self._create_fp_collection(1, proj, **{'audited': True})[0]
        self.assertTrue(fp.audited)

        fp.add_firewall_rule(fr, FirewallSequence(sequence='1.0'))
        self._vnc_lib.firewall_policy_update(fp)
        fp = self._vnc_lib.firewall_policy_read(id=fp.uuid)
        self.assertFalse(fp.audited)

    def test_fp_audited_set_to_false_if_asssociated_rule_updated(self):
        proj = Project('project-%s' % self.id())
        self._vnc_lib.project_create(proj)
        fr = self._create_fr_collection(1, proj)[0]
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=proj, audited=True)
        fp.add_firewall_rule(fr, FirewallSequence(sequence='1.0'))
        self._vnc_lib.firewall_policy_create(fp)
        self.assertTrue(fp.audited)

        fr.set_action_list(ActionListType(simple_action='pass'))
        self._vnc_lib.firewall_rule_update(fr)
        fp = self._vnc_lib.firewall_policy_read(id=fp.uuid)
        self.assertFalse(fp.audited)


class TestFirewallRule(TestFWBase):
    def test_tag_basic_sanity(self):
        pobj = Project('proj-%s' % self.id())
        self._vnc_lib.project_create(pobj)

        tag_obj = Tag(parent_obj=pobj)

        # tag type must be valid
        tag_obj.set_tag_type('Foobar')
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.tag_create(tag_obj)

        # tag type and value both are required
        tag_obj.set_tag_type('application')
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.tag_create(tag_obj)

        # tag ID isn't settable by user
        tag_obj.set_tag_id(0x12345678)
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.tag_create(tag_obj)

        # create a valid project scoped tag
        tag_obj = Tag(tag_type='application', tag_value='MyTestApp',
                      parent_obj=pobj)
        self._vnc_lib.tag_create(tag_obj)
        tag_obj = self._vnc_lib.tag_read(id=tag_obj.uuid)
        sleep(5)

        # detect duplication of project-scoped tag
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.tag_create(Tag(tag_type='application',
                                         tag_value='MyTestApp',
                                         parent_obj=pobj))

        # tag type, value or id can't be updated
        tag = self._vnc_lib.tag_read(id=tag_obj.uuid)
        tag_obj.set_tag_value('MyTestApp')
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.tag_update(tag_obj)
        tag = self._vnc_lib.tag_read(id=tag_obj.uuid)
        tag_obj.set_tag_type('application')
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.tag_update(tag_obj)
        tag = self._vnc_lib.tag_read(id=tag_obj.uuid)
        tag_obj.set_tag_id(1 << 27 | 1)
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.tag_update(tag_obj)

        # create a valid global tag
        gtag_obj = Tag(tag_type='deployment', tag_value='Production')
        self._vnc_lib.tag_create(gtag_obj)
        gtag_obj = self._vnc_lib.tag_read(id=gtag_obj.uuid)
        sleep(5)

        # detect duplication of globally-scoped tag
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.tag_create(Tag(tag_type='deployment',
                                         tag_value='Production'))

        # create a VN and attach project and global tag
        vn_obj = VirtualNetwork('vn-%s' % self.id(), parent_obj=pobj)
        self._vnc_lib.virtual_network_create(vn_obj)
        self._vnc_lib.set_tag(vn_obj, tag_type='application',
                              tag_value='MyTestApp')
        self._vnc_lib.set_tag(vn_obj, tag_type='deployment',
                              tag_value='Production', is_global=True)

        # validate vn->tag ref exists
        vn = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        tag_refs = vn.get_tag_refs()
        self.assertEqual(len(tag_refs), 2)
        received_set = set([ref['uuid'] for ref in tag_refs])
        expected_set = set([tag_obj.uuid, gtag_obj.uuid])
        self.assertEqual(received_set, expected_set)

        # validate tag->vn back ref exists
        tag = self._vnc_lib.tag_read(id=tag_obj.uuid)
        vn_refs = tag.get_virtual_network_back_refs()
        self.assertEqual(len(vn_refs), 1)
        self.assertEqual(vn_refs[0]['uuid'], vn_obj.uuid)
        tag = self._vnc_lib.tag_read(id=gtag_obj.uuid)
        vn_refs = tag.get_virtual_network_back_refs()
        self.assertEqual(len(vn_refs), 1)
        self.assertEqual(vn_refs[0]['uuid'], vn_obj.uuid)

        # only one tag of type (latest) can be associated
        tag2_obj = Tag(tag_type='application', tag_value='MyTestApp2',
                       parent_obj=pobj)
        self._vnc_lib.tag_create(tag2_obj)
        tag2_obj = self._vnc_lib.tag_read(id=tag2_obj.uuid)
        self._vnc_lib.set_tag(vn_obj, tag_type='application',
                              tag_value='MyTestApp2')
        vn = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        tag_refs = vn.get_tag_refs()
        self.assertEqual(len(tag_refs), 2)
        received_set = set([ref['uuid'] for ref in tag_refs])
        expected_set = set([tag2_obj.uuid, gtag_obj.uuid])
        self.assertEqual(received_set, expected_set)

    # end test_basic_sanity

    # verify unique id are allocated for global and per-project tags
    def test_tag_id(self):
        # create valid tags
        pobj = Project('%s-project' % self.id())
        self._vnc_lib.project_create(pobj)

        tag_list = [
            ('application', 'App1'),
            ('deployment', 'Site1'),
            ('application', 'App2'),
            ('deployment', 'Site2')
        ]

        tag_ids = []
        tag_uuids = []
        for tt, tv in tag_list:
            tag_obj = Tag(tag_type=tt, tag_value=tv, parent_obj=pobj)
            self._vnc_lib.tag_create(tag_obj)
            tag = self._vnc_lib.tag_read(id=tag_obj.uuid)
            tag_id = tag.get_tag_id()
            self.assertEqual(tag_id >> 27, tag_dict[tt])
            tag_ids.append(tag_id)
            tag_uuids.append(tag.uuid)
            sleep(5)

        # validate all ids are unique
        tag_ids_set = set(tag_ids)
        self.assertEqual(len(tag_ids_set), 4)

        # validate ids are allocated sequentially per type
        self.assertEqual(tag_ids[2], tag_ids[0]+1)
        self.assertEqual(tag_ids[3], tag_ids[1]+1)

        # validate ids get freed up on delete (and re-allocated on create)
        for tag_uuid in tag_uuids:
            self._vnc_lib.tag_delete(id=tag_uuid)

        tag_ids_post_delete = []
        for tt, tv in tag_list:
            tag_obj = Tag(tag_type=tt, tag_value=tv, parent_obj=pobj)
            self._vnc_lib.tag_create(tag_obj)
            tag = self._vnc_lib.tag_read(id=tag_obj.uuid)
            tag_id = tag.get_tag_id()
            tag_ids_post_delete.append(tag_id)
            sleep(5)

        self.assertEqual(tag_ids, tag_ids_post_delete)
    # end test_tag_id

    def test_firewall_rule_using_ep_tag(self):
        pobj = Project('%s-project' % self.id())
        self._vnc_lib.project_create(pobj)
        pm_obj = PolicyManagement('pm-%s' % self.id())
        self._vnc_lib.policy_management_create(pm_obj)

        tag1_obj = Tag(tag_type='application', tag_value='App1',
                       parent_obj=pobj)
        self._vnc_lib.tag_create(tag1_obj)
        tag1 = self._vnc_lib.tag_read(id=tag1_obj.uuid)
        sleep(5)

        tag2_obj = Tag(tag_type='tier', tag_value='Web', parent_obj=pobj)
        self._vnc_lib.tag_create(tag2_obj)
        tag2 = self._vnc_lib.tag_read(id=tag2_obj.uuid)
        sleep(5)

        tag3_obj = Tag(tag_type='application', tag_value='App2',
                       parent_obj=pobj)
        self._vnc_lib.tag_create(tag3_obj)
        tag3 = self._vnc_lib.tag_read(id=tag3_obj.uuid)
        sleep(5)

        tag4_obj = Tag(tag_type='tier', tag_value='Db', parent_obj=pobj)
        self._vnc_lib.tag_create(tag4_obj)
        tag4 = self._vnc_lib.tag_read(id=tag4_obj.uuid)
        sleep(5)

        rule_obj = FirewallRule(
            name='rule-%s' % self.id(), parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            service=FirewallServiceType(protocol="tcp",
                                        dst_ports=PortType(8080, 8082)),
            endpoint_1=FirewallRuleEndpointType(
                tags=['application=App1', 'tier=Web']),
            endpoint_2=FirewallRuleEndpointType(
                tags=['application=App2', 'tier=Db']),
            direction='<>',
        )
        self._vnc_lib.firewall_rule_create(rule_obj)

        # validate rule->tag refs exists
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        tag_refs = rule.get_tag_refs()
        self.assertEqual(len(tag_refs), 4)
        expected_set = set([obj.get_uuid() for obj in
                           [tag1_obj, tag2_obj, tag3_obj, tag4_obj]])
        received_set = set([ref['uuid'] for ref in tag_refs])
        self.assertEqual(received_set, expected_set)

        # validate tag_ids get populated
        expected_set = set([obj.get_tag_id() for obj in [tag1, tag2]])
        received_set = set(rule.get_endpoint_1().get_tag_ids())
        self.assertEqual(received_set, expected_set)
        expected_set = set([obj.get_tag_id() for obj in [tag3, tag4]])
        received_set = set(rule.get_endpoint_2().get_tag_ids())
        self.assertEqual(received_set, expected_set)

        # validate protocol_id in service get populated
        self.assertEqual(rule.get_service().get_protocol_id(), 6)
    # end test_firewall_rule_using_ep_tag

    def test_firewall_rule_using_ep_ag_tag(self):
        pobj = Project('%s-project' % self.id())
        self._vnc_lib.project_create(pobj)
        pm_obj = PolicyManagement('pm-%s' % self.id())
        self._vnc_lib.policy_management_create(pm_obj)

        # create a valid global tag
        tag_value = 'Production-%s' % self.id()
        gtag_obj = Tag(tag_type='deployment', tag_value=tag_value)
        self._vnc_lib.tag_create(gtag_obj)
        gtag_obj = self._vnc_lib.tag_read(id=gtag_obj.uuid)
        sleep(5)

        # create a valid global label
        tag_obj = Tag(tag_type='label', tag_value='RedVlan')
        self._vnc_lib.tag_create(tag_obj)
        tag_obj = self._vnc_lib.tag_read(id=tag_obj.uuid)
        sleep(5)

        ag_prefix = SubnetListType(subnet=[SubnetType('1.1.1.0', 24),
                                   SubnetType('2.2.2.0', 24)])
        ag_obj = AddressGroup(address_group_prefix=ag_prefix,
                              parent_obj=pobj)
        self._vnc_lib.address_group_create(ag_obj)
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.set_tag(ag_obj, tag_type='deployment',
                                  tag_value=tag_value, is_global=True)
        self._vnc_lib.set_tag(ag_obj, tag_type='label', tag_value='RedVlan',
                              is_global=True)

        tag3_obj = Tag(tag_type='application', tag_value='App2',
                       parent_obj=pobj)
        self._vnc_lib.tag_create(tag3_obj)
        tag3_obj = self._vnc_lib.tag_read(id=tag3_obj.uuid)
        sleep(5)

        tag4_obj = Tag(tag_type='tier', tag_value='Db', parent_obj=pobj)
        self._vnc_lib.tag_create(tag4_obj)
        tag4_obj = self._vnc_lib.tag_read(id=tag4_obj.uuid)
        sleep(5)

        rule_obj = FirewallRule(
            name='rule-%s' % self.id(), parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            service=FirewallServiceType(protocol="tcp",
                                        dst_ports=PortType(8080, 8082)),
            endpoint_1=FirewallRuleEndpointType(
                address_group=":".join(ag_obj.get_fq_name())),
            endpoint_2=FirewallRuleEndpointType(
                tags=['application=App2', 'tier=Db']),
            direction='<>',
        )
        self._vnc_lib.firewall_rule_create(rule_obj)

        # validate rule->tag refs exist
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        tag_refs = rule.get_tag_refs()
        self.assertEqual(len(tag_refs), 2)
        expected_set = set([obj.get_uuid() for obj in [tag3_obj, tag4_obj]])
        received_set = set([ref['uuid'] for ref in tag_refs])
        self.assertEqual(received_set, expected_set)

        # validate rule->address-group refs exist
        ag_refs = rule.get_address_group_refs()
        self.assertEqual(len(ag_refs), 1)
        self.assertEqual(ag_refs[0]['uuid'], ag_obj.uuid)
    # end test_firewall_rule_using_ep_ap_tag

    def test_firewall_rule_using_sg_vn(self):
        pobj = Project('%s-project' % self.id())
        self._vnc_lib.project_create(pobj)

        fst = FirewallServiceType(protocol="tcp",
                                  dst_ports=PortType(8080, 8082))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(service_group_firewall_service_list=fsgt,
                              parent_obj=pobj)
        self._vnc_lib.service_group_create(sg_obj)

        vn1_obj = VirtualNetwork('vn-%s-fe' % self.id(), parent_obj=pobj)
        self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_obj = VirtualNetwork('vn-%s-be' % self.id(), parent_obj=pobj)
        self._vnc_lib.virtual_network_create(vn2_obj)

        rule_obj = FirewallRule(
            name='rule-%s' % self.id(), parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            endpoint_1=FirewallRuleEndpointType(
                virtual_network=":".join(vn1_obj.get_fq_name())),
            endpoint_2=FirewallRuleEndpointType(
                virtual_network=":".join(vn2_obj.get_fq_name())),
            direction='<>',
        )
        rule_obj.set_service_group(sg_obj)
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule_obj = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)

        # validate protocol_id in service get populated
        sg = self._vnc_lib.service_group_read(fq_name=sg_obj.get_fq_name())
        sg_fsl = sg.get_service_group_firewall_service_list()
        sg_firewall_service = sg_fsl.get_firewall_service()
        self.assertEqual(sg_firewall_service[0].protocol_id, 6)
    # end test_firewall_rule_using_sg_vn

    def test_firewall_rule_match_tags(self):
        pobj = Project('%s-project' % self.id())
        self._vnc_lib.project_create(pobj)

        # validate specified match-tag types
        match_tags = ['application', 'tier', 'site']
        rule_obj = FirewallRule(
            name='rule-%s' % self.id(), parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            match_tags=FirewallRuleMatchTagsType(tag_list=match_tags),
            endpoint_1=FirewallRuleEndpointType(any=True),
            endpoint_2=FirewallRuleEndpointType(any=True),
            direction='<>',
        )
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        received_set = set(rule.get_match_tag_types().get_tag_type())
        expected_set = set([tag_dict[tag_type] for tag_type in
                            match_tags])
        self.assertEqual(expected_set, received_set)
        self._vnc_lib.firewall_rule_delete(id=rule_obj.uuid)

        # validate default match-tags
        rule_obj = FirewallRule(
            name='rule-%s' % self.id(), parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            endpoint_1=FirewallRuleEndpointType(any=True),
            endpoint_2=FirewallRuleEndpointType(any=True),
            direction='<>',
        )
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        received_set = set(rule.get_match_tag_types().get_tag_type())
        expected_set = set([tag_dict[tag_type] for tag_type in
                            DEFAULT_MATCH_TAG_TYPE])
        self.assertEqual(expected_set, received_set)
        self._vnc_lib.firewall_rule_delete(id=rule_obj.uuid)

        # validate override of default match-tags
        rule_obj = FirewallRule(
            name='rule-%s' % self.id(), parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            match_tags=FirewallRuleMatchTagsType(tag_list=[]),
            endpoint_1=FirewallRuleEndpointType(any=True),
            endpoint_2=FirewallRuleEndpointType(any=True),
            direction='<>',
        )
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        received_set = set(rule.get_match_tag_types().get_tag_type())
        expected_set = set()
        self.assertEqual(expected_set, received_set)
        self._vnc_lib.firewall_rule_delete(id=rule_obj.uuid)
    # end test_firewall_rule_match_tags

    def test_firewall_service(self):
        pobj = Project('%s-project' % self.id())
        self._vnc_lib.project_create(pobj)

        rule_obj = FirewallRule(name='rule-%s' % self.id(), parent_obj=pobj)
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule_obj = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)

        # rule + service (negative test case)
        rule_obj.set_service(
            FirewallServiceType(protocol="1234",
                                dst_ports=PortType(8080, 8082)))
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.firewall_rule_update(rule_obj)

        # rule + service (positive test case)
        rule_obj.set_service(
            FirewallServiceType(protocol="udp",
                                dst_ports=PortType(8080, 8082)))
        self._vnc_lib.firewall_rule_update(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        self.assertEqual(rule.get_service().get_protocol_id(), 17)

        # service group negative test case
        fst = FirewallServiceType(protocol="1234",
                                  dst_ports=PortType(8080, 8082))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(service_group_firewall_service_list=fsgt,
                              parent_obj=pobj)
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.service_group_create(sg_obj)

        # create blank service group
        fst = FirewallServiceType()
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(name="sg1-%s" % self.id(),
                              service_group_firewall_service_list=fsgt,
                              parent_obj=pobj)
        self._vnc_lib.service_group_create(sg_obj)
        sg = self._vnc_lib.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 1)

        # update service group
        fst = FirewallServiceType(protocol="tcp",
                                  dst_ports=PortType(8080, 8082))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg.set_service_group_firewall_service_list(fsgt)
        self._vnc_lib.service_group_update(sg)
        sg = self._vnc_lib.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 1)
        expected_protocol_id_list = [6]
        received_list = [service.protocol_id for service in fst]
        self.assertEqual(received_list, expected_protocol_id_list)

        # update service group again and verify
        fsgt.add_firewall_service(
            FirewallServiceType(protocol="udp",
                                dst_ports=PortType(52, 53)))
        sg.set_service_group_firewall_service_list(fsgt)
        self._vnc_lib.service_group_update(sg)
        sg = self._vnc_lib.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 2)
        expected_protocol_id_list = [6, 17]
        received_list = [service.protocol_id for service in fst]
        self.assertEqual(received_list, expected_protocol_id_list)

        # create a new service group
        fst = FirewallServiceType(protocol="tcp", dst_ports=PortType(80, 80))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(name="sg2-%s" % self.id(),
                              service_group_firewall_service_list=fsgt,
                              parent_obj=pobj)
        self._vnc_lib.service_group_create(sg_obj)
        sg = self._vnc_lib.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 1)
        expected_protocol_id_list = [6]
        received_list = [service.protocol_id for service in fst]
        self.assertEqual(received_list, expected_protocol_id_list)

        # update service group and verify all items
        fsgt.add_firewall_service(
            FirewallServiceType(protocol="udp", dst_ports=PortType(52, 53)))
        sg.set_service_group_firewall_service_list(fsgt)
        self._vnc_lib.service_group_update(sg)
        sg = self._vnc_lib.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 2)
        expected_protocol_id_list = [6, 17]
        received_list = [service.protocol_id for service in fst]
        self.assertEqual(received_list, expected_protocol_id_list)
    # end test_firewall_service

    def test_firewall_rule_update(self):
        pobj = Project('%s-project' % self.id())
        self._vnc_lib.project_create(pobj)

        rule_obj = FirewallRule(name='rule-%s' % self.id(), parent_obj=pobj)
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule_obj = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)

        # update action_list
        rule_obj.set_action_list(ActionListType(simple_action='pass'))
        self._vnc_lib.firewall_rule_update(rule_obj)
        rule_obj = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        self.assertEqual(rule_obj.action_list.simple_action, 'pass')

        # update rule with service - validate protocol_id in service get
        # populated
        rule_obj.set_service(
            FirewallServiceType(protocol="tcp", dst_ports=PortType(8080, 8082))
        )
        self._vnc_lib.firewall_rule_update(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        self.assertEqual(rule.get_service().get_protocol_id(), 6)

        # update match_tags
        match_tags = ['application', 'tier', 'deployment', 'site']
        rule_obj.set_action_list(ActionListType(simple_action='deny'))
        rule_obj.set_match_tags(FirewallRuleMatchTagsType(tag_list=match_tags))
        self._vnc_lib.firewall_rule_update(rule_obj)
        rule_obj = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        self.assertEqual(rule_obj.action_list.simple_action, 'deny')
        self.assertEqual(set(rule_obj.match_tags.tag_list), set(match_tags))
        self.assertEqual(rule_obj.get_service().get_protocol_id(), 6)

        # create tags and a label
        tag1_obj = Tag(tag_type='application', tag_value='App1',
                       parent_obj=pobj)
        self._vnc_lib.tag_create(tag1_obj)
        tag1 = self._vnc_lib.tag_read(id=tag1_obj.uuid)
        sleep(5)
        tag2_obj = Tag(tag_type='tier', tag_value='Web', parent_obj=pobj)
        self._vnc_lib.tag_create(tag2_obj)
        tag2 = self._vnc_lib.tag_read(id=tag2_obj.uuid)
        sleep(5)
        tag3_obj = Tag(tag_type='application', tag_value='App2',
                       parent_obj=pobj)
        self._vnc_lib.tag_create(tag3_obj)
        tag3 = self._vnc_lib.tag_read(id=tag3_obj.uuid)
        sleep(5)
        tag4_obj = Tag(tag_type='tier', tag_value='Db', parent_obj=pobj)
        self._vnc_lib.tag_create(tag4_obj)
        tag4 = self._vnc_lib.tag_read(id=tag4_obj.uuid)
        sleep(5)
        tag_obj = Tag(tag_type='label', tag_value='RedVlan', parent_obj=pobj)
        self._vnc_lib.tag_create(tag_obj)
        tag_obj = self._vnc_lib.tag_read(id=tag_obj.uuid)
        sleep(5)

        # update endpoint1-tags
        rule_obj.set_endpoint_1(
            FirewallRuleEndpointType(tags=['application=App1', 'tier=Web']))
        self._vnc_lib.firewall_rule_update(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        self.assertEqual(rule.action_list.simple_action, 'deny')
        self.assertEqual(set(rule.match_tags.tag_list), set(match_tags))
        self.assertEqual(rule.get_service().get_protocol_id(), 6)

        # validate rule->tag refs exists after update
        tag_refs = rule.get_tag_refs()
        self.assertEqual(len(tag_refs), 2)
        expected_set = set([obj.get_uuid() for obj in [tag1_obj, tag2_obj]])
        received_set = set([ref['uuid'] for ref in tag_refs])
        self.assertEqual(received_set, expected_set)

        # validate tag_ids get populated
        expected_set = set([obj.get_tag_id() for obj in [tag1, tag2]])
        received_set = set(rule.get_endpoint_1().get_tag_ids())
        self.assertEqual(received_set, expected_set)

        # update endpoint2-tags
        rule_obj = rule
        rule_obj.set_endpoint_2(
            FirewallRuleEndpointType(tags=['application=App2', 'tier=Db']))
        self._vnc_lib.firewall_rule_update(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)

        # validate rule->tag refs exists after update
        tag_refs = rule.get_tag_refs()
        self.assertEqual(len(tag_refs), 4)
        expected_set = set([obj.get_uuid() for obj in
                            [tag1_obj, tag2_obj, tag3_obj, tag4_obj]])
        received_set = set([ref['uuid'] for ref in tag_refs])
        self.assertEqual(received_set, expected_set)

        # validate tag_ids get populated
        expected_set = set([obj.get_tag_id() for obj in [tag1, tag2]])
        received_set = set(rule.get_endpoint_1().get_tag_ids())
        self.assertEqual(received_set, expected_set)
        expected_set = set([obj.get_tag_id() for obj in [tag3, tag4]])
        received_set = set(rule.get_endpoint_2().get_tag_ids())
        self.assertEqual(received_set, expected_set)

        # validate rule->address-group refs exist
        ag_prefix = SubnetListType(subnet=[SubnetType('1.1.1.0', 24),
                                   SubnetType('2.2.2.0', 24)])
        ag_obj = AddressGroup(address_group_prefix=ag_prefix, parent_obj=pobj)
        self._vnc_lib.address_group_create(ag_obj)
        self._vnc_lib.set_tag(ag_obj, tag_type='label', tag_value='RedVlan')
        rule.set_endpoint_2(FirewallRuleEndpointType(
            address_group=":".join(ag_obj.get_fq_name())))
        self._vnc_lib.firewall_rule_update(rule)
        rule = self._vnc_lib.firewall_rule_read(id=rule.uuid)
        ag_refs = rule.get_address_group_refs()
        self.assertEqual(len(ag_refs), 1)
        self.assertEqual(ag_refs[0]['uuid'], ag_obj.uuid)

        # update SG and VN in endpoint
        fst = FirewallServiceType(protocol="tcp",
                                  dst_ports=PortType(8080, 8082))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(service_group_firewall_service_list=fsgt,
                              parent_obj=pobj)
        self._vnc_lib.service_group_create(sg_obj)

        vn1_obj = VirtualNetwork('vn-%s-fe' % self.id(), parent_obj=pobj)
        self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_obj = VirtualNetwork('vn-%s-be' % self.id(), parent_obj=pobj)
        self._vnc_lib.virtual_network_create(vn2_obj)

        rule.set_service_group(sg_obj)
        rule.set_endpoint_2(FirewallRuleEndpointType(
            virtual_network=":".join(vn1_obj.get_fq_name())))
        self._vnc_lib.firewall_rule_update(rule)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)

        self.assertEqual(set(rule.match_tags.tag_list), set(match_tags))
        self.assertEqual(rule.get_service().get_protocol_id(), 6)

        # validate rule->tag refs exists after update
        tag_refs = rule.get_tag_refs()
        self.assertEqual(len(tag_refs), 2)
        expected_set = set([obj.get_uuid() for obj in [tag1_obj, tag2_obj]])
        received_set = set([ref['uuid'] for ref in tag_refs])
        self.assertEqual(received_set, expected_set)
        self.assertEqual(rule.endpoint_2.virtual_network,
                         ":".join(vn1_obj.get_fq_name()))

        # validate protocol_id in service stays populated
        sg = self._vnc_lib.service_group_read(fq_name=sg_obj.get_fq_name())
        sg_fsl = sg.get_service_group_firewall_service_list()
        sg_firewall_service = sg_fsl.get_firewall_service()
        self.assertEqual(sg_firewall_service[0].protocol_id, 6)
    # end test_firewall_rule_update

    def test_aps_global(self):
        pm_obj = PolicyManagement('pm-%s' % self.id())
        self._vnc_lib.policy_management_create(pm_obj)

        # validate another global APS can't be created
        aps = ApplicationPolicySet('aps-%s' % self.id(), parent_obj=pm_obj,
                                   is_global=True)
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.application_policy_set_create(pm_obj)

        # validate APS can't be updated to global
        aps = ApplicationPolicySet('aps-%s' % self.id(), parent_obj=pm_obj)
        self._vnc_lib.application_policy_set_create(aps)
        aps.set_is_global(True)
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.application_policy_set_update(aps)
        aps.set_is_global(False)
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.application_policy_set_update(aps)

        # validate default global APS can't be deleted
        global_aps = ['default-policy-management',
                      'global-application-policy-set']
        aps = self._vnc_lib.application_policy_set_read(fq_name=global_aps)
        with ExpectedException(exceptions.BadRequest):
            self._vnc_lib.application_policy_set_delete(id=aps.uuid)
    # end test_aps_global

    def test_unset_tag(self):
        pobj = Project('proj-%s' % self.id())
        self._vnc_lib.project_create(pobj)

        tag_list = [
            ('application', 'HR',         False),
            ('deployment',  '%s' % self.id(), True),
            ('label',       'RedVlan',    False),
            ('label',       'BlueVlan',   False)
        ]

        tag_uuids = []
        for tt, tv, is_global in tag_list:
            tag_obj = Tag(tag_type=tt, tag_value=tv,
                          parent_obj=(pobj if not is_global else None))
            self._vnc_lib.tag_create(tag_obj)
            tag = self._vnc_lib.tag_read(id=tag_obj.uuid)
            tag_uuids.append(tag.uuid)
            sleep(5)

        # create a VN and attach tags
        vn_obj = VirtualNetwork('vn-%s' % self.id(), parent_obj=pobj)
        self._vnc_lib.virtual_network_create(vn_obj)
        for tt, tv, is_global in tag_list:
            self._vnc_lib.set_tag(vn_obj, tag_type=tt, tag_value=tv,
                                  is_global=is_global)

        # validate vn->tag ref exists
        vn = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        tag_refs = vn.get_tag_refs()
        self.assertEqual(len(tag_refs), 4)
        received_set = set([ref['uuid'] for ref in tag_refs])
        expected_set = set(tag_uuids)
        self.assertEqual(received_set, expected_set)

        # deattach some tags
        for tt, tv, is_global in [tag_list[0], tag_list[3]]:
            self._vnc_lib.unset_tag(vn_obj, tag_type=tt, tag_value=tv,
                                    is_global=is_global)

        # validate remaining vn->tag ref exists
        vn = self._vnc_lib.virtual_network_read(id=vn_obj.uuid)
        tag_refs = vn.get_tag_refs()
        self.assertEqual(len(tag_refs), 2)
        received_set = set([ref['uuid'] for ref in tag_refs])
        expected_set = set([tag_uuids[1], tag_uuids[2]])
        self.assertEqual(received_set, expected_set)
    # end test_unset_tag

# end class TestFw
