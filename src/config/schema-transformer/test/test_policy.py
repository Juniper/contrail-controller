#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import gevent
import gevent.monkey
gevent.monkey.patch_all()
from testtools.matchers import Contains

from vnc_api.vnc_api import (VirtualNetwork, SequenceType,
        VirtualNetworkPolicyType, NoIdError)

from test_case import STTestCase, retries, VerifyCommon
sys.path.append("../common/tests")
import test_common
from schema_transformer.to_bgp import DBBaseST


class VerifyPolicy(VerifyCommon):
    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib

    def get_ri_name(self, vn, ri_name=None):
        return vn.get_fq_name() + [ri_name or vn.name]

    @retries(5)
    def check_vn_ri_state(self, fq_name):
        self._vnc_lib.routing_instance_read(fq_name)

    @retries(5)
    def check_ri_ref_present(self, fq_name, to_fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        for ri_ref in ri.get_routing_instance_refs() or []:
            if ri_ref['to'] == to_fq_name:
                return
        raise Exception('ri_ref not found from %s to %s' % (fq_name, to_fq_name))

    @retries(5)
    def check_ri_ref_not_present(self, fq_name, to_fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        for ri_ref in ri.get_routing_instance_refs() or []:
            if ri_ref['to'] == to_fq_name:
                raise Exception('ri_ref found from %s to %s' % (fq_name, to_fq_name))

    @retries(5)
    def check_ri_refs_are_deleted(self, fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        ri_refs = ri.get_routing_instance_refs()
        if ri_refs:
            print "retrying ... ", test_common.lineno()
            raise Exception('ri_refs still exist for %s' % fq_name)

    @retries(5)
    def check_ri_is_deleted(self, fq_name):
        try:
            self._vnc_lib.routing_instance_read(fq_name)
            print "retrying ... ", test_common.lineno()
            raise Exception('routing instance %s still exists' % fq_name)
        except NoIdError:
            print 'ri deleted'

    @retries(5)
    def check_vn_is_deleted(self, uuid):
        try:
            self._vnc_lib.virtual_network_read(id=uuid)
            print "retrying ... ", test_common.lineno()
            raise Exception('virtual network %s still exists' % uuid)
        except NoIdError:
            print 'vn deleted'

    @retries(5)
    def check_acl_match_dst_cidr(self, fq_name, ip_prefix, ip_len):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.access_control_list_entries.acl_rule:
            subnets = []
            if rule.match_condition.dst_address.subnet:
                subnets.append(rule.match_condition.dst_address.subnet)
            if rule.match_condition.dst_address.subnet_list:
                subnets.extend(rule.match_condition.dst_address.subnet_list)
            for subnet in subnets:
                if (subnet.ip_prefix == ip_prefix and
                       subnet.ip_prefix_len == ip_len):
                    return
        raise Exception('prefix %s/%d not found in ACL rules for %s' %
                        (ip_prefix, ip_len, fq_name))

    @retries(5)
    def check_acl_implicit_deny_rule(self, fq_name, src_vn, dst_vn):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.access_control_list_entries.acl_rule:
            match_condition = rule.match_condition
            if (match_condition.src_address.virtual_network == src_vn and
                   match_condition.dst_address.virtual_network == dst_vn and
                   rule.action_list.simple_action == 'deny'):
                return
        raise Exception('Implicit deny ACL rule not found')

    @retries(5)
    def check_rt_in_ri(self, ri_name, rt_id, is_present=True, exim=None):
        ri_obj = self._vnc_lib.routing_instance_read(fq_name=ri_name)
        ri_rt_refs = [ref for ref in ri_obj.get_route_target_refs() or []
                      if ref['to'][0] == rt_id]
        if not is_present:
            self.assertEqual(ri_rt_refs, [])
            return
        else:
            self.assertEqual(len(ri_rt_refs), 1)
            self.assertEqual(ri_rt_refs[0]['to'][0], rt_id)
            self.assertEqual(ri_rt_refs[0]['attr'].import_export, exim)

    @retries(5)
    def check_route_target_in_routing_instance(self, ri_name, rt_list):
        ri_obj = self._vnc_lib.routing_instance_read(fq_name=ri_name)
        ri_rt_refs = set([ref['to'][0] for ref in ri_obj.get_route_target_refs() or []])
        self.assertTrue(set(rt_list) <= ri_rt_refs)


class TestPolicy(STTestCase, VerifyPolicy):

    def test_basic_policy(self):
        vn1_name = self.id() + 'vn1'
        vn2_name = self.id() + 'vn2'
        vn1_obj = VirtualNetwork(vn1_name)
        vn2_obj = VirtualNetwork(vn2_name)

        np = self.create_network_policy(vn1_obj, vn2_obj)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_create(vn1_obj)
        self._vnc_lib.virtual_network_create(vn2_obj)

        for obj in [vn1_obj, vn2_obj]:
            self.assertTill(self.vnc_db_has_ident, obj=obj)

        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj),
                                  self.get_ri_name(vn1_obj))

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        self.check_ri_refs_are_deleted(fq_name=self.get_ri_name(vn2_obj))

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
    # end test_basic_policy

    def test_multiple_policy(self):
        vn1_name = self.id() + 'vn1'
        vn2_name = self.id() + 'vn2'
        vn1_obj = VirtualNetwork(vn1_name)
        vn2_obj = VirtualNetwork(vn2_name)

        np1 = self.create_network_policy(vn1_obj, vn2_obj)
        np2 = self.create_network_policy(vn2_obj, vn1_obj)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np1, vnp)
        vn2_obj.set_network_policy(np2, vnp)
        self._vnc_lib.virtual_network_create(vn1_obj)
        self._vnc_lib.virtual_network_create(vn2_obj)

        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj),
                                  self.get_ri_name(vn1_obj))

        np1.network_policy_entries.policy_rule[0].action_list.simple_action = 'deny'
        np1.set_network_policy_entries(np1.network_policy_entries)
        self._vnc_lib.network_policy_update(np1)

        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))
        np1.network_policy_entries.policy_rule[0].action_list.simple_action = 'pass'
        np1.set_network_policy_entries(np1.network_policy_entries)
        self._vnc_lib.network_policy_update(np1)
        np2.network_policy_entries.policy_rule[0].action_list.simple_action = 'deny'
        np2.set_network_policy_entries(np2.network_policy_entries)
        self._vnc_lib.network_policy_update(np2)

        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))
        vn1_obj.del_network_policy(np1)
        vn2_obj.del_network_policy(np2)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        self.check_ri_refs_are_deleted(fq_name=self.get_ri_name(vn2_obj))

        self.delete_network_policy(np1)
        self.delete_network_policy(np2)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
    # end test_multiple_policy

    def test_policy_in_policy(self):
        vn1_name = self.id() + 'vn1'
        vn2_name = self.id() + 'vn2'
        vn3_name = self.id() + 'vn3'
        vn1_obj = VirtualNetwork(vn1_name)
        vn2_obj = VirtualNetwork(vn2_name)

        np1 = self.create_network_policy(vn1_obj, vn2_obj)
        np2 = self.create_network_policy(vn2_obj, vn1_obj)

        np1.network_policy_entries.policy_rule[0].dst_addresses[0].virtual_network = None
        np1.network_policy_entries.policy_rule[0].dst_addresses[0].network_policy = np2.get_fq_name_str()
        np1.set_network_policy_entries(np1.network_policy_entries)
        self._vnc_lib.network_policy_update(np1)

        np2.network_policy_entries.policy_rule[0].src_addresses[0].virtual_network = 'local'
        np2.set_network_policy_entries(np1.network_policy_entries)
        self._vnc_lib.network_policy_update(np2)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np1, vnp)
        vn2_obj.set_network_policy(np2, vnp)
        self._vnc_lib.virtual_network_create(vn1_obj)
        self._vnc_lib.virtual_network_create(vn2_obj)

        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj),
                                  self.get_ri_name(vn1_obj))

        vn3_obj = VirtualNetwork(vn3_name)
        vn3_obj.set_network_policy(np2, vnp)
        self._vnc_lib.virtual_network_create(vn3_obj)

        self.check_ri_ref_present(self.get_ri_name(vn3_obj),
                                  self.get_ri_name(vn1_obj))

        vn3_obj.del_network_policy(np2)
        self._vnc_lib.virtual_network_update(vn3_obj)

        @retries(5)
        def _match_acl_rule():
            acl = self._vnc_lib.access_control_list_read(
                    fq_name=self.get_ri_name(vn1_obj))
            for rule in acl.get_access_control_list_entries().get_acl_rule():
                if rule.match_condition.dst_address.virtual_network == vn3_obj.get_fq_name_str():
                    raise Exception("ACL rule still present")

        _match_acl_rule()

        vn1_obj.del_network_policy(np1)
        vn2_obj.del_network_policy(np2)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self.delete_network_policy(np1)
        self.delete_network_policy(np2)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn3_obj.get_fq_name())

        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
    # end test_multiple_policy

    def test_policy_with_cidr(self):
        vn1_name = self.id() + 'vn1'
        vn2_name = self.id() + 'vn2'
        vn1 = self.create_virtual_network(vn1_name, "10.1.1.0/24")
        vn2 = self.create_virtual_network(vn2_name, "10.2.1.0/24")
        rules = []
        rule1 = {"protocol": "icmp",
                 "direction": "<>",
                 "src": {"type": "vn", "value": vn1},
                 "dst": {"type": "cidr", "value": "10.2.1.1/32"},
                 "action": "deny"
                 }
        rule2 = {"protocol": "icmp",
                 "direction": "<>",
                 "src": {"type": "vn", "value": vn1},
                 "dst": {"type": "cidr", "value": "10.2.1.2/32"},
                 "action": "deny"
                 }
        rules.append(rule1)
        rules.append(rule2)

        np = self.create_network_policy_with_multiple_rules(rules)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1)

        self.assertTill(self.vnc_db_has_ident, obj=vn1)

        self.check_vn_ri_state(fq_name=self.get_ri_name(vn1))

        self.check_acl_match_dst_cidr(fq_name=self.get_ri_name(vn1),
                                      ip_prefix="10.2.1.1", ip_len=32)
        self.check_acl_match_dst_cidr(fq_name=self.get_ri_name(vn1),
                                      ip_prefix="10.2.1.2", ip_len=32)

        # cleanup
        self.delete_network_policy(np, auto_policy=True)
        self._vnc_lib.virtual_network_delete(fq_name=vn1.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2.get_fq_name())

        # check if vn is deleted
        self.check_vn_is_deleted(uuid=vn1.uuid)
    # end test_policy_with_cidr

    def test_policy_with_cidr_and_vn(self):
        vn1_name = self.id() + 'vn1'
        vn2_name = self.id() + 'vn2'
        vn1 = self.create_virtual_network(vn1_name, "10.1.1.0/24")
        vn2 = self.create_virtual_network(vn2_name, ["10.2.1.0/24",
                                                     "10.2.2.0/24"])
        rules = []
        rule1 = {"protocol": "icmp",
                 "direction": "<>",
                 "src": {"type": "vn", "value": vn1},
                 "dst": [{"type": "cidr_list", "value": ["10.2.1.0/24"]},
                         {"type": "vn", "value": vn2}],
                 "action": "pass"
                 }
        rules.append(rule1)

        np = self.create_network_policy_with_multiple_rules(rules)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1)

        self.assertTill(self.vnc_db_has_ident, obj=vn1)

        self.check_vn_ri_state(fq_name=self.get_ri_name(vn1))

        self.check_acl_match_dst_cidr(fq_name=self.get_ri_name(vn1),
                                      ip_prefix="10.2.1.0", ip_len=24)
        self.check_acl_implicit_deny_rule(fq_name=self.get_ri_name(vn1),
                                          src_vn=':'.join(vn1.get_fq_name()),
                                          dst_vn=':'.join(vn2.get_fq_name()))

        # cleanup
        self.delete_network_policy(np, auto_policy=True)
        self._vnc_lib.virtual_network_delete(fq_name=vn1.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2.get_fq_name())

        # check if vn is deleted
        self.check_vn_is_deleted(uuid=vn1.uuid)
        self.check_vn_is_deleted(uuid=vn2.uuid)
    # end test_policy_with_cidr_and_vn

    def test_vn_delete(self):
        vn_name = self.id() + 'vn'
        vn = self.create_virtual_network(vn_name, "10.1.1.0/24")
        gevent.sleep(2)
        self.assertTill(self.vnc_db_has_ident, obj=vn)

        self.check_vn_ri_state(fq_name=self.get_ri_name(vn))

        # stop st
        test_common.kill_schema_transformer(self._st_greenlet)
        gevent.sleep(5)

        # delete vn in api server
        self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        # start st on a free port
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self.id(), self._api_server_ip, self._api_server_port)
        test_common.wait_for_schema_transformer_up()

        # check if vn is deleted
        self.check_vn_is_deleted(uuid=vn.uuid)

        # check if ri is deleted
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn))
    # test_vn_delete

    def schema_transformer_restart(self):
        def mock_acl_update(*args, **kwargs):
            self.assertTrue(False, 'Error: Should not have updated acl entries')
        old_acl_update = DBBaseST._vnc_lib.access_control_list_update
        DBBaseST._vnc_lib.access_control_list_update = mock_acl_update
        test_common.reinit_schema_transformer()
        DBBaseST._vnc_lib.access_control_list_update = old_acl_update


    def test_acl_hash_entries(self):
        vn1_name = self.id() + 'vn1'
        vn2_name = self.id() + 'vn2'
        vn1_obj = self.create_virtual_network(vn1_name, "10.2.1.0/24")
        vn2_obj = self.create_virtual_network(vn2_name, "20.2.1.0/24")

        np = self.create_network_policy(vn1_obj, vn2_obj)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        gevent.sleep(5)
        acl = self._vnc_lib.access_control_list_read(fq_name=self.get_ri_name(vn1_obj))
        acl2 = self._vnc_lib.access_control_list_read(fq_name=self.get_ri_name(vn2_obj))
        acl_hash = acl.access_control_list_hash
        acl_hash2 = acl2.access_control_list_hash

        self.assertEqual(acl_hash, hash(acl.access_control_list_entries))
        self.assertEqual(acl_hash2, hash(acl2.access_control_list_entries))

        self.schema_transformer_restart()

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
    #end test_acl_hash_entries

class TestCompressPolicy(TestPolicy):

    def setUp(self):
         extra_config_knobs = ("--acl_direction_comp True ")
         super(TestCompressPolicy, self).setUp(extra_config_knobs=extra_config_knobs)

    @retries(5)
    def check_compress_acl_match_condition(self, condition, fq_name):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        if (len(acl.access_control_list_entries.acl_rule) != 3):
            raise Exception('Incorrect number of acls in the rule')
        for rule in acl.access_control_list_entries.acl_rule:
            subnets = []
            if rule.match_condition.src_address == condition:
                raise Exception('acl not compressed')
        return


    def test_compressed_policy(self):
        vn1_name = self.id() + 'vn1'
        vn2_name = self.id() + 'vn2'
        vn1 = self.create_virtual_network(vn1_name, "10.1.1.0/24")
        vn2 = self.create_virtual_network(vn2_name, "10.2.1.0/24")
        rules = []
        rule = {"protocol": "any",
                 "direction": "<>",
                 "src": {"type": "vn", "value": vn1},
                 "dst": {"type": "vn", "value": vn2},
                 "action": "pass"
                 }
        rules.append(rule)
        np = self.create_network_policy_with_multiple_rules(rules)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1.set_network_policy(np, vnp)
        vn2.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1)
        self._vnc_lib.virtual_network_update(vn2)

        self.assertTill(self.vnc_db_has_ident, obj=vn1)
        self.assertTill(self.vnc_db_has_ident, obj=vn2)

        self.check_vn_ri_state(fq_name=self.get_ri_name(vn1))
        self.check_vn_ri_state(fq_name=self.get_ri_name(vn2))

        self.check_compress_acl_match_condition(np._network_policy_entries.policy_rule[0].dst_addresses,
                                                fq_name=self.get_ri_name(vn1))
        self.check_compress_acl_match_condition(np._network_policy_entries.policy_rule[0].src_addresses,
                                                fq_name=self.get_ri_name(vn2))

        # cleanup
        self.delete_network_policy(np, auto_policy=True)
        self._vnc_lib.virtual_network_delete(fq_name=vn1.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2.get_fq_name())

        # check if vn is deleted
        self.check_vn_is_deleted(uuid=vn1.uuid)
        self.check_vn_is_deleted(uuid=vn2.uuid)
    # end test_compressed_policy

    def test_compressed_policy_with_cidr(self):
        vn1_name = self.id() + 'vn1'
        vn2_name = self.id() + 'vn2'
        vn1 = self.create_virtual_network(vn1_name, "10.1.1.0/24")
        vn2 = self.create_virtual_network(vn2_name, "10.2.1.0/24")
        rules = []

        rule = {"protocol": "any",
                 "direction": "<>",
                 "src": {"type": "cidr", "value": "10.1.1.1/32"},
                 "dst": {"type": "cidr", "value": "10.2.1.1/32"},
                 "action": "deny"
                 }
        rules.append(rule)
        np = self.create_network_policy_with_multiple_rules(rules)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1.set_network_policy(np, vnp)
        vn2.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1)
        self._vnc_lib.virtual_network_update(vn2)

        self.assertTill(self.vnc_db_has_ident, obj=vn1)
        self.assertTill(self.vnc_db_has_ident, obj=vn2)

        self.check_vn_ri_state(fq_name=self.get_ri_name(vn1))
        self.check_vn_ri_state(fq_name=self.get_ri_name(vn2))

        self.check_compress_acl_match_condition(np._network_policy_entries.policy_rule[0].dst_addresses,
                                                fq_name=self.get_ri_name(vn1))
        self.check_compress_acl_match_condition(np._network_policy_entries.policy_rule[0].src_addresses,
                                                fq_name=self.get_ri_name(vn2))

        # cleanup
        self.delete_network_policy(np, auto_policy=True)
        self._vnc_lib.virtual_network_delete(fq_name=vn1.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2.get_fq_name())

        # check if vn is deleted
        self.check_vn_is_deleted(uuid=vn1.uuid)
        self.check_vn_is_deleted(uuid=vn2.uuid)
    # end test_compressed_policy_with_cidr


# end TestPolicy
