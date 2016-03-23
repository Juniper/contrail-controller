#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import uuid
import copy

try:
    import config_db
except ImportError:
    from schema_transformer import config_db
from vnc_api.vnc_api import (AddressType, SubnetType, PolicyRuleType,
        PortType, PolicyEntriesType, SecurityGroup, NoIdError)

from test_case import STTestCase, retries
from test_policy import VerifyPolicy


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
    def check_acl_match_sg(self, fq_name, acl_name, sg_id, is_all_rules = False):
        sg_obj = self._vnc_lib.security_group_read(fq_name)
        acls = sg_obj.get_access_control_lists()
        acl = None
        for acl_to in acls or []:
            if (acl_to['to'][-1] == acl_name):
                acl = self._vnc_lib.access_control_list_read(id=acl_to['uuid'])
                break
        self.assertTrue(acl != None)
        match = False
        for rule in acl.access_control_list_entries.acl_rule:
            if acl_name == 'egress-access-control-list':
                if rule.match_condition.dst_address.security_group != str(sg_id):
                    if is_all_rules:
                        raise Exception('sg %s/%s not found in %s - for some rule' %
                                           (str(fq_name), str(sg_id), acl_name))
                else:
                    match = True
                    break
            if acl_name == 'ingress-access-control-list':
                if rule.match_condition.src_address.security_group != str(sg_id):
                    if is_all_rules:
                        raise Exception('sg %s/%s not found in %s - for some rule' %
                                           (str(fq_name), str(sg_id), acl_name))
                else:
                    match = True
                    break
        if match == False:
            raise Exception('sg %s/%s not found in %s' %
                        (str(fq_name), str(sg_id), acl_name))
        return

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
        self.assertEqual(is_present, sg_referred_by in config_db.SecurityGroupST._sg_dict.get(sg_referrer, []))

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
                if acl == None:
                    return
                for rule in acl.access_control_list_entries.acl_rule:
                    if acl_name == 'egress-access-control-list':
                        if rule.match_condition.dst_address.security_group == str(sg_id):
                            raise Exception('sg %s/%s found in %s - for some rule' %
                                           (str(fq_name), str(sg_id), acl_name))
                    if acl_name == 'ingress-access-control-list':
                        if rule.match_condition.src_address.security_group == str(sg_id):
                            raise Exception('sg %s/%s found in %s - for some rule' %
                                           (str(fq_name), str(sg_id), acl_name))
        except NoIdError:
            pass


class TestSecurityGroup(STTestCase, VerifySecurityGroup):
    def _security_group_rule_build(self, rule_info, sg_fq_name_str):
        protocol = rule_info['protocol']
        port_min = rule_info['port_min'] or 0
        port_max = rule_info['port_max'] or 65535
        direction = rule_info['direction'] or 'ingress'
        ip_prefix = rule_info['ip_prefix']
        ether_type = rule_info['ether_type']

        if ip_prefix:
            cidr = ip_prefix.split('/')
            pfx = cidr[0]
            pfx_len = int(cidr[1])
            endpt = [AddressType(subnet=SubnetType(pfx, pfx_len))]
        else:
            endpt = [AddressType(security_group=sg_fq_name_str)]

        local = None
        remote = None
        if direction == 'ingress':
            dir = '>'
            local = endpt
            remote = [AddressType(security_group='local')]
        else:
            dir = '>'
            remote = endpt
            local = [AddressType(security_group='local')]

        if not protocol:
            protocol = 'any'

        if protocol.isdigit():
            protocol = int(protocol)
            if protocol < 0 or protocol > 255:
                raise Exception('SecurityGroupRuleInvalidProtocol-%s' % protocol)
        else:
            if protocol not in ['any', 'tcp', 'udp', 'icmp']:
                raise Exception('SecurityGroupRuleInvalidProtocol-%s' % protocol)

        if not ip_prefix and not sg_fq_name_str:
            if not ether_type:
                ether_type = 'IPv4'

        sgr_uuid = str(uuid.uuid4())
        rule = PolicyRuleType(rule_uuid=sgr_uuid, direction=dir,
                                  protocol=protocol,
                                  src_addresses=local,
                                  src_ports=[PortType(0, 65535)],
                                  dst_addresses=remote,
                                  dst_ports=[PortType(port_min, port_max)],
                                  ethertype=ether_type)
        return rule
    #end _security_group_rule_build

    def _security_group_rule_append(self, sg_obj, sg_rule):
        rules = sg_obj.get_security_group_entries()
        if rules is None:
            rules = PolicyEntriesType([sg_rule])
        else:
            for sgr in rules.get_policy_rule() or []:
                sgr_copy = copy.copy(sgr)
                sgr_copy.rule_uuid = sg_rule.rule_uuid
                if sg_rule == sgr_copy:
                    raise Exception('SecurityGroupRuleExists %s' % sgr.rule_uuid)
            rules.add_policy_rule(sg_rule)

        sg_obj.set_security_group_entries(rules)
    #end _security_group_rule_append

    def _security_group_rule_remove(self, sg_obj, sg_rule):
        rules = sg_obj.get_security_group_entries()
        if rules is None:
            raise Exception('SecurityGroupRuleNotExists %s' % sgr.rule_uuid)
        else:
            for sgr in rules.get_policy_rule() or []:
                if sgr.rule_uuid == sg_rule.rule_uuid:
                    rules.delete_policy_rule(sgr)
                    sg_obj.set_security_group_entries(rules)
                    return
            raise Exception('SecurityGroupRuleNotExists %s' % sg_rule.rule_uuid)

    #end _security_group_rule_append

    def security_group_create(self, sg_name, project_fq_name):
        project_obj = self._vnc_lib.project_read(project_fq_name)
        sg_obj = SecurityGroup(name=sg_name, parent_obj=project_obj)
        self._vnc_lib.security_group_create(sg_obj)
        return sg_obj
    #end security_group_create

    def test_sg_reference(self):
        #create sg and associate egress rules with sg names
        sg1_obj = self.security_group_create('sg-1', ['default-domain',
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
        #create rule with forward sg-names
        sg_rule1 = self._security_group_rule_build(rule1, "default-domain:default-project:sg-2")
        self._security_group_rule_append(sg1_obj, sg_rule1)
        sg_rule3 = self._security_group_rule_build(rule1, "default-domain:default-project:sg-3")
        self._security_group_rule_append(sg1_obj, sg_rule3)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_security_group_id(sg1_obj.get_fq_name())

        #check ST SG refer dict for right association
        self.check_sg_refer_list(sg1_obj.get_fq_name_str(),
                                 "default-domain:default-project:sg-2", True)
        self.check_sg_refer_list(sg1_obj.get_fq_name_str(),
                                 "default-domain:default-project:sg-3", True)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())

        #create another sg and associate ingress rule and check acls
        sg2_obj = self.security_group_create('sg-2', ['default-domain',
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
        #reference to SG1
        sg_rule2 = self._security_group_rule_build(rule2, sg1_obj.get_fq_name_str())
        self._security_group_rule_append(sg2_obj, sg_rule2)
        self._vnc_lib.security_group_update(sg2_obj)
        self.check_security_group_id(sg2_obj.get_fq_name())

        #check acl updates sg2 should have sg1 id and sg1 should have sg2
        self.check_acl_match_sg(sg2_obj.get_fq_name(),
                                'ingress-access-control-list',
                                sg1_obj.get_security_group_id())

        self.check_acl_match_sg(sg1_obj.get_fq_name(),
                                'egress-access-control-list',
                                sg2_obj.get_security_group_id())

        #create sg3
        sg3_obj = self.security_group_create('sg-3', ['default-domain',
                                                      'default-project'])
        self.check_sg_refer_list(sg1_obj.get_fq_name_str(),
                                 sg3_obj.get_fq_name_str(), True)

        #remove sg2 reference rule from sg1
        self._security_group_rule_remove(sg1_obj, sg_rule1)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_acl_not_match_sg(sg1_obj.get_fq_name(),
                                    'egress-access-control-list',
                                    sg2_obj.get_security_group_id())
        self.check_sg_refer_list(sg1_obj.get_fq_name_str(),
                                 sg2_obj.get_fq_name_str(), False)

        #delete sg3
        self._vnc_lib.security_group_delete(fq_name=sg3_obj.get_fq_name())
        #sg1 still should have sg3 ref
        self.check_sg_refer_list(sg1_obj.get_fq_name_str(),
                                 sg3_obj.get_fq_name_str(), True)

        #delete sg3 ref rule from sg1
        self._security_group_rule_remove(sg1_obj, sg_rule3)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_acl_not_match_sg(sg1_obj.get_fq_name(),
                                    'egress-access-control-list',
                                    sg3_obj.get_security_group_id())
        self.check_sg_refer_list(sg1_obj.get_fq_name_str(),
                                 sg3_obj.get_fq_name_str(), False)

        #delete all SGs
        self._vnc_lib.security_group_delete(fq_name=sg1_obj.get_fq_name())
        self._vnc_lib.security_group_delete(fq_name=sg2_obj.get_fq_name())

    #end test_sg_reference

    def test_sg(self):
        #create sg and associate egress rule and check acls
        sg1_obj = self.security_group_create('sg-1', ['default-domain',
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

        #create another sg and associate ingress rule and check acls
        sg2_obj = self.security_group_create('sg-2', ['default-domain',
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
        sg_rule2 = self._security_group_rule_build(rule2, sg2_obj.get_fq_name_str())
        self._security_group_rule_append(sg2_obj, sg_rule2)
        self._vnc_lib.security_group_update(sg2_obj)
        self.check_security_group_id(sg2_obj.get_fq_name())
        self.check_acl_match_sg(sg2_obj.get_fq_name(),
                                'ingress-access-control-list',
                                sg2_obj.get_security_group_id())

        #add ingress and egress rules to same sg and check for both
        sg_rule3 = self._security_group_rule_build(rule1, sg2_obj.get_fq_name_str())
        self._security_group_rule_append(sg2_obj, sg_rule3)
        self._vnc_lib.security_group_update(sg2_obj)
        self.check_security_group_id(sg2_obj.get_fq_name())
        self.check_acl_match_sg(sg2_obj.get_fq_name(),
                                'egress-access-control-list',
                                sg2_obj.get_security_group_id())
        self.check_acl_match_sg(sg2_obj.get_fq_name(),
                                'ingress-access-control-list',
                                sg2_obj.get_security_group_id())

        #add one more ingress and egress
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

        # duplicate security group id configured, vnc api allows
        # isn't this a problem?
        sg2_obj.set_configured_security_group_id(100)
        self._vnc_lib.security_group_update(sg2_obj)
        self.check_security_group_id(sg2_obj.get_fq_name(), 100)

        #sg id '0' is not allowed, should not get modified
        sg1_obj.set_configured_security_group_id(0)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_security_group_id(sg1_obj.get_fq_name(), 8000001)

        # -ve security group id not allowed, should not get modified
        sg1_obj.set_configured_security_group_id(-100)
        self._vnc_lib.security_group_update(sg1_obj)
        self.check_security_group_id(sg1_obj.get_fq_name(), -100)

        self._vnc_lib.security_group_delete(id=sg1_obj.uuid)
        self._vnc_lib.security_group_delete(id=sg2_obj.uuid)
    #end test_sg

    def test_delete_sg(self):
        #create sg and associate egress rule and check acls
        sg1_obj = self.security_group_create('sg-1', ['default-domain',
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
        rule_in_obj = self._security_group_rule_build(rule1,
                                                      sg1_obj.get_fq_name_str())
        rule1['direction'] = 'egress'
        rule1['port_min'] = 101
        rule1['port_max'] = 200
        rule_eg_obj = self._security_group_rule_build(rule1,
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
    #end test_delete_sg
