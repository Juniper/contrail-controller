#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from schema_transformer.resources._resource_base import ResourceBaseST
from vnc_api.gen.resource_xsd import SecurityLoggingObjectRuleListType
from vnc_api.gen.resource_xsd import SecurityLoggingObjectRuleEntryType
from schema_transformer.utils import _create_pprinted_prop_list


class SecurityLoggingObjectST(ResourceBaseST):
    _dict = {}
    obj_type = 'security_logging_object'
    ref_fields = ['network_policy', 'security_group']
    prop_fields = ['security_logging_object_rate']

    def __init__(self, name, obj=None):
        self.name = name
        self.network_policys = {}
        self.security_groups = {}
        self.security_logging_object_rate = None
        self.security_logging_object_rules = set()
        self.update(obj)
        self.uuid = self.obj.uuid
    # end __init__

    def evaluate(self):
        rule_entries = set()
        if self.network_policys:
            rule_entries = self.populate_rules('network_policy',
                                               self.network_policys)
        if self.security_groups:
            rule_entries |= self.populate_rules('security_group',
                                                self.security_groups)
        # Update the rules if it has changed.
        if (rule_entries != self.security_logging_object_rules):
            self.obj.set_security_logging_object_rules(
                        SecurityLoggingObjectRuleListType(list(rule_entries)))
            self._vnc_lib.security_logging_object_update(self.obj)
            self.security_logging_object_rules = rule_entries

    def populate_rules(self, rule_type, refs):
        rule_entries = set()
        for np_sg, attr in refs.items():
            if attr and attr.rule:
                # List of rule UUIDs is provided. Populate
                # the SLO with the list alone.
                np_sg_rule_list = attr.rule
            else:
                # No rule uuid is specified. Query for the
                # network policy or security group
                # and include all rules in them.
                # Use the default rule rate in the SLO.
                np_sg_st_obj = (ResourceBaseST.get_obj_type_map().get('network_policy').get(np_sg) or
                                ResourceBaseST.get_obj_type_map().get('security_group').get(np_sg))
                np_sg_rule_list = getattr(np_sg_st_obj.obj,
                                         'get_'+rule_type+'_entries')(
                                             ).get_policy_rule()
            for rule_entry in np_sg_rule_list:
                rate_to_use = (getattr(rule_entry, 'rate', None) or
                               self.security_logging_object_rate)
                rule_entries.add(SecurityLoggingObjectRuleEntryType(
                               rule_entry.rule_uuid, rate=rate_to_use))
            # end for rule_entry
        # end for np_sg
        return rule_entries
    # end populate_rules

    def handle_st_object_req(self):
        resp = super(SecurityLoggingObjectST, self).handle_st_object_req()
        resp.properties = [
            _create_pprinted_prop_list('rule', rule)
            for rule in self.security_logging_object_rules
        ]
        return resp
# end SecurityLoggingObjectST
