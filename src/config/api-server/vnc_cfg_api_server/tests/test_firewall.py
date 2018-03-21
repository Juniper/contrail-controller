#
# Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
#
import logging

from gevent import monkey
monkey.patch_all()  # noqa
from mock import patch
from sandesh_common.vns import constants
from testtools import ExpectedException
from vnc_api.exceptions import BadRequest
from vnc_api.exceptions import HttpError
from vnc_api.exceptions import NoIdError
from vnc_api.exceptions import RefsExistError
from vnc_api.vnc_api import ActionListType
from vnc_api.vnc_api import AddressGroup
from vnc_api.vnc_api import ApplicationPolicySet
from vnc_api.vnc_api import FirewallPolicy
from vnc_api.vnc_api import FirewallRule
from vnc_api.vnc_api import FirewallRuleEndpointType
from vnc_api.vnc_api import FirewallRuleMatchTagsType
from vnc_api.vnc_api import FirewallSequence
from vnc_api.vnc_api import FirewallServiceGroupType
from vnc_api.vnc_api import FirewallServiceType
from vnc_api.vnc_api import KeyValuePair
from vnc_api.vnc_api import PolicyManagement
from vnc_api.vnc_api import PortType
from vnc_api.vnc_api import Project
from vnc_api.vnc_api import ServiceGroup
from vnc_api.vnc_api import SubnetListType
from vnc_api.vnc_api import SubnetType
from vnc_api.vnc_api import Tag
from vnc_api.vnc_api import VirtualNetwork


from vnc_cfg_api_server.tests import test_case


logger = logging.getLogger(__name__)


class TestFirewallBase(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestFirewallBase, cls).setUpClass(*args, **kwargs)

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestFirewallBase, cls).tearDownClass(*args, **kwargs)

    @property
    def api(self):
        return self._vnc_lib


class TestFirewall(TestFirewallBase):
    def test_firewall_rule_using_ep_tag(self):
        pobj = Project('%s-project' % self.id())
        self.api.project_create(pobj)
        pm_obj = PolicyManagement('pm-%s' % self.id())
        self.api.policy_management_create(pm_obj)

        app1 = 'App1-%s' % self.id()
        tag1_obj = Tag(tag_type_name='application', tag_value=app1,
                       parent_obj=pobj)
        self.api.tag_create(tag1_obj)
        tag1 = self.api.tag_read(id=tag1_obj.uuid)

        web = 'Web-%s' % self.id()
        tag2_obj = Tag(tag_type_name='tier', tag_value=web, parent_obj=pobj)
        self.api.tag_create(tag2_obj)
        tag2 = self.api.tag_read(id=tag2_obj.uuid)

        app2 = 'App2-%s' % self.id()
        tag3_obj = Tag(tag_type_name='application', tag_value=app2,
                       parent_obj=pobj)
        self.api.tag_create(tag3_obj)
        tag3 = self.api.tag_read(id=tag3_obj.uuid)

        db = 'Db-%s' % self.id()
        tag4_obj = Tag(tag_type_name='tier', tag_value=db, parent_obj=pobj)
        self.api.tag_create(tag4_obj)
        tag4 = self.api.tag_read(id=tag4_obj.uuid)

        rule_obj = FirewallRule(
            name='rule-%s' % self.id(),
            parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            service=FirewallServiceType(protocol="tcp",
                                        dst_ports=PortType(8080, 8082)),
            endpoint_1=FirewallRuleEndpointType(
                tags=['application=%s' % app1, 'tier=%s' % web]),
            endpoint_2=FirewallRuleEndpointType(
                tags=['application=%s' % app2, 'tier=%s' % db]),
            direction='<>',
        )
        self.api.firewall_rule_create(rule_obj)

        # validate rule->tag refs exists
        rule = self.api.firewall_rule_read(id=rule_obj.uuid)
        tag_refs = rule.get_tag_refs()
        self.assertEqual(len(tag_refs), 4)
        expected_set = set([obj.get_uuid() for obj in [tag1_obj, tag2_obj,
                                                       tag3_obj, tag4_obj]])
        received_set = set([ref['uuid'] for ref in tag_refs])
        self.assertEqual(received_set, expected_set)

        # validate tag_ids get populated
        expected_set = set([int(obj.get_tag_id(), 0) for obj in [tag1, tag2]])
        received_set = set(rule.get_endpoint_1().get_tag_ids())
        self.assertEqual(received_set, expected_set)
        expected_set = set([int(obj.get_tag_id(), 0) for obj in [tag3, tag4]])
        received_set = set(rule.get_endpoint_2().get_tag_ids())
        self.assertEqual(received_set, expected_set)

        # validate protocol_id in service get populated
        self.assertEqual(rule.get_service().get_protocol_id(), 6)
    # end test_firewall_rule_using_ep_tag

    def test_firewall_rule_using_ep_ag_tag(self):
        pobj = Project('%s-project' % self.id())
        self.api.project_create(pobj)
        pm_obj = PolicyManagement('pm-%s' % self.id())
        self.api.policy_management_create(pm_obj)

        # create a valid global tag
        tag_value = 'Production-%s' % self.id()
        gtag_obj = Tag(tag_type_name='deployment', tag_value=tag_value)
        self.api.tag_create(gtag_obj)
        gtag_obj = self.api.tag_read(id=gtag_obj.uuid)

        # create a valid global label
        red_vlan = 'RedVlan-%s' % self.id()
        tag_obj = Tag(tag_type_name='label', tag_value=red_vlan)
        self.api.tag_create(tag_obj)
        tag_obj = self.api.tag_read(id=tag_obj.uuid)

        ag_prefix = SubnetListType(subnet=[SubnetType('1.1.1.0', 24),
                                           SubnetType('2.2.2.0', 24)])
        ag_obj = AddressGroup(address_group_prefix=ag_prefix, parent_obj=pobj)
        self.api.address_group_create(ag_obj)
        with ExpectedException(BadRequest):
            self.api.set_tag(ag_obj, 'deployment', tag_value, True)
        self.api.set_tag(ag_obj, 'label', red_vlan, True)

        app2 = 'App2-%s' % self.id()
        tag3_obj = Tag(tag_type_name='application', tag_value=app2,
                       parent_obj=pobj)
        self.api.tag_create(tag3_obj)
        self.api.tag_read(id=tag3_obj.uuid)

        db = 'Db-%s' % self.id()
        tag4_obj = Tag(tag_type_name='tier', tag_value=db, parent_obj=pobj)
        self.api.tag_create(tag4_obj)
        self.api.tag_read(id=tag4_obj.uuid)

        rule_obj = FirewallRule(
            name='rule-%s' % self.id(),
            parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            service=FirewallServiceType(protocol="tcp",
                                        dst_ports=PortType(8080, 8082)),
            endpoint_1=FirewallRuleEndpointType(
                address_group=":".join(ag_obj.get_fq_name())),
            endpoint_2=FirewallRuleEndpointType(
                tags=['application=%s' % app2, 'tier=%s' % db]),
            direction='<>',
        )
        self.api.firewall_rule_create(rule_obj)

        # validate rule->tag refs exist
        rule = self.api.firewall_rule_read(id=rule_obj.uuid)
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
        self.api.project_create(pobj)

        fst = FirewallServiceType(protocol="tcp",
                                  dst_ports=PortType(8080, 8082))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(service_group_firewall_service_list=fsgt,
                              parent_obj=pobj)
        self.api.service_group_create(sg_obj)

        vn1_obj = VirtualNetwork('vn-%s-fe' % self.id(), parent_obj=pobj)
        self.api.virtual_network_create(vn1_obj)
        vn2_obj = VirtualNetwork('vn-%s-be' % self.id(), parent_obj=pobj)
        self.api.virtual_network_create(vn2_obj)

        rule_obj = FirewallRule(
            name='rule-%s' % self.id(),
            parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            endpoint_1=FirewallRuleEndpointType(
                virtual_network=vn1_obj.get_fq_name_str()),
            endpoint_2=FirewallRuleEndpointType(
                virtual_network=vn2_obj.get_fq_name_str()),
            direction='<>',
        )
        rule_obj.set_service_group(sg_obj)
        self.api.firewall_rule_create(rule_obj)
        self.api.firewall_rule_read(id=rule_obj.uuid)

        # validate protocol_id in service get populated
        sg = self.api.service_group_read(fq_name=sg_obj.get_fq_name())
        sg_fsl = sg.get_service_group_firewall_service_list()
        sg_firewall_service = sg_fsl.get_firewall_service()
        self.assertEqual(sg_firewall_service[0].protocol_id, 6)
    # end test_firewall_rule_using_sg_vn

    def test_firewall_rule_match_tags(self):
        pobj = Project('%s-project' % self.id())
        self.api.project_create(pobj)

        # validate specified match-tag types
        match_tags = ['application', 'tier', 'site']
        rule_obj = FirewallRule(
            name='rule-%s' % self.id(),
            parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            match_tags=FirewallRuleMatchTagsType(tag_list=match_tags),
            endpoint_1=FirewallRuleEndpointType(any=True),
            endpoint_2=FirewallRuleEndpointType(any=True),
            direction='<>',
            service=FirewallServiceType(),
        )
        self.api.firewall_rule_create(rule_obj)
        rule = self.api.firewall_rule_read(id=rule_obj.uuid)
        received_set = set(rule.get_match_tag_types().get_tag_type())
        expected_set = set([constants.TagTypeNameToId[tag_type] for tag_type
                            in match_tags])
        self.assertEqual(expected_set, received_set)
        self.api.firewall_rule_delete(id=rule_obj.uuid)

        # validate default match-tags
        rule_obj = FirewallRule(
            name='rule-%s' % self.id(),
            parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            endpoint_1=FirewallRuleEndpointType(any=True),
            endpoint_2=FirewallRuleEndpointType(any=True),
            direction='<>',
            service=FirewallServiceType(),
        )
        self.api.firewall_rule_create(rule_obj)
        rule = self.api.firewall_rule_read(id=rule_obj.uuid)
        received_set = set(rule.get_match_tag_types().get_tag_type())
        expected_set = set([tag_type for tag_type
                            in constants.DEFAULT_MATCH_TAG_TYPE])
        self.assertEqual(expected_set, received_set)
        self.api.firewall_rule_delete(id=rule_obj.uuid)

        # validate override of default match-tags
        rule_obj = FirewallRule(
            name='rule-%s' % self.id(),
            parent_obj=pobj,
            action_list=ActionListType(simple_action='pass'),
            match_tags=FirewallRuleMatchTagsType(tag_list=[]),
            endpoint_1=FirewallRuleEndpointType(any=True),
            endpoint_2=FirewallRuleEndpointType(any=True),
            direction='<>',
            service=FirewallServiceType(),
        )
        self.api.firewall_rule_create(rule_obj)
        rule = self.api.firewall_rule_read(id=rule_obj.uuid)
        received_set = set(rule.get_match_tag_types().get_tag_type())
        expected_set = set()
        self.assertEqual(expected_set, received_set)
        self.api.firewall_rule_delete(id=rule_obj.uuid)
    # end test_firewall_rule_match_tags

    def test_firewall_rule_endpoint_match_limited_to_one(self):
        project = Project('%s-project' % self.id())
        self.api.project_create(project)
        type = 'type-%s' % self.id()
        value = 'value-%s' % self.id()
        tag = Tag(tag_type_name=type, tag_value=value, parent_obj=project)
        self.api.tag_create(tag)
        ag = AddressGroup(
            address_group_prefix=SubnetListType(
                subnet=[SubnetType('1.1.1.0', 24)]),
            parent_obj=project,
        )
        self.api.address_group_create(ag)
        vn = VirtualNetwork('vn-%s' % self.id(), parent_obj=project)
        self.api.virtual_network_create(vn)
        ep = FirewallRuleEndpointType(
            tags=['%s=%s' % (type.lower(), value)],
            address_group=ag.get_fq_name_str(),
            virtual_network=vn.get_fq_name_str(),
            subnet=SubnetType('1.1.1.0', 24),
        )

        fr = FirewallRule(
            parent_obj=project,
            name='rule-%s' % self.id(),
            action_list=ActionListType(simple_action='pass'),
            endpoint_1=ep,
            endpoint_2=FirewallRuleEndpointType(any=True),
            direction='<>',
            service=FirewallServiceType(),
        )
        with ExpectedException(BadRequest):
            self.api.firewall_rule_create(fr)

    def test_firewall_service(self):
        pobj = Project('%s-project' % self.id())
        self.api.project_create(pobj)

        rule_obj = FirewallRule(
            name='rule-%s' % self.id(),
            parent_obj=pobj,
            service=FirewallServiceType(),
        )
        self.api.firewall_rule_create(rule_obj)
        rule_obj = self.api.firewall_rule_read(id=rule_obj.uuid)

        # rule + service (negative test case)
        rule_obj.set_service(FirewallServiceType(
            protocol="1234", dst_ports=PortType(8080, 8082)))
        with ExpectedException(BadRequest):
            self.api.firewall_rule_update(rule_obj)

        # rule + service (positive test case)
        rule_obj.set_service(FirewallServiceType(
            protocol="udp", dst_ports=PortType(8080, 8082)))
        self.api.firewall_rule_update(rule_obj)
        rule = self.api.firewall_rule_read(id=rule_obj.uuid)
        self.assertEqual(rule.get_service().get_protocol_id(), 17)

        # service group negative test case
        fst = FirewallServiceType(protocol="1234",
                                  dst_ports=PortType(8080, 8082))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(service_group_firewall_service_list=fsgt,
                              parent_obj=pobj)
        with ExpectedException(BadRequest):
            self.api.service_group_create(sg_obj)

        # create blank service group
        fst = FirewallServiceType()
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(
            name="sg1-%s" % self.id(),
            service_group_firewall_service_list=fsgt,
            parent_obj=pobj,
        )
        self.api.service_group_create(sg_obj)
        sg = self.api.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 1)

        # update service group
        fst = FirewallServiceType(protocol="tcp",
                                  dst_ports=PortType(8080, 8082))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg.set_service_group_firewall_service_list(fsgt)
        self.api.service_group_update(sg)
        sg = self.api.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 1)
        expected_protocol_id_list = [6]
        received_list = [service.protocol_id for service in fst]
        self.assertEqual(received_list, expected_protocol_id_list)

        # update service group again and verify
        fsgt.add_firewall_service(FirewallServiceType(
            protocol="udp", dst_ports=PortType(52, 53)))
        sg.set_service_group_firewall_service_list(fsgt)
        self.api.service_group_update(sg)
        sg = self.api.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 2)
        expected_protocol_id_list = [6, 17]
        received_list = [service.protocol_id for service in fst]
        self.assertEqual(received_list, expected_protocol_id_list)

        # create a new service group
        fst = FirewallServiceType(protocol="tcp", dst_ports=PortType(80, 80))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(
            name="sg2-%s" % self.id(),
            service_group_firewall_service_list=fsgt,
            parent_obj=pobj,
        )
        self.api.service_group_create(sg_obj)
        sg = self.api.service_group_read(id=sg_obj.uuid)
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
        self.api.service_group_update(sg)
        sg = self.api.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 2)
        expected_protocol_id_list = [6, 17]
        received_list = [service.protocol_id for service in fst]
        self.assertEqual(received_list, expected_protocol_id_list)
    # end test_firewall_service

    def test_firewall_rule_update(self):
        pobj = Project('%s-project' % self.id())
        self.api.project_create(pobj)

        rule_obj = FirewallRule(
            name='rule-%s' % self.id(),
            parent_obj=pobj,
            service=FirewallServiceType(),
        )
        self.api.firewall_rule_create(rule_obj)
        rule_obj = self.api.firewall_rule_read(id=rule_obj.uuid)

        # update action_list
        rule_obj.set_action_list(ActionListType(simple_action='pass'))
        self.api.firewall_rule_update(rule_obj)
        rule_obj = self.api.firewall_rule_read(id=rule_obj.uuid)
        self.assertEqual(rule_obj.action_list.simple_action, 'pass')

        # update rule with service
        # validate protocol_id in service get populated
        rule_obj.set_service(
            FirewallServiceType(protocol="tcp",
                                dst_ports=PortType(8080, 8082)))
        self.api.firewall_rule_update(rule_obj)
        rule = self.api.firewall_rule_read(id=rule_obj.uuid)
        self.assertEqual(rule.get_service().get_protocol_id(), 6)

        # update match_tags
        match_tags = ['application', 'tier', 'deployment', 'site']
        rule_obj.set_action_list(ActionListType(simple_action='deny'))
        rule_obj.set_match_tags(FirewallRuleMatchTagsType(tag_list=match_tags))
        self.api.firewall_rule_update(rule_obj)
        rule_obj = self.api.firewall_rule_read(id=rule_obj.uuid)
        self.assertEqual(rule_obj.action_list.simple_action, 'deny')
        self.assertEqual(set(rule_obj.match_tags.tag_list), set(match_tags))
        self.assertEqual(rule_obj.get_service().get_protocol_id(), 6)

        # create tags and a label
        app1 = 'App1-%s' % self.id()
        tag1_obj = Tag(tag_type_name='application',
                       tag_value=app1, parent_obj=pobj)
        self.api.tag_create(tag1_obj)
        tag1 = self.api.tag_read(id=tag1_obj.uuid)
        web = 'web-%s' % self.id()
        tag2_obj = Tag(tag_type_name='tier', tag_value=web, parent_obj=pobj)
        self.api.tag_create(tag2_obj)
        tag2 = self.api.tag_read(id=tag2_obj.uuid)
        app2 = 'App2-%s' % self.id()
        tag3_obj = Tag(tag_type_name='application',
                       tag_value=app2, parent_obj=pobj)
        self.api.tag_create(tag3_obj)
        tag3 = self.api.tag_read(id=tag3_obj.uuid)
        db = 'Db-%s' % self.id()
        tag4_obj = Tag(tag_type_name='tier', tag_value=db, parent_obj=pobj)
        self.api.tag_create(tag4_obj)
        tag4 = self.api.tag_read(id=tag4_obj.uuid)
        red_vlan = 'RedVlan-%s' % self.id()
        tag_obj = Tag(tag_type_name='label',
                      tag_value=red_vlan, parent_obj=pobj)
        self.api.tag_create(tag_obj)
        tag_obj = self.api.tag_read(id=tag_obj.uuid)

        # update endpoint1-tags
        rule_obj.set_endpoint_1(FirewallRuleEndpointType(
            tags=['application=%s' % app1, 'tier=%s' % web]))
        self.api.firewall_rule_update(rule_obj)
        rule = self.api.firewall_rule_read(id=rule_obj.uuid)
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
        expected_set = set([int(obj.get_tag_id(), 0) for obj in [tag1, tag2]])
        received_set = set(rule.get_endpoint_1().get_tag_ids())
        self.assertEqual(received_set, expected_set)

        # update endpoint2-tags
        rule_obj = rule
        rule_obj.set_endpoint_2(FirewallRuleEndpointType(
            tags=['application=%s' % app2, 'tier=%s' % db]))
        self.api.firewall_rule_update(rule_obj)
        rule = self.api.firewall_rule_read(id=rule_obj.uuid)

        # validate rule->tag refs exists after update
        tag_refs = rule.get_tag_refs()
        self.assertEqual(len(tag_refs), 4)
        expected_set = set([
            obj.get_uuid() for obj in [tag1_obj, tag2_obj, tag3_obj, tag4_obj]
        ])
        received_set = set([ref['uuid'] for ref in tag_refs])
        self.assertEqual(received_set, expected_set)

        # validate tag_ids get populated
        expected_set = set([int(obj.get_tag_id(), 0) for obj in [tag1, tag2]])
        received_set = set(rule.get_endpoint_1().get_tag_ids())
        self.assertEqual(received_set, expected_set)
        expected_set = set([int(obj.get_tag_id(), 0) for obj in [tag3, tag4]])
        received_set = set(rule.get_endpoint_2().get_tag_ids())
        self.assertEqual(received_set, expected_set)

        # validate rule->address-group refs exist
        ag_prefix = SubnetListType(
            subnet=[SubnetType('1.1.1.0', 24), SubnetType('2.2.2.0', 24)])
        ag_obj = AddressGroup(address_group_prefix=ag_prefix, parent_obj=pobj)
        self.api.address_group_create(ag_obj)
        self.api.set_tag(ag_obj, 'label', red_vlan)
        rule.set_endpoint_2(FirewallRuleEndpointType(
            address_group=":".join(ag_obj.get_fq_name())))
        self.api.firewall_rule_update(rule)
        rule = self.api.firewall_rule_read(id=rule.uuid)
        ag_refs = rule.get_address_group_refs()
        self.assertEqual(len(ag_refs), 1)
        self.assertEqual(ag_refs[0]['uuid'], ag_obj.uuid)

        # update SG and VN in endpoint
        fst = FirewallServiceType(
            protocol="tcp", dst_ports=PortType(8080, 8082))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(
            service_group_firewall_service_list=fsgt, parent_obj=pobj)
        self.api.service_group_create(sg_obj)

        vn1_obj = VirtualNetwork('vn-%s-fe' % (self.id()), parent_obj=pobj)
        self.api.virtual_network_create(vn1_obj)
        vn2_obj = VirtualNetwork('vn-%s-be' % (self.id()), parent_obj=pobj)
        self.api.virtual_network_create(vn2_obj)

        rule.set_service(None)
        rule.set_service_group(sg_obj)
        rule.set_endpoint_2(FirewallRuleEndpointType(
            virtual_network=":".join(vn1_obj.get_fq_name())))
        self.api.firewall_rule_update(rule)
        rule = self.api.firewall_rule_read(id=rule_obj.uuid)

        self.assertEqual(set(rule.match_tags.tag_list), set(match_tags))
        self.assertIsNone(rule.get_service())

        # validate rule->tag refs exists after update
        tag_refs = rule.get_tag_refs()
        self.assertEqual(len(tag_refs), 2)
        expected_set = set([obj.get_uuid() for obj in [tag1_obj, tag2_obj]])
        received_set = set([ref['uuid'] for ref in tag_refs])
        self.assertEqual(received_set, expected_set)
        self.assertEqual(rule.endpoint_2.virtual_network,
                         ":".join(vn1_obj.get_fq_name()))

        # validate protocol_id in service stays populated
        sg = self.api.service_group_read(fq_name=sg_obj.get_fq_name())
        sg_fsl = sg.get_service_group_firewall_service_list()
        sg_firewall_service = sg_fsl.get_firewall_service()
        self.assertEqual(sg_firewall_service[0].protocol_id, 6)
    # end test_firewall_rule_update

    def test_default_global_application_policy_set(self):
        pm_obj = PolicyManagement('pm-%s' % self.id())
        self.api.policy_management_create(pm_obj)

        # check global default APS is instantiated
        fq_name = ['default-policy-management',
                   'default-application-policy-set']
        try:
            self.api.application_policy_set_read(fq_name=fq_name)
        except NoIdError:
            self.fail("Default global APS %s not instantiated" %
                      ':'.join(fq_name))

        # validate another default global APS can't be created
        aps = ApplicationPolicySet('aps-%s' % self.id(), parent_obj=pm_obj,
                                   all_applications=True)
        with ExpectedException(BadRequest):
            self.api.application_policy_set_create(aps)

        # validate global APS can't be updated for all applications
        aps = ApplicationPolicySet('aps-%s' % self.id(), parent_obj=pm_obj)
        self.api.application_policy_set_create(aps)
        aps.set_all_applications(True)
        with ExpectedException(BadRequest):
            self.api.application_policy_set_update(aps)

        # validate default global APS can't be deleted
        aps = self.api.application_policy_set_read(fq_name=fq_name)
        with ExpectedException(BadRequest):
            self.api.application_policy_set_delete(id=aps.uuid)

    def test_default_scoped_application_policy_set(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        project = self.api.project_read(id=project.uuid)

        # check scoped default APS is instantiated
        fq_name = project.fq_name + ['default-application-policy-set']
        try:
            self.api.application_policy_set_read(fq_name=fq_name)
        except NoIdError:
            self.fail("Default scoped APS %s not instantiated" %
                      ':'.join(fq_name))
        # And linked as a child and reference of the project
        self.assertEqual(len(project.get_application_policy_sets()),
                         len(project.get_application_policy_set_refs()))
        self.assertEqual(project.get_application_policy_sets()[0]['uuid'],
                         project.get_application_policy_set_refs()[0]['uuid'])

        # validate another default scoped APS can't be created
        aps = ApplicationPolicySet('aps-%s' % self.id(), parent_obj=project,
                                   all_applications=True)
        with ExpectedException(BadRequest):
            self.api.application_policy_set_create(aps)

        # validate default scoped APS can't be updated for all applications
        aps = ApplicationPolicySet('aps-%s' % self.id(), parent_obj=project)
        self.api.application_policy_set_create(aps)
        aps.set_all_applications(True)
        with ExpectedException(BadRequest):
            self.api.application_policy_set_update(aps)
        self.api.application_policy_set_delete(id=aps.uuid)

        # validate default scoped APS can't be deleted
        default_aps = self.api.application_policy_set_read(
            fq_name=fq_name)
        # remove Project reference before trying to delete default APS
        project.del_application_policy_set(default_aps)
        self.api.project_update(project)
        with ExpectedException(BadRequest):
            self.api.application_policy_set_delete(id=default_aps.uuid)

        # validate default scoped APS is cleaned when project destroyed
        project.add_application_policy_set(default_aps)
        self.api.project_update(project)
        self.api.project_delete(id=project.uuid)
        with ExpectedException(NoIdError):
            self.api.application_policy_set_read(id=default_aps.uuid)

    def test_cannot_delete_project_if_default_aps_is_in_use(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        project = self.api.project_read(id=project.uuid)
        default_aps_uuid = project.get_application_policy_sets()[0]['uuid']
        default_aps = self.api.application_policy_set_read(
            id=default_aps_uuid)
        fp = FirewallPolicy('firewall-policy-%s' % self.id(),
                            parent_obj=project)
        self.api.firewall_policy_create(fp)
        default_aps.add_firewall_policy(fp, FirewallSequence(sequence='1.0'))
        self.api.application_policy_set_update(default_aps)

        with ExpectedException(RefsExistError):
            self.api.project_delete(id=project.uuid)

    def test_can_delete_project_if_default_aps_already_deleted(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        project = self.api.project_read(id=project.uuid)
        default_aps_uuid = project.get_application_policy_sets()[0]['uuid']
        default_aps = self.api.application_policy_set_read(
            id=default_aps_uuid)
        project.del_application_policy_set(default_aps)
        self.api.project_update(project)
        self._api_server._db_conn.dbe_delete(
            'application_policy_set',
            default_aps_uuid,
            {'fq_name': default_aps.fq_name},
        )

        try:
            self.api.project_delete(id=project.uuid)
        except Exception as e:
            self.fail("Cannot delete project %s where default APS already "
                      "removed: %s" % str(e))

    def test_cannot_associate_scoped_firewall_rule_to_a_global_aps(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        pm = PolicyManagement('pm-%s' % self.id())
        self.api.policy_management_create(pm)
        scoped_fp = FirewallPolicy('scoped-fp-%s' % self.id(),
                                   parent_obj=project)
        self.api.firewall_policy_create(scoped_fp)

        global_aps = ApplicationPolicySet('global-aps-%s' % self.id(),
                                          parent_obj=pm)
        global_aps.add_firewall_policy(scoped_fp,
                                       FirewallSequence(sequence='1.0'))
        with ExpectedException(BadRequest):
            self.api.application_policy_set_create(global_aps)

        global_aps = ApplicationPolicySet('global-aps-%s' % self.id(),
                                          parent_obj=pm)
        self.api.application_policy_set_create(global_aps)
        global_aps.add_firewall_policy(scoped_fp,
                                       FirewallSequence(sequence='1.0'))
        with ExpectedException(BadRequest):
            self.api.application_policy_set_update(global_aps)

    def test_cannot_associate_scoped_fr_to_a_global_fp(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        pm = PolicyManagement('pm-%s' % self.id())
        self.api.policy_management_create(pm)
        scoped_fr = FirewallRule(
            name='scoped-fr-%s' % self.id(),
            parent_obj=project,
            service=FirewallServiceType(),
        )
        self.api.firewall_rule_create(scoped_fr)

        global_fp = FirewallPolicy('global-fp-%s' % self.id(),
                                   parent_obj=pm)
        global_fp.add_firewall_rule(scoped_fr,
                                    FirewallSequence(sequence='1.0'))
        with ExpectedException(BadRequest):
            self.api.firewall_policy_create(global_fp)

        global_fp = FirewallPolicy('global-fp-%s' % self.id(),
                                   parent_obj=pm)
        self.api.firewall_policy_create(global_fp)
        global_fp.add_firewall_rule(scoped_fr,
                                    FirewallSequence(sequence='1.0'))
        with ExpectedException(BadRequest):
            self.api.firewall_policy_update(global_fp)

    def _cannot_associate_scoped_resource_to_a_global_firewall_rule(
            self, r_class):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        pm = PolicyManagement('pm-%s' % self.id())
        self.api.policy_management_create(pm)
        scoped_r = r_class(
            name='scoped-%s-%s' % (r_class.resource_type, self.id()),
            parent_obj=project,
        )
        getattr(self._vnc_lib, '%s_create' % r_class.object_type)(scoped_r)

        global_fr = FirewallRule(
            'global-firewall-rule-%s' % self.id(),
            parent_obj=pm,
            service=FirewallServiceType(),
        )
        getattr(global_fr, 'add_%s' % r_class.object_type)(scoped_r)
        with ExpectedException(BadRequest):
            self.api.firewall_rule_create(global_fr)

        global_fr = FirewallRule(
            'global-firewall-rule-%s' % self.id(),
            parent_obj=pm,
            service=FirewallServiceType(),
        )
        self.api.firewall_rule_create(global_fr)
        getattr(global_fr, 'add_%s' % r_class.object_type)(scoped_r)
        with ExpectedException(BadRequest):
            self.api.firewall_rule_update(global_fr)

    def test_cannot_associate_scoped_address_group_to_a_global_fr(self):
        self._cannot_associate_scoped_resource_to_a_global_firewall_rule(
            AddressGroup,
        )

    def test_cannot_associate_scoped_service_group_to_a_global_fr(self):
        self._cannot_associate_scoped_resource_to_a_global_firewall_rule(
            ServiceGroup,
        )

    def test_cannot_associate_scoped_virtual_network_to_a_global_fr(self):
        self._cannot_associate_scoped_resource_to_a_global_firewall_rule(
            VirtualNetwork
        )

    def test_create_firewall_rule_with_ref_without_uuid(self):
        pm = PolicyManagement('pm-%s' % self.id())
        self.api.policy_management_create(pm)
        ag = AddressGroup('ag-%s' % self.id(), parent_obj=pm)
        self.api.address_group_create(ag)
        fr = FirewallRule(
            'fr-%s' % self.id(),
            parent_obj=pm,
            service=FirewallServiceType(),
        )
        # Re-create Address Group VNC API object without the UUID for the ref
        fr.add_address_group(
            AddressGroup(
                fq_name=ag.fq_name,
                parent_type=PolicyManagement.object_type,
            ),
        )
        self.api.firewall_rule_create(fr)

    def test_create_firewall_rule_with_or_without_defined_services(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        fr = FirewallRule(name='fr-%s' % self.id(), parent_obj=project)
        sg = ServiceGroup(
            name='fr-%s' % self.id(),
            service_group_firewall_service_list=FirewallServiceGroupType(
                firewall_service=[FirewallServiceType()]),
            parent_obj=project,
        )
        self.api.service_group_create(sg)

        with ExpectedException(BadRequest):
            self.api.firewall_rule_create(fr)

        fr.add_service_group(sg)
        fr.set_service(FirewallServiceType())
        with ExpectedException(BadRequest):
            self.api.firewall_rule_create(fr)

    def test_update_firewall_rule_with_or_without_defined_services(self):
        project = Project('project-%s' % self.id())
        self.api.project_create(project)
        fr1 = FirewallRule(name='fr1-%s' % self.id(), parent_obj=project)
        fr1.set_service(FirewallServiceType())
        fr1_id = self.api.firewall_rule_create(fr1)
        fr2 = FirewallRule(name='fr2-%s' % self.id(), parent_obj=project)
        sg = ServiceGroup(
            name='fr-%s' % self.id(),
            service_group_firewall_service_list=FirewallServiceGroupType(
                firewall_service=[FirewallServiceType()]),
            parent_obj=project,
        )
        self.api.service_group_create(sg)
        fr2.add_service_group(sg)
        fr2_id = self.api.firewall_rule_create(fr2)

        fr1.set_service(None)
        with ExpectedException(BadRequest):
            self.api.firewall_rule_update(fr1)

        fr1 = self.api.firewall_rule_read(id=fr1_id)
        fr1.add_service_group(sg)
        with ExpectedException(BadRequest):
            self.api.firewall_rule_update(fr1)

        fr2.del_service_group(sg)
        with ExpectedException(BadRequest):
            self.api.firewall_rule_update(fr2)

        fr2 = self.api.firewall_rule_read(id=fr2_id)
        fr2.set_service(FirewallServiceType())
        with ExpectedException(BadRequest):
            self.api.firewall_rule_update(fr2)


class FirewallDraftModeBase(object):
    _scope_type_name = PolicyManagement.object_type
    SECURITY_RESOURCES = [
        ApplicationPolicySet,
        FirewallPolicy,
        FirewallRule,
        ServiceGroup,
        AddressGroup,
    ]
    ACTIONS = set(['commit', 'revert'])

    def set_scope_instance(self, draft_enable=True):
        draft_pm_name = constants.POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT
        if self._scope_type_name == PolicyManagement.object_type:
            try:
                global_pm = self.api.policy_management_read(
                    ['default-policy-management'])
            except NoIdError:
                self.fail("Global policy management not initialized")
            global_pm.enable_security_policy_draft = draft_enable
            self.api.policy_management_update(global_pm)
            self._draft_pm_fq_name = [draft_pm_name]
            self._scope = global_pm
            self._project = Project('project-for-global-tests-%s' % self.id())
            self.api.project_create(self._project)
        elif self._scope_type_name == Project.object_type:
            project = Project('project-%s' % self.id())
            if draft_enable:
                project.enable_security_policy_draft = True
            self.api.project_create(project)
            self._draft_pm_fq_name = project.fq_name + [draft_pm_name]
            self._scope = project
            self._project = project
        else:
            self.fail("Resource type %s cannot own security resources" %
                      self._scope_type_name)

    def _global_scope(self):
        return self._scope.object_type == PolicyManagement.object_type

    def test_update_scope_with_security_policy_draft_enabled(self):
        self.set_scope_instance(draft_enable=False)

        with ExpectedException(NoIdError):
            self.api.policy_management_read(fq_name=self._draft_pm_fq_name)

        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)
        try:
            draft_pm = self.api.policy_management_read(self._draft_pm_fq_name)
        except NoIdError:
            self.fail("Project policy management %s dedicated to own pending "
                      "security resources was not created" %
                      ':'.join(self._draft_pm_fq_name))
        if self._global_scope():
            self.assertNotIn('parent_type', draft_pm)
            self.assertNotIn('parent_uuid', draft_pm)
        else:
            self.assertEqual(draft_pm.parent_type, self._scope.resource_type)
            self.assertEqual(draft_pm.parent_uuid, self._scope.uuid)

        self._scope.enable_security_policy_draft = False
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)
        with ExpectedException(NoIdError):
            self.api.policy_management_read(fq_name=self._draft_pm_fq_name)

    def test_cannot_set_draft_mode_state(self):
        self.set_scope_instance(draft_enable=False)

        for r_class in self.SECURITY_RESOURCES:
            resource = r_class(
                name='%s-%s' % (r_class.resource_type, self.id()),
                parent_obj=self._scope,
            )
            if r_class == FirewallRule:
                resource.set_service(FirewallServiceType())
            resource.draft_mode_state = 'deleted'
            with ExpectedException(BadRequest):
                getattr(self.api, '%s_create' % r_class.object_type)(resource)

    def test_cannot_update_draft_mode_state(self):
        self.set_scope_instance(draft_enable=False)
        resources = []
        for r_class in self.SECURITY_RESOURCES:
            resource = r_class(
                name='%s-%s' % (r_class.resource_type, self.id()),
                parent_obj=self._scope,
            )
            if r_class == FirewallRule:
                resource.set_service(FirewallServiceType())
            getattr(self.api, '%s_create' % r_class.object_type)(resource)
            resources.append(resource)

        for resource in resources:
            resource.draft_mode_state = 'created'

            with ExpectedException(BadRequest):
                getattr(self.api, '%s_update' % resource.object_type)(resource)

    def test_create_security_resources(self):
        self.set_scope_instance()

        for r_class in self.SECURITY_RESOURCES:
            resource = r_class(
                name='%s-%s' % (r_class.resource_type, self.id()),
                parent_obj=self._scope,
            )
            fq_name = resource.fq_name
            if r_class == FirewallRule:
                resource.set_service(FirewallServiceType())
            getattr(self.api, '%s_create' % r_class.object_type)(resource)
            pending_fq_name = resource.fq_name

            # No resource enforced
            with ExpectedException(NoIdError):
                resource = getattr(
                    self.api, '%s_read' % resource.object_type)(fq_name)
            # just a pending resource
            try:
                pending_resource = getattr(
                    self.api,
                    '%s_read' % resource.object_type,
                )(pending_fq_name)
            except NoIdError:
                self.fail("Pending %s resource was not created" %
                          pending_fq_name)
            self.assertEqual(pending_resource.parent_type,
                             PolicyManagement.resource_type)
            self.assertEqual(pending_fq_name[:-1], self._draft_pm_fq_name)
            self.assertEqual(pending_resource.draft_mode_state, 'created')

    def test_update_security_resources(self):
        self.set_scope_instance(draft_enable=False)
        resources = []
        for r_class in self.SECURITY_RESOURCES:
            resource = r_class(
                name='%s-%s' % (r_class.resource_type, self.id()),
                parent_obj=self._scope,
            )
            if r_class == FirewallRule:
                resource.set_service(FirewallServiceType())
            getattr(self.api, '%s_create' % r_class.object_type)(resource)
            resources.append(resource)
            self.assertEqual(resource.parent_type, self._scope.resource_type)
            self.assertIsNone(resource.draft_mode_state)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)

        for resource in resources:
            old_resource_name = resource.name
            new_resource_name = 'new-name-%s' % old_resource_name
            resource.display_name = new_resource_name
            # Add list property to generate collection update call
            resource.add_annotations(KeyValuePair(key='foo', value='bar'))
            getattr(self.api, '%s_update' % resource.object_type)(resource)

            # Resource was not updated
            resource = getattr(
                self.api, '%s_read' % resource.object_type)(resource.fq_name)
            self.assertEqual(resource.display_name, old_resource_name)
            self.assertIsNone(resource.draft_mode_state)

            # Cloned pending resource was created with updated properties and
            # owned by dedicated policy management
            pending_fq_name = self._draft_pm_fq_name + [resource.name]
            try:
                pending_resource = getattr(
                    self.api,
                    '%s_read' % resource.object_type,
                )(pending_fq_name)
            except NoIdError:
                self.fail('Pending update resource %s was not created' %
                          ':'.join(pending_fq_name))
            self.assertEqual(pending_resource.display_name, new_resource_name)
            kv = pending_resource.get_annotations().get_key_value_pair()[0]
            self.assertEqual('foo', kv.key)
            self.assertEqual('bar', kv.value)
            self.assertEqual(pending_resource.parent_type,
                             PolicyManagement.resource_type)
            self.assertEqual(pending_fq_name[:-1], self._draft_pm_fq_name)
            self.assertEqual(pending_resource.draft_mode_state, 'updated')

    def test_collection_update_security_resources(self):
        self.set_scope_instance(draft_enable=False)
        resources = []
        for r_class in self.SECURITY_RESOURCES:
            resource = r_class(
                name='%s-%s' % (r_class.resource_type, self.id()),
                parent_obj=self._scope,
            )
            if r_class == FirewallRule:
                resource.set_service(FirewallServiceType())
            getattr(self.api, '%s_create' % r_class.object_type)(resource)
            resources.append(resource)
            self.assertEqual(resource.parent_type, self._scope.resource_type)
            self.assertIsNone(resource.draft_mode_state)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)

        for resource in resources:
            resource.add_annotations(KeyValuePair(key='foo', value='bar'))
            getattr(self.api, '%s_update' % resource.object_type)(resource)

            # Resource was not updated
            resource = getattr(
                self.api, '%s_read' % resource.object_type)(resource.fq_name)
            self.assertIsNone(resource.get_annotations())
            self.assertIsNone(resource.draft_mode_state)

            # Cloned pending resource was created with updated properties and
            # owned by dedicated policy management
            pending_fq_name = self._draft_pm_fq_name + [resource.name]
            try:
                pending_resource = getattr(
                    self.api,
                    '%s_read' % resource.object_type,
                )(pending_fq_name)
            except NoIdError:
                self.fail('Pending update resource %s was not created' %
                          ':'.join(pending_fq_name))
            annotations = pending_resource.get_annotations()
            self.assertIsNotNone(annotations)
            kvs = annotations.get_key_value_pair()
            self.assertEqual(1, len(kvs))
            kv = kvs[0]
            self.assertEqual('foo', kv.key)
            self.assertEqual('bar', kv.value)
            self.assertEqual(pending_resource.parent_type,
                             PolicyManagement.resource_type)
            self.assertEqual(pending_fq_name[:-1], self._draft_pm_fq_name)
            self.assertEqual(pending_resource.draft_mode_state, 'updated')

    def test_delete_security_resources(self):
        self.set_scope_instance(draft_enable=False)
        resources = []
        for r_class in self.SECURITY_RESOURCES:
            resource = r_class(
                name='%s-%s' % (r_class.resource_type, self.id()),
                parent_obj=self._scope,
            )
            if r_class == FirewallRule:
                resource.set_service(FirewallServiceType())
            getattr(self.api, '%s_create' % r_class.object_type)(resource)
            resources.append(resource)
            self.assertEqual(resource.parent_type, self._scope.resource_type)
            self.assertIsNone(resource.draft_mode_state)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)

        for resource in resources:
            getattr(self.api, '%s_delete' % resource.object_type)(
                id=resource.uuid)

            # Resource was not deleted
            try:
                resource = getattr(self.api, '%s_read' % resource.object_type)(
                    id=resource.uuid)
            except NoIdError:
                self.fail("%s was deleted while the draft mode is enable." %
                          resource.object_type.replace('_', ' ').title())
            self.assertIsNone(resource.draft_mode_state)

            # Cloned pending resource was created with pending delete set to
            # true and owned by dedicated policy management
            pending_fq_name = self._draft_pm_fq_name + [resource.name]
            pending_resource = getattr(
                self.api, '%s_read' % resource.object_type)(pending_fq_name)
            self.assertEqual(pending_resource.parent_type,
                             PolicyManagement.resource_type)
            self.assertEqual(pending_fq_name[:-1], self._draft_pm_fq_name)
            self.assertEqual(pending_resource.draft_mode_state, 'deleted')

    def test_create_security_resource_in_pending_create(self):
        self.set_scope_instance()
        fp_name = 'fp-%s' % self.id()
        fp1 = FirewallPolicy(fp_name, parent_obj=self._scope)
        self.api.firewall_policy_create(fp1)

        fp2 = FirewallPolicy(fp_name, parent_obj=self._scope)
        with ExpectedException(BadRequest):
            self.api.firewall_policy_create(fp2)

    def test_delete_security_resource_in_pending_create(self):
        self.set_scope_instance()
        pending_fp = FirewallPolicy('fp-%s' % self.id(),
                                    parent_obj=self._scope)
        self.api.firewall_policy_create(pending_fp)

        self.api.firewall_policy_delete(id=pending_fp.uuid)
        with ExpectedException(NoIdError):
            self.api.firewall_policy_read(id=pending_fp.uuid)

    def test_update_security_resource_in_pending_create(self):
        self.set_scope_instance()
        pending_fp = FirewallPolicy('fp-%s' % self.id(),
                                    parent_obj=self._scope)
        self.api.firewall_policy_create(pending_fp)

        new_fp_name = 'new-name-%s' % pending_fp.name
        pending_fp.display_name = new_fp_name
        self.api.firewall_policy_update(pending_fp)
        pending_fp = self.api.firewall_policy_read(id=pending_fp.uuid)
        self.assertEqual(pending_fp.display_name, new_fp_name)
        self.assertFalse(
            self.api.firewall_policys_list(
                parent_id=self._scope.uuid)['firewall-policys'],
        )

    def test_update_security_resource_in_pending_update(self):
        self.set_scope_instance(draft_enable=False)
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=self._scope)
        self.api.firewall_policy_create(fp)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)

        # First time firewall policy modified since draft mode enabled,
        # resource cloned with properties updated
        old_fp_name = fp.name
        new_fp_name = 'new-name-%s' % old_fp_name
        fp.display_name = new_fp_name
        self.api.firewall_policy_update(fp)
        fp = self.api.firewall_policy_read(id=fp.uuid)
        self.assertEqual(fp.display_name, old_fp_name)
        pending_fp_fq_name = self._draft_pm_fq_name + [fp.name]
        pending_fp = self.api.firewall_policy_read(pending_fp_fq_name)
        self.assertEqual(pending_fp.display_name, new_fp_name)

        # Second time firewall policy modified since draft mode enabled,
        # cloned resource is updated with new properties
        new_new_fp_name = 'new-new-name-%s' % old_fp_name
        fp.display_name = new_new_fp_name
        self.api.firewall_policy_update(fp)
        fp = self.api.firewall_policy_read(id=fp.uuid)
        self.assertEqual(fp.display_name, old_fp_name)
        # Same cloned resource used
        pending_fp = self.api.firewall_policy_read(id=pending_fp.uuid)
        self.assertEqual(pending_fp.display_name, new_new_fp_name)

    def test_update_security_resource_in_pending_delete(self):
        self.set_scope_instance(draft_enable=False)
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=self._scope)
        self.api.firewall_policy_create(fp)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)
        self.api.firewall_policy_delete(id=fp.uuid)

        fp.display_name = 'new-name-%s' % fp.name
        with ExpectedException(BadRequest):
            self.api.firewall_policy_update(fp)

    def test_list_pending_resources(self):
        self.set_scope_instance(draft_enable=False)
        fr1 = FirewallRule('fr1-%s' % self.id(), parent_obj=self._scope)
        fr1.set_service(FirewallServiceType())
        self.api.firewall_rule_create(fr1)
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=self._scope)
        fp.add_firewall_rule(fr1, FirewallSequence(sequence='1.0'))
        self.api.firewall_policy_create(fp)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)
        fr2 = FirewallRule('fr2-%s' % self.id(), parent_obj=self._scope)
        fr2.set_service(FirewallServiceType())
        self.api.firewall_rule_create(fr2)
        fp.del_firewall_rule(fr1)
        fp.add_firewall_rule(fr2, FirewallSequence(sequence='1.0'))
        self.api.firewall_policy_update(fp)
        self.api.firewall_rule_delete(id=fr1.uuid)

        pending_resources = self.api.policy_management_read(
            self._draft_pm_fq_name)
        self.assertEqual(len(pending_resources.get_firewall_policys()), 1)
        pending_fp = self.api.firewall_policy_read(
            id=pending_resources.get_firewall_policys()[0]['uuid'],
        )
        self.assertEqual(len(pending_fp.get_firewall_rule_refs() or []), 1)
        self.assertEqual(pending_fp.get_firewall_rule_refs()[0]['uuid'],
                         fr2.uuid)
        self.assertEqual(len(pending_resources.get_firewall_rules()), 2)

    def test_commit_pending_create_resource(self):
        self.set_scope_instance()
        fp_name = 'fp-%s' % self.id()
        fp = FirewallPolicy(fp_name, parent_obj=self._scope)
        pending_fp_uuid = self.api.firewall_policy_create(fp)

        self.assertEqual(fp.fq_name[-2],
                         constants.POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT)
        self.api.commit_security(self._scope)
        try:
            fp = self.api.firewall_policy_read(self._scope.fq_name + [fp_name])
        except NoIdError:
            self.fail("Pending created Firewall Policy %s was not committed" %
                      ':'.join(self._scope.fq_name + [fp_name]))
        self.assertNotIn(constants.POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT,
                         fp.fq_name)
        self.assertEqual(fp.uuid, pending_fp_uuid)
        self.assertEqual(fp.parent_uuid, self._scope.uuid)
        with ExpectedException(NoIdError):
            self.api.firewall_policy_read(self._draft_pm_fq_name + [fp.name])

    def test_commit_pending_update_resource(self):
        self.set_scope_instance(draft_enable=False)
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=self._scope)
        self.api.firewall_policy_create(fp)
        fp = self.api.firewall_policy_read(id=fp.uuid)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)
        old_fp_name = fp.display_name
        new_fp_name = 'new-name-%s' % old_fp_name
        fp.display_name = new_fp_name
        self.api.firewall_policy_update(fp)

        fp = self.api.firewall_policy_read(id=fp.uuid)
        self.assertEqual(fp.display_name, old_fp_name)
        self.api.commit_security(self._scope)
        fp = self.api.firewall_policy_read(id=fp.uuid)
        self.assertEqual(fp.display_name, new_fp_name)
        with ExpectedException(NoIdError):
            self.api.firewall_policy_read(self._draft_pm_fq_name + [fp.name])

    def test_commit_pending_delete_resource(self):
        self.set_scope_instance(draft_enable=False)
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=self._scope)
        self.api.firewall_policy_create(fp)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)

        self.api.firewall_policy_delete(id=fp.uuid)
        self.api.commit_security(self._scope)
        with ExpectedException(NoIdError):
            self.api.firewall_policy_read(id=fp.uuid)
        with ExpectedException(NoIdError):
            self.api.firewall_policy_read(self._draft_pm_fq_name + [fp.name])

    def test_cannot_commit_if_draft_mode_not_enabled(self):
        self.set_scope_instance(draft_enable=False)

        with ExpectedException(BadRequest):
            self.api.commit_security(self._scope)

    def test_revert_pending_create_resource(self):
        self.set_scope_instance()
        fp_name = 'fp-%s' % self.id()
        fp = FirewallPolicy(fp_name, parent_obj=self._scope)
        self.api.firewall_policy_create(fp)

        self.assertEqual(fp.fq_name[-2],
                         constants.POLICY_MANAGEMENT_NAME_FOR_SECURITY_DRAFT)
        self.api.revert_security(self._scope)
        with ExpectedException(NoIdError):
            fp = self.api.firewall_policy_read(fp.fq_name)
        with ExpectedException(NoIdError):
            fp = self.api.firewall_policy_read(self._scope.fq_name + [fp_name])

    def test_revert_pending_update_resource(self):
        self.set_scope_instance(draft_enable=False)
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=self._scope)
        self.api.firewall_policy_create(fp)
        fp = self.api.firewall_policy_read(id=fp.uuid)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)
        old_fp_name = fp.display_name
        new_fp_name = 'new-name-%s' % old_fp_name
        fp.display_name = new_fp_name
        self.api.firewall_policy_update(fp)

        fp = self.api.firewall_policy_read(id=fp.uuid)
        self.assertEqual(fp.display_name, old_fp_name)
        self.api.revert_security(self._scope)
        fp = self.api.firewall_policy_read(id=fp.uuid)
        self.assertEqual(fp.display_name, old_fp_name)
        with ExpectedException(NoIdError):
            self.api.firewall_policy_read(self._draft_pm_fq_name + [fp.name])

    def test_revert_pending_delete_resource(self):
        self.set_scope_instance(draft_enable=False)
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=self._scope)
        self.api.firewall_policy_create(fp)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)

        self.api.firewall_policy_delete(id=fp.uuid)
        self.api.revert_security(self._scope)
        try:
            self.api.firewall_policy_read(id=fp.uuid)
        except NoIdError:
            self.fail("Firewall Policy removed while pending delete was "
                      "reverted")
        with ExpectedException(NoIdError):
            self.api.firewall_policy_read(self._draft_pm_fq_name + [fp.name])

    def test_cannot_revert_or_commit_if_draft_mode_not_enabled(self):
        self.set_scope_instance(draft_enable=False)

        for action in self.ACTIONS:
            with ExpectedException(BadRequest):
                getattr(self.api, '%s_security' % action)(self._scope)

    def test_lock_during_commit_or_revert_action_is_in_progress(self):
        self.set_scope_instance()

        path = '%s/%s/%s' % (
            self._api_server.security_lock_prefix,
            self._scope_type_name,
            self._scope.get_fq_name_str(),
        )
        for action in self.ACTIONS:
            opposite_action = (self.ACTIONS - set([action])).pop()
            with self._api_server._db_conn._zk_db._zk_client.lock(
                    path, 'fake_identifier %s' % opposite_action):
                with ExpectedException(BadRequest):
                    getattr(self.api, '%s_security' % action)(self._scope)

    def test_lock_released_if_any_issue_occurs_during_an_action(self):
        self.set_scope_instance()
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=self._scope)
        self.api.firewall_policy_create(fp)
        path = '%s/%s/%s' % (
            self._api_server.security_lock_prefix,
            self._scope_type_name,
            self._scope.get_fq_name_str(),
        )

        for action in self.ACTIONS:
            with patch('vnc_cfg_api_server.vnc_cfg_api_server.VncApiServer.'
                       'internal_request_delete') as ird_mock:
                ird_mock.side_effect = Exception('fake %s exception' % action)
                with ExpectedException(HttpError):
                    getattr(self.api, '%s_security' % action)(self._scope)

            scope_lock = self._api_server._db_conn._zk_db._zk_client.lock(path)
            self.assertTrue(scope_lock.acquire(blocking=False))
            scope_lock.release()

    def test_draft_mode_still_enabled_after_commit_or_revert_done(self):
        self.set_scope_instance()
        scope_read = getattr(self.api, '%s_read' % self._scope.object_type)
        scope_update = getattr(self.api, '%s_update' % self._scope.object_type)

        for action in self.ACTIONS:
            self.assertTrue(self._scope.enable_security_policy_draft)
            getattr(self.api, '%s_security' % action)(self._scope)
            self._scope = scope_read(id=self._scope.uuid)
            self.assertTrue(self._scope.enable_security_policy_draft)
            self._scope.enable_security_policy_draft = True
            scope_update(self._scope)

    def test_cannot_create_security_resource_during_action(self):
        self.set_scope_instance()
        path = '%s/%s/%s' % (
            self._api_server.security_lock_prefix,
            self._scope_type_name,
            self._scope.get_fq_name_str(),
        )

        with self._api_server._db_conn._zk_db._zk_client.lock(path,
                                                              'fake commit'):
            for r_class in self.SECURITY_RESOURCES:
                resource = r_class(
                    name='%s-%s' % (r_class.resource_type, self.id()),
                    parent_obj=self._scope,
                )
                if r_class == FirewallRule:
                    resource.set_service(FirewallServiceType())
                with ExpectedException(BadRequest):
                    getattr(
                        self.api, '%s_create' % r_class.object_type)(resource)

    def test_cannot_update_security_resource_during_action(self):
        self.set_scope_instance(draft_enable=False)
        path = '%s/%s/%s' % (
            self._api_server.security_lock_prefix,
            self._scope_type_name,
            self._scope.get_fq_name_str(),
        )
        resources = []
        for r_class in self.SECURITY_RESOURCES:
            resource = r_class(
                name='%s-%s' % (r_class.resource_type, self.id()),
                parent_obj=self._scope,
            )
            if r_class == FirewallRule:
                resource.set_service(FirewallServiceType())
            getattr(self.api, '%s_create' % r_class.object_type)(resource)
            resources.append(resource)
            self.assertEqual(resource.parent_type, self._scope.resource_type)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)

        with self._api_server._db_conn._zk_db._zk_client.lock(path,
                                                              'fake commit'):
            for resource in resources:
                resource.display_name = 'new-name-%s' % resource.name
                with ExpectedException(BadRequest):
                    getattr(self.api, '%s_update' % resource.object_type)(
                        resource)

    def test_cannot_delete_security_resource_during_action(self):
        self.set_scope_instance(draft_enable=False)
        path = '%s/%s/%s' % (
            self._api_server.security_lock_prefix,
            self._scope_type_name,
            self._scope.get_fq_name_str(),
        )
        resources = []
        for r_class in self.SECURITY_RESOURCES:
            resource = r_class(
                name='%s-%s' % (r_class.resource_type, self.id()),
                parent_obj=self._scope,
            )
            if r_class == FirewallRule:
                resource.set_service(FirewallServiceType())
            getattr(self.api, '%s_create' % r_class.object_type)(resource)
            resources.append(resource)
            self.assertEqual(resource.parent_type, self._scope.resource_type)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)

        with self._api_server._db_conn._zk_db._zk_client.lock(path,
                                                              'fake commit'):
            for resource in resources:
                with ExpectedException(BadRequest):
                    getattr(self.api, '%s_delete' % resource.object_type)(
                        id=resource.uuid)

    def test_read_draft_security_resource_required_fq_name_or_uuid(self):
        self.set_scope_instance()
        fp_name = 'fp-%s' % self.id()
        fp = FirewallPolicy(fp_name, parent_obj=self._scope)
        self.api.firewall_policy_create(fp)
        future_fq_name = self._scope.fq_name + [fp_name]

        self.assertTrue(
            isinstance(
                self.api.firewall_policy_read_draft(), str))
        self.assertTrue(
            isinstance(
                self.api.firewall_policy_read_draft(fq_name=future_fq_name),
                FirewallPolicy,
            ),
        )
        self.assertTrue(
            isinstance(
                self.api.firewall_policy_read_draft(id=fp.uuid),
                FirewallPolicy,
            ),
        )

    def test_read_draft_version_of_created_security_resource(self):
        self.set_scope_instance()
        fp_name = 'fp-%s' % self.id()
        fp = FirewallPolicy(fp_name, parent_obj=self._scope)
        self.api.firewall_policy_create(fp)
        future_fq_name = self._scope.fq_name + [fp_name]

        with ExpectedException(NoIdError):
            self.api.firewall_policy_read(fq_name=future_fq_name)
        try:
            fp_draft_from_uuid = self.api.firewall_policy_read(id=fp.uuid)
        except NoIdError:
            self.fail("Cannot read the pending created Firewall Policy")
        try:
            fp_draft_from_fq_name = self.api.firewall_policy_read_draft(
                fq_name=future_fq_name)
        except NoIdError:
            self.fail("Cannot read the draft version of the pending created "
                      "Firewall Policy")
        self.assertEqual(fp_draft_from_fq_name.uuid, fp_draft_from_uuid.uuid)
        self.assertEqual(fp_draft_from_fq_name.fq_name,
                         fp_draft_from_uuid.fq_name)
        self.assertEqual(fp_draft_from_fq_name.draft_mode_state, 'created')
        self.assertEqual(fp_draft_from_uuid.draft_mode_state, 'created')

    def test_read_draft_version_of_updated_security_resource(self):
        self.set_scope_instance(draft_enable=False)
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=self._scope)
        self.api.firewall_policy_create(fp)
        fp = self.api.firewall_policy_read(id=fp.uuid)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)
        old_fp_name = fp.display_name
        new_fp_name = 'new-name-%s' % old_fp_name
        fp.display_name = new_fp_name
        self.api.firewall_policy_update(fp)

        fp = self.api.firewall_policy_read(id=fp.uuid)
        try:
            fp_draft_from_fq_name = self.api.firewall_policy_read_draft(
                fq_name=fp.fq_name)
        except NoIdError:
            self.fail("Cannot read the draft version of the pending updated "
                      "Firewall Policy")
        try:
            fp_draft_from_uuid = self.api.firewall_policy_read_draft(
                id=fp.uuid)
        except NoIdError:
            self.fail("Cannot read the draft version of the pending updated "
                      "Firewall Policy")
        self.assertNotEqual(fp.uuid, fp_draft_from_fq_name.uuid)
        self.assertNotEqual(fp.uuid, fp_draft_from_uuid.uuid)
        self.assertEqual(fp_draft_from_fq_name.uuid, fp_draft_from_uuid.uuid)
        self.assertNotEqual(fp.fq_name, fp_draft_from_fq_name.fq_name)
        self.assertNotEqual(fp.fq_name, fp_draft_from_uuid.fq_name)
        self.assertEqual(fp_draft_from_fq_name.fq_name,
                         fp_draft_from_uuid.fq_name)
        self.assertEqual(fp.display_name, old_fp_name)
        self.assertEqual(fp_draft_from_fq_name.display_name, new_fp_name)
        self.assertEqual(fp_draft_from_uuid.display_name, new_fp_name)
        self.assertIsNone(fp.draft_mode_state)
        self.assertEqual(fp_draft_from_fq_name.draft_mode_state, 'updated')
        self.assertEqual(fp_draft_from_uuid.draft_mode_state, 'updated')

    def test_read_draft_version_of_deleted_security_resource(self):
        self.set_scope_instance(draft_enable=False)
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=self._scope)
        self.api.firewall_policy_create(fp)
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)
        self.api.firewall_policy_delete(id=fp.uuid)

        fp = self.api.firewall_policy_read(id=fp.uuid)
        try:
            fp_draft_from_fq_name = self.api.firewall_policy_read_draft(
                fq_name=fp.fq_name)
        except NoIdError:
            self.fail("Cannot read the draft version of the pending deleted "
                      "Firewall Policy")
        try:
            fp_draft_from_uuid = self.api.firewall_policy_read_draft(
                id=fp.uuid)
        except NoIdError:
            self.fail("Cannot read the draft version of the pending deleted "
                      "Firewall Policy")
        self.assertNotEqual(fp.uuid, fp_draft_from_fq_name.uuid)
        self.assertNotEqual(fp.uuid, fp_draft_from_uuid.uuid)
        self.assertEqual(fp_draft_from_fq_name.uuid, fp_draft_from_uuid.uuid)
        self.assertNotEqual(fp.fq_name, fp_draft_from_fq_name.fq_name)
        self.assertNotEqual(fp.fq_name, fp_draft_from_uuid.fq_name)
        self.assertEqual(fp_draft_from_fq_name.fq_name,
                         fp_draft_from_uuid.fq_name)
        self.assertIsNone(fp.draft_mode_state)
        self.assertEqual(fp_draft_from_fq_name.draft_mode_state, 'deleted')
        self.assertEqual(fp_draft_from_uuid.draft_mode_state, 'deleted')

    def _setup_complex_security_environment(self):
        self.vn1 = VirtualNetwork('vn1-%s' % self.id(),
                                  parent_obj=self._project)
        self.api.virtual_network_create(self.vn1)
        self.ag1 = AddressGroup(
            name='ag1-%s' % self.id(), parent_obj=self._scope)
        self.api.address_group_create(self.ag1)
        self.sg1 = ServiceGroup(
            name='sg1-%s' % self.id(),
            parent_obj=self._scope,
            service_group_firewall_service_list=FirewallServiceGroupType(
                firewall_service=[FirewallServiceType()]),
        )
        self.api.service_group_create(self.sg1)
        self.fr1 = FirewallRule(
            name='fr1-%s' % self.id(),
            parent_obj=self._scope,
            action_list=ActionListType(simple_action='pass'),
            direction='<>',
            endpoint_1=FirewallRuleEndpointType(
                virtual_network=self.vn1.get_fq_name_str()),
            service=FirewallServiceType(),
        )
        self.api.firewall_rule_create(self.fr1)
        self.fr2 = FirewallRule(
            name='fr2-%s' % self.id(),
            parent_obj=self._scope,
            action_list=ActionListType(simple_action='pass'),
            direction='<>',
        )
        self.fr2.set_service_group(self.sg1)
        self.api.firewall_rule_create(self.fr2)
        self.fr3 = FirewallRule(
            name='fr3-%s' % self.id(),
            parent_obj=self._scope,
            action_list=ActionListType(simple_action='pass'),
            direction='<>',
            endpoint_1=FirewallRuleEndpointType(
                address_group=self.ag1.get_fq_name_str()),
            service=FirewallServiceType(),
        )
        self.api.firewall_rule_create(self.fr3)

        self.fp1 = FirewallPolicy('fp1-%s' % self.id(), parent_obj=self._scope)
        self.fp1.add_firewall_rule(self.fr1, FirewallSequence(sequence='1.0'))
        self.fp1.add_firewall_rule(self.fr2, FirewallSequence(sequence='2.0'))
        self.fp1.add_firewall_rule(self.fr3, FirewallSequence(sequence='3.0'))
        self.api.firewall_policy_create(self.fp1)
        self.aps1 = ApplicationPolicySet('aps1-%s' % self.id(),
                                         parent_obj=self._scope)
        self.aps1.add_firewall_policy(self.fp1,
                                      FirewallSequence(sequence='1.0'))
        self.api.application_policy_set_create(self.aps1)

        self.security_resources = {}
        for r in [self.aps1, self.fp1, self.fr1, self.fr2, self.fr3, self.sg1,
                  self.ag1]:
            self.security_resources[r.uuid] = r

    def _update_complex_security_environment(self):
        self.updates_security_resources = dict(self.security_resources)

        # Use same service group between two firewall rule (#1 & #2)
        self.fr1.set_service('')
        self.fr1.set_service_group(self.sg1)
        self.api.firewall_rule_update(self.fr1)

        # delete firewall rule which reference an address group
        self.fp1.del_firewall_rule(self.fr3)
        self.api.firewall_policy_update(self.fp1)
        self.api.firewall_rule_delete(id=self.fr3.uuid)
        self.updates_security_resources.pop(self.fr3.uuid)

        # create a new firewall rule which uses the orphan address group
        self.fr4 = FirewallRule(
            name='fr4-%s' % self.id(),
            parent_obj=self._scope,
            action_list=ActionListType(simple_action='pass'),
            direction='<>',
            endpoint_1=FirewallRuleEndpointType(
                address_group=self.ag1.get_fq_name_str()),
            service=FirewallServiceType(),
        )
        self.api.firewall_rule_create(self.fr4)
        self.updates_security_resources[self.fr4.uuid] = self.fr4

        # create new firewall policy which reference the new firewall rule and
        # reference it from the application policy set
        self.fp2 = FirewallPolicy('fp2-%s' % self.id(), parent_obj=self._scope)
        self.fp2.add_firewall_rule(self.fr4, FirewallSequence(sequence='1.0'))
        # also use existing firewall rule #1 in that new policy
        self.fp2.add_firewall_rule(self.fr1, FirewallSequence(sequence='2.0'))
        self.api.firewall_policy_create(self.fp2)
        self.updates_security_resources[self.fp2.uuid] = self.fp2
        self.aps1.add_firewall_policy(self.fp2,
                                      FirewallSequence(sequence='2.0'))
        self.api.application_policy_set_update(self.aps1)

    def test_commit_complex_scenario(self):
        self.set_scope_instance()
        self._setup_complex_security_environment()

        self.api.commit_security(self._scope)
        for r in self.security_resources.values():
            r = getattr(self.api, '%s_read' % r.object_type)(id=r.uuid)
            self.assertEqual(r.parent_uuid, self._scope.uuid)

    def test_revert_complex_scenario(self):
        self.set_scope_instance()
        self._setup_complex_security_environment()

        self.api.revert_security(self._scope)
        for r in self.security_resources.values():
            with ExpectedException(NoIdError):
                getattr(self.api, '%s_read' % r.object_type)(id=r.uuid)

    def test_commit_updated_complex_scenario(self):
        self.set_scope_instance(draft_enable=False)
        self._setup_complex_security_environment()
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)

        self._update_complex_security_environment()
        self.api.commit_security(self._scope)
        for r in self.updates_security_resources.values():
            r = getattr(self.api, '%s_read' % r.object_type)(id=r.uuid)
            self.assertEqual(r.parent_uuid, self._scope.uuid)

    def test_revert_updated_complex_scenario(self):
        self.set_scope_instance(draft_enable=False)
        self._setup_complex_security_environment()
        self._scope.enable_security_policy_draft = True
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)

        self._update_complex_security_environment()
        self.api.revert_security(self._scope)

        for r in self.security_resources.values():
            r = getattr(self.api, '%s_read' % r.object_type)(id=r.uuid)
            self.assertEqual(r.parent_uuid, self._scope.uuid)

        for uuid in (set(self.updates_security_resources.keys()) -
                     set(self.security_resources.keys())):
            object_type = self.updates_security_resources[uuid].object_type
            with ExpectedException(NoIdError):
                getattr(self.api, '%s_read' % object_type)(id=uuid)


class TestFirewallDraftModeGlobalScope(TestFirewallBase,
                                       FirewallDraftModeBase):
    _scope_type_name = PolicyManagement.object_type

    def setUp(self):
        super(TestFirewallDraftModeGlobalScope, self).setUp()
        self._scope = None
        self._draft_pm_fq_name = None

    def tearDown(self):
        super(TestFirewallDraftModeGlobalScope, self).tearDown()
        # clean resources for the global scope case to prevent side effect
        # between tests
        if not self._scope:
            return
        path = '%s/%s/%s' % (
            self._api_server.security_lock_prefix,
            self._scope_type_name,
            self._scope.get_fq_name_str(),
        )
        scope_lock = self._api_server._db_conn._zk_db._zk_client.lock(path)
        scope_lock.destroy()
        try:
            self.api.revert_security(self._scope)
        except BadRequest:
            # Draft mode not enabled
            pass
        self._scope.enable_security_policy_draft = False
        getattr(self.api, '%s_update' % self._scope.object_type)(self._scope)
        self._scope = getattr(self.api, '%s_read' % self._scope.object_type)(
            id=self._scope.uuid)
        for r_class in self.SECURITY_RESOURCES:
            for ref in getattr(self._scope,
                               'get_%ss' % r_class.object_type)() or []:
                if ref['to'][-1] == "default-%s" % r_class.resource_type:
                    continue
                getattr(self.api, '%s_delete' % r_class.object_type)(
                    id=ref['uuid'])


class TestFirewallDraftModeProjectScope(TestFirewallBase,
                                        FirewallDraftModeBase):
    _scope_type_name = Project.object_type

    def setUp(self):
        super(TestFirewallDraftModeProjectScope, self).setUp()
        self._scope = None
        self._draft_pm_fq_name = None

    def test_create_project_with_security_policy_draft_enabled(self):
        self.set_scope_instance()
        project = self._scope

        try:
            draft_pm = self.api.policy_management_read(self._draft_pm_fq_name)
        except NoIdError:
            self.fail("Project policy management %s dedicated to own pending "
                      "security resources was not created" %
                      ':'.join(self._draft_pm_fq_name))
        self.assertEqual(draft_pm.parent_type, project.resource_type)
        self.assertEqual(draft_pm.parent_uuid, project.uuid)

        self.api.project_delete(id=project.uuid)
        with ExpectedException(NoIdError):
            self.api.policy_management_read(self._draft_pm_fq_name)

    def test_cannot_remove_project_if_pending_security_creation(self):
        self.set_scope_instance()
        project = self._scope
        fp = FirewallPolicy('fp-%s' % self.id(), parent_obj=project)
        self.api.firewall_policy_create(fp)

        with ExpectedException(RefsExistError):
            self.api.project_delete(id=project.uuid)

        self.api.firewall_policy_delete(id=fp.uuid)
        try:
            self.api.project_delete(id=project.uuid)
        except RefsExistError as e:
            self.fail("Fail to delete project %s because still have a "
                      "reference: %s" % (project.get_fq_name_str(), str(e)))
