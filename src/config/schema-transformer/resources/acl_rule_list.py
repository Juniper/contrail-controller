#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

from netaddr import IPNetwork

class AclRuleListST(object):

    def __init__(self, rule_list=None, dynamic=False):
        self._list = rule_list or []
        self.dynamic = dynamic
    # end __init__

    def get_list(self):
        return self._list
    # end get_list

    def append(self, rule):
        if not self._rule_is_subset(rule):
            self._list.append(rule)
            return True
        return False
    # end append

    # for types that have start and end integer
    @staticmethod
    def _port_is_subset(lhs, rhs):
        return (lhs.start_port >= rhs.start_port and
                (rhs.end_port == -1 or lhs.end_port <= rhs.end_port))

    @staticmethod
    def _address_is_subset(lhs, rhs):
        if not(rhs.subnet or lhs.subnet or lhs.subnet_list or rhs.subnet_list):
            return rhs.virtual_network in [lhs.virtual_network, 'any']
        l_subnets = lhs.subnet_list or []
        if lhs.subnet:
            l_subnets.append(lhs.subnet)
        l_subnets = [IPNetwork('%s/%d'%(s.ip_prefix, s.ip_prefix_len))
                     for s in l_subnets]
        r_subnets = rhs.subnet_list or []
        if rhs.subnet:
            r_subnets.append(rhs.subnet)
        r_subnets = [IPNetwork('%s/%d'%(s.ip_prefix, s.ip_prefix_len))
                     for s in r_subnets]
        for l_subnet in l_subnets:
            for r_subnet in r_subnets:
                if l_subnet in r_subnet:
                    return True
        else:
            return False
        return True


    def _rule_is_subset(self, rule):
        for elem in self._list:
            lhs = rule.match_condition
            rhs = elem.match_condition
            if (self._port_is_subset(lhs.src_port, rhs.src_port) and
                self._port_is_subset(lhs.dst_port, rhs.dst_port) and
                rhs.protocol in [lhs.protocol, 'any'] and
                self._address_is_subset(lhs.src_address, rhs.src_address) and
                self._address_is_subset(lhs.dst_address, rhs.dst_address)):

                if not self.dynamic:
                    return True
                if (rule.action_list.mirror_to.analyzer_name ==
                        elem.action_list.mirror_to.analyzer_name):
                    return True
        # end for elem
        return False
    # end _rule_is_subset

    def update_acl_entries(self, acl_entries):
        old_list = AclRuleListST(acl_entries.get_acl_rule(), self.dynamic)
        self._list[:] = [rule for rule in self._list if old_list.append(rule)]
        acl_entries.set_acl_rule(old_list.get_list())
    # end update_acl_entries
# end AclRuleListST