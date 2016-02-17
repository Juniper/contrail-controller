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
try:
    import config_db
except ImportError:
    from schema_transformer import config_db

from gevent import sleep

def retry_exc_handler(tries_remaining, exception, delay):
    print >> sys.stderr, "."


def retries(max_tries, delay=1, backoff=1, exceptions=(Exception,),
            hook=retry_exc_handler):
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

    @retries(5)
    def check_ri_asn(self, fq_name, rt_target):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        rt_refs = ri.get_route_target_refs()
        if not rt_refs:
            print "retrying ... ", test_common.lineno()
            raise Exception('ri_refs is None for %s' % fq_name)
        for rt_ref in rt_refs:
            if rt_ref['to'][0] == rt_target:
                return
        raise Exception('rt_target %s not found in ri %s' % (rt_target, fq_name))

    @retries(5)
    def check_bgp_asn(self, fq_name, asn):
        router = self._vnc_lib.bgp_router_read(fq_name)
        params = router.get_bgp_router_parameters()
        if not params:
            print "retrying ... ", test_common.lineno()
            raise Exception('bgp params is None for %s' % fq_name)
        self.assertEqual(params.get_autonomous_system(), asn)

    @retries(5)
    def check_lr_asn(self, fq_name, rt_target):
        router = self._vnc_lib.logical_router_read(fq_name)
        rt_refs = router.get_route_target_refs()
        if not rt_refs:
            print "retrying ... ", test_common.lineno()
            raise Exception('ri_refs is None for %s' % fq_name)
        self.assertEqual(rt_refs[0]['to'][0], rt_target)

    @retries(5)
    def check_service_chain_prefix_match(self, fq_name, prefix):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        sci = ri.get_service_chain_information()
        if sci is None:
            print "retrying ... ", test_common.lineno()
            raise Exception('Service chain info not found for %s' % fq_name)
        self.assertEqual(sci.prefix[0], prefix)

    @retries(5)
    def check_service_chain_info(self, fq_name, expected):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        sci = ri.get_service_chain_information()
        if sci is None:
            raise Exception('Service chain info not found for %s' % fq_name)
        self.assertEqual(sci, expected)

    @retries(5)
    def check_v6_service_chain_info(self, fq_name, expected):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        sci = ri.get_ipv6_service_chain_information()
        if sci is None:
            raise Exception('Ipv6 service chain info not found for %s' % fq_name)
        self.assertEqual(sci, expected)

    @retries(5)
    def check_analyzer_ip(self, vmi_fq_name):
        vmi = self._vnc_lib.virtual_machine_interface_read(vmi_fq_name)
        vmi_props = vmi.get_virtual_machine_interface_properties()
        ip = vmi_props.get_interface_mirror().get_mirror_to().get_analyzer_ip_address()
        self.assertTrue(ip != None)

    @retries(5)
    def check_analyzer_no_ip(self, vmi_fq_name):
        vmi = self._vnc_lib.virtual_machine_interface_read(vmi_fq_name)
        vmi_props = vmi.get_virtual_machine_interface_properties()
        ip = None
        try:
            ip = vmi_props.get_interface_mirror().get_mirror_to().get_analyzer_ip_address()
        except AttributeError as e:
            pass
        self.assertTrue(ip == None)

    @retries(5)
    def check_service_chain_pbf_rules(self, vn1, vn2, sc_ri_name, service_name, sc_ip):
        mac1 = '02:00:00:00:00:01'
        mac2 = '02:00:00:00:00:02'
        expected_pbf = PolicyBasedForwardingRuleType(
            vlan_tag=1, direction='both', service_chain_address=sc_ip)
        for interface_type in ('left', 'right'):
            if interface_type == 'left':
                expected_pbf.src_mac = mac1
                expected_pbf.dst_mac = mac2
                vmi_fq_name = ['default-domain', 'default-project',
                               'default-domain__default-project__%s__1__left__1' %
                                service_name]
                service_ri_fq_name = self.get_ri_name(vn1, sc_ri_name)
            else:
                expected_pbf.src_mac = mac2
                expected_pbf.dst_mac = mac1
                vmi_fq_name = ['default-domain', 'default-project',
                               'default-domain__default-project__%s__1__right__2' %
                                service_name]
                service_ri_fq_name = self.get_ri_name(vni2, sc_ri_name)
            vmi = self._vnc_lib.virtual_machine_interface_read(vmi_fq_name)
            ri_refs = vmi.get_routing_instance_refs()
            for ri_ref in ri_refs:
                sc_name = ri_ref['to']
                if sc_name == service_ri_fq_name:
                    pbf_rule = ri_ref['attr']
                    self.assertEqual(pbf_rule, expected_pbf)
                    return
            raise Exception('Service chain pbf rules not found for %s' % service_ri_fq_name)
 
    @retries(5)
    def check_service_chain_ip(self, sc_name):
        _SC_IP_CF = 'service_chain_ip_address_table'
        cf = CassandraCFs.get_cf(_SC_IP_CF)
        ip = cf.get(sc_name)['ip_address']

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
    def delete_vn(self, fq_name):
        try:
            self._vnc_lib.virtual_network_delete(fq_name=fq_name)
            print 'vn deleted'
        except RefsExistError:
            print "retrying ... ", test_common.lineno()
            raise Exception('virtual network %s still exists' % str(fq_name))

    @retries(5)
    def check_st_vm_is_deleted(self, name):
        vm_obj = config_db.VirtualMachineST.get(name)
        if vm_obj is not None:
            raise Exception('vm %s still exists' % name)
        return

    @retries(5)
    def check_lr_is_deleted(self, uuid):
        try:
            self._vnc_lib.logical_router_read(id=uuid)
            print "retrying ... ", test_common.lineno()
            raise Exception('logical router %s still exists' % uuid)
        except NoIdError:
            print 'lr deleted'

    @retries(5)
    def check_rt_is_deleted(self, name):
        try:
            self._vnc_lib.route_target_read(fq_name=[name])
            print "retrying ... ", test_common.lineno()
            raise Exception('rt %s still exists' % uuid)
        except NoIdError:
            print 'rt deleted'

    @retries(5)
    def check_vn_is_deleted(self, uuid):
        try:
            self._vnc_lib.virtual_network_read(id=uuid)
            print "retrying ... ", test_common.lineno()
            raise Exception('virtual network %s still exists' % uuid)
        except NoIdError:
            print 'vn deleted'

    @retries(5)
    def check_all_vmis_are_deleted(self):
        vmi_list = self._vnc_lib.virtual_machine_interfaces_list()
        if vmi_list['virtual-machine-interfaces']:
            raise Exception('virtual machine interfaces still exist' + str(vmi_list))
        print 'all virtual machine interfaces deleted'

    @retries(5)
    def check_ri_is_deleted(self, fq_name):
        try:
            self._vnc_lib.routing_instance_read(fq_name)
            print "retrying ... ", test_common.lineno()
            raise Exception('routing instance %s still exists' % fq_name)
        except NoIdError:
            print 'ri deleted'

    @retries(5)
    def check_ri_is_present(self, fq_name):
        self._vnc_lib.routing_instance_read(fq_name)

    @retries(5)
    def check_link_in_ifmap_graph(self, fq_name_str, links):
        self._vnc_lib.routing_instance_read(fq_name)

    @retries(5)
    def wait_to_get_object(self, obj_class, obj_name):
        if obj_name not in obj_class._dict:
            raise Exception('%s not found' % obj_name)

    @retries(5)
    def wait_to_delete_object(self, obj_class, obj_name):
        if obj_name in obj_class._dict:
            raise Exception('%s still found' % obj_name)

    @retries(5)
    def wait_to_get_sc(self, left_vn=None, right_vn=None, si_name=None):
        for sc in to_bgp.ServiceChain.values():
            if (left_vn in (None, sc.left_vn) and
                    right_vn in (None, sc.right_vn) and
                    si_name in (sc.service_list[0], None)):
                return sc.name
        raise Exception('Service chain not found')

    @retries(5)
    def wait_to_get_link(self, ident_name, link_fq_name):
        self.assertThat(str(FakeIfmapClient._graph[ident_name]['links']),
                        Contains(link_fq_name))

    @retries(5)
    def wait_to_remove_link(self, ident_name, link_fq_name):
        self.assertThat(str(FakeIfmapClient._graph[ident_name]['links']),
                        Not(Contains(link_fq_name)))

    @retries(5)
    def wait_to_get_sg_id(self, sg_fq_name):
        sg_obj = self._vnc_lib.security_group_read(sg_fq_name)
        if sg_obj.get_security_group_id() is None:
            raise Exception('Security Group Id is none %s' % str(sg_fq_name)) 

    @retries(5)
    def check_rt_in_ri(self, ri_name, rt_id, is_present, exim=None):
        ri_obj = self._vnc_lib.routing_instance_read(fq_name=ri_name)
        ri_rt_refs = [ref for ref in ri_obj.get_route_target_refs() or []
                      if ref['to'][0] == rt_id]
        if not is_present:
            self.assertEqual(ri_rt_refs, [])
            return
        else:
            self.assertEqual(ri_rt_refs[0]['to'][0], rt_id)
            self.assertEqual(ri_rt_refs[0]['attr'].import_export, exim)

    @retries(5)
    def check_acl_match_dst_cidr(self, fq_name, ip_prefix, ip_len):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.access_control_list_entries.acl_rule:
            if (rule.match_condition.dst_address.subnet is not None and
                rule.match_condition.dst_address.subnet.ip_prefix == ip_prefix and
                rule.match_condition.dst_address.subnet.ip_prefix_len == ip_len):
                    return
        raise Exception('prefix %s/%d not found in ACL rules for %s' %
                        (ip_prefix, ip_len, fq_name))

    @retries(5)
    def check_acl_match_nets(self, fq_name, vn1_fq_name, vn2_fq_name):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.access_control_list_entries.acl_rule:
            if (rule.match_condition.src_address.virtual_network == vn1_fq_name and
                rule.match_condition.dst_address.virtual_network == vn2_fq_name):
                    return
        raise Exception('nets %s/%s not found in ACL rules for %s' %
                        (vn1_fq_name, vn2_fq_name, fq_name))

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
                if rule.match_condition.dst_address.security_group != sg_id:
                    if is_all_rules:
                        raise Exception('sg %s/%s not found in %s - for some rule' %
                                           (str(fq_name), str(sg_id), acl_name))
                else:
                    match = True
                    break
            if acl_name == 'ingress-access-control-list':
                if rule.match_condition.src_address.security_group != sg_id:
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
                        if rule.match_condition.dst_address.security_group == sg_id:
                            raise Exception('sg %s/%s found in %s - for some rule' %
                                           (str(fq_name), str(sg_id), acl_name))
                    if acl_name == 'ingress-access-control-list':
                        if rule.match_condition.src_address.security_group == sg_id:
                            raise Exception('sg %s/%s found in %s - for some rule' %
                                           (str(fq_name), str(sg_id), acl_name))
        except NoIdError:
            pass

    @retries(5)
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

    @retries(5)
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

    @retries(5)
    def check_acl_match_mirror_to_ip(self, fq_name):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.access_control_list_entries.acl_rule:
            if (rule.action_list.mirror_to.analyzer_ip_address is not None):
                return
        raise Exception('mirror to ip not found in ACL rules for %s' % (fq_name))

    @retries(5)
    def check_route_target_in_routing_instance(self, ri_name, rt_list):
        ri_obj = self._vnc_lib.routing_instance_read(fq_name=ri_name)
        ri_rt_refs = set([ref['to'][0] for ref in ri_obj.get_route_target_refs() or []])
        self.assertTrue(set(rt_list) <= ri_rt_refs)

    @retries(5)
    def check_bgp_router_ip(self, router_name, ip):
        router_obj = self._vnc_lib.bgp_router_read(fq_name_str=router_name)
        self.assertEqual(router_obj.get_bgp_router_parameters().address,
                         ip)
    @retries(5)
    def check_bgp_router_identifier(self, router_name, ip):
        router_obj = self._vnc_lib.bgp_router_read(fq_name_str=router_name)
        self.assertEqual(router_obj.get_bgp_router_parameters().identifier,
                         ip)

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
        vn1_uuid = self._vnc_lib.virtual_network_create(vn1_obj)
        vn2_uuid = self._vnc_lib.virtual_network_create(vn2_obj)

        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj),
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

        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj),
                                  self.get_ri_name(vn1_obj))

        vn3_obj = VirtualNetwork(vn3_name)
        vn3_obj.set_network_policy(np2, vnp)
        vn3_uuid = self._vnc_lib.virtual_network_create(vn3_obj)

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

    def service_policy_test_with_version(self, version=None):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, ['10.0.0.0/24', '1000::/16'])

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, ['20.0.0.0/24', '2000::/16'])

        service_name = self.id() + 's1'
        np = self.create_network_policy(vn1_obj, vn2_obj, [service_name], version=version)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        sc = self.wait_to_get_sc()
        sc_ri_name = 'service-'+sc+'-default-domain_default-project_' + service_name
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_name),
                                  self.get_ri_name(vn2_obj))

        si_name = 'default-domain:default-project:' + service_name
        sci = ServiceChainInfo(prefix = ['10.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn1_obj)),
                               service_chain_address = '10.0.0.252',
                               service_instance = si_name)
        self.check_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)
        sci.prefix = ['1000::/16']
        sci.service_chain_address = '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffc'
        self.check_v6_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)
        sci = ServiceChainInfo(prefix = ['20.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn2_obj)),
                               service_chain_address = '10.0.0.252',
                               service_instance = si_name)
        self.check_service_chain_info(self.get_ri_name(vn1_obj, sc_ri_name), sci)
        sci.prefix = ['2000::/16']
        sci.service_chain_address = '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffc'
        self.check_v6_service_chain_info(self.get_ri_name(vn1_obj, sc_ri_name), sci)

        vn1_obj.set_multi_policy_service_chains_enabled(True)
        vn2_obj.set_multi_policy_service_chains_enabled(True)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self.check_ri_ref_not_present(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_not_present(self.get_ri_name(vn2_obj),
                                  self.get_ri_name(vn2_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))

        vn1_obj.set_multi_policy_service_chains_enabled(False)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj),
                                  self.get_ri_name(vn2_obj, sc_ri_name))
        self.check_ri_ref_not_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))

        rp_name = self.id() + 'rp1'
        rp = RoutingPolicy(rp_name)
        si_obj = self._vnc_lib.service_instance_read(fq_name_str=si_name)
        si_rp = RoutingPolicyServiceInstanceType(left_sequence='1.0')
        rp.add_service_instance(si_obj, si_rp)
        self._vnc_lib.routing_policy_create(rp)
        self.wait_to_get_object(config_db.RoutingPolicyST,
                                rp.get_fq_name_str())
        ident_name = self.get_obj_imid(rp)
        self.wait_to_get_link(ident_name, ':'.join(self.get_ri_name(vn1_obj, sc_ri_name)))
        rp.del_service_instance(si_obj)
        self._vnc_lib.routing_policy_update(rp)
        self.wait_to_remove_link(ident_name, ':'.join(self.get_ri_name(vn1_obj, sc_ri_name)))
        self._vnc_lib.routing_policy_delete(id=rp.uuid)

        rlist = RouteListType(route=['100.0.0.0/24'])
        ra = RouteAggregate('ra1', aggregate_route_entries=rlist)

        sit = ServiceInterfaceTag(interface_type='left')
        ra.add_service_instance(si_obj, sit)
        self._vnc_lib.route_aggregate_create(ra)
        self.wait_to_get_object(config_db.RouteAggregateST,
                                ra.get_fq_name_str())
        ra = self._vnc_lib.route_aggregate_read(id=ra.uuid)
        self.assertEqual(ra.get_aggregate_route_nexthop(), '10.0.0.252')
        ident_name = self.get_obj_imid(ra)
        self.wait_to_get_link(ident_name, ':'.join(self.get_ri_name(vn1_obj, sc_ri_name)))
        ra.del_service_instance(si_obj)
        self._vnc_lib.route_aggregate_update(ra)
        self.wait_to_remove_link(ident_name, ':'.join(self.get_ri_name(vn1_obj, sc_ri_name)))
        self._vnc_lib.route_aggregate_delete(id=ra.uuid)

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
    # end service_policy_test_with_version

    def test_service_policy(self):
        self.service_policy_test_with_version()
        # TODO: Remove comment after the cleanup issue is resolved
        # Issue seen after port tuple commit in svc_monitor
        #self.service_policy_test_with_version(2)
    # end test_service_policy

    def test_service_policy_with_any(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, ['10.0.0.0/24'])

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, ['20.0.0.0/24'])

        # create vn3
        vn3_name = self.id() + 'vn3'
        vn3_obj = self.create_virtual_network(vn3_name, ['30.0.0.0/24'])

        service_name = self.id() + 's1'
        np1 = self.create_network_policy(vn1_obj, vn2_obj, [service_name])
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np1, vnp)
        np2 = self.create_network_policy('any', vn2_obj)
        np2.get_network_policy_entries().policy_rule[0].set_action_list(
            copy.deepcopy(np1.get_network_policy_entries().policy_rule[0].action_list))
        np2.set_network_policy_entries(np2.get_network_policy_entries())
        self._vnc_lib.network_policy_update(np2)

        vn2_obj.set_network_policy(np2, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        sc = self.wait_to_get_sc(vn1_obj.get_fq_name_str(),
                                 vn2_obj.get_fq_name_str())
        sc_ri_name = 'service-'+sc+'-default-domain_default-project_' + service_name
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_name),
                                  self.get_ri_name(vn2_obj))

        si_name = 'default-domain:default-project:' + service_name
        sci = ServiceChainInfo(prefix = ['10.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn1_obj)),
                               service_chain_address = '10.0.0.252',
                               service_instance = si_name)
        self.check_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)
        sci = ServiceChainInfo(prefix = ['20.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn2_obj)),
                               service_chain_address = '10.0.0.252',
                               service_instance = si_name)
        self.check_service_chain_info(self.get_ri_name(vn1_obj, sc_ri_name), sci)

        np3 = self.create_network_policy(vn3_obj, vn2_obj)
        np3.get_network_policy_entries().policy_rule[0].set_action_list(
            copy.deepcopy(np1.get_network_policy_entries().policy_rule[0].action_list))
        np3.set_network_policy_entries(np3.get_network_policy_entries())
        self._vnc_lib.network_policy_update(np3)
        vn3_obj.set_network_policy(np3, vnp)
        self._vnc_lib.virtual_network_update(vn3_obj)

        sc = self.wait_to_get_sc(vn3_obj.get_fq_name_str(),
                                 vn2_obj.get_fq_name_str())
        sc_ri_name = 'service-'+sc+'-default-domain_default-project_' + service_name
        self.check_ri_ref_present(self.get_ri_name(vn3_obj),
                                  self.get_ri_name(vn3_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_name),
                                  self.get_ri_name(vn2_obj))

        si_name = 'default-domain:default-project:' + service_name
        sci = ServiceChainInfo(prefix = ['30.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn3_obj)),
                               service_chain_address = '30.0.0.252',
                               service_instance = si_name)
        self.check_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)
        sci = ServiceChainInfo(prefix = ['20.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn2_obj)),
                               service_chain_address = '30.0.0.252',
                               service_instance = si_name)
        self.check_service_chain_info(self.get_ri_name(vn3_obj, sc_ri_name), sci)

        vn1_obj.del_network_policy(np1)
        vn2_obj.del_network_policy(np2)
        vn3_obj.del_network_policy(np3)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self._vnc_lib.virtual_network_update(vn3_obj)
        self.check_ri_refs_are_deleted(fq_name=self.get_ri_name(vn1_obj))

        self.delete_network_policy(np1)
        self._vnc_lib.network_policy_delete(id=np2.uuid)
        self._vnc_lib.network_policy_delete(id=np3.uuid)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn3_obj.get_fq_name())
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn3_obj))
    # end test_service_policy_with_any

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
        sc_ri_name = 'service-'+sc+'-default-domain_default-project_' + service_name
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

    def test_multi_service_in_policy(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, '20.0.0.0/24')

        service_names = [self.id() + 's1', self.id() + 's2', self.id() + 's3']
        np = self.create_network_policy(vn1_obj, vn2_obj, service_names, auto_policy=False, service_mode='in-network')
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

        sc = self.wait_to_get_sc()
        sc_ri_names = ['service-'+sc+'-default-domain_default-project_' + s for s in service_names]
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_names[0]))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_names[2]),
                                  self.get_ri_name(vn2_obj))

        si_name = 'default-domain:default-project:test.test_service.TestPolicy.test_multi_service_in_policys3'
        sci = ServiceChainInfo(prefix = ['10.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn1_obj)),
                               service_chain_address = '20.0.0.250',
                               service_instance = si_name,
                               source_routing_instance = ':'.join(self.get_ri_name(vn2_obj)),
                              )
        self.check_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_names[2]), sci)
        sci = ServiceChainInfo(prefix = ['20.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn2_obj)),
                               service_chain_address = '10.0.0.250',
                               service_instance = si_name,
                               source_routing_instance = ':'.join(self.get_ri_name(vn1_obj)),
                              )
        self.check_service_chain_info(self.get_ri_name(vn1_obj, sc_ri_names[2]), sci)

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn1_obj, sc_ri_names[0]))
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj, sc_ri_names[0]))

        self.check_ri_refs_are_deleted(fq_name=self.get_ri_name(vn1_obj))

        self.delete_network_policy(np)
        self.delete_vn(fq_name=vn1_obj.get_fq_name())
        self.delete_vn(fq_name=vn2_obj.get_fq_name())
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
    # end test_multi_service_in_policy

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
        sc_ri_names = ['service-'+sc+'-default-domain_default-project_' + s for s in service_names]
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_names[0]))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_names[-1]),
                                  self.get_ri_name(vn2_obj))

        self.check_service_chain_prefix_match(fq_name=self.get_ri_name(vn2_obj, sc_ri_names[0]),
                                       prefix='10.0.0.0/24')

        self.check_service_chain_ip(sc_ri_names[0])
        self.check_service_chain_ip(sc_ri_names[1])
        self.check_service_chain_ip(sc_ri_names[2])

        vmi_fq_names = [['default-domain', 'default-project',
                         'default-domain__default-project__%s__1__%s' %
                         (service_name, if_type)]
                        for service_name in service_names for if_type in ('left__1', 'right__2')]

        self.check_service_chain_pbf_rules(vn1_obj, vn2_obj, sc_ri_names[0], service_names[0], '10.0.0.252')
        self.check_service_chain_pbf_rules(vn1_obj, vn2_obj, sc_ri_names[1], service_names[1], '10.0.0.251')
        self.check_service_chain_pbf_rules(vn1_obj, vn2_obj, sc_ri_names[2], service_names[2], '10.0.0.250')

        np.network_policy_entries.policy_rule[0].action_list.apply_service = \
            np.network_policy_entries.policy_rule[0].action_list.apply_service[:-1]
        np.set_network_policy_entries(np.network_policy_entries)
        self._vnc_lib.network_policy_update(np)

        for i in range(0, 5):
            try:
                self.check_service_chain_pbf_rules(vn1_obj, vn2_obj, sc_ri_names[2], service_names[2], '10.0.0.250')
                gevent.sleep(1)
            except Exception:
                break

        self.check_service_chain_pbf_rules(vn1_obj, vn2_obj, sc_ri_names[0], service_names[0], '10.0.0.252')
        self.check_service_chain_pbf_rules(vn1_obj, vn2_obj, sc_ri_names[1], service_names[1], '10.0.0.251')
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
    # end test_muliti_service_policy

    def test_multi_policy_service_chain(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, ['10.0.0.0/24', '1000::/16'])

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, ['20.0.0.0/24', '2000::/16'])

        policies = []
        for i in range(1, 3):
            service_name = self.id() + 's%s' % i
            np = self.create_network_policy(vn1_obj, vn2_obj, [service_name])
            npe = np.network_policy_entries
            npe.policy_rule[0].src_ports[0].start_port = i
            npe.policy_rule[0].src_ports[0].end_port = i
            np.set_network_policy_entries(npe)
            self._vnc_lib.network_policy_update(np)
            seq = SequenceType(1, i)
            vnp = VirtualNetworkPolicyType(seq)

            vn1_obj.add_network_policy(np, vnp)
            vn2_obj.add_network_policy(np, vnp)
            policies.append(np)
        vn1_obj.set_multi_policy_service_chains_enabled(True)
        vn2_obj.set_multi_policy_service_chains_enabled(True)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        for i in range(1, 3):
            service_name = self.id() + 's%s' % i
            si_name = 'default-domain:default-project:' + service_name
            sc = self.wait_to_get_sc(si_name=si_name)
            sc_ri_name = 'service-'+sc+'-default-domain_default-project_' + service_name
            self.check_ri_ref_not_present(self.get_ri_name(vn1_obj),
                                          self.get_ri_name(vn1_obj, sc_ri_name))
            self.check_ri_ref_not_present(self.get_ri_name(vn2_obj),
                                          self.get_ri_name(vn2_obj, sc_ri_name))
            self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn2_obj))

            sci = ServiceChainInfo(prefix = ['10.0.0.0/24'],
                                   routing_instance = ':'.join(self.get_ri_name(vn1_obj)),
                                   service_chain_address = '10.0.0.%s' % (253-i),
                                   service_instance = si_name)
            self.check_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)
            sci.prefix = ['1000::/16']
            if i == 1:
                sci.service_chain_address = '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffc'
            else:
                sci.service_chain_address = '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffb'

            self.check_v6_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)
            sci = ServiceChainInfo(prefix = ['20.0.0.0/24'],
                                   routing_instance = ':'.join(self.get_ri_name(vn2_obj)),
                                   service_chain_address = '10.0.0.%s' % (253-i),
                                   service_instance = si_name)
            self.check_service_chain_info(self.get_ri_name(vn1_obj, sc_ri_name), sci)
            sci.prefix = ['2000::/16']
            if i == 1:
                sci.service_chain_address = '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffc'
            else:
                sci.service_chain_address = '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffb'
            self.check_v6_service_chain_info(self.get_ri_name(vn1_obj, sc_ri_name), sci)



            rp = RoutingPolicy('rp1')
            si_obj = self._vnc_lib.service_instance_read(fq_name_str=si_name)
            si_rp = RoutingPolicyServiceInstanceType(left_sequence='1.0')
            rp.add_service_instance(si_obj, si_rp)
            self._vnc_lib.routing_policy_create(rp)
            self.wait_to_get_object(config_db.RoutingPolicyST,
                                    rp.get_fq_name_str())
            ident_name = self.get_obj_imid(rp)
            self.wait_to_get_link(ident_name, ':'.join(self.get_ri_name(vn1_obj, sc_ri_name)))
            rp.del_service_instance(si_obj)
            self._vnc_lib.routing_policy_update(rp)
            self.wait_to_remove_link(ident_name, ':'.join(self.get_ri_name(vn1_obj, sc_ri_name)))
            self._vnc_lib.routing_policy_delete(id=rp.uuid)

            rlist = RouteListType(route=['100.0.0.0/24'])
            ra = RouteAggregate('ra1', aggregate_route_entries=rlist)

            sit = ServiceInterfaceTag(interface_type='left')
            ra.add_service_instance(si_obj, sit)
            self._vnc_lib.route_aggregate_create(ra)
            self.wait_to_get_object(config_db.RouteAggregateST,
                                    ra.get_fq_name_str())
            ra = self._vnc_lib.route_aggregate_read(id=ra.uuid)
            self.assertEqual(ra.get_aggregate_route_nexthop(), '10.0.0.%s' % (253-i))

            ident_name = self.get_obj_imid(ra)
            self.wait_to_get_link(ident_name, ':'.join(self.get_ri_name(vn1_obj, sc_ri_name)))
            ra.del_service_instance(si_obj)
            self._vnc_lib.route_aggregate_update(ra)
            self.wait_to_remove_link(ident_name, ':'.join(self.get_ri_name(vn1_obj, sc_ri_name)))
            self._vnc_lib.route_aggregate_delete(id=ra.uuid)

        for np in policies:
            vn1_obj.del_network_policy(np)
            vn2_obj.del_network_policy(np)

        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self.check_ri_refs_are_deleted(fq_name=self.get_ri_name(vn1_obj))

        for np in policies:
            self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
    # end test_multi_policy_service_chain


# end class TestPolicy

#class TestRouteTable(test_case.STTestCase):
    def test_add_delete_route(self):
        lvn_name = self.id() + 'lvn'
        rvn_name = self.id() + 'rvn'
        lvn = self.create_virtual_network(lvn_name, "10.0.0.0/24")
        rvn = self.create_virtual_network(rvn_name, "20.0.0.0/24")

        service_name = self.id() + 's1'
        np = self.create_network_policy(lvn, rvn, [service_name], auto_policy=True, service_mode="in-network")

        vn_name = self.id() + 'vn100'
        vn = self.create_virtual_network(vn_name, "1.0.0.0/24")
        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        vn.set_route_target_list(rtgt_list)
        exp_rtgt_list = RouteTargetList(route_target=['target:2:1'])
        vn.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list = RouteTargetList(route_target=['target:3:1'])
        vn.set_import_route_target_list(imp_rtgt_list)
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

        @retries(5)
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
            rt100 = set(ref['to'][0] for ref in ri100.get_route_target_refs())
            lrt = set(ref['to'][0] for ref in lri.get_route_target_refs() or [])
            if rt100 & lrt:
                return sc_ri_name, (rt100 & lrt)
            raise Exception("rt100 route-target ref not found")

        sc_ri_name, rt100 = _match_route_table(rtgt_list.get_route_target() +
                                               imp_rtgt_list.get_route_target())

        rtgt_list.add_route_target('target:1:2')
        vn.set_route_target_list(rtgt_list)
        exp_rtgt_list.add_route_target('target:2:2')
        vn.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list.add_route_target('target:3:2')
        vn.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn)
        _match_route_table(rtgt_list.get_route_target() +
                           imp_rtgt_list.get_route_target())
       
        rtgt_list.delete_route_target('target:1:1')
        vn.set_route_target_list(rtgt_list)
        exp_rtgt_list.delete_route_target('target:2:1')
        vn.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list.delete_route_target('target:3:1')
        vn.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn)
        _match_route_table(rtgt_list.get_route_target() +
                           imp_rtgt_list.get_route_target())

        routes.set_route([])
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        @retries(5)
        def _match_route_table_cleanup(sc_ri_name, rt100):
            lri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(lvn, sc_ri_name))
            sr = lri.get_static_route_entries()
            if sr and sr.route:
                raise Exception("sr has route")
            ri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(lvn))
            rt_refs = ri.get_route_target_refs()
            rt_set = set(ref['to'][0] for ref in ri.get_route_target_refs() or [])
            if rt100 & rt_set:
                raise Exception("route-target ref still found: %s" % (rt100 & rt_set))

        _match_route_table_cleanup(sc_ri_name, rt100)

        # add the route again, then delete the network without deleting the
        # link to route table
        routes.add_route(route)
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)
        _match_route_table(rtgt_list.get_route_target())
        self._vnc_lib.virtual_network_delete(fq_name=vn.get_fq_name())
        _match_route_table_cleanup(sc_ri_name, rt100)

        self._vnc_lib.route_table_delete(fq_name=rt.get_fq_name())
        self.delete_network_policy(np, auto_policy=True)
        gevent.sleep(2)
        self._vnc_lib.virtual_network_delete(fq_name=lvn.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=rvn.get_fq_name())
    # test_add_delete_route

    def test_add_delete_static_route(self):

        vn1_name = 'vn1'
        vn2_name = 'vn2'
        vn1 = self.create_virtual_network(vn1_name, "1.0.0.0/24")
        vn2 = self.create_virtual_network(vn2_name, "2.0.0.0/24")

        rt = RouteTable("rt1")
        self._vnc_lib.route_table_create(rt)
        vn1.add_route_table(rt)
        self._vnc_lib.virtual_network_update(vn1)
        routes = RouteTableType()
        route = RouteType(prefix="1.1.1.1/0",
                          next_hop="10.10.10.10", next_hop_type="ip-address")
        routes.add_route(route)
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        @retries(5)
        def _match_route_table(vn, prefix, next_hop, should_present=True):
            ri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(vn))
            sr_list = ri.get_static_route_entries()
            if sr_list is None:
                if should_present:
                    raise Exception("sr is None")
                else:
                    return
            found = False
            for sr in sr_list.get_route() or []:
                if sr.prefix == prefix and sr.next_hop == next_hop:
                    found = True
                    break
            if found != should_present:
                raise Exception("route " + prefix + "" + next_hop + "not found")
            return

        _match_route_table(vn1, "1.1.1.1/0", "10.10.10.10")

        route = RouteType(prefix="2.2.2.2/0",
                          next_hop="20.20.20.20", next_hop_type="ip-address")
        routes.add_route(route)
        rt.set_routes(routes)

        self._vnc_lib.route_table_update(rt)
        _match_route_table(vn1, "1.1.1.1/0", "10.10.10.10")
        _match_route_table(vn1, "2.2.2.2/0", "20.20.20.20")

        vn2.add_route_table(rt)
        self._vnc_lib.virtual_network_update(vn2)

        _match_route_table(vn1, "1.1.1.1/0", "10.10.10.10")
        _match_route_table(vn1, "2.2.2.2/0", "20.20.20.20")
        _match_route_table(vn2, "1.1.1.1/0", "10.10.10.10")
        _match_route_table(vn2, "2.2.2.2/0", "20.20.20.20")

        # delete second route and check vn ri sr entries
        routes.delete_route(route)
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        gevent.sleep(10)
        _match_route_table(vn1, "2.2.2.2/0", "20.20.20.20", False)
        _match_route_table(vn2, "2.2.2.2/0", "20.20.20.20", False)

        @retries(5)
        def _match_route_table_cleanup(vn):
            ri = self._vnc_lib.routing_instance_read(
                fq_name=self.get_ri_name(vn))
            sr = ri.get_static_route_entries()
            if sr and sr.route:
                raise Exception("sr has route")

        vn2.del_route_table(rt)
        self._vnc_lib.virtual_network_update(vn2)
        _match_route_table(vn1, "1.1.1.1/0", "10.10.10.10")
        _match_route_table_cleanup(vn2)

        vn1.del_route_table(rt)
        self._vnc_lib.virtual_network_update(vn1)
        _match_route_table_cleanup(vn1)

        vn1.add_route_table(rt)
        self._vnc_lib.virtual_network_update(vn1)

        vn2.add_route_table(rt)
        self._vnc_lib.virtual_network_update(vn2)

        routes.set_route([])
        rt.set_routes(routes)
        self._vnc_lib.route_table_update(rt)

        _match_route_table_cleanup(vn1)
        _match_route_table_cleanup(vn2)

        self._vnc_lib.virtual_network_delete(fq_name=vn1.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2.get_fq_name())
        gevent.sleep(2)
        self._vnc_lib.route_table_delete(fq_name=rt.get_fq_name())
    # test_add_delete_static_route

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
            self.id(), self._api_server_ip, self._api_server_port)
        gevent.sleep(2)

        # check if vn is deleted
        self.check_vn_is_deleted(uuid=vn.uuid)

        # check if ri is deleted
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn))
    # test_vn_delete

    @retries(5)
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

        si_name = 'default-domain:default-project:' + service_name
        rp_name = self.id() + 'rp1'
        rp = RoutingPolicy(rp_name)
        si_obj = self._vnc_lib.service_instance_read(fq_name_str=si_name)
        si_rp = RoutingPolicyServiceInstanceType(left_sequence='1.0')
        rp.add_service_instance(si_obj, si_rp)
        self._vnc_lib.routing_policy_create(rp)
        self.wait_to_get_object(config_db.RoutingPolicyST,
                                rp.get_fq_name_str())

        rlist = RouteListType(route=['100.0.0.0/24'])
        ra = RouteAggregate('ra1', aggregate_route_entries=rlist)

        sit = ServiceInterfaceTag(interface_type='left')
        ra.add_service_instance(si_obj, sit)
        self._vnc_lib.route_aggregate_create(ra)
        self.wait_to_get_object(config_db.RouteAggregateST,
                                ra.get_fq_name_str())

        sc = self.wait_to_get_sc()
        sc_ri_name = ('service-' + sc + '-default-domain_default-project_'
                      + service_name)
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_name),
                                  self.get_ri_name(vn2_obj))

        # stop st
        test_common.kill_schema_transformer(self._st_greenlet)

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        ra.del_service_instance(si_obj)
        self._vnc_lib.route_aggregate_update(ra)
        rp.del_service_instance(si_obj)
        self._vnc_lib.routing_policy_update(rp)
        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self.check_all_vmis_are_deleted()

        # start st on a free port
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self.id(), self._api_server_ip, self._api_server_port)

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

        sc = self.wait_to_get_sc()
        sc_ri_name = ('service-' + sc + '-default-domain_default-project_'
                      + service_name)
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_name),
                                  self.get_ri_name(vn2_obj))
        # stop st and wait for sometime
        test_common.kill_schema_transformer(self._st_greenlet)
        gevent.sleep(5)

        # start st on a free port
        self._st_greenlet = gevent.spawn(test_common.launch_schema_transformer,
            self.id(), self._api_server_ip, self._api_server_port)

        #check service chain state
        sc = self.wait_to_get_sc()
        sc_ri_name = ('service-' + sc + '-default-domain_default-project_'
                      + service_name)

        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_name),
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
        self.check_route_target_in_routing_instance(ri_name,rtgt_list.get_route_target())

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
        self.check_lr_is_deleted(uuid=lr.uuid)
        self.check_rt_is_deleted(name='target:64512:8000002')

    @retries(5)
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

    @retries(10)
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
        np = self.create_network_policy(
            vn1_obj, vn2_obj, mirror_service=service_name, auto_policy=False,
            service_mode='transparent', service_type='analyzer')
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

        svc_ri_fq_name = 'default-domain:default-project:svc-vn-left:svc-vn-left'.split(':')
        self.check_ri_ref_present(svc_ri_fq_name, self.get_ri_name(vn1_obj))
        self.check_ri_ref_present(svc_ri_fq_name, self.get_ri_name(vn2_obj))

        self.check_acl_match_mirror_to_ip(self.get_ri_name(vn1_obj))
        self.check_acl_match_nets(self.get_ri_name(vn1_obj),
                                  ':'.join(vn1_obj.get_fq_name()),
                                  ':'.join(vn2_obj.get_fq_name()))
        self.check_acl_match_nets(self.get_ri_name(vn2_obj),
                                  ':'.join(vn2_obj.get_fq_name()),
                                  ':'.join(vn1_obj.get_fq_name()))

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        self.check_acl_not_match_mirror_to_ip(self.get_ri_name(vn1_obj))
        self.check_acl_not_match_nets(self.get_ri_name(vn1_obj),
                                      ':'.join(vn1_obj.get_fq_name()),
                                      ':'.join(vn2_obj.get_fq_name()))
        self.check_acl_not_match_nets(self.get_ri_name(vn2_obj),
                                      ':'.join(vn2_obj.get_fq_name()),
                                      ':'.join(vn1_obj.get_fq_name()))

    def test_service_and_analyzer_policy(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, '20.0.0.0/24')

        service_name = self.id() + 's1'
        analyzer_service_name = self.id() + '_analyzer'
        np = self.create_network_policy(vn1_obj, vn2_obj, [service_name],
                                        analyzer_service_name)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        sc = self.wait_to_get_sc()
        sc_ri_name = 'service-'+sc+'-default-domain_default-project_' + service_name
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_name),
                                  self.get_ri_name(vn2_obj))

        self.check_service_chain_prefix_match(fq_name=self.get_ri_name(vn2_obj, sc_ri_name),
                                       prefix='10.0.0.0/24')

        svc_ri_fq_name = 'default-domain:default-project:svc-vn-left:svc-vn-left'.split(':')
        self.check_ri_ref_present(svc_ri_fq_name, self.get_ri_name(vn1_obj))
        self.check_ri_ref_present(svc_ri_fq_name, self.get_ri_name(vn2_obj))

        self.check_acl_match_mirror_to_ip(self.get_ri_name(vn1_obj))
        self.check_acl_match_nets(self.get_ri_name(vn1_obj),
                                  ':'.join(vn1_obj.get_fq_name()),
                                  ':'.join(vn2_obj.get_fq_name()))
        self.check_acl_match_nets(self.get_ri_name(vn2_obj),
                                  ':'.join(vn2_obj.get_fq_name()),
                                  ':'.join(vn1_obj.get_fq_name()))

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

    @retries(5)
    def check_security_group_id(self, sg_fq_name, verify_sg_id = None):
        sg = self._vnc_lib.security_group_read(sg_fq_name)
        sg_id = sg.get_security_group_id()
        if sg_id is None: 
            raise Exception('sg id is not present for %s' % sg_fq_name)
        if verify_sg_id is not None and str(sg_id) != str(verify_sg_id):
            raise Exception('sg id is not same as passed value (%s, %s)' %
                            (str(sg_id), str(verify_sg_id)))

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

    def test_asn(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')
        ident_name = self.get_obj_imid(vn1_obj)
        gevent.sleep(2)
        ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        self.check_ri_asn(self.get_ri_name(vn1_obj), 'target:64512:8000001')

        # create router1
        r1_name = self.id() + 'router1'
        router1 = self.create_bgp_router(r1_name, 'contrail')
        self.check_bgp_asn(router1.get_fq_name(), 64512)

        # create virtual machine interface
        vmi_name = self.id() + 'vmi1'
        vmi = VirtualMachineInterface(vmi_name, parent_type='project',
                                      fq_name=['default-domain',
                                               'default-project', vmi_name])
        vmi.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        # create logical router
        lr_name = self.id() + 'lr1'
        lr = LogicalRouter(lr_name)
        lr.add_virtual_machine_interface(vmi)
        self._vnc_lib.logical_router_create(lr)
        self.check_lr_asn(lr.get_fq_name(), 'target:64512:8000002')

        #update global system config but dont change asn value for equality path
        gs = self._vnc_lib.global_system_config_read(
            fq_name=['default-global-system-config'])
        gs.set_autonomous_system(64512)
        self._vnc_lib.global_system_config_update(gs)

        # check route targets
        self.check_ri_asn(self.get_ri_name(vn1_obj), 'target:64512:8000001')
        self.check_bgp_asn(router1.get_fq_name(), 64512)
        self.check_lr_asn(lr.get_fq_name(), 'target:64512:8000002')

        #update ASN value
        gs = self._vnc_lib.global_system_config_read(
            fq_name=[u'default-global-system-config'])
        gs.set_autonomous_system(50000)
        self._vnc_lib.global_system_config_update(gs)

        # check new route targets
        self.check_ri_asn(self.get_ri_name(vn1_obj), 'target:50000:8000001')
        self.check_bgp_asn(router1.get_fq_name(), 50000)
        self.check_lr_asn(lr.get_fq_name(), 'target:50000:8000002')
    #end test_asn

    def test_fip(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, '20.0.0.0/24')

        service_name = self.id() + 's1'
        np = self.create_network_policy(vn1_obj, vn2_obj, [service_name],
                                        service_mode='in-network',
                                        auto_policy=True)

        sc = self.wait_to_get_sc()
        sc_ri_name = 'service-'+sc+'-default-domain_default-project_' + service_name
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_name),
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
        default_project = self._vnc_lib.project_read(
            fq_name=[u'default-domain', u'default-project'])
        fip_obj.set_project(default_project)
        fip_uuid = self._vnc_lib.floating_ip_create(fip_obj)
        fip_obj.set_virtual_machine_interface(vmi)
        self._vnc_lib.floating_ip_update(fip_obj)

        fip_obj = self._vnc_lib.floating_ip_read(fip_obj.get_fq_name())

        for obj in [fip_obj]:
            ident_name = self.get_obj_imid(obj)
            ifmap_ident = self.assertThat(FakeIfmapClient._graph,
                                          Contains(ident_name))

        self.wait_to_get_link(ident_name, vmi_fq_name)

        fip = fip_obj.get_floating_ip_address()
        self.check_vrf_assign_table(vmi.get_fq_name(), fip, True)

        fip_fq_name = fip_obj.get_fq_name()
        self._vnc_lib.floating_ip_delete(fip_fq_name)
        self.wait_to_remove_link(self.get_obj_imid(vmi), fip_fq_name)
        self.check_vrf_assign_table(vmi.get_fq_name(), fip, False)

    def test_pnf_service(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, '20.0.0.0/24')

        service_name = self.id() + 's1'
        np = self.create_network_policy(vn1_obj, vn2_obj, [service_name],
                                        service_virtualization_type='physical-device')
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        sc = self.wait_to_get_sc()
        sc_ri_name = 'service-'+sc+'-default-domain_default-project_' + service_name
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_name),
                                  self.get_ri_name(vn2_obj))

        self.check_service_chain_prefix_match(
            fq_name=self.get_ri_name(vn2_obj, sc_ri_name), prefix='10.0.0.0/24')
        ri1 = self._vnc_lib.routing_instance_read(fq_name=self.get_ri_name(vn1_obj))
        self.assertEqual(ri1.get_routing_instance_has_pnf(), True)

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self.check_ri_refs_are_deleted(fq_name=self.get_ri_name(vn1_obj))
        ri1 = self._vnc_lib.routing_instance_read(fq_name=self.get_ri_name(vn1_obj))
        self.assertEqual(ri1.get_routing_instance_has_pnf(), False)

        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
    # end test_pnf_service

    def test_interface_mirror(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        service_name = self.id() + 's1'
        si_fq_name_str = self._create_service(
            [('left', vn1_obj)], service_name, False,
            service_mode='transparent', service_type='analyzer')

        ident_name = self.get_obj_imid(vn1_obj)
        gevent.sleep(2)
        ifmap_ident = self.assertThat(FakeIfmapClient._graph, Contains(ident_name))

        # create virtual machine interface with interface mirror property
        vmi_name = self.id() + 'vmi1'
        vmi_fq_name = ['default-domain', 'default-project', vmi_name]
        vmi = VirtualMachineInterface(vmi_name, parent_type='project',
                                      fq_name=vmi_fq_name)
        vmi.add_virtual_network(vn1_obj)
        props = VirtualMachineInterfacePropertiesType()
        mirror_type = InterfaceMirrorType()
        mirror_act_type = MirrorActionType()
        mirror_act_type.analyzer_name = 'default-domain:default-project:test.test_service.TestPolicy.test_interface_mirrors1'
        mirror_type.mirror_to = mirror_act_type
        props.interface_mirror = mirror_type
        vmi.set_virtual_machine_interface_properties(props)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        self.check_analyzer_ip(vmi_fq_name)

        props = VirtualMachineInterfacePropertiesType()
        mirror_type = InterfaceMirrorType()
        mirror_act_type = MirrorActionType()
        mirror_act_type.analyzer_name = None
        mirror_type.mirror_to = mirror_act_type
        props.interface_mirror = mirror_type
        vmi.set_virtual_machine_interface_properties(props)
        self._vnc_lib.virtual_machine_interface_update(vmi)

        self.check_analyzer_no_ip(vmi_fq_name)

        self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)

    #end test_interface_mirror

    def test_transit_vn(self):
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

        sc_ri_name = 'service-'+sc+'-default-domain_default-project_' + service_name
        #basic checks
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_name),
                                  self.get_ri_name(vn2_obj))

        self.check_service_chain_prefix_match(
            fq_name=self.get_ri_name(vn2_obj, sc_ri_name), prefix='10.0.0.0/24')

        #vn1 rt is in not sc ri
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:64512:8000001', False)

        #set transit and check vn1 rt is in sc ri
        vn_props = VirtualNetworkType()
        vn_props.allow_transit = True
        vn1_obj.set_virtual_network_properties(vn_props)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:64512:8000001', True, 'export')

        #unset transit and check vn1 rt is not in sc ri
        vn_props.allow_transit = False
        vn1_obj.set_virtual_network_properties(vn_props)
        self._vnc_lib.virtual_network_update(vn1_obj)

        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:64512:8000001', False)

        #set transit on both vn1, vn2 and check vn1 & vn2 rt's are in sc ri
        vn_props.allow_transit = True
        vn1_obj.set_virtual_network_properties(vn_props)
        vn2_obj.set_virtual_network_properties(vn_props)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:64512:8000001', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn2_obj,sc_ri_name),
                            'target:64512:8000002', True, 'export')

        #unset transit on both vn1, vn2 and check vn1 & vn2 rt's are not in sc ri
        vn_props.allow_transit = False
        vn1_obj.set_virtual_network_properties(vn_props)
        vn2_obj.set_virtual_network_properties(vn_props)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:64512:8000001', False)
        self.check_rt_in_ri(self.get_ri_name(vn2_obj,sc_ri_name),
                            'target:64512:8000002', False)

        #test external rt
        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        vn1_obj.set_route_target_list(rtgt_list)
        rtgt_list = RouteTargetList(route_target=['target:2:1'])
        vn1_obj.set_export_route_target_list(rtgt_list)
        vn_props.allow_transit = True
        vn1_obj.set_virtual_network_properties(vn_props)
        self._vnc_lib.virtual_network_update(vn1_obj)

        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:64512:8000001', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:1:1', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:2:1', True, 'export')

        #modify external rt
        rtgt_list = RouteTargetList(route_target=['target:1:2'])
        vn1_obj.set_route_target_list(rtgt_list)
        rtgt_list = RouteTargetList(route_target=['target:2:2'])
        vn1_obj.set_export_route_target_list(rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:64512:8000001', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:1:2', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:2:2', True, 'export')

        #have more than one external rt
        rtgt_list = RouteTargetList(route_target=['target:1:1', 'target:1:2'])
        vn1_obj.set_route_target_list(rtgt_list)
        rtgt_list = RouteTargetList(route_target=['target:2:1', 'target:2:2'])
        vn1_obj.set_export_route_target_list(rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:64512:8000001', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:1:1', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:1:2', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:2:1', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:2:2', True, 'export')

        #unset external rt
        vn1_obj.set_route_target_list(RouteTargetList())
        vn1_obj.set_export_route_target_list(RouteTargetList())
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:64512:8000001', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:1:1', False)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:1:2', False)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:2:1', False)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:2:2', False)

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)

        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self._vnc_lib.virtual_network_delete(id=vn2_obj.uuid)
    #end test_transit_vn

    def test_misc(self):
        # create a service chain
        # find the service chain
        # check sandesh message parameters
        # make a copy of it
        # check for the equality of these service chains

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

        self.wait_to_get_sc()

        sp_list1 = [PortType(start_port=5000, end_port=8000),
                    PortType(start_port=2000, end_port=3000)]
        dp_list1 = [PortType(start_port=1000, end_port=1500),
                    PortType(start_port=500, end_port=800)]
        service_names1 = [self.id() + 's1', self.id() + 's2', self.id() + 's3']

        sp_list2 = [PortType(start_port=5000, end_port=8000),
                    PortType(start_port=2000, end_port=3000)]
        dp_list2 = [PortType(start_port=1000, end_port=1500),
                    PortType(start_port=500, end_port=800)]
        service_names2 = [self.id() + 's1', self.id() + 's2', self.id() + 's3']

        sc11 = config_db.ServiceChain.find_or_create(
            "vn1", "vn2", "<>", sp_list1, dp_list1, "icmp", service_names1)

        #build service chain introspect and check if it has got right values
        sandesh_sc = sc11.build_introspect()

        self.assertEqual(sandesh_sc.left_virtual_network, sc11.left_vn)
        self.assertEqual(sandesh_sc.right_virtual_network, sc11.right_vn)
        self.assertEqual(sandesh_sc.protocol, sc11.protocol)
        port_list = []
        for sp in sp_list1:
            port_list.append("%s-%s" % (sp.start_port, sp.end_port))
        self.assertEqual(sandesh_sc.src_ports,  ','.join(port_list))

        port_list = []
        for dp in dp_list1:
            port_list.append("%s-%s" % (dp.start_port, dp.end_port))

        self.assertEqual(sandesh_sc.dst_ports,  ','.join(port_list))
        self.assertEqual(sandesh_sc.direction, sc11.direction)
        self.assertEqual(sandesh_sc.service_list, service_names1)

        sc22 = config_db.ServiceChain.find_or_create(
            "vn1", "vn2", "<>", sp_list1, dp_list1, "icmp", service_names2)

        sc33 = copy.deepcopy(sc11)

        # check for SC equality, sc11 && sc22 are references
        self.assertEqual(sc11, sc22)

        # check for SC equality, sc11 && sc33 are different
        self.assertEqual(sc11, sc33)

        # change values and test
        sc33.protocol = "tcp"

        self.assertTrue(sc11 != sc33)

        sc33.service_list = []
        self.assertTrue(sc11 != sc33)

        sc33.direction = "<"
        self.assertTrue(sc11 != sc33)

        sc33.dp_list = []
        self.assertTrue(sc11 != sc33)

        sc33.sp_list = []
        self.assertTrue(sc11 != sc33)

        sc33.name = "dummy"
        self.assertTrue(sc11 != sc33)

        sc11.delete()

        vn1_obj.del_network_policy(np)
        vn2_obj.del_network_policy(np)

        # create virtual machine and create VMI and set VM as parent of VMI
        # perform delete operations
        vm_name = self.id() + 'vm1'
        vm = VirtualMachine(vm_name)
        self._vnc_lib.virtual_machine_create(vm)

        # create virtual machine interface
        vmi_name = self.id() + 'vmi1'
        vmi = VirtualMachineInterface(vmi_name, parent_type='virtual-machine',
                                      fq_name=[vm_name, vmi_name])
        vmi.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(vmi)

        self._vnc_lib.virtual_machine_interface_delete(id=vmi.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self._vnc_lib.virtual_network_delete(id=vn2_obj.uuid)
        self._vnc_lib.virtual_machine_delete(id=vm.uuid)
        self.check_st_vm_is_deleted(vm_name)

    #end test_misc

    @retries(5)
    def check_bgp_no_peering(self, router1, router2):
        r1 = self._vnc_lib.bgp_router_read(fq_name=router1.get_fq_name())
        ref_names = [ref['to'] for ref in r1.get_bgp_router_refs() or []]
        self.assertThat(ref_names, Not(Contains(router2.get_fq_name())))

    def test_bgpaas(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name,
                                              ['10.0.0.0/24', '1000::/16'])

        project_name = ['default-domain', 'default-project']
        project_obj = self._vnc_lib.project_read(fq_name=project_name)
        port_name = self.id() + 'p1'
        port_obj = VirtualMachineInterface(port_name, parent_obj=project_obj)
        port_obj.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(port_obj)

        v6_obj = InstanceIp(name=port_name+'-v6')
        v6_obj.set_virtual_machine_interface(port_obj)
        v6_obj.set_virtual_network(vn1_obj)
        v6_obj.set_instance_ip_family('v6')
        self._vnc_lib.instance_ip_create(v6_obj)

        v4_obj = InstanceIp(name=port_name+'-v4')
        v4_obj.set_virtual_machine_interface(port_obj)
        v4_obj.set_virtual_network(vn1_obj)
        v4_obj.set_instance_ip_family('v4')
        self._vnc_lib.instance_ip_create(v4_obj)

        bgpaas_name = self.id() + 'bgp1'
        bgpaas = BgpAsAService(bgpaas_name, parent_obj=project_obj)
        bgpaas.add_virtual_machine_interface(port_obj)
        self._vnc_lib.bgp_as_a_service_create(bgpaas)

        router1_name = vn1_obj.get_fq_name_str() + ':' + vn1_name + ':' + port_name
        self.wait_to_get_object(config_db.BgpAsAServiceST,
                                bgpaas.get_fq_name_str())
        self.wait_to_get_object(config_db.BgpRouterST, router1_name)
        server_fq_name = ':'.join(self.get_ri_name(vn1_obj)) + ':bgpaas-server'
        self.wait_to_get_object(config_db.BgpRouterST, server_fq_name)
        server_router_obj = self._vnc_lib.bgp_router_read(fq_name_str=server_fq_name)

        mx_bgp_router = self.create_bgp_router("mx-bgp-router", "contrail")
        mx_bgp_router_name = mx_bgp_router.get_fq_name_str()
        self.wait_to_get_object(config_db.BgpRouterST, mx_bgp_router_name)
        mx_bgp_router = self._vnc_lib.bgp_router_read(fq_name_str=mx_bgp_router_name)
        self.check_bgp_no_peering(server_router_obj, mx_bgp_router)

        router1_obj = self._vnc_lib.bgp_router_read(fq_name_str=router1_name)
        self.assertEqual(router1_obj.get_bgp_router_parameters().address,
                         '10.0.0.252')
        self.assertEqual(router1_obj.get_bgp_router_parameters().identifier,
                         '10.0.0.252')

        self.check_bgp_peering(server_router_obj, router1_obj, 1)

        v4_obj.set_instance_ip_address('10.0.0.60')
        self._vnc_lib.instance_ip_update(v4_obj)
        self.check_bgp_router_ip(router1_name, '10.0.0.60')
        self.check_bgp_router_identifier(router1_name, '10.0.0.60')

        bgpaas.set_bgpaas_ip_address('10.0.0.70')
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        self.check_bgp_router_ip(router1_name, '10.0.0.70')
        v4_obj.del_virtual_machine_interface(port_obj)
        v4_obj.del_virtual_network(vn1_obj)
        self._vnc_lib.instance_ip_delete(id=v4_obj.uuid)
        self.check_bgp_router_ip(router1_name, '10.0.0.70')
        self.check_bgp_router_identifier(router1_name, '10.0.0.70')

        port2_name = self.id() + 'p2'
        port2_obj = VirtualMachineInterface(port2_name, parent_obj=project_obj)
        port2_obj.add_virtual_network(vn1_obj)
        self._vnc_lib.virtual_machine_interface_create(port2_obj)
        bgpaas.add_virtual_machine_interface(port2_obj)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        router2_name = vn1_obj.get_fq_name_str() + ':' + vn1_name + ':' + port2_name
        self.wait_to_get_object(config_db.BgpRouterST, router2_name)

        router2_obj = self._vnc_lib.bgp_router_read(fq_name_str=router2_name)
        self.check_bgp_peering(server_router_obj, router2_obj, 2)
        self.check_bgp_peering(server_router_obj, router1_obj, 2)

        bgpaas.del_virtual_machine_interface(port_obj)
        self._vnc_lib.bgp_as_a_service_update(bgpaas)
        self.wait_to_delete_object(config_db.BgpRouterST, router1_name)
        self._vnc_lib.bgp_as_a_service_delete(id=bgpaas.uuid)
        self.wait_to_delete_object(config_db.BgpRouterST, router2_name)

        self._vnc_lib.instance_ip_delete(id=v6_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port_obj.uuid)
        self._vnc_lib.virtual_machine_interface_delete(id=port2_obj.uuid)
    # end test_bgpaas

    def test_configured_targets(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')
        self.wait_to_get_object(config_db.RoutingInstanceST,
                                vn1_obj.get_fq_name_str()+':'+vn1_name)

        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        vn1_obj.set_route_target_list(rtgt_list)
        exp_rtgt_list = RouteTargetList(route_target=['target:2:1'])
        vn1_obj.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list = RouteTargetList(route_target=['target:3:1'])
        vn1_obj.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)

        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:2:1', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:3:1', True, 'import')

        exp_rtgt_list.route_target.append('target:1:1')
        vn1_obj.set_export_route_target_list(exp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:2:1', True, 'export')

        imp_rtgt_list.route_target.append('target:1:1')
        vn1_obj.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:3:1', True, 'import')

        exp_rtgt_list = RouteTargetList(route_target=['target:2:1'])
        vn1_obj.set_export_route_target_list(exp_rtgt_list)
        imp_rtgt_list = RouteTargetList(route_target=['target:3:1'])
        vn1_obj.set_import_route_target_list(imp_rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:1:1', True)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:2:1', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj), 'target:3:1', True, 'import')
# end class TestRouteTable
