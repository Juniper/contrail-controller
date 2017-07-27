#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import gevent
import gevent.monkey
gevent.monkey.patch_all()  # noqa
import logging

from testtools import ExpectedException
import unittest

from vnc_api.vnc_api import *
from vnc_api.gen.resource_test import *
from sandesh_common.vns import constants

import test_case


logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)


class TestFw(test_case.ApiServerTestCase):
    @classmethod
    def setUpClass(cls, *args, **kwargs):
        cls.console_handler = logging.StreamHandler()
        cls.console_handler.setLevel(logging.DEBUG)
        logger.addHandler(cls.console_handler)
        super(TestFw, cls).setUpClass(*args, **kwargs)
    # end setUpClass

    @classmethod
    def tearDownClass(cls, *args, **kwargs):
        logger.removeHandler(cls.console_handler)
        super(TestFw, cls).tearDownClass(*args, **kwargs)
    # end tearDownClass

    def test_firewall_rule_using_ep_tag(self):
        pobj = Project('%s-project' %(self.id()))
        self._vnc_lib.project_create(pobj)
        pm_obj = PolicyManagement('pm-%s' % self.id())
        self._vnc_lib.policy_management_create(pm_obj)

        app1 = 'App1-%s' % self.id()
        tag1_obj = Tag(tag_type_name='application', tag_value=app1, parent_obj=pobj)
        self._vnc_lib.tag_create(tag1_obj)
        tag1 = self._vnc_lib.tag_read(id=tag1_obj.uuid)

        web = 'Web-%s' % self.id()
        tag2_obj = Tag(tag_type_name='tier', tag_value=web, parent_obj=pobj)
        self._vnc_lib.tag_create(tag2_obj)
        tag2 = self._vnc_lib.tag_read(id=tag2_obj.uuid)

        app2 = 'App2-%s' % self.id()
        tag3_obj = Tag(tag_type_name='application', tag_value=app2, parent_obj=pobj)
        self._vnc_lib.tag_create(tag3_obj)
        tag3 = self._vnc_lib.tag_read(id=tag3_obj.uuid)

        db = 'Db-%s' % self.id()
        tag4_obj = Tag(tag_type_name='tier', tag_value=db, parent_obj=pobj)
        self._vnc_lib.tag_create(tag4_obj)
        tag4 = self._vnc_lib.tag_read(id=tag4_obj.uuid)

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
        self._vnc_lib.firewall_rule_create(rule_obj)

        # validate rule->tag refs exists
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        tag_refs = rule.get_tag_refs()
        self.assertEqual(len(tag_refs), 4)
        expected_set = set([obj.get_uuid() for obj in [tag1_obj, tag2_obj, tag3_obj, tag4_obj]])
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
        pobj = Project('%s-project' %(self.id()))
        self._vnc_lib.project_create(pobj)
        pm_obj = PolicyManagement('pm-%s' % self.id())
        self._vnc_lib.policy_management_create(pm_obj)

        # create a valid global tag
        tag_value = 'Production-%s' % self.id()
        gtag_obj = Tag(tag_type_name='deployment', tag_value=tag_value)
        self._vnc_lib.tag_create(gtag_obj)
        gtag_obj = self._vnc_lib.tag_read(id=gtag_obj.uuid)

        # create a valid global label
        red_vlan = 'RedVlan-%s' % self.id()
        tag_obj = Tag(tag_type_name='label', tag_value=red_vlan)
        self._vnc_lib.tag_create(tag_obj)
        tag_obj = self._vnc_lib.tag_read(id=tag_obj.uuid)

        ag_prefix = SubnetListType(subnet=[SubnetType('1.1.1.0', 24), SubnetType('2.2.2.0', 24)])
        ag_obj = AddressGroup(address_group_prefix=ag_prefix, parent_obj=pobj)
        self._vnc_lib.address_group_create(ag_obj)
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.set_tag(ag_obj, 'deployment', tag_value, True)
        self._vnc_lib.set_tag(ag_obj, 'label', red_vlan, True)

        app2 = 'App2-%s' % self.id()
        tag3_obj = Tag(tag_type_name='application', tag_value=app2, parent_obj=pobj)
        self._vnc_lib.tag_create(tag3_obj)
        tag3 = self._vnc_lib.tag_read(id=tag3_obj.uuid)

        db = 'Db-%s' % self.id()
        tag4_obj = Tag(tag_type_name='tier', tag_value=db, parent_obj=pobj)
        self._vnc_lib.tag_create(tag4_obj)
        tag4 = self._vnc_lib.tag_read(id=tag4_obj.uuid)

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
        pobj = Project('%s-project' %(self.id()))
        self._vnc_lib.project_create(pobj)

        fst = FirewallServiceType(protocol="tcp", dst_ports=PortType(8080, 8082))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(service_group_firewall_service_list=fsgt, parent_obj=pobj)
        self._vnc_lib.service_group_create(sg_obj)

        vn1_obj = VirtualNetwork('vn-%s-fe' %(self.id()), parent_obj=pobj)
        self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_obj = VirtualNetwork('vn-%s-be' %(self.id()), parent_obj=pobj)
        self._vnc_lib.virtual_network_create(vn2_obj)

        rule_obj = FirewallRule(name='rule-%s' % self.id(), parent_obj=pobj,
                     action_list=ActionListType(simple_action='pass'),
                     endpoint_1=FirewallRuleEndpointType(virtual_network=":".join(vn1_obj.get_fq_name())),
                     endpoint_2=FirewallRuleEndpointType(virtual_network=":".join(vn2_obj.get_fq_name())),
                     direction='<>')
        rule_obj.set_service_group(sg_obj)
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)

        # validate protocol_id in service get populated
        sg = self._vnc_lib.service_group_read(fq_name=sg_obj.get_fq_name())
        sg_fsl = sg.get_service_group_firewall_service_list()
        sg_firewall_service = sg_fsl.get_firewall_service()
        self.assertEqual(sg_firewall_service[0].protocol_id, 6)
    # end test_firewall_rule_using_sg_vn

    def test_firewall_rule_match_tags(self):
        pobj = Project('%s-project' %(self.id()))
        self._vnc_lib.project_create(pobj)

        # validate specified match-tag types
        match_tags = ['application', 'tier', 'site']
        rule_obj = FirewallRule(name='rule-%s' % self.id(), parent_obj=pobj,
                     action_list=ActionListType(simple_action='pass'),
                     match_tags=FirewallRuleMatchTagsType(tag_list=match_tags),
                     endpoint_1=FirewallRuleEndpointType(any=True),
                     endpoint_2=FirewallRuleEndpointType(any=True),
                     direction='<>')
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        received_set = set(rule.get_match_tag_types().get_tag_type())
        expected_set = set([constants.TagTypeNameToId[tag_type] for tag_type
                            in match_tags])
        self.assertEqual(expected_set, received_set)
        self._vnc_lib.firewall_rule_delete(id=rule_obj.uuid)

        # validate default match-tags
        rule_obj = FirewallRule(name='rule-%s' % self.id(), parent_obj=pobj,
                     action_list=ActionListType(simple_action='pass'),
                     endpoint_1=FirewallRuleEndpointType(any=True),
                     endpoint_2=FirewallRuleEndpointType(any=True),
                     direction='<>')
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        received_set = set(rule.get_match_tag_types().get_tag_type())
        expected_set = set([tag_type for tag_type
                            in constants.DEFAULT_MATCH_TAG_TYPE])
        self.assertEqual(expected_set, received_set)
        self._vnc_lib.firewall_rule_delete(id=rule_obj.uuid)

        # validate override of default match-tags
        rule_obj = FirewallRule(name='rule-%s' % self.id(), parent_obj=pobj,
                     action_list=ActionListType(simple_action='pass'),
                     match_tags=FirewallRuleMatchTagsType(tag_list=[]),
                     endpoint_1=FirewallRuleEndpointType(any=True),
                     endpoint_2=FirewallRuleEndpointType(any=True),
                     direction='<>')
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        received_set = set(rule.get_match_tag_types().get_tag_type())
        expected_set = set()
        self.assertEqual(expected_set, received_set)
        self._vnc_lib.firewall_rule_delete(id=rule_obj.uuid)
    # end test_firewall_rule_match_tags

    def test_firewall_service(self):
        pobj = Project('%s-project' %(self.id()))
        self._vnc_lib.project_create(pobj)

        rule_obj = FirewallRule(name='rule-%s' % self.id(), parent_obj=pobj)
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule_obj = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)

        # rule + service (negative test case)
        rule_obj.set_service(FirewallServiceType(protocol="1234", dst_ports=PortType(8080, 8082)))
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.firewall_rule_update(rule_obj)

        # rule + service (positive test case)
        rule_obj.set_service(FirewallServiceType(protocol="udp", dst_ports=PortType(8080, 8082)))
        self._vnc_lib.firewall_rule_update(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        self.assertEqual(rule.get_service().get_protocol_id(), 17)

        # service group negative test case
        fst = FirewallServiceType(protocol="1234", dst_ports=PortType(8080, 8082))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(service_group_firewall_service_list=fsgt, parent_obj=pobj)
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.service_group_create(sg_obj)

        # create blank service group
        fst = FirewallServiceType()
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(name="sg1-%s" % self.id(),
                     service_group_firewall_service_list=fsgt, parent_obj=pobj)
        self._vnc_lib.service_group_create(sg_obj)
        sg = self._vnc_lib.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 1)

        # update service group
        fst = FirewallServiceType(protocol="tcp", dst_ports=PortType(8080, 8082))
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
        fsgt.add_firewall_service(FirewallServiceType(protocol="udp", dst_ports=PortType(52, 53)))
        sg.set_service_group_firewall_service_list(fsgt)
        self._vnc_lib.service_group_update(sg)
        sg = self._vnc_lib.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 2)
        expected_protocol_id_list = [6,17]
        received_list = [service.protocol_id for service in fst]
        self.assertEqual(received_list, expected_protocol_id_list)

        # create a new service group
        fst = FirewallServiceType(protocol="tcp", dst_ports=PortType(80, 80))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(name="sg2-%s" % self.id(),
                     service_group_firewall_service_list=fsgt, parent_obj=pobj)
        self._vnc_lib.service_group_create(sg_obj)
        sg = self._vnc_lib.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 1)
        expected_protocol_id_list = [6]
        received_list = [service.protocol_id for service in fst]
        self.assertEqual(received_list, expected_protocol_id_list)

        # update service group and verify all items
        fsgt.add_firewall_service(FirewallServiceType(protocol="udp", dst_ports=PortType(52, 53)))
        sg.set_service_group_firewall_service_list(fsgt)
        self._vnc_lib.service_group_update(sg)
        sg = self._vnc_lib.service_group_read(id=sg_obj.uuid)
        fsgt = sg.get_service_group_firewall_service_list()
        fst = fsgt.get_firewall_service()
        self.assertEqual(len(fst), 2)
        expected_protocol_id_list = [6,17]
        received_list = [service.protocol_id for service in fst]
        self.assertEqual(received_list, expected_protocol_id_list)
    # end test_firewall_service

    def test_firewall_rule_update(self):
        pobj = Project('%s-project' %(self.id()))
        self._vnc_lib.project_create(pobj)

        rule_obj = FirewallRule(name='rule-%s' % self.id(), parent_obj=pobj)
        self._vnc_lib.firewall_rule_create(rule_obj)
        rule_obj = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)

        # update action_list
        rule_obj.set_action_list(ActionListType(simple_action='pass'))
        self._vnc_lib.firewall_rule_update(rule_obj)
        rule_obj = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)
        self.assertEqual(rule_obj.action_list.simple_action, 'pass')

        # update rule with service - validate protocol_id in service get populated
        rule_obj.set_service(FirewallServiceType(protocol="tcp", dst_ports=PortType(8080, 8082)))
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
        app1 = 'App1-%s' % self.id()
        tag1_obj = Tag(tag_type_name='application', tag_value=app1, parent_obj=pobj)
        self._vnc_lib.tag_create(tag1_obj)
        tag1 = self._vnc_lib.tag_read(id=tag1_obj.uuid)
        web = 'web-%s' % self.id()
        tag2_obj = Tag(tag_type_name='tier', tag_value=web, parent_obj=pobj)
        self._vnc_lib.tag_create(tag2_obj)
        tag2 = self._vnc_lib.tag_read(id=tag2_obj.uuid)
        app2 = 'App2-%s' % self.id()
        tag3_obj = Tag(tag_type_name='application', tag_value=app2, parent_obj=pobj)
        self._vnc_lib.tag_create(tag3_obj)
        tag3 = self._vnc_lib.tag_read(id=tag3_obj.uuid)
        db = 'Db-%s' % self.id()
        tag4_obj = Tag(tag_type_name='tier', tag_value=db, parent_obj=pobj)
        self._vnc_lib.tag_create(tag4_obj)
        tag4 = self._vnc_lib.tag_read(id=tag4_obj.uuid)
        red_vlan = 'RedVlan-%s' % self.id()
        tag_obj = Tag(tag_type_name='label', tag_value=red_vlan, parent_obj=pobj)
        self._vnc_lib.tag_create(tag_obj)
        tag_obj = self._vnc_lib.tag_read(id=tag_obj.uuid)

        # update endpoint1-tags
        rule_obj.set_endpoint_1(FirewallRuleEndpointType(
            tags=['application=%s' % app1, 'tier=%s' % web]))
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
        expected_set = set([int(obj.get_tag_id(), 0) for obj in [tag1, tag2]])
        received_set = set(rule.get_endpoint_1().get_tag_ids())
        self.assertEqual(received_set, expected_set)

        # update endpoint2-tags
        rule_obj = rule
        rule_obj.set_endpoint_2(FirewallRuleEndpointType(
            tags=['application=%s' % app2, 'tier=%s' % db]))
        self._vnc_lib.firewall_rule_update(rule_obj)
        rule = self._vnc_lib.firewall_rule_read(id=rule_obj.uuid)

        # validate rule->tag refs exists after update
        tag_refs = rule.get_tag_refs()
        self.assertEqual(len(tag_refs), 4)
        expected_set = set([obj.get_uuid() for obj in [tag1_obj, tag2_obj, tag3_obj, tag4_obj]])
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
        ag_prefix = SubnetListType(subnet=[SubnetType('1.1.1.0', 24), SubnetType('2.2.2.0', 24)])
        ag_obj = AddressGroup(address_group_prefix=ag_prefix, parent_obj=pobj)
        self._vnc_lib.address_group_create(ag_obj)
        self._vnc_lib.set_tag(ag_obj, 'label', red_vlan)
        rule.set_endpoint_2(FirewallRuleEndpointType(address_group=":".join(ag_obj.get_fq_name())))
        self._vnc_lib.firewall_rule_update(rule)
        rule = self._vnc_lib.firewall_rule_read(id=rule.uuid)
        ag_refs = rule.get_address_group_refs()
        self.assertEqual(len(ag_refs), 1)
        self.assertEqual(ag_refs[0]['uuid'], ag_obj.uuid)

        # update SG and VN in endpoint
        fst = FirewallServiceType(protocol="tcp", dst_ports=PortType(8080, 8082))
        fsgt = FirewallServiceGroupType(firewall_service=[fst])
        sg_obj = ServiceGroup(service_group_firewall_service_list=fsgt, parent_obj=pobj)
        self._vnc_lib.service_group_create(sg_obj)

        vn1_obj = VirtualNetwork('vn-%s-fe' %(self.id()), parent_obj=pobj)
        self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_obj = VirtualNetwork('vn-%s-be' %(self.id()), parent_obj=pobj)
        self._vnc_lib.virtual_network_create(vn2_obj)

        rule.set_service_group(sg_obj)
        rule.set_endpoint_2(FirewallRuleEndpointType(virtual_network=":".join(vn1_obj.get_fq_name())))
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
        self.assertEqual(rule.endpoint_2.virtual_network, ":".join(vn1_obj.get_fq_name()))

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
        aps = ApplicationPolicySet('aps-%s' % self.id(), parent_obj=pm_obj, is_global=True)
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.application_policy_set_create(pm_obj)

        # validate APS can't be updated to global
        aps = ApplicationPolicySet('aps-%s' % self.id(), parent_obj=pm_obj)
        self._vnc_lib.application_policy_set_create(aps)
        aps.set_is_global(True)
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.application_policy_set_update(aps)
        aps.set_is_global(False)
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.application_policy_set_update(aps)

        # validate default global APS can't be deleted
        global_aps = ['default-policy-management', 'global-application-policy-set']
        aps = self._vnc_lib.application_policy_set_read(fq_name=global_aps)
        with ExpectedException(BadRequest) as e:
            self._vnc_lib.application_policy_set_delete(id=aps.uuid)
    # end test_aps_global

# end class TestFw

if __name__ == '__main__':
    ch = logging.StreamHandler()
    ch.setLevel(logging.DEBUG)
    logger.addHandler(ch)

    # unittest.main(failfast=True)
    unittest.main()
