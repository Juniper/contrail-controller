#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

from __future__ import absolute_import

from builtins import str
import json
import uuid

from cfgm_common import rest
from vnc_api.vnc_api import NoIdError
from vnc_api.vnc_api import SecurityGroup

from schema_transformer.resources._access_control_list import \
    _access_control_list_update
from schema_transformer.resources.security_group import SecurityGroupST
from .test_case import retries, STTestCase
from .test_policy import VerifyPolicy


_PROTO_STR_TO_NUM = {
    'icmp6': '58',
    'icmp': '1',
    'tcp': '6',
    'udp': '17',
    'any': 'any',
}


class VerifySecurityGroup(VerifyPolicy):

    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib

    @retries(5)
    def wait_to_get_sg_id(self, sg_fq_name):
        sg_obj = self._vnc_lib.security_group_read(sg_fq_name)
        if sg_obj.get_security_group_id() is None:
            raise Exception('Security Group Id is none %s' % str(sg_fq_name))

    @retries(5)
    def check_security_group_id(self, sg_fq_name, verify_sg_id=None):
        sg = self._vnc_lib.security_group_read(sg_fq_name)
        sg_id = sg.get_security_group_id()
        if sg_id is None:
            raise Exception('sg id is not present for %s' % sg_fq_name)
        if verify_sg_id is not None and str(sg_id) != str(verify_sg_id):
            raise Exception('sg id is not same as passed value (%s, %s)' %
                            (str(sg_id), str(verify_sg_id)))

    @retries(5)
    def check_acl_match_sg(self, fq_name, acl_name, sg_id, is_all_rules=False):
        sg_obj = self._vnc_lib.security_group_read(fq_name)
        acls = sg_obj.get_access_control_lists()
        acl = None
        for acl_to in acls or []:
            if (acl_to['to'][-1] == acl_name):
                acl = self._vnc_lib.access_control_list_read(id=acl_to['uuid'])
                break
        self.assertTrue(acl is not None)
        match = False
        for rule in acl.access_control_list_entries.acl_rule:
            if acl_name == 'egress-access-control-list':
                if rule.match_condition.dst_address.security_group != \
                        str(sg_id):
                    if is_all_rules:
                        raise Exception(
                            'sg %s/%s not found in %s - for some rule' %
                            (str(fq_name), str(sg_id), acl_name))
                else:
                    match = True
                    break
            if acl_name == 'ingress-access-control-list':
                if rule.match_condition.src_address.security_group != \
                        str(sg_id):
                    if is_all_rules:
                        raise Exception(
                            'sg %s/%s not found in %s - for some rule' %
                            (str(fq_name), str(sg_id), acl_name))
                else:
                    match = True
                    break
        if not match:
            raise Exception('sg %s/%s not found in %s' %
                            (str(fq_name), str(sg_id), acl_name))
        return

    @retries(5)
    def check_acl_match_protocol(self, fq_name, acl_name, protocol):
        sg_obj = self._vnc_lib.security_group_read(fq_name)
        acls = sg_obj.get_access_control_lists()
        acl = None
        for acl_to in acls or []:
            if (acl_to['to'][-1] == acl_name):
                acl = self._vnc_lib.access_control_list_read(id=acl_to['uuid'])
                break
        self.assertTrue(acl is not None)
        for rule in acl.access_control_list_entries.acl_rule:
            self.assertEqual(rule.match_condition.protocol,
                             _PROTO_STR_TO_NUM.get(protocol.lower()))

    @retries(5)
    def check_no_policies_for_sg(self, fq_name):
        try:
            sg_obj = self._vnc_lib.security_group_read(fq_name)
            sg_entries = sg_obj.get_security_group_entries()
            if sg_entries.get_policy_rule():
                raise Exception('sg %s found policies' % (str(fq_name)))
        except NoIdError:
            pass

    @retries(5)
    def check_sg_refer_list(self, sg_referred_by, sg_referrer, is_present):
        self.assertEqual(is_present, sg_referred_by in
                         SecurityGroupST._sg_dict.get(sg_referrer, []))

    @retries(5)
    def check_acl_not_match_sg(self, fq_name, acl_name, sg_id):
        try:
            sg_obj = self._vnc_lib.security_group_read(fq_name)
            acls = sg_obj.get_access_control_lists()
            acl = None
            for acl_to in acls or []:
                if (acl_to['to'][-1] != acl_name):
                    continue
                acl = self._vnc_lib.access_control_list_read(id=acl_to['uuid'])
                if acl is None:
                    return
                for rule in acl.access_control_list_entries.acl_rule:
                    if acl_name == 'egress-access-control-list':
                        if rule.match_condition.dst_address.security_group == \
                                str(sg_id):
                            raise Exception(
                                'sg %s/%s found in %s - for some rule' %
                                (str(fq_name), str(sg_id), acl_name))
                    if acl_name == 'ingress-access-control-list':
                        if rule.match_condition.src_address.security_group == \
                                str(sg_id):
                            raise Exception(
                                'sg %s/%s found in %s - for some rule' %
                                (str(fq_name), str(sg_id), acl_name))
        except NoIdError:
            pass


class TestSecurityGroup(STTestCase, VerifySecurityGroup):
    def security_group_create(self, sg_name, project_fq_name):
        project_obj = self._vnc_lib.project_read(project_fq_name)
        sg_obj = SecurityGroup(name=sg_name, parent_obj=project_obj)
        self._vnc_lib.security_group_create(sg_obj)
        return sg_obj
    # end security_group_create

    def test_sg_reference(self):
        # create sg and associate egress rules with sg names
        sg1_obj = self.security_group_create('sg-1-%s' % (self.id()),
                                             ['default-domain',
                                              'default-project'])
        self.wait_to_get_sg_id(sg1_obj.get_fq_name())
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())
        rule1 = {}
        rule1['port_min'] = 0
        rule1['port_max'] = 65535
        rule1['direction'] = 'egress'
        rule1['ip_prefix'] = None
        rule1['protocol'] = 'any'
        rule1['ether_type'] = 'IPv4'
        # create rule with forward sg-names
        sg_rule1 = self._security_group_rule_build(
            rule1, "default-domain:default-project:sg-2-%s" % self.id())
        self._security_group_rule_append(sg1_obj, sg_rule1)
        sg_rule3 = self._security_group_rule_build(
            rule1, "default-domain:default-project:sg-3-%s" % self.id())
        self._security_group_rule_append(sg1_obj, sg_rule3)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_security_group_id(sg1_obj.get_fq_name())

        # check ST SG refer dict for right association
        self.check_sg_refer_list(
            sg1_obj.get_fq_name_str(),
            "default-domain:default-project:sg-2-%s" % self.id(), True)
        self.check_sg_refer_list(
            sg1_obj.get_fq_name_str(),
            "default-domain:default-project:sg-3-%s" % self.id(), True)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())

        # create another sg and associate ingress rule and check acls
        sg2_obj = self.security_group_create('sg-2-%s' % self.id(),
                                             ['default-domain',
                                              'default-project'])
        self.wait_to_get_sg_id(sg2_obj.get_fq_name())
        sg2_obj = self._vnc_lib.security_group_read(sg2_obj.get_fq_name())
        rule2 = {}
        rule2['port_min'] = 0
        rule2['port_max'] = 65535
        rule2['direction'] = 'ingress'
        rule2['ip_prefix'] = None
        rule2['protocol'] = 'any'
        rule2['ether_type'] = 'IPv4'
        # reference to SG1
        sg_rule2 = self._security_group_rule_build(rule2,
                                                   sg1_obj.get_fq_name_str())
        self._security_group_rule_append(sg2_obj, sg_rule2)
        self._vnc_lib.security_group_update(sg2_obj)
        self.check_security_group_id(sg2_obj.get_fq_name())

        # check acl updates sg2 should have sg1 id and sg1 should have sg2
        self.check_acl_match_sg(sg2_obj.get_fq_name(),
                                'ingress-access-control-list',
                                sg1_obj.get_security_group_id())

        self.check_acl_match_sg(sg1_obj.get_fq_name(),
                                'egress-access-control-list',
                                sg2_obj.get_security_group_id())

        # create sg3
        sg3_obj = self.security_group_create('sg-3-%s' % self.id(),
                                             ['default-domain',
                                              'default-project'])
        self.check_sg_refer_list(sg1_obj.get_fq_name_str(),
                                 sg3_obj.get_fq_name_str(), True)

        # remove sg2 reference rule from sg1
        self._security_group_rule_remove(sg1_obj, sg_rule1)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_acl_not_match_sg(sg1_obj.get_fq_name(),
                                    'egress-access-control-list',
                                    sg2_obj.get_security_group_id())
        self.check_sg_refer_list(sg1_obj.get_fq_name_str(),
                                 sg2_obj.get_fq_name_str(), False)

        # delete sg3
        self._vnc_lib.security_group_delete(fq_name=sg3_obj.get_fq_name())
        # sg1 still should have sg3 ref
        self.check_sg_refer_list(sg1_obj.get_fq_name_str(),
                                 sg3_obj.get_fq_name_str(), True)

        # delete sg3 ref rule from sg1
        self._security_group_rule_remove(sg1_obj, sg_rule3)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_acl_not_match_sg(sg1_obj.get_fq_name(),
                                    'egress-access-control-list',
                                    sg3_obj.get_security_group_id())
        self.check_sg_refer_list(sg1_obj.get_fq_name_str(),
                                 sg3_obj.get_fq_name_str(), False)

        # delete all SGs
        self._vnc_lib.security_group_delete(fq_name=sg1_obj.get_fq_name())
        self._vnc_lib.security_group_delete(fq_name=sg2_obj.get_fq_name())

    # end test_sg_reference

    def test_sg(self):
        # create sg and associate egress rule and check acls
        sg1_obj = self.security_group_create('sg-1-%s' % self.id(),
                                             ['default-domain',
                                              'default-project'])
        self.wait_to_get_sg_id(sg1_obj.get_fq_name())
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())
        rule1 = {}
        rule1['port_min'] = 0
        rule1['port_max'] = 65535
        rule1['direction'] = 'egress'
        rule1['ip_prefix'] = None
        rule1['protocol'] = 'any'
        rule1['ether_type'] = 'IPv4'
        sg_rule1 = self._security_group_rule_build(rule1,
                                                   sg1_obj.get_fq_name_str())
        self._security_group_rule_append(sg1_obj, sg_rule1)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_security_group_id(sg1_obj.get_fq_name())
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())
        self.check_acl_match_sg(sg1_obj.get_fq_name(),
                                'egress-access-control-list',
                                sg1_obj.get_security_group_id())

        sg1_obj.set_configured_security_group_id(100)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_security_group_id(sg1_obj.get_fq_name(), 100)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())
        self.check_acl_match_sg(sg1_obj.get_fq_name(),
                                'egress-access-control-list',
                                sg1_obj.get_security_group_id())

        # create another sg and associate ingress rule and check acls
        sg2_obj = self.security_group_create('sg-2-%s' % self.id(),
                                             ['default-domain',
                                              'default-project'])
        self.wait_to_get_sg_id(sg2_obj.get_fq_name())
        sg2_obj = self._vnc_lib.security_group_read(sg2_obj.get_fq_name())
        rule2 = {}
        rule2['port_min'] = 0
        rule2['port_max'] = 65535
        rule2['direction'] = 'ingress'
        rule2['ip_prefix'] = None
        rule2['protocol'] = 'any'
        rule2['ether_type'] = 'IPv4'
        sg_rule2 = self._security_group_rule_build(rule2,
                                                   sg2_obj.get_fq_name_str())
        self._security_group_rule_append(sg2_obj, sg_rule2)
        self._vnc_lib.security_group_update(sg2_obj)
        self.check_security_group_id(sg2_obj.get_fq_name())
        self.check_acl_match_sg(sg2_obj.get_fq_name(),
                                'ingress-access-control-list',
                                sg2_obj.get_security_group_id())

        # add ingress and egress rules to same sg and check for both
        sg_rule3 = self._security_group_rule_build(rule1,
                                                   sg2_obj.get_fq_name_str())
        self._security_group_rule_append(sg2_obj, sg_rule3)
        self._vnc_lib.security_group_update(sg2_obj)
        self.check_security_group_id(sg2_obj.get_fq_name())
        self.check_acl_match_sg(sg2_obj.get_fq_name(),
                                'egress-access-control-list',
                                sg2_obj.get_security_group_id())
        self.check_acl_match_sg(sg2_obj.get_fq_name(),
                                'ingress-access-control-list',
                                sg2_obj.get_security_group_id())

        # add one more ingress and egress
        rule1['direction'] = 'ingress'
        rule1['port_min'] = 1
        rule1['port_max'] = 100
        self._security_group_rule_append(
            sg2_obj, self._security_group_rule_build(
                rule1, sg2_obj.get_fq_name_str()))
        rule1['direction'] = 'egress'
        rule1['port_min'] = 101
        rule1['port_max'] = 200
        self._security_group_rule_append(
            sg2_obj, self._security_group_rule_build(
                rule1, sg2_obj.get_fq_name_str()))
        self._vnc_lib.security_group_update(sg2_obj)
        self.check_acl_match_sg(sg2_obj.get_fq_name(),
                                'egress-access-control-list',
                                sg2_obj.get_security_group_id(), True)
        self.check_acl_match_sg(sg2_obj.get_fq_name(),
                                'ingress-access-control-list',
                                sg2_obj.get_security_group_id(), True)

        self._vnc_lib.security_group_delete(id=sg1_obj.uuid)
        self._vnc_lib.security_group_delete(id=sg2_obj.uuid)
    # end test_sg

    def test_delete_sg(self):
        # create sg and associate egress rule and check acls
        sg1_obj = self.security_group_create('sg-1-%s' % self.id(),
                                             ['default-domain',
                                              'default-project'])
        self.wait_to_get_sg_id(sg1_obj.get_fq_name())
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())
        rule1 = {}
        rule1['ip_prefix'] = None
        rule1['protocol'] = 'any'
        rule1['ether_type'] = 'IPv4'
        rule1['sg_id'] = sg1_obj.get_security_group_id()

        rule1['direction'] = 'ingress'
        rule1['port_min'] = 1
        rule1['port_max'] = 100
        rule_in_obj = self._security_group_rule_build(
            rule1,
            sg1_obj.get_fq_name_str())
        rule1['direction'] = 'egress'
        rule1['port_min'] = 101
        rule1['port_max'] = 200
        rule_eg_obj = self._security_group_rule_build(
            rule1,
            sg1_obj.get_fq_name_str())

        self._security_group_rule_append(sg1_obj, rule_in_obj)
        self._security_group_rule_append(sg1_obj, rule_eg_obj)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_acl_match_sg(sg1_obj.get_fq_name(),
                                'egress-access-control-list',
                                sg1_obj.get_security_group_id())
        self.check_acl_match_sg(sg1_obj.get_fq_name(),
                                'ingress-access-control-list',
                                sg1_obj.get_security_group_id())

        self._security_group_rule_remove(sg1_obj, rule_in_obj)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_acl_not_match_sg(sg1_obj.get_fq_name(),
                                    'ingress-access-control-list',
                                    sg1_obj.get_security_group_id())
        self.check_acl_match_sg(sg1_obj.get_fq_name(),
                                'egress-access-control-list',
                                sg1_obj.get_security_group_id())

        self._security_group_rule_append(sg1_obj, rule_in_obj)
        self._security_group_rule_remove(sg1_obj, rule_eg_obj)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_acl_match_sg(sg1_obj.get_fq_name(),
                                'ingress-access-control-list',
                                sg1_obj.get_security_group_id())
        self.check_acl_not_match_sg(sg1_obj.get_fq_name(),
                                    'egress-access-control-list',
                                    sg1_obj.get_security_group_id())

        self._security_group_rule_remove(sg1_obj, rule_in_obj)
        self._vnc_lib.security_group_update(sg1_obj)
        self._vnc_lib.security_group_delete(fq_name=sg1_obj.get_fq_name())
        self.check_acl_not_match_sg(sg1_obj.get_fq_name(),
                                    'ingress-access-control-list',
                                    sg1_obj.get_security_group_id())
        self.check_acl_not_match_sg(sg1_obj.get_fq_name(),
                                    'egress-access-control-list',
                                    sg1_obj.get_security_group_id())
    # end test_delete_sg

    def test_create_sg_check_acl_protocol(self):
        # create sg and associate egress rule and check acls
        sg1_obj = self.security_group_create('sg-%s' % (self.id()),
                                             ['default-domain',
                                              'default-project'])
        self.wait_to_get_sg_id(sg1_obj.get_fq_name())
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())
        rule1 = {}
        rule1['ip_prefix'] = None
        rule1['protocol'] = 'icmp6'
        rule1['ether_type'] = 'IPv6'
        rule1['sg_id'] = sg1_obj.get_security_group_id()

        rule1['direction'] = 'ingress'
        rule1['port_min'] = 1
        rule1['port_max'] = 100
        rule_in_obj = self._security_group_rule_build(
            rule1,
            sg1_obj.get_fq_name_str())
        self._security_group_rule_append(sg1_obj, rule_in_obj)
        self._vnc_lib.security_group_update(sg1_obj)

        self.check_acl_match_protocol(sg1_obj.get_fq_name(),
                                      'ingress-access-control-list',
                                      rule1['protocol'])

        rule1['direction'] = 'egress'
        rule1['port_min'] = 101
        rule1['port_max'] = 200
        rule1['protocol'] = 'icmp'
        rule1['ether_type'] = 'IPv4'
        rule_eg_obj = self._security_group_rule_build(
            rule1,
            sg1_obj.get_fq_name_str())
        self._security_group_rule_append(sg1_obj, rule_eg_obj)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_acl_match_protocol(sg1_obj.get_fq_name(),
                                      'egress-access-control-list',
                                      rule1['protocol'])
        self._vnc_lib.security_group_delete(id=sg1_obj.uuid)
    # end test_create_sg_check_acl_protocol

    def test_stale_acl(self):
        # create security group
        sg_obj = self.security_group_create('sg-%s' % (self.id()),
                                            ['default-domain',
                                             'default-project'])
        # create stale acl
        acl_fq_name = sg_obj.get_fq_name() + ['egress-access-control-list']
        acl_data = json.dumps({
            'access-control-list': {
                'uuid': str(uuid.uuid4()),
                'fq_name': acl_fq_name,
                'parent_type': 'security-group'
            }
        })
        self._vnc_lib._request(rest.OP_POST,
                               url=u'/access-control-lists',
                               data=acl_data)

        # try to create another acl beside stale acl
        _access_control_list_update(
            acl_obj=None,
            name="egress-access-control-list",
            obj=sg_obj,
            entries={})

        # cleanup
        self._vnc_lib.security_group_delete(id=sg_obj.uuid)
