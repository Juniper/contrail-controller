#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

import itertools
from schema_transformer.resources._resource_base import ResourceBaseST
from schema_transformer.sandesh.st_introspect import ttypes as sandesh
from schema_transformer.utils import _create_pprinted_prop_list
from vnc_api.gen.resource_xsd import VirtualNetworkPolicyType


# a struct to store attributes related to Network Policy needed by schema
# transformer
class NetworkPolicyST(ResourceBaseST):
    _dict = {}
    obj_type = 'network_policy'
    prop_fields = ['network_policy_entries']
    _internal_policies = set()
    _service_instances = {}
    _network_policys = {}

    def __init__(self, name, obj=None):
        self.name = name
        self.virtual_networks = set()
        self.service_instances = set()
        self.internal = False
        self.rules = []
        self.has_subnet_only_rules = True
        self.security_logging_objects = set()
        # policies referred in this policy as src or dst
        self.referred_policies = set()
        # policies referring to this policy as src or dst
        self.update(obj)
        for vn_ref in self.obj.get_virtual_network_back_refs() or []:
            vn_name = ':'.join(vn_ref['to'])
            self.virtual_networks.add(vn_name)
            vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(vn_name)
            if vn is None:
                continue
            vnp = VirtualNetworkPolicyType(**vn_ref['attr'])
            vn.add_policy(name, vnp)
        self.network_policys = NetworkPolicyST._network_policys.get(name, set())
        self.update_multiple_refs('security_logging_object', self.obj)
    # end __init__

    def skip_evaluate(self, from_type):
        if from_type == 'virtual_network' and self.has_subnet_only_rules == True:
            return True
        return False
    # end skip_evaluate

    def update_subnet_only_rules(self):
        for rule in self.rules:
            for address in itertools.chain(rule.src_addresses,
                                           rule.dst_addresses):
                if (address.virtual_network not in (None, 'local') or
                    address.network_policy):
                    self.has_subnet_only_rules = False
    # end update_subnet_only_rules

    def set_internal(self):
        self.internal = True
        self._internal_policies.add(self.name)
    # end set_internal

    @classmethod
    def get_internal_policies(cls, vn_name):
        policies = []
        for policy_name in cls._internal_policies:
            policy = NetworkPolicyST.get(policy_name)
            if policy is None:
                continue
            if vn_name in policy.virtual_networks:
                policies.append(policy.name)
        return policies

    @classmethod
    def get_by_service_instance(cls, si_name):
        return cls._service_instances.get(si_name, set())

    def update(self, obj=None):
        ret = self.update_vnc_obj(obj)
        if ret:
            self.add_rules(self.obj.get_network_policy_entries())
        return ret
    # end update

    def add_rules(self, entries):
        if entries is None:
            if not self.rules:
                return False
            self.rules = []
        else:
            if self.rules == entries.policy_rule:
                return False
            self.rules = entries.policy_rule
        np_set = set()
        si_set = set()
        for prule in self.rules:
            if prule.action_list is None:
                continue
            if (prule.action_list.mirror_to and
                prule.action_list.mirror_to.analyzer_name):
                si_set.add(prule.action_list.mirror_to.analyzer_name)
            if prule.action_list.apply_service:
                si_set = si_set.union(prule.action_list.apply_service)
            for addr in prule.src_addresses + prule.dst_addresses:
                if addr.network_policy:
                    np_set.add(addr.network_policy)
        # end for prule

        for policy_name in self.referred_policies - np_set:
            policy_set = self._network_policys.get(policy_name)
            if policy_set is None:
                continue
            policy_set.discard(self.name)
            if not policy_set:
                del self._network_policys[policy_name]
            policy = NetworkPolicyST.get(policy_name)
            if policy:
                policy.network_policys = policy_set
        for policy_name in np_set - self.referred_policies:
            policy_set = self._network_policys.setdefault(policy_name, set())
            policy_set.add(self.name)
            policy = NetworkPolicyST.get(policy_name)
            if policy:
                policy.network_policys = policy_set
        self.referred_policies = np_set
        self.update_service_instances(si_set)
        self.update_subnet_only_rules()
        return True
    # end add_rules

    def update_service_instances(self, si_set):
        old_si_set = self.service_instances
        for si_name in old_si_set - si_set:
            si_list = self._service_instances.get(si_name)
            if si_list:
                si_list.discard(self.name)
            if not si_list:
                del self._service_instances[si_name]
            si = ResourceBaseST.get_obj_type_map().get('service_instance').get(si_name)
            if si is None:
                continue
            si.network_policys.discard(self.name)

        for si_name in si_set - old_si_set:
            si_list = self._service_instances.setdefault(si_name, set())
            si_list.add(self.name)
            si = ResourceBaseST.get_obj_type_map().get('service_instance').get(si_name)
            if si is None:
                continue
            si.network_policys.add(self.name)
        self.service_instances = si_set
    # update_service_instances

    def delete_obj(self):
        self.add_rules(None)
        self._internal_policies.discard(self.name)
        for vn_name in self.virtual_networks:
            vn = ResourceBaseST.get_obj_type_map().get('virtual_network').get(vn_name)
            if vn is None:
                continue
            if self.name in vn.network_policys:
                del vn.network_policys[self.name]
    # end delete_obj

    def handle_st_object_req(self):
        resp = super(NetworkPolicyST, self).handle_st_object_req()
        resp.obj_refs = [
            self._get_sandesh_ref_list('virtual_network'),
            self._get_sandesh_ref_list('service_instance'),
            self._get_sandesh_ref_list('network_policy'),
            sandesh.RefList('referred_policy', self.referred_policies)
        ]
        resp.properties = [
            _create_pprinted_prop_list('rule', rule) for rule in self.rules
        ]
        return resp
    # end handle_st_object_req
# end class NetworkPolicyST
