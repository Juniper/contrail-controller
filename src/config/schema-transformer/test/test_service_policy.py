#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#
import sys
import copy
import gevent
from testtools.matchers import Contains, Not

try:
    import to_bgp
except ImportError:
    from schema_transformer import to_bgp
try:
    import config_db
except ImportError:
    from schema_transformer import config_db
from vnc_api.vnc_api import (VirtualNetwork, SequenceType, VirtualNetworkType,
        VirtualNetworkPolicyType, NoIdError, NetworkIpam, VirtualMachine,
        VnSubnetsType, IpamSubnetType, SubnetType, FloatingIpPool,
        FloatingIp, VirtualMachineInterfacePropertiesType, PortType,
        VirtualMachineInterface, InterfaceMirrorType, MirrorActionType,
        ServiceChainInfo, RoutingPolicy, RoutingPolicyServiceInstanceType,
        RouteListType, RouteAggregate,RouteTargetList, ServiceInterfaceTag,
        PolicyBasedForwardingRuleType)

from cfgm_common.exceptions import RefsExistError
from test_case import STTestCase, retries
from test_policy import VerifyPolicy
sys.path.append("../common/tests")
from test_utils import CassandraCFs
import test_common
from netaddr import IPNetwork, IPAddress
import uuid


class VerifyServicePolicy(VerifyPolicy):
    def __init__(self, vnc_lib):
        self._vnc_lib = vnc_lib

    @retries(5)
    def wait_to_get_sc(self, left_vn=None, right_vn=None, si_name=None,
                       check_create=False):
        for sc in to_bgp.ServiceChain.values():
            if (left_vn in (None, sc.left_vn) and
                    right_vn in (None, sc.right_vn) and
                    si_name in (sc.service_list[0], None)):
                if check_create and not sc.created:
                    raise Exception('Service chain not created')
                return sc.name
        raise Exception('Service chain not found')

    @retries(5)
    def check_service_chain_prefix_match(self, fq_name, prefix):
        ip_version = IPNetwork(prefix).version
        ri = self._vnc_lib.routing_instance_read(fq_name)
        if ip_version == 6:
            sci = ri.get_ipv6_service_chain_information()
        else:
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
    def check_service_chain_is_deleted(self, sc_uuid):
        for sc in to_bgp.ServiceChain.values():
            if sc_uuid == sc.name:
                raise Exception('Service chain %s not deleted' % sc_uuid)

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
        cf = CassandraCFs.get_cf('to_bgp_keyspace', _SC_IP_CF)
        ip = cf.get(sc_name)['ip_address']

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
    def check_acl_match_mirror_to_ip(self, fq_name):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.access_control_list_entries.acl_rule:
            if (rule.action_list.mirror_to.analyzer_ip_address is not None):
                return
        raise Exception('mirror to ip not found in ACL rules for %s' % (fq_name))

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

    @retries(5)
    def check_st_vm_is_deleted(self, name):
        vm_obj = config_db.VirtualMachineST.get(name)
        if vm_obj is not None:
            raise Exception('vm %s still exists' % name)
        return

    @retries(5)
    def check_default_ri_rtgt_imported(self, fq_name, service_ri_fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        service_ri = self._vnc_lib.routing_instance_read(service_ri_fq_name)
        to_fq_names = [rt_ref['to'] for rt_ref in service_ri.get_route_target_refs()]
        for rt_ref in ri.get_route_target_refs() or []:
            if rt_ref['to'] in to_fq_names:
                return
        raise Exception('%s not imported to service_ri:%s ' % (rt_ref['to'],
                    service_ri_fq_name))

    @retries(5)
    def check_default_ri_rtgt_not_imported(self, fq_name, service_ri_fq_name):
        ri = self._vnc_lib.routing_instance_read(fq_name)
        service_ri = self._vnc_lib.routing_instance_read(service_ri_fq_name)
        to_fq_names = [rt_ref['to'] for rt_ref in service_ri.get_route_target_refs()]
        for rt_ref in ri.get_route_target_refs() or []:
            if rt_ref['to'] in to_fq_names:
                raise Exception('%s imported to service_ri:%s ' % (rt_ref['to'],
                    service_ri_fq_name))

    @retries(5)
    def delete_vn(self, fq_name):
        try:
            self._vnc_lib.virtual_network_delete(fq_name=fq_name)
            print 'vn deleted'
        except RefsExistError:
            print "retrying ... ", test_common.lineno()
            raise Exception('virtual network %s still exists' % str(fq_name))

    @retries(5)
    def check_acl_action_assign_rules(self, fq_name, vn1_fq_name, vn2_fq_name, sc_ri_fq_name):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.access_control_list_entries.acl_rule:
            if (rule.match_condition.src_address.virtual_network == vn1_fq_name and
                rule.match_condition.dst_address.virtual_network == vn2_fq_name):
                    if rule.action_list.assign_routing_instance == sc_ri_fq_name:
                        return
        raise Exception('vrf assign for  nets %s/%s not matched in ACL rules for %s; sc: %s' %
                        (vn1_fq_name, vn2_fq_name, fq_name, sc_ri_fq_name))

    @retries(5)
    def check_all_vmis_are_deleted(self):
        vmi_list = self._vnc_lib.virtual_machine_interfaces_list()
        if vmi_list['virtual-machine-interfaces']:
            raise Exception('virtual machine interfaces still exist' + str(vmi_list))
        print 'all virtual machine interfaces deleted'

    @retries(5)
    def check_acl_match_subnets(self, fq_name, subnet1, subnet2, sc_ri_fq_name):
        acl = self._vnc_lib.access_control_list_read(fq_name)
        for rule in acl.access_control_list_entries.acl_rule:
            if (rule.match_condition.src_address.subnet == subnet1 and
                rule.match_condition.dst_address.subnet == subnet2):
                    if rule.action_list.assign_routing_instance == sc_ri_fq_name:
                        return
        raise Exception('subnets assigned not matched in ACL rules for %s; sc: %s' %
                        (fq_name, sc_ri_fq_name))

class TestServicePolicy(STTestCase, VerifyServicePolicy):
    def test_match_subnets_in_service_policy(self, version=None):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, ['10.0.0.2/24'])
        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, ['20.0.0.2/24'])

        subnet1= SubnetType('10.0.0.2', 24)
        subnet2= SubnetType('20.0.0.2', 24)

        service_name = self.id() + 's1'
        kwargs = {'subnet_1': subnet1, 'subnet_2': subnet2}
        np = self.create_network_policy(vn1_obj, vn2_obj,
                                        [service_name], version=version, **kwargs)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        sc = self.wait_to_get_sc()
        sc_ri_name = 'service-'+sc+'-default-domain_default-project_' + service_name
        self.check_acl_match_subnets(vn1_obj.get_fq_name(), subnet1,
                                  subnet2, ':'.join(self.get_ri_name(vn1_obj, sc_ri_name)))

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
    # end test_match_subnets_in_service_policy

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

        self.check_acl_action_assign_rules(vn1_obj.get_fq_name(), vn1_obj.get_fq_name_str(),
                                  vn2_obj.get_fq_name_str(), ':'.join(self.get_ri_name(vn1_obj, sc_ri_name)))
        self.check_acl_action_assign_rules(vn1_obj.get_fq_name(), vn2_obj.get_fq_name_str(),
                                  vn1_obj.get_fq_name_str(), ':'.join(self.get_ri_name(vn1_obj, sc_ri_name)))
        self.check_acl_action_assign_rules(vn2_obj.get_fq_name(), vn2_obj.get_fq_name_str(),
                                  vn1_obj.get_fq_name_str(), ':'.join(self.get_ri_name(vn2_obj, sc_ri_name)))
        self.check_acl_action_assign_rules(vn2_obj.get_fq_name(), vn1_obj.get_fq_name_str(),
                                  vn2_obj.get_fq_name_str(), ':'.join(self.get_ri_name(vn2_obj, sc_ri_name)))

        si_name = 'default-domain:default-project:' + service_name
        if version == 2:
            v4_service_chain_address = '10.0.0.251'
            v6_service_chain_address = '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffb'
        else:
            v4_service_chain_address = '10.0.0.252'
            v6_service_chain_address = '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffc'
        sci = ServiceChainInfo(prefix = ['10.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn1_obj)),
                               service_chain_address = v4_service_chain_address,
                               service_instance = si_name)
        self.check_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)
        sci.prefix = ['1000::/16']
        sci.service_chain_address = v6_service_chain_address
        self.check_v6_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)
        sci = ServiceChainInfo(prefix = ['20.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn2_obj)),
                               service_chain_address = v4_service_chain_address,
                               service_instance = si_name)
        self.check_service_chain_info(self.get_ri_name(vn1_obj, sc_ri_name), sci)
        sci.prefix = ['2000::/16']
        sci.service_chain_address = v6_service_chain_address
        self.check_v6_service_chain_info(self.get_ri_name(vn1_obj, sc_ri_name), sci)

        vn1_obj.set_multi_policy_service_chains_enabled(True)
        vn2_obj.set_multi_policy_service_chains_enabled(True)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        self.check_ri_ref_not_present(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_default_ri_rtgt_imported(self.get_ri_name(vn1_obj),
                self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_not_present(self.get_ri_name(vn2_obj),
                                  self.get_ri_name(vn2_obj, sc_ri_name))
        self.check_default_ri_rtgt_imported(self.get_ri_name(vn2_obj),
                self.get_ri_name(vn2_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn2_obj))

        vn1_obj.set_multi_policy_service_chains_enabled(False)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_default_ri_rtgt_not_imported(self.get_ri_name(vn1_obj),
                self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj),
                                  self.get_ri_name(vn2_obj, sc_ri_name))
        self.check_default_ri_rtgt_not_imported(self.get_ri_name(vn2_obj),
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
        link_name = ':'.join(self.get_ri_name(vn1_obj, sc_ri_name))
        self.assertTill(self.ifmap_ident_has_link, obj=rp, link_name=link_name)
        rp.del_service_instance(si_obj)
        self._vnc_lib.routing_policy_update(rp)
        self.assertTill(self.ifmap_ident_doesnt_have_link, obj=rp,
                        link_name=link_name)
        self._vnc_lib.routing_policy_delete(id=rp.uuid)

        rlist = RouteListType(route=['100.0.0.0/24'])
        ra = RouteAggregate('ra1', aggregate_route_entries=rlist)

        sit = ServiceInterfaceTag(interface_type='left')
        ra.add_service_instance(si_obj, sit)
        self._vnc_lib.route_aggregate_create(ra)
        self.wait_to_get_object(config_db.RouteAggregateST,
                                ra.get_fq_name_str())
        ra = self._vnc_lib.route_aggregate_read(id=ra.uuid)
        self.assertEqual(ra.get_aggregate_route_nexthop(), v4_service_chain_address)
        link_name = ':'.join(self.get_ri_name(vn1_obj, sc_ri_name))
        self.assertTill(self.ifmap_ident_has_link, obj=ra, link_name=link_name)
        ra.del_service_instance(si_obj)
        self._vnc_lib.route_aggregate_update(ra)
        self.assertTill(self.ifmap_ident_doesnt_have_link, obj=rp,
                        link_name=link_name)
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
        self.service_policy_test_with_version(2)
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

    def test_service_policy_delete_vns_deletes_scs(self):
        # Test to check deleting VNs without deleting the
        # policy associated, deletes the service chain.

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

        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())

        self.check_service_chain_is_deleted(sc_uuid=sc)
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self.check_vn_is_deleted(uuid=vn2_obj.uuid)
    # end test_service_policy_delete_vns_deletes_scs

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
            self.assertTill(self.ifmap_has_ident, obj=obj)

        sc = self.wait_to_get_sc()
        sc_ri_names = ['service-'+sc+'-default-domain_default-project_' + s for s in service_names]
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_names[0]))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_names[2]),
                                  self.get_ri_name(vn2_obj))

        si_name = 'default-domain:default-project:%s.test_multi_service_in_policys3' % self._class_str()
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

        old_service_list = np.network_policy_entries.policy_rule[0].action_list.apply_service
        np.network_policy_entries.policy_rule[0].action_list.apply_service = \
            old_service_list[:-1]
        np.set_network_policy_entries(np.network_policy_entries)
        self._vnc_lib.network_policy_update(np)

        sc_old = sc
        for i in range(0, 5):
            sc = self.wait_to_get_sc()
            if sc_old == sc:
                gevent.sleep(1)
                continue
            sc_ri_names = ['service-'+sc+'-default-domain_default-project_' + s for s in service_names]
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

        np.network_policy_entries.policy_rule[0].action_list.apply_service = \
            old_service_list
        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(fq_name=vn1_obj.get_fq_name())
        self._vnc_lib.virtual_network_delete(fq_name=vn2_obj.get_fq_name())
        self.check_vn_is_deleted(uuid=vn1_obj.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
    # end test_multi_service_policy

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
            self.check_default_ri_rtgt_imported(self.get_ri_name(vn1_obj),
                                    self.get_ri_name(vn1_obj, sc_ri_name))
            self.check_ri_ref_not_present(self.get_ri_name(vn2_obj),
                                          self.get_ri_name(vn2_obj, sc_ri_name))
            self.check_default_ri_rtgt_imported(self.get_ri_name(vn2_obj),
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
            link_name = ':'.join(self.get_ri_name(vn1_obj, sc_ri_name))
            self.assertTill(self.ifmap_ident_has_link, obj=rp,
                            link_name=link_name)
            rp.del_service_instance(si_obj)
            self._vnc_lib.routing_policy_update(rp)
            self.assertTill(self.ifmap_ident_doesnt_have_link, obj=rp,
                            link_name=link_name)
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

            link_name = ':'.join(self.get_ri_name(vn1_obj, sc_ri_name))
            self.assertTill(self.ifmap_ident_has_link, obj=ra,
                            link_name=link_name)
            ra.del_service_instance(si_obj)
            self._vnc_lib.route_aggregate_update(ra)
            self.assertTill(self.ifmap_ident_doesnt_have_link, obj=ra,
                            link_name=link_name)
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

    def _test_multi_rule_service_chain(self):
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, ['1.1.1.0/24',
                                                         '1.1.2.0/24',
                                                         '1000::/16'])

        # create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, ['20.0.0.0/24', '2000::/16'])

        rules = []
        rule1 = {"protocol": "icmp",
                 "direction": "<>",
                 "src-port": 'any',
                 "src": [{"type": "cidr", "value": "1.1.1.0/24"},
                         {"type": "vn", "value": vn1_obj}],
                 "dst": [{"type": "vn", "value": vn2_obj}],
                 "dst-port": 'any',
                 "action": "pass",
                 "service_list": [self.id() + 's1'],
                 "service_kwargs": {},
                 "auto_policy": False
                 }
        rule2 = {"protocol": "icmp",
                 "direction": "<>",
                 "src-port": 'any',
                 "src": [{"type": "vn", "value": vn1_obj},
                         {"type": "cidr", "value": "1.1.2.0/24"}],
                 "dst": {"type": "vn", "value": vn2_obj},
                 "dst-port": 'any',
                 "action": "pass",
                 "service_list": [self.id() + 's2'],
                 "service_kwargs": {},
                 "auto_policy": False
                 }
        rules.append(rule1)
        rules.append(rule2)

        np = self.create_network_policy_with_multiple_rules(rules)
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)
        vn1_obj.add_network_policy(np, vnp)
        vn2_obj.add_network_policy(np, vnp)
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
            self.check_default_ri_rtgt_imported(self.get_ri_name(vn1_obj),
                                    self.get_ri_name(vn1_obj, sc_ri_name))
            self.check_ri_ref_not_present(self.get_ri_name(vn2_obj),
                                          self.get_ri_name(vn2_obj, sc_ri_name))
            self.check_default_ri_rtgt_imported(self.get_ri_name(vn2_obj),
                                    self.get_ri_name(vn2_obj, sc_ri_name))
            self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                      self.get_ri_name(vn2_obj))


            sci = ServiceChainInfo(prefix=['1.1.1.0/24', '1.1.2.0/24'],
                                   routing_instance = ':'.join(self.get_ri_name(vn1_obj)),
                                   service_instance = si_name)
            service_chain_address = '1.1.1.%s' % (253-i)
            sci.service_chain_address = service_chain_address
            self.check_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)
            sci.prefix = ['1000::/16']
            if i == 1:
                sci.service_chain_address = '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffc'
            else:
                sci.service_chain_address = '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffb'

            self.check_v6_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)
            sci = ServiceChainInfo(prefix = ['20.0.0.0/24'],
                                   routing_instance = ':'.join(self.get_ri_name(vn2_obj)),
                                   service_chain_address=service_chain_address,
                                   service_instance=si_name)
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
            link_name = ':'.join(self.get_ri_name(vn1_obj, sc_ri_name))
            self.assertTill(self.ifmap_ident_has_link, obj=rp,
                            link_name=link_name)
            rp.del_service_instance(si_obj)
            self._vnc_lib.routing_policy_update(rp)
            self.assertTill(self.ifmap_ident_doesnt_have_link, obj=rp,
                            link_name=link_name)
            self._vnc_lib.routing_policy_delete(id=rp.uuid)

            rlist = RouteListType(route=['100.0.0.0/24'])
            ra = RouteAggregate('ra1', aggregate_route_entries=rlist)

            sit = ServiceInterfaceTag(interface_type='left')
            ra.add_service_instance(si_obj, sit)
            self._vnc_lib.route_aggregate_create(ra)
            self.wait_to_get_object(config_db.RouteAggregateST,
                                    ra.get_fq_name_str())
            ra = self._vnc_lib.route_aggregate_read(id=ra.uuid)
            self.assertEqual(ra.get_aggregate_route_nexthop(), service_chain_address)

            link_name = ':'.join(self.get_ri_name(vn1_obj, sc_ri_name))
            self.assertTill(self.ifmap_ident_has_link, obj=ra,
                            link_name=link_name)
            ra.del_service_instance(si_obj)
            self._vnc_lib.route_aggregate_update(ra)
            self.assertTill(self.ifmap_ident_doesnt_have_link, obj=ra,
                            link_name=link_name)
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
        self.check_vn_is_deleted(uuid=vn2_obj.uuid)
        self.check_ri_is_deleted(fq_name=self.get_ri_name(vn2_obj))
    # end test_multi_rule_service_chain

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

        # check if all ri's  are deleted
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
    # end test_st_restart_service_chain

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
            self.assertTill(self.ifmap_has_ident, obj=obj)

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
        self.delete_network_policy(np)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self._vnc_lib.virtual_network_delete(id=vn2_obj.uuid)
        self.check_ri_is_deleted(fq_name=vn1_obj.fq_name+[vn1_obj.name])
        self.check_ri_is_deleted(fq_name=vn2_obj.fq_name+[vn2_obj.name])
        #self.check_ri_is_deleted(fq_name=vn2_obj.fq_name+[vn2_obj.name])

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
    # end test_service_and_analyzer_policy

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

        vmi_fq_name = 'default-domain:default-project:default-domain__default-project__%s.test_fips1__1__left__1' % self._class_str()
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
        self.assertTill(self.ifmap_ident_has_link, obj=fip_obj,
                        link_name=vmi_fq_name)

        fip = fip_obj.get_floating_ip_address()
        self.check_vrf_assign_table(vmi.get_fq_name(), fip, True)

        fip_fq_name = fip_obj.get_fq_name()
        self._vnc_lib.floating_ip_delete(fip_fq_name)
        self.assertTill(self.ifmap_ident_doesnt_have_link, obj=fip_obj,
                        link_name=vmi_fq_name)
        self.check_vrf_assign_table(vmi.get_fq_name(), fip, False)

        self.delete_network_policy(np)
        gevent.sleep(1)
        self._vnc_lib.floating_ip_pool_delete(id=fip_pool.uuid)
        self._vnc_lib.virtual_network_delete(id=vn1_obj.uuid)
        self._vnc_lib.virtual_network_delete(id=vn2_obj.uuid)
        self._vnc_lib.virtual_network_delete(id=vn3_obj.uuid)
        self.check_ri_is_deleted(vn1_obj.fq_name+[vn1_obj.name])
        self.check_ri_is_deleted(vn2_obj.fq_name+[vn2_obj.name])
        self.check_ri_is_deleted(vn3_obj.fq_name+[vn3_obj.name])

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
        self.skipTest("Skipping test_interface_mirror due to a new dependency "
                      "tracker error appeared since we use the VNC ifmap "
                      "server instead of irond.")
        # create  vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        service_name = self.id() + 's1'
        si_fq_name_str = self._create_service(
            [('left', vn1_obj)], service_name, False,
            service_mode='transparent', service_type='analyzer')

        self.assertTill(self.ifmap_has_ident, obj=vn1_obj)

        # create virtual machine interface with interface mirror property
        vmi_name = self.id() + 'vmi1'
        vmi_fq_name = ['default-domain', 'default-project', vmi_name]
        vmi = VirtualMachineInterface(vmi_name, parent_type='project',
                                      fq_name=vmi_fq_name)
        vmi.add_virtual_network(vn1_obj)
        props = VirtualMachineInterfacePropertiesType()
        mirror_type = InterfaceMirrorType()
        mirror_act_type = MirrorActionType()
        mirror_act_type.analyzer_name = 'default-domain:default-project:%s.test_interface_mirrors1' % self._class_str()
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

        self.delete_service(si_fq_name_str)
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

        vn1_st = config_db.VirtualNetworkST.get(vn1_obj.get_fq_name_str())
        rt_vn1 = vn1_st.get_route_target()
        #vn1 rt is in not sc ri
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            rt_vn1, False)

        #set transit and check vn1 rt is in sc ri
        vn_props = VirtualNetworkType()
        vn_props.allow_transit = True
        vn1_obj.set_virtual_network_properties(vn_props)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj, sc_ri_name),
                            rt_vn1, True, 'export')

        #unset transit and check vn1 rt is not in sc ri
        vn_props.allow_transit = False
        vn1_obj.set_virtual_network_properties(vn_props)
        self._vnc_lib.virtual_network_update(vn1_obj)

        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            rt_vn1, False)

        #set transit on both vn1, vn2 and check vn1 & vn2 rt's are in sc ri
        vn_props.allow_transit = True
        vn1_obj.set_virtual_network_properties(vn_props)
        vn2_obj.set_virtual_network_properties(vn_props)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        vn2_st = config_db.VirtualNetworkST.get(vn2_obj.get_fq_name_str())
        rt_vn2 = vn2_st.get_route_target()
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            rt_vn1, True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn2_obj,sc_ri_name),
                            rt_vn2, True, 'export')

        #unset transit on both vn1, vn2 and check vn1 & vn2 rt's are not in sc ri
        vn_props.allow_transit = False
        vn1_obj.set_virtual_network_properties(vn_props)
        vn2_obj.set_virtual_network_properties(vn_props)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)

        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            rt_vn1, False)
        self.check_rt_in_ri(self.get_ri_name(vn2_obj,sc_ri_name),
                            rt_vn2, False)

        #test external rt
        rtgt_list = RouteTargetList(route_target=['target:1:1'])
        vn1_obj.set_route_target_list(rtgt_list)
        rtgt_list = RouteTargetList(route_target=['target:2:1'])
        vn1_obj.set_export_route_target_list(rtgt_list)
        vn_props.allow_transit = True
        vn1_obj.set_virtual_network_properties(vn_props)
        self._vnc_lib.virtual_network_update(vn1_obj)

        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            rt_vn1, True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:1:1', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:2:1', False)

        #modify external rt
        rtgt_list = RouteTargetList(route_target=['target:1:2'])
        vn1_obj.set_route_target_list(rtgt_list)
        rtgt_list = RouteTargetList(route_target=['target:2:2'])
        vn1_obj.set_export_route_target_list(rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            rt_vn1, True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:1:2', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:2:2', False)

        #have more than one external rt
        rtgt_list = RouteTargetList(route_target=['target:1:1', 'target:1:2'])
        vn1_obj.set_route_target_list(rtgt_list)
        rtgt_list = RouteTargetList(route_target=['target:2:1', 'target:2:2'])
        vn1_obj.set_export_route_target_list(rtgt_list)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            rt_vn1, True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:1:1', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:1:2', True, 'export')
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:2:1', False)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            'target:2:2', False)

        #unset external rt
        vn1_obj.set_route_target_list(RouteTargetList())
        vn1_obj.set_export_route_target_list(RouteTargetList())
        self._vnc_lib.virtual_network_update(vn1_obj)
        self.check_rt_in_ri(self.get_ri_name(vn1_obj,sc_ri_name),
                            rt_vn1, True, 'export')
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
        self.check_ri_is_deleted(vn1_obj.fq_name+[vn1_obj.name])
        self.check_ri_is_deleted(vn2_obj.fq_name+[vn2_obj.name])
        self.delete_network_policy(np)
    # end test_transit_vn

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

        self.wait_to_get_sc(check_create=True)

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

        self.delete_network_policy(np)
    #end test_misc

    def assign_vn_subnet(self, vn_obj, subnet_list):
        subnet_info = []
        ipam_fq_name = [
            'default-domain', 'default-project', 'default-network-ipam']
        for subnet in subnet_list:
            cidr = IPNetwork(subnet)
            subnet_info.append(IpamSubnetType(
                                   subnet = SubnetType(
                                       str(cidr.network),
                                       int(cidr.prefixlen),
                                   ),
                                   default_gateway = str(IPAddress(cidr.last - 1))
                               )
                           )
        subnet_data = VnSubnetsType(subnet_info)
        vn_obj.set_network_ipam_list([ipam_fq_name], [subnet_data])
        self._vnc_lib.virtual_network_update(vn_obj)
        vn_obj.clear_pending_updates()

    def test_service_policy_with_v4_v6_subnets(self):

        # If the SC chain info changes after the SI is created
        # (for example, IP address assignment) then the
        # RI needs to be updated with the new info.

        #create vn1
        vn1_name = self.id() + 'vn1'
        vn1_obj = self.create_virtual_network(vn1_name, '10.0.0.0/24')

        #create vn2
        vn2_name = self.id() + 'vn2'
        vn2_obj = self.create_virtual_network(vn2_name, '20.0.0.0/24')

        # Create SC
        service_name = self.id() + 's1'
        np = self.create_network_policy(vn1_obj, vn2_obj, 
                                        service_list = [service_name], 
                                        version = 2,
                                        service_mode = 'in-network')
        seq = SequenceType(1, 1)
        vnp = VirtualNetworkPolicyType(seq)

        vn1_obj.set_network_policy(np, vnp)
        vn2_obj.set_network_policy(np, vnp)
        self._vnc_lib.virtual_network_update(vn1_obj)
        self._vnc_lib.virtual_network_update(vn2_obj)
        sc = self.wait_to_get_sc()

        # Assign prefix after the SC is created
        self.assign_vn_subnet(vn1_obj, ['10.0.0.0/24', '1000::/16'])
        self.assign_vn_subnet(vn2_obj, ['20.0.0.0/24', '2000::/16'])

        sc_ri_name = 'service-'+sc+'-default-domain_default-project_' + service_name
        self.check_ri_ref_present(self.get_ri_name(vn1_obj),
                                  self.get_ri_name(vn1_obj, sc_ri_name))
        self.check_ri_ref_present(self.get_ri_name(vn2_obj, sc_ri_name),
                                  self.get_ri_name(vn2_obj))

        # Checking the Service chain address in the service RI
        sci = ServiceChainInfo(prefix = ['10.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn1_obj)),
                               service_chain_address = '20.0.0.252',
                               service_instance = 'default-domain:default-project:' + service_name,
                               source_routing_instance = ':'.join(self.get_ri_name(vn2_obj)))
        self.check_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)
        sci.prefix = ['1000::/16']
        sci.service_chain_address = '2000:ffff:ffff:ffff:ffff:ffff:ffff:fffc' 
        self.check_v6_service_chain_info(self.get_ri_name(vn2_obj, sc_ri_name), sci)

        sci = ServiceChainInfo(prefix = ['20.0.0.0/24'],
                               routing_instance = ':'.join(self.get_ri_name(vn2_obj)),
                               service_chain_address = '10.0.0.252',
                               service_instance = 'default-domain:default-project:' + service_name,
                               source_routing_instance = ':'.join(self.get_ri_name(vn1_obj)))
        self.check_service_chain_info(self.get_ri_name(vn1_obj, sc_ri_name), sci)
        sci.prefix = ['2000::/16']
        sci.service_chain_address = '1000:ffff:ffff:ffff:ffff:ffff:ffff:fffc'
        self.check_v6_service_chain_info(self.get_ri_name(vn1_obj, sc_ri_name), sci)

        left_ri_fq_name = ['default-domain', 'default-project', vn1_name, sc_ri_name]
        right_ri_fq_name = ['default-domain', 'default-project', vn2_name, sc_ri_name]
        self.check_service_chain_prefix_match(left_ri_fq_name, prefix='2000::/16')
        self.check_service_chain_prefix_match(left_ri_fq_name, prefix='20.0.0.0/24')
        self.check_service_chain_prefix_match(right_ri_fq_name, prefix='1000::/16')
        self.check_service_chain_prefix_match(right_ri_fq_name, prefix='10.0.0.0/24')

        vn2_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn2_obj)
        vn1_obj.del_network_policy(np)
        self._vnc_lib.virtual_network_update(vn1_obj)

        self.delete_network_policy(np)
        self.delete_vn(fq_name=vn1_obj.get_fq_name())
        self.delete_vn(fq_name=vn2_obj.get_fq_name())
    #end test_service_policy_with_v4_v6_subnets

# end class TestServicePolicy
