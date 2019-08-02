#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from schema_transformer.sandesh.st_introspect import ttypes as sandesh
from vnc_api.gen.resource_xsd import AclEntriesType, PortType
from vnc_api.gen.resource_xsd import ActionListType, MatchConditionType
from vnc_api.gen.resource_xsd import AclRuleType, MatchConditionType
import copy
from schema_transformer.resources._access_control_list import _access_control_list_update
from schema_transformer.utils import _PROTO_STR_TO_NUM_IPV6,\
      _PROTO_STR_TO_NUM_IPV4, _create_pprinted_prop_list


# a struct to store attributes related to Security Group needed by schema
# transformer


class SecurityGroupST(ResourceBaseST):
    _dict = {}
    obj_type = 'security_group'
    prop_fields = ['security_group_entries', 'configured_security_group_id']
    _sg_dict = {}

    def __init__(self, name, obj=None, acl_dict=None):
        def _get_acl(uuid):
            if acl_dict:
                return acl_dict[uuid]
            return self.read_vnc_obj(uuid, obj_type='access_control_list')

        self.name = name
        self.obj = obj or self.read_vnc_obj(fq_name=name)
        self.uuid = self.obj.uuid
        self.configured_security_group_id = None
        self.sg_id = None
        self.ingress_acl = None
        self.egress_acl = None
        self.referred_sgs = set()
        self.security_group_entries = None
        self.security_logging_objects = set()
        acls = self.obj.get_access_control_lists()
        for acl in acls or []:
            if acl['to'][-1] == 'egress-access-control-list':
                self.egress_acl = _get_acl(acl['uuid'])
            elif acl['to'][-1] == 'ingress-access-control-list':
                self.ingress_acl = _get_acl(acl['uuid'])
            else:
                self._vnc_lib.access_control_list_delete(id=acl['uuid'])
        self.update(self.obj)
        self.security_groups = SecurityGroupST._sg_dict.get(name, set())
        self.update_multiple_refs('security_logging_object', self.obj)
    # end __init__

    def update(self, obj=None):
        changed = self.update_vnc_obj(obj)
        if changed:
            self.process_referred_sgs()
        return changed
    # end update

    def process_referred_sgs(self):
        if self.security_group_entries:
            prules = self.security_group_entries.get_policy_rule() or []
        else:
            prules = []

        sg_refer_set = set()
        for prule in prules:
            for addr in prule.src_addresses + prule.dst_addresses:
                if addr.security_group:
                    if addr.security_group not in ['local', self.name, 'any']:
                        sg_refer_set.add(addr.security_group)
        # end for prule

        for sg_name in self.referred_sgs - sg_refer_set:
            sg_set = SecurityGroupST._sg_dict.get(sg_name)
            if sg_set is None:
                continue
            sg_set.discard(self.name)
            if not sg_set:
                del self._sg_dict[sg_name]
            sg = SecurityGroupST.get(sg_name)
            if sg:
                sg.security_groups = sg_set

        for sg_name in sg_refer_set - self.referred_sgs:
            sg_set = SecurityGroupST._sg_dict.setdefault(sg_name, set())
            sg_set.add(self.name)
            sg = SecurityGroupST.get(sg_name)
            if sg:
                sg.security_groups = sg_set
        self.referred_sgs = sg_refer_set
    # end process_referred_sgs

    def delete_obj(self):
        if self.ingress_acl:
            self._vnc_lib.access_control_list_delete(id=self.ingress_acl.uuid)
        if self.egress_acl:
            self._vnc_lib.access_control_list_delete(id=self.egress_acl.uuid)
        self.security_group_entries = None
        self.process_referred_sgs()
    # end delete_obj

    def evaluate(self):
        self.update_policy_entries()

    def update_policy_entries(self):
        ingress_acl_entries = AclEntriesType()
        egress_acl_entries = AclEntriesType()

        if self.security_group_entries:
            prules = self.security_group_entries.get_policy_rule() or []
        else:
            prules = []

        for prule in prules:
            (ingress_list, egress_list) = self.policy_to_acl_rule(prule)
            ingress_acl_entries.acl_rule.extend(ingress_list)
            egress_acl_entries.acl_rule.extend(egress_list)
        # end for prule

        self.ingress_acl = _access_control_list_update(
            self.ingress_acl, "ingress-access-control-list", self.obj,
            ingress_acl_entries)
        self.egress_acl = _access_control_list_update(
            self.egress_acl, "egress-access-control-list", self.obj,
            egress_acl_entries)
    # end update_policy_entries

    def _convert_security_group_name_to_id(self, addr):
        if addr.security_group is None:
            return True
        if addr.security_group in ['local', self.name]:
            addr.security_group = str(self.obj.get_security_group_id())
        elif addr.security_group == 'any':
            addr.security_group = '-1'
        elif addr.security_group in self._dict:
            addr.security_group = str(self._dict[
                addr.security_group].obj.get_security_group_id())
        else:
            return False
        return True
    # end _convert_security_group_name_to_id

    @staticmethod
    def protocol_policy_to_acl(pproto, ethertype):
        # convert policy proto input(in str) to acl proto (num)
        if pproto is None:
            return None
        if pproto.isdigit():
            return pproto
        if ethertype == 'IPv6':
            return _PROTO_STR_TO_NUM_IPV6.get(pproto.lower())
        else: # IPv4
            return _PROTO_STR_TO_NUM_IPV4.get(pproto.lower())

    def policy_to_acl_rule(self, prule):
        ingress_acl_rule_list = []
        egress_acl_rule_list = []
        rule_uuid = prule.get_rule_uuid()

        ethertype = prule.ethertype
        arule_proto = self.protocol_policy_to_acl(prule.protocol, ethertype)
        if arule_proto is None:
            # TODO log unknown protocol
            return (ingress_acl_rule_list, egress_acl_rule_list)

        acl_rule_list = None
        for saddr in prule.src_addresses:
            saddr_match = copy.deepcopy(saddr)
            if not self._convert_security_group_name_to_id(saddr_match):
                continue
            if saddr.security_group == 'local':
                saddr_match.security_group = None
                acl_rule_list = egress_acl_rule_list

            # If no src port is specified, assume 0-65535
            for sp in prule.src_ports or [PortType()]:
                for daddr in prule.dst_addresses:
                    daddr_match = copy.deepcopy(daddr)
                    if not self._convert_security_group_name_to_id(daddr_match):
                        continue
                    if daddr.security_group == 'local':
                        daddr_match.security_group = None
                        acl_rule_list = ingress_acl_rule_list
                    if acl_rule_list is None:
                        self._logger.error("SG rule must have either source "
                                           "or destination as 'local': " +
                                           self.name)
                        continue

                    # If no dst port is specified, assume 0-65535
                    for dp in prule.dst_ports or [PortType()]:
                        action = ActionListType(simple_action='pass')
                        match = MatchConditionType(arule_proto,
                                                   saddr_match, sp,
                                                   daddr_match, dp,
                                                   ethertype)
                        acl = AclRuleType(match, action, rule_uuid)
                        acl_rule_list.append(acl)
                    # end for dp
                # end for daddr
            # end for sp
        # end for saddr
        return (ingress_acl_rule_list, egress_acl_rule_list)
    # end policy_to_acl_rule

    def handle_st_object_req(self):
        resp = super(SecurityGroupST, self).handle_st_object_req()
        resp.obj_refs = [
            self._get_sandesh_ref_list('security_group'),
            sandesh.RefList('referred_security_group', self.referred_sgs)
        ]
        resp.properties.extend([
            sandesh.PropList('sg_id', str(self.sg_id)),
        ] + [_create_pprinted_prop_list('rule', rule)
             for rule in self.security_group_entries.get_policy_rule() or []])
        return resp
    # end handle_st_object_req
# end class SecurityGroupST
