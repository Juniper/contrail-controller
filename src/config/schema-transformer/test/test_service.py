#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

import sys
import gevent
sys.path.append("../common/tests")
from testtools.matchers import Equals, Contains, Not
from test_utils import *
import test_common
import test_case

from vnc_api.vnc_api import *
try:
    import to_bgp
except ImportError:
    from schema_transformer import to_bgp

from gevent import sleep

def retry_exc_handler(tries_remaining, exception, delay):
    print >> sys.stderr, "Caught '%s', %d tries remaining, sleeping for %s seconds" % (exception, tries_remaining, delay)


def retries(max_tries, delay=1, backoff=1, exceptions=(Exception,), hook=None):
    def dec(func):
        def f2(*args, **kwargs):
            mydelay = delay
            tries = range(max_tries)
            tries.reverse()
            for tries_remaining in tries:
                try:
                   return func(*args, **kwargs)
                except exceptions as e:
                    if tries_remaining > 0:
                        if hook is not None:
                            hook(tries_remaining, e, mydelay)
                        sleep(mydelay)
                        mydelay = mydelay * backoff
                    else:
                        raise
                else:
                    break
        return f2
    return dec


class TestPolicy(test_case.STTestCase):

    @retries(5, hook=retry_exc_handler)
    def check_service_chain_prefix_match(self, fq_name, prefix):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        sci = ri.get_service_chain_information()
        if sci is None:
            print "retrying ... ", test_common.lineno()
            raise Exception('Service chain info not found for %s' % fq_name)
        self.assertEqual(sci.prefix[0], prefix)

    @retries(5, hook=retry_exc_handler)
    def check_ri_rt_state_vn_policy(self, fq_name, to_fq_name, expect_to_find):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        rt_refs = ri.get_route_target_refs()
        if not rt_refs:
            print "retrying ... ", test_common.lineno()
            raise Exception('ri_refs is None for %s' % fq_name)

        found = False
        for rt_ref in rt_refs:
            rt_obj = self._vnc_lib.route_target_read(id=rt_ref['uuid'])
            ri_refs = rt_obj.get_routing_instance_back_refs()
            for ri_ref in ri_refs:
                if ri_ref['to'] == to_fq_name:
                    found = True
                    break
            if found == True:
                break
        self.assertTrue(found == expect_to_find)

    @retries(5, hook=retry_exc_handler)
    def check_ri_state_vn_policy(self, fq_name, to_fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        ri_refs = ri.get_routing_instance_refs()
        if not ri_refs:
            print "retrying ... ", test_common.lineno()
            raise Exception('ri_refs is None for %s' % fq_name)
        found = False
        for ri_ref in ri_refs:
            if ri_ref['to'] == to_fq_name:
                found = True
                break
        self.assertTrue(found)

    @retries(5, hook=retry_exc_handler)
    def check_ri_refs_are_deleted(self, fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        ri_refs = ri.get_routing_instance_refs()
        if ri_refs:
            print "retrying ... ", test_common.lineno()
            raise Exception('ri_refs still exist for %s' % fq_name)

    @retries(5, hook=retry_exc_handler)
    def check_vn_is_deleted(self, uuid):
        try:
            self._vnc_lib.virtual_network_read(id=uuid)
            print "retrying ... ", test_common.lineno()
            raise Exception('virtual network %s still exists' % uuid)
        except NoIdError:
            print 'vn deleted'

    @retries(5, hook=retry_exc_handler)
    def check_ri_is_deleted(self, fq_name):
        try:
            self._vnc_lib.routing_instance_read(fq_name)
            print "retrying ... ", test_common.lineno()
            raise Exception('routing instance %s still exists' % fq_name)
        except NoIdError:
            print 'ri deleted'

    @retries(5, hook=retry_exc_handler)
    def check_ri_is_present(self, fq_name):
        self._vnc_lib.routing_instance_read(fq_name)

    @retries(5, hook=retry_exc_handler)
    def check_link_in_ifmap_graph(self, fq_name_str, links):
        self._vnc_lib.routing_instance_read(fq_name)

    @retries(5, hook=retry_exc_handler)
    def wait_to_get_sc(self):
        sc = [x for x in to_bgp.ServiceChain]
        if len(sc) == 0:
            print "retrying ... ", test_common.lineno()
            raise Exception('Service chain not found')
        return sc

    @retries(5, hook=retry_exc_handler)
    def wait_to_get_link(self, ident_name, link_fq_name):
        self.assertThat(str(FakeIfmapClient._graph[ident_name]['links']), Contains(link_fq_name))

    @retries(5, hook=retry_exc_handler)
    def wait_to_remove_link(self, ident_name, link_fq_name):
        self.assertThat(str(FakeIfmapClient._graph[ident_name]['links']), Not(Contains(link_fq_name)))

    @retries(5, hook=retry_exc_handler)
    def wait_to_get_sg_id(self, sg_fq_name):
        sg_obj = self._vnc_lib.security_group_read(sg_fq_name)
        if sg_obj.get_security_group_id() is None:
            raise Exception('Security Group Id is none %s' % str(sg_fq_name)) 

    @retries(5, hook=retry_exc_handler)
    def check_acl_match_dst_cidr(self, fq_name, ip_prefix, ip_len):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.access_control_list_entries.acl_rule:
            if (rule.match_condition.dst_address.subnet is not None and
                rule.match_condition.dst_address.subnet.ip_prefix == ip_prefix and
                rule.match_condition.dst_address.subnet.ip_prefix_len == ip_len):
                    return
        raise Exception('prefix %s/%d not found in ACL rules for %s' %
                        (ip_prefix, ip_len, fq_name))

    @retries(5, hook=retry_exc_handler)
    def check_acl_match_nets(self, fq_name, vn1_fq_name, vn2_fq_name):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.access_control_list_entries.acl_rule:
            if (rule.match_condition.src_address.virtual_network == vn1_fq_name and
                rule.match_condition.dst_address.virtual_network == vn2_fq_name):
                    return
        raise Exception('nets %s/%s not found in ACL rules for %s' %
                        (vn1_fq_name, vn2_fq_name, fq_name))

    @retries(5, hook=retry_exc_handler)
    def check_acl_match_sg(self, fq_name, acl_name, sg_id):
        sg_obj = self._vnc_lib.security_group_read(fq_name)
        acls = sg_obj.get_access_control_lists()
        for acl_to in acls or []:
            if (acl_to['to'][-1] == 'egress-access-control-list' and 
                acl_name == 'egress-access-control-list'):
                acl = self._vnc_lib.access_control_list_read(id=acl_to['uuid'])
                break
            elif (acl_to['to'][-1] == 'ingress-access-control-list' and
                  acl_name == 'ingress-access-control-list'):
                acl = self._vnc_lib.access_control_list_read(id=acl_to['uuid'])
                break
        self.assertTrue(acl != None)
        for rule in acl.access_control_list_entries.acl_rule:
            if acl_name == 'egress-access-control-list':
                if rule.match_condition.dst_address.security_group == sg_id:
                    return
            if acl_name == 'ingress-access-control-list':
                if rule.match_condition.src_address.security_group == sg_id:
                    return
        raise Exception('sg %s/%s not found in %s' %
                        (str(fq_name), str(sg_id), acl_name))

    @retries(5, hook=retry_exc_handler)
    def check_acl_not_match_nets(self, fq_name, vn1_fq_name, vn2_fq_name):
        acl = None
        try:
            acl = self._vnc_lib.access_control_list_read(fq_name)
        except NoIdError:
            return
        found = False
        for rule in acl.access_control_list_entries.acl_rule:
            if (rule.match_condition.src_address.virtual_network == vn1_fq_name and
                rule.match_condition.dst_address.virtual_network == vn2_fq_name):
                found = True
        if found == True:
            raise Exception('nets %s/%s found in ACL rules for %s' %
                        (vn1_fq_name, vn2_fq_name, fq_name))
        return

    @retries(5, hook=retry_exc_handler)
    def check_acl_not_match_mirror_to_ip(self, fq_name):
        acl = None
        try:
            acl = self._vnc_lib.access_control_list_read(fq_name)
        except NoIdError:
            return
        for rule in acl.access_control_list_entries.acl_rule:
            if (rule.action_list.mirror_to.analyzer_ip_address is not None):
                raise Exception('mirror to ip %s found in ACL rules for %s' % (fq_name))
        return

    @retries(5, hook=retry_exc_handler)
    def check_acl_match_mirror_to_ip(self, fq_name):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.access_control_list_entries.acl_rule:
            if (rule.action_list.mirror_to.analyzer_ip_address is not None):
                return
        raise Exception('mirror to ip not found in ACL rules for %s' % (fq_name))

    @retries(5, hook=retry_exc_handler)
    def check_route_target_in_routing_instance(self, ri_name, rt_list):
        ri_obj = self._vnc_lib.routing_instance_read(fq_name=ri_name)
        ri_rt_refs = set([ref['to'][0] for ref in ri_obj.get_route_target_refs() or []])
        self.assertTrue(set(rt_list) <= ri_rt_refs)

    def get_ri_name(self, vn, ri_name=None):
        return vn.get_fq_name() + [ri_name or vn.name]

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
        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)

        for obj in [vn1_obj, vn2_obj]:
            ident_name = self.get_obj_imid(obj)
            gevent.sleep(2)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        self.check_ri_state_vn_policy(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn2_obj))
        self.check_ri_state_vn_policy(self.get_ri_name(vn2_obj),
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
        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)

        self.check_ri_state_vn_policy(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn2_obj))
        self.check_ri_state_vn_policy(self.get_ri_name(vn2_obj),
                                      self.get_ri_name(vn1_obj))

        np1.network_policy_entries.policy_rule[0].action_list.simple_action = 'deny'
        np1.set_network_policy_entries(np1.network_policy_entries)
        self._vnc_lib.network_policy_update(np1)

        expr =("('contrail:connection contrail:routing-instance:%s' in FakeIfmapClient._graph['contrail:routing-instance:%s']['links'])"
               % (':'.join(self.get_ri_name(vn2_obj)),
                  ':'.join(self.get_ri_name(vn1_obj))))
        self.assertTill(expr)
        np1.network_policy_entries.policy_rule[0].action_list.simple_action = 'pass'
        np1.set_network_policy_entries(np1.network_policy_entries)
        self._vnc_lib.network_policy_update(np1)
        np2.network_policy_entries.policy_rule[0].action_list.simple_action = 'deny'
        np2.set_network_policy_entries(np2.network_policy_entries)
        self._vnc_lib.network_policy_update(np2)

        expr = ("('contrail:connection contrail:routing-instance:%s' in FakeIfmapClient._graph['contrail:routing-instance:%s']['links'])"
               % (':'.join(self.get_ri_name(vn1_obj)),
                  ':'.join(self.get_ri_name(vn2_obj))))

        self.assertTill(expr)
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
        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)

        self.check_ri_state_vn_policy(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn2_obj))
        self.check_ri_state_vn_policy(self.get_ri_name(vn2_obj),
                                      self.get_ri_name(vn1_obj))

        vn3_obj = VirtualNetwork(vn3_name)
        vn3_obj.set_network_policy(np2, vnp)
        vn3_uuid = self._vnc_lib.virtual_network_create(vn3_obj)

        self.check_ri_state_vn_policy(self.get_ri_name(vn3_obj),
                                      self.get_ri_name(vn1_obj))

        vn3_obj.del_network_policy(np2)
        self._vnc_lib.virtual_network_update(vn3_obj)

        @retries(5, hook=retry_exc_handler)
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

    def test_service_policy(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, '20.0.0.0/24')

        service_name = self.id() + 's1'
        np = self.create_network_policy(vn1_obj, vn2_obj, [service_name])
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        sc = self.wait_to_get_sc()
        sc_ri_name = 'service-'+sc[0]+'-default-domain_default-project_' + service_name
        self.check_ri_state_vn_policy(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_state_vn_policy(self.get_ri_name(vn2_obj, sc_ri_name),
                                      self.get_ri_name(vn2_obj))

        self.check_service_chain_prefix_match(fq_name=self.get_ri_name(vn2_obj, sc_ri_name),
                                       prefix='10.0.0.0/24')

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self.check_ri_refs_are_deleted(fq_name=self.get_ri_name(vn1_obj))

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
    # end test_service_policy

    def test_service_policy_no_vm(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, '20.0.0.0/24')

        service_name = self.id() + 's1'
        np = self.create_network_policy(vn1_obj, vn2_obj)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        np.network_policy_entries.policy_rule[0].action_list.apply_service = ["default-domain:default-project:"+service_name]
        np.set_network_policy_entries(np.network_policy_entries)
        self._vnc_lib.network_policy_update(np)
        sc = self.wait_to_get_sc()
        sc_ri_name = 'service-'+sc[0]+'-default-domain_default-project_' + service_name
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn1_obj, sc_ri_name))

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self.check_ri_refs_are_deleted(fq_name=self.get_ri_name(vn1_obj))

        np.network_policy_entries.policy_rule[0].action_list.apply_service = []
        np.set_network_policy_entries(np.network_policy_entries)
        self._vnc_lib.network_policy_update(np)
        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
    # end test_service_policy_no_vm

    def test_multi_service_policy(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, '20.0.0.0/24')

        service_names = [self.id() + 's1', self.id() + 's2', self.id() + 's3']
        np = self.create_network_policy(vn1_obj, vn2_obj, service_names)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        sc = self.wait_to_get_sc()
        sc_ri_names = ['service-'+sc[0]+'-default-domain_default-project_' + s for s in service_names]
        self.check_ri_state_vn_policy(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn1_obj, sc_ri_names[0]))
        self.check_ri_state_vn_policy(self.get_ri_name(vn2_obj, sc_ri_names[-1]),
                                      self.get_ri_name(vn2_obj))

        self.check_service_chain_prefix_match(fq_name=self.get_ri_name(vn2_obj, sc_ri_names[0]),
                                       prefix='10.0.0.0/24')

        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn1_obj, sc_ri_names[0]))
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn1_obj, sc_ri_names[1]))
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn1_obj, sc_ri_names[2]))


        vn1_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_ri_refs_are_deleted(fq_name=self.get_ri_name(vn1_obj))

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
    # end test_service_policy

# end class TestPolicy

#class TestRouteTable(test_case.STTestCase):
    def test_add_delete_route(self):
        lvn_name = self.id() + 'lvn'
        rvn_name = self.id() + 'rvn'
        lvn = self.create_virtual_network(lvn_name, "10.0.0.0/24")
        rvn = self.create_virtual_network(rvn_name, "20.0.0.0/24")

        service_name = self.id() + 's1'
        np = self.create_network_policy(lvn, rvn, [service_name], "in-network")

        vn_name = self.id() + 'vn100'
        vn = self.create_virtual_network(vn_name, "1.0.0.0/24")
        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        vn.set_route_target_list(rtgt_list)
        self._vnc_lib.virtual_network_update(vn)
        rt = RouteTable("rt1")
        self._vnc_lib.route_table_create(rt)
        vn.add_route_table(rt)
        self._vnc_lib.virtual_network_update(vn)
        routes = RouteTableType()
        route = RouteType(prefix="0.0.0.0/0",
                          next_hop="default-domain:default-project:"+service_name)
        routes.add_route(route)
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        @retries(5, hook=retry_exc_handler)
        def _match_route_table(rtgt_list):
            sc = [x for x in to_bgp.ServiceChain]
            if len(sc) == 0:
                raise Exception("sc has 0 len")

            sc_ri_name = ('service-'+sc[0] +
                          '-default-domain_default-project_' + service_name)
            lri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(lvn, sc_ri_name))
            sr = lri.get_static_route_entries()
            if sr is None:
                raise Exception("sr is None")
            route = sr.route[0]
            self.assertEqual(route.prefix, "0.0.0.0/0")
            self.assertEqual(route.next_hop, "10.0.0.252")
            for rtgt in rtgt_list:
                self.assertIn(rtgt, route.route_target)
            ri100 = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(vn))
            rt100 = ri100.get_route_target_refs()[0]['to']
            for rt_ref in lri.get_route_target_refs() or []:
                if rt100 == rt_ref['to']:
                    return sc_ri_name, rt100
            raise Exception("rt100 route-target ref not found")

        sc_ri_name, rt100 = _match_route_table(rtgt_list.get_route_target())

        rtgt_list.add_route_target('target:1:2')
        vn.set_route_target_list(rtgt_list)
        self._vnc_lib.virtual_network_update(vn)
        _match_route_table(rtgt_list.get_route_target())
       
        rtgt_list.delete_route_target('target:1:1')
        vn.set_route_target_list(rtgt_list)
        self._vnc_lib.virtual_network_update(vn)
        _match_route_table(rtgt_list.get_route_target())

        routes.set_route([])
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        @retries(5, hook=retry_exc_handler)
        def _match_route_table_cleanup(sc_ri_name, rt100):
            lri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(lvn, sc_ri_name))
            sr = lri.get_static_route_entries()
            if sr and sr.route:
                raise Exception("sr has route")
            ri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(lvn))
            rt_refs = ri.get_route_target_refs()
            for rt_ref in ri.get_route_target_refs() or []:
                if rt100 == rt_ref['to']:
                    raise Exception("rt100 route-target ref found")

        _match_route_table_cleanup(sc_ri_name, rt100)

        self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())
        self.delete_network_policy(np, auto_policy=True)
        gevent.sleep(2)
        self._vnc_lib.virtual_network_delete(fq_name=lvn.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=rvn.get_fq_name())
    # test_add_delete_route

    def test_vn_delete(self):
        vn_name = self.id() + 'vn'
        vn = self.create_virtual_network(vn_name, "10.1.1.0/24")
        gevent.sleep(2)
        for obj in [vn]:
            ident_name = self.get_obj_imid(obj)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        self.check_vn_ri_state(fq_name=self.get_ri_name(vn))

        # stop st
        self._st_greenlet.kill()
        gevent.sleep(5)

        # delete vn in api server
        self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())

        # start st on a free port
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self._api_server_ip, self._api_server_port)
        gevent.sleep(2)

        # check if vn is deleted
        self.check_vn_is_deleted(uuid=vn.uuid)

        # check if ri is deleted
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn))
    # test_vn_delete

    @retries(5, hook=retry_exc_handler)
    def check_vn_ri_state(self, fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)

    def test_policy_with_cidr(self):
        vn1_name = self.id() + 'vn1'
        vn2_name = self.id() + 'vn2'
        vn1 = self.create_virtual_network(vn1_name, "10.1.1.0/24")
        vn2 = self.create_virtual_network(vn2_name, "10.2.1.0/24")
        rules = []
        rule1 = { "protocol": "icmp",
                  "direction": "<>",
                  "src-port": "any",
                  "src": {"type": "vn", "value": vn1},
                  "dst": {"type": "cidr", "value": "10.2.1.1/32"},
                  "dst-port": "any",
                  "action": "deny"
                 }
        rule2 = { "protocol": "icmp",
                  "direction": "<>",
                  "src-port": "any",
                  "src": {"type": "vn", "value": vn1},
                  "dst": {"type": "cidr", "value": "10.2.1.2/32"},
                  "dst-port": "any",
                  "action": "deny"
                 }
        rules.append(rule1)
        rules.append(rule2)

        np = self.create_network_policy_with_multiple_rules(rules)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1)

        for obj in [vn1]:
            ident_name = self.get_obj_imid(obj)
            gevent.sleep(2)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        self.check_vn_ri_state(fq_name=self.get_ri_name(vn1))

        self.check_acl_match_dst_cidr(fq_name=self.get_ri_name(vn1),
                                      ip_prefix="10.2.1.1", ip_len=32)
        self.check_acl_match_dst_cidr(fq_name=self.get_ri_name(vn1),
                                      ip_prefix="10.2.1.2", ip_len=32)

        #cleanup
        self.delete_network_policy(np, auto_policy=True)
        self._vnc_lib.virtual_network_delete(fq_name=vn1.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2.get_fq_name())

        # check if vn is deleted
        self.check_vn_is_deleted(uuid=vn1.uuid)

    # test st restart while service chain is configured
    def test_st_restart_service_chain_delete(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create vn2
        vn1_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn1_name, '20.0.0.0/24')

        service_name = self.id() + 's1'
        np = self.create_network_policy(vn1_obj, vn2_obj, [service_name])
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.clear_pending_updates()
        vn2_obj.clear_pending_updates()
        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        sc = self.wait_to_get_sc()
        sc_ri_name = ('service-' + sc[0] + '-default-domain_default-project_'
                      + service_name)
        self.check_ri_state_vn_policy(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_state_vn_policy(self.get_ri_name(vn2_obj, sc_ri_name),
                                      self.get_ri_name(vn2_obj))

        # stop st
        gevent.sleep(1)
        test_common.kill_schema_transformer(self._st_greenlet)
        gevent.sleep(5)

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        self.check_vn_is_deleted(uuid=vn1_obj.uuid)

        # start st on a free port
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self._api_server_ip, self._api_server_port)
        gevent.sleep(4)

        #check if all ri's  are deleted
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn1_obj))
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj, sc_ri_name))
    #end

    # test service chain configuration while st is restarted
    def test_st_restart_service_chain(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, '20.0.0.0/24')

        service_name = self.id() + 's1'
        np = self.create_network_policy(vn1_obj, vn2_obj, [service_name])
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        # stop st and wait for sometime
        gevent.sleep(1)
        test_common.kill_schema_transformer(self._st_greenlet)
        gevent.sleep(5)

        # start st on a free port
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self._api_server_ip, self._api_server_port)
        gevent.sleep(4)

        #check service chain state
        sc = self.wait_to_get_sc()
        sc_ri_name = ('service-' + sc[0] + '-default-domain_default-project_'
                      + service_name)

        self.check_ri_state_vn_policy(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_state_vn_policy(self.get_ri_name(vn2_obj, sc_ri_name),
                                      self.get_ri_name(vn2_obj))

        #cleanup
        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self.check_ri_refs_are_deleted(fq_name=self.get_ri_name(vn1_obj))

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        self.check_vn_is_deleted(uuid=vn1_obj.uuid)

        #check if all ri's  are deleted
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn1_obj))
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj, sc_ri_name))
    #end

    # test logical router functionality
    def test_logical_router(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create virtual machine interface
        vmi_name = self.id() + 'vmi1'
        vmi = VirtualMachineInterface(vmi_name, parent_type='project', fq_name=['default-domain', 'default-project', vmi_name])
        vmi.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        # create logical router
        lr_name = self.id() + 'lr1'
        lr = LogicalRouter(lr_name)
        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        lr.set_configured_route_target_list(rtgt_list)
        lr.add_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_create(lr)

        ri_name = self.get_ri_name(vn1_obj)
        self.check_route_target_in_routing_instance(ri_name, rtgt_list.get_route_target())

        rtgt_list.add_route_target('target:1:2')
        lr.set_configured_route_target_list(rtgt_list)
        self._vnc_lib.logical_router_update(lr)
        self.check_route_target_in_routing_instance(ri_name, rtgt_list.get_route_target())

        rtgt_list.delete_route_target('target:1:1')
        lr.set_configured_route_target_list(rtgt_list)
        self._vnc_lib.logical_router_update(lr)
        self.check_route_target_in_routing_instance(ri_name, rtgt_list.get_route_target())

        lr.del_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_update(lr)
        self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self._vnc_lib.logical_router_delete(id=lr.uuid)

    @retries(5, hook=retry_exc_handler)
    def check_bgp_peering(self, router1, router2, length):
        r1 = self._vnc_lib.bgp_router_read(fq_name=router1.get_fq_name())
        ref_names = [ref['to'] for ref in r1.get_bgp_router_refs() or []]
        self.assertEqual(len(ref_names), length)
        self.assertThat(ref_names, Contains(router2.get_fq_name()))

    def create_bgp_router(self, name, vendor, asn=None):
        ip_fabric_ri = self._vnc_lib.routing_instance_read(
            fq_name=['default-domain', 'default-project', 'ip-fabric', '__default__'])
        router = BgpRouter(name, parent_obj=ip_fabric_ri)
        params = BgpRouterParams()
        params.vendor = 'contrail'
        params.autonomous_system = asn
        router.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_create(router)
        return router

    def test_ibgp_auto_mesh(self):
        # create router1
        r1_name = self.id() + 'router1'
        router1 = self.create_bgp_router(r1_name, 'contrail')

        # create router2
        r2_name = self.id() + 'router2'
        router2 = self.create_bgp_router(r2_name, 'contrail')

        self.check_bgp_peering(router1, router2, 1)

        r3_name = self.id() + 'router3'
        router3 = self.create_bgp_router(r3_name, 'juniper', 1)

        self.check_bgp_peering(router1, router2, 1)

        params = router3.get_bgp_router_parameters()
        params.autonomous_system = 64512
        router3.set_bgp_router_parameters(params)
        self._vnc_lib.bgp_router_update(router3)

        self.check_bgp_peering(router1, router3, 2)

        r4_name = self.id() + 'router4'
        router4 = self.create_bgp_router(r4_name, 'juniper', 1)

        gsc = self._vnc_lib.global_system_config_read(
            fq_name=['default-global-system-config'])

        gsc.set_autonomous_system(1)
        self.check_bgp_peering(router1, router4, 3)

	self._vnc_lib.bgp_router_delete(id=router1.uuid)
	self._vnc_lib.bgp_router_delete(id=router2.uuid)
	self._vnc_lib.bgp_router_delete(id=router3.uuid)
	self._vnc_lib.bgp_router_delete(id=router4.uuid)
        gevent.sleep(1)

    @retries(10, hook=retry_exc_handler)
    def check_vrf_assign_table(self, vmi_fq_name, floating_ip, is_present = True):
        vmi = self._vnc_lib.virtual_machine_interface_read(vmi_fq_name)
        if is_present:
            self.assertEqual(vmi.get_vrf_assign_table().vrf_assign_rule[1].match_condition.src_address.subnet.ip_prefix, floating_ip)
        else:
            try:
                self.assertEqual(vmi.get_vrf_assign_table().vrf_assign_rule[1].match_condition.src_address.subnet.ip_prefix, floating_ip)
                raise Exception('floating is still present: ' + floating_ip)
            except:
                pass

    def test_analyzer(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, '20.0.0.0/24')

        service_name = self.id() + 's1'
        np = self.create_network_policy(vn1_obj, vn2_obj, [service_name], 'transparent', 'analyzer', action_type = 'mirror-to')
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        vn1_uuid = self._vnc_lib.virtual_network_update(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_update(vn2_obj)

        for obj in [vn1_obj, vn2_obj]:
            ident_name = self.get_obj_imid(obj)
            gevent.sleep(2)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        svc_vn_fq_name = 'default-domain:default-project:svc-vn-left'.split(':')
        self.check_ri_state_vn_policy(svc_vn_fq_name, self.get_ri_name(vn1_obj))
        self.check_ri_state_vn_policy(svc_vn_fq_name, self.get_ri_name(vn2_obj))

        self.check_acl_match_mirror_to_ip(self.get_ri_name(vn1_obj))
        self.check_acl_match_nets(self.get_ri_name(vn1_obj), ':'.join(vn1_obj.get_fq_name()), ':'.join(vn2_obj.get_fq_name()))
        self.check_acl_match_nets(self.get_ri_name(vn2_obj), ':'.join(vn2_obj.get_fq_name()), ':'.join(vn1_obj.get_fq_name()))

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        self.check_acl_not_match_mirror_to_ip(self.get_ri_name(vn1_obj))
        self.check_acl_not_match_nets(self.get_ri_name(vn1_obj), ':'.join(vn1_obj.get_fq_name()), ':'.join(vn2_obj.get_fq_name()))
        self.check_acl_not_match_nets(self.get_ri_name(vn2_obj), ':'.join(vn2_obj.get_fq_name()), ':'.join(vn1_obj.get_fq_name()))

    @retries(5, hook=retry_exc_handler)
    def check_security_group_id(self, sg_fq_name, verify_sg_id = None):
        sg = self._vnc_lib.security_group_read(sg_fq_name)
        sg_id = sg.get_security_group_id()
        if sg_id is None: 
            raise Exception('sg id is not present for %s' % sg_fq_name)
        if verify_sg_id is not None and str(sg_id) != str(verify_sg_id):
            raise Exception('sg id is not same as passed value (%s, %s)' % (str(sg_id), str(verify_sg_id)))

    def _security_group_rule_build(self, rule_info, sg_uuid):
        protocol = rule_info['protocol']
        port_min = rule_info['port_min'] or 0
        port_max = rule_info['port_max'] or 65535 
        direction = rule_info['direction'] or 'ingress'
        ip_prefix = rule_info['ip_prefix']
        ether_type = rule_info['ether_type']
        sg_id = rule_info['sg_id']

        if ip_prefix:
            cidr = ip_prefix.split('/')
            pfx = cidr[0]
            pfx_len = int(cidr[1])
            endpt = [AddressType(subnet=SubnetType(pfx, pfx_len))]
        elif sg_id:
            try:
                sg_obj = self._vnc_lib.security_group_read(id=sg_uuid)
            except NoIdError:
                raise Exception('SecurityGroupNotFound %s' % sg_uuid)
            endpt = [AddressType(security_group=sg_obj.get_fq_name_str())]

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

        if not ip_prefix and not sg_id:
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

    def _security_group_update(self, sg_obj):
        try:
            self._vnc_lib.security_group_update(sg_obj)
        except PermissionDenied as e:
            raise Exception('SG Rule - BadRequest %s' % str(e))
        return
    #end _security_group_update

    def security_group_create(self, sg_name, project_fq_name):
        project_obj = self._vnc_lib.project_read(project_fq_name)
        sg_obj = SecurityGroup(name=sg_name, parent_obj=project_obj)
        self._vnc_lib.security_group_create(sg_obj)
        return sg_obj
    #end security_group_create

    def test_sg(self):
        #create sg and associate egress rule and check acls
        sg1_obj = self.security_group_create('sg-1', [u'default-domain', u'default-project'])
        self.wait_to_get_sg_id(sg1_obj.get_fq_name())
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())
        rule1 = {}
        rule1['port_min'] = 0
        rule1['port_max'] = 65535
        rule1['direction'] = 'egress'
        rule1['ip_prefix'] = None
        rule1['protocol'] = 'any'
        rule1['ether_type'] = 'IPv4'
        rule1['sg_id'] = sg1_obj.get_security_group_id()
        sg_rule1 = self._security_group_rule_build(rule1, sg1_obj.get_uuid())
        self._security_group_rule_append(sg1_obj, sg_rule1)
        self._security_group_update(sg1_obj)
        self.check_security_group_id(sg1_obj.get_fq_name())
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())
        self.check_acl_match_sg(sg1_obj.get_fq_name(), 'egress-access-control-list', 
                                                        sg1_obj.get_security_group_id())

        sg1_obj.set_configured_security_group_id(100)
        self._security_group_update(sg1_obj)
        self.check_security_group_id(sg1_obj.get_fq_name(), 100)
        sg1_obj = self._vnc_lib.security_group_read(sg1_obj.get_fq_name())
        self.check_acl_match_sg(sg1_obj.get_fq_name(), 'egress-access-control-list', 
                                                         sg1_obj.get_security_group_id())

        #create another sg and associate ingress rule and check acls
        sg2_obj = self.security_group_create('sg-2', [u'default-domain', u'default-project'])
        self.wait_to_get_sg_id(sg2_obj.get_fq_name())
        sg2_obj = self._vnc_lib.security_group_read(sg2_obj.get_fq_name())
        rule2 = {}
        rule2['port_min'] = 0
        rule2['port_max'] = 65535
        rule2['direction'] = 'ingress'
        rule2['ip_prefix'] = None
        rule2['protocol'] = 'any'
        rule2['ether_type'] = 'IPv4'
        rule2['sg_id'] = sg2_obj.get_security_group_id()
        sg_rule2 = self._security_group_rule_build(rule2, sg2_obj.get_uuid())
        self._security_group_rule_append(sg2_obj, sg_rule2)
        self._security_group_update(sg2_obj)
        self.check_security_group_id(sg2_obj.get_fq_name())
        self.check_acl_match_sg(sg2_obj.get_fq_name(), 'ingress-access-control-list', 
                                                        sg2_obj.get_security_group_id())

        #add ingress and egress rules to same sg and check for both
        rule1['sg_id'] = sg2_obj.get_security_group_id()
        sg_rule3 = self._security_group_rule_build(rule1, sg2_obj.get_uuid())
        self._security_group_rule_append(sg2_obj, sg_rule3)
        self._security_group_update(sg2_obj)
        self.check_security_group_id(sg2_obj.get_fq_name())
        self.check_acl_match_sg(sg2_obj.get_fq_name(), 'egress-access-control-list', 
                                                        sg2_obj.get_security_group_id())
        self.check_acl_match_sg(sg2_obj.get_fq_name(), 'ingress-access-control-list', 
                                                        sg2_obj.get_security_group_id())

        # duplicate security group id configured, vnc api allows
        # isn't this a problem?
        sg2_obj.set_configured_security_group_id(100)
        self._security_group_update(sg2_obj)
        self.check_security_group_id(sg2_obj.get_fq_name(), 100)

        #sg id '0' is not allowed, should not get modified
        sg1_obj.set_configured_security_group_id(0)
        self._security_group_update(sg1_obj)
        self.check_security_group_id(sg1_obj.get_fq_name(), 100)

        # -ve security group id not allowed, should not get modified
        sg1_obj.set_configured_security_group_id(-100)
        self._security_group_update(sg1_obj)
        self.check_security_group_id(sg1_obj.get_fq_name(), 100)

    #end test_sg

    def test_fip(self):

        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, '20.0.0.0/24')

        service_name = self.id() + 's1'
        np = self.create_network_policy(vn1_obj, vn2_obj, [service_name], 'in-network')

        sc = self.wait_to_get_sc()
        sc_ri_name = 'service-'+sc[0]+'-default-domain_default-project_' + service_name
        self.check_ri_state_vn_policy(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_state_vn_policy(self.get_ri_name(vn2_obj, sc_ri_name),
                                      self.get_ri_name(vn2_obj))

        vmi_fq_name = 'default-domain:default-project:default-domain__default-project__test.test_service.TestPolicy.test_fips1__1__left__1'
        vmi = self._vnc_lib.virtual_machine_interface_read(vmi_fq_name.split(':'))

        vn3_name = 'vn-public'
        vn3_obj = VirtualNetwork(vn3_name)
        vn3_obj.set_router_external(True)
        ipam3_obj = NetworkIpam('ipam3')
        self._vnc_lib.network_ipam_create(ipam3_obj)
        vn3_obj.add_network_ipam(ipam3_obj, VnSubnetsType(
            [IpamSubnetType(SubnetType("192.168.7.0", 24))]))
        vn3_uuid = self._vnc_lib.virtual_network_create(vn3_obj)
        fip_pool_name = 'vn_public_fip_pool'
        fip_pool = FloatingIpPool(fip_pool_name, vn3_obj)
        self._vnc_lib.floating_ip_pool_create(fip_pool)
        fip_obj = FloatingIp("fip1", fip_pool)
        default_project = self._vnc_lib.project_read(fq_name=[u'default-domain', u'default-project'])
        fip_obj.set_project(default_project)
        fip_uuid = self._vnc_lib.floating_ip_create(fip_obj)
        fip_obj.set_virtual_machine_interface(vmi)
        self._vnc_lib.floating_ip_update(fip_obj)

        fip_obj = self._vnc_lib.floating_ip_read(fip_obj.get_fq_name())

        for obj in [fip_obj]:
            ident_name = self.get_obj_imid(obj)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        self.wait_to_get_link(ident_name, vmi_fq_name)

        fip = fip_obj.get_floating_ip_address()
        self.check_vrf_assign_table(vmi.get_fq_name(), fip, True)

        fip_fq_name = fip_obj.get_fq_name()
        self._vnc_lib.floating_ip_delete(fip_fq_name)
        self.wait_to_remove_link(self.get_obj_imid(vmi), fip_fq_name)
        self.check_vrf_assign_table(vmi.get_fq_name(), fip, False)

# end class TestRouteTable
