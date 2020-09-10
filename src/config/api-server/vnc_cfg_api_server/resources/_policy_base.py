#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from builtins import str
import itertools
import uuid

from cfgm_common import protocols
from netaddr import IPNetwork


def check_policy_rules(entries, network_policy_rule=False):
    if not entries:
        return True, ""
    rules = entries.get('policy_rule') or []
    ignore_keys = ['rule_uuid', 'created', 'last_modified']
    rules_no_uuid = [dict((k, v) for k, v in list(r.items())
                          if k not in ignore_keys)
                     for r in rules]
    for index, rule in enumerate(rules_no_uuid):
        rules_no_uuid[index] = None
        if rule in rules_no_uuid:
            try:
                rule_uuid = rules[index]['rule_uuid']
            except KeyError:
                rule_uuid = None
            return (False, (409, 'Rule already exists : %s' % rule_uuid))
    for rule in rules:
        if not rule.get('rule_uuid'):
            rule['rule_uuid'] = str(uuid.uuid4())
        # TODO(sahid): This all check can be factorized in
        # cfgm_common.protocols
        protocol = rule['protocol']
        if protocol.isdigit():
            if int(protocol) < 0 or int(protocol) > 255:
                return (False, (400, 'Rule with invalid protocol : %s' %
                                protocol))
        else:
            protocol = protocol.lower()
            if protocol not in protocols.IP_PROTOCOL_NAMES:
                return (False, (400, 'Rule with invalid protocol : %s' %
                                protocol))
        src_sg_list = [addr.get('security_group') for addr in
                       rule.get('src_addresses') or []]
        dst_sg_list = [addr.get('security_group') for addr in
                       rule.get('dst_addresses') or []]

        if network_policy_rule:
            if rule.get('action_list') is None:
                return (False, (400, 'Action is required'))

            src_sg = [True for sg in src_sg_list if sg is not None]
            dst_sg = [True for sg in dst_sg_list if sg is not None]
            if True in src_sg or True in dst_sg:
                return (False, (400, 'Config Error: policy rule refering to'
                                ' security group is not allowed'))
        else:
            ethertype = rule.get('ethertype')
            if ethertype is not None:
                for addr in itertools.chain(rule.get('src_addresses') or [],
                                            rule.get('dst_addresses') or []):
                    if addr.get('subnet') is not None:
                        ip_prefix = addr["subnet"].get('ip_prefix')
                        ip_prefix_len = addr["subnet"].get('ip_prefix_len')
                        network = IPNetwork("%s/%s" % (ip_prefix,
                                                       ip_prefix_len))
                        if not ethertype == "IPv%s" % network.version:
                            msg = ("Rule subnet %s doesn't match ethertype %s"
                                   % (network, ethertype))
                            return False, (400, msg)

            if ('local' not in src_sg_list and 'local' not in dst_sg_list):
                return (False, (400, "At least one of source or destination"
                                     " addresses must be 'local'"))
    return True, ""
