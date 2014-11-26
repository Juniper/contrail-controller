#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of inetconf interface for physical router
configuration manager
"""

from lxml import etree
from ncclient import manager
import copy


class PhysicalRouterConfig(object):
    # mapping from contrail family names to junos
    _FAMILY_MAP = {
        'route-target': '<route-target/>',
        'inet-vpn': '<inet-vpn><unicast/></inet-vpn>',
        'inet6-vpn': '<inet6-vpn><unicast/></inet6-vpn>',
        'e-vpn': '<evpn><signaling/></evpn>'
    }

    def __init__(self, management_ip, user_creds, logger=None):
        self.management_ip = management_ip
        self.user_creds = user_creds
        self.reset_bgp_config()
        self._logger = logger
    # end __init__

    def send_netconf(self, new_config, default_operation="merge",
                     operation=None):
        try:
            with manager.connect(host=self.management_ip, port=22,
                                 username=self.user_creds['username'],
                                 password=self.user_creds['password'],
                                 unknown_host_cb=lambda x, y: True) as m:
                add_config = etree.Element(
                    "config",
                    nsmap={"xc": "urn:ietf:params:xml:ns:netconf:base:1.0"})
                config = etree.SubElement(add_config, "configuration")
                config_group = etree.SubElement(config, "groups")
                if operation:
                    contrail_group = etree.SubElement(config_group, "name",
                                                      operation=operation)
                else:
                    contrail_group = etree.SubElement(config_group, "name")
                contrail_group.text = "__contrail__"
                if isinstance(new_config, list):
                    for nc in new_config:
                        config_group.append(nc)
                else:
                    config_group.append(new_config)
                if operation != 'delete':
                    apply_groups = etree.SubElement(config, "apply-groups")
                    apply_groups.text = "__contrail__"
                m.edit_config(
                    target='candidate', config=etree.tostring(add_config),
                    test_option='test-then-set',
                    default_operation=default_operation)
                m.commit()
        except Exception as e:
            if self._logger:
                self._logger.error("Router %s: %s" % (self.management_ip,
                                                      e.message))
    # end send_config

    def add_routing_instance(self, name, import_targets, export_targets,
                             prefixes=[], interfaces=[]):
        self.routing_instances[name] = {'import_targets': import_targets,
                                        'export_targets': export_targets,
                                        'prefixes': prefixes,
                                        'interfaces': interfaces}

        ri_config = self.ri_config or etree.Element("routing-instances")
        policy_config = self.policy_config or etree.Element("policy-options")
        ri = etree.SubElement(ri_config, "instance", operation="replace")
        ri_name = "__contrail__" + name.replace(':', '_')
        etree.SubElement(ri, "name").text = ri_name
        etree.SubElement(ri, "instance-type").text = "vrf"
        for interface in interfaces:
            if_element = etree.SubElement(ri, "interface")
            etree.SubElement(if_element, "name").text = interface
        etree.SubElement(ri, "vrf-import").text = ri_name + "-import"
        etree.SubElement(ri, "vrf-export").text = ri_name + "-export"
        rt_element = etree.SubElement(ri, "vrf-target")
        etree.SubElement(ri, "vrf-table-label")
        if prefixes:
            ri_opt = etree.SubElement(ri, "routing-options")
            static_config = etree.SubElement(ri_opt, "static")
            for prefix in prefixes:
                route_config = etree.SubElement(static_config, "route")
                etree.SubElement(route_config, "name").text = prefix
                etree.SubElement(route_config, "discard")
            auto_export = "<auto-export><family><inet><unicast/></inet></family></auto-export>"
            ri_opt.append(etree.fromstring(auto_export))

        # add policies for export route targets
        ps = etree.SubElement(policy_config, "policy-statement")
        etree.SubElement(ps, "name").text = ri_name + "-export"
        term = etree.SubElement(ps, "term")
        etree.SubElement(term, "name").text= "t1"
        then = etree.SubElement(term, "then")
        comm = etree.SubElement(then, "community")
        etree.SubElement(comm, "add")
        for route_target in export_targets:
            etree.SubElement(comm, route_target)
        etree.SubElement(then, "accept")

        # add policies for import route targets
        ps = etree.SubElement(policy_config, "policy-statement")
        etree.SubElement(ps, "name").text = ri_name + "-import"
        term = etree.SubElement(ps, "term")
        etree.SubElement(term, "name").text= "t1"
        from_ = etree.SubElement(term, "from")
        for route_target in import_targets:
            target_name = route_target.replace(':', '_')
            etree.SubElement(from_, "community").text = target_name
        then = etree.SubElement(term, "then")
        etree.SubElement(then, "accept")
        then = etree.SubElement(ps, "then")
        etree.SubElement(then, "reject")

        self.policy_config = policy_config
        self.route_targets |= import_targets | export_targets
        self.ri_config = ri_config
    # end add_routing_instance

    def _add_family_etree(self, parent, params):
        if params.get('address_families') is None:
            return
        family_etree = etree.SubElement(parent, "family")
        for family in params['address_families'].get('family', []):
            if family in self._FAMILY_MAP:
                family_subtree = etree.fromstring(self._FAMILY_MAP[family])
                family_etree.append(family_subtree)
            else:
                etree.SubElement(family_etree, family)
    # end _add_family_etree

    def set_bgp_config(self, params):
        self.bgp_params = params
        if params['vendor'] != "mx" or not params['vnc_managed']:
            if self.bgp_config:
                self.delete_bgp_config()
            return

        self.bgp_config = etree.Element("group", operation="replace")
        etree.SubElement(self.bgp_config, "name").text = "__contrail__"
        etree.SubElement(self.bgp_config, "type").text = "internal"
        etree.SubElement(self.bgp_config, "multihop")
        local_address = etree.SubElement(self.bgp_config, "local-address")
        local_address.text = params['address']
        self._add_family_etree(self.bgp_config, params)
        etree.SubElement(self.bgp_config, "keep").text = "all"
    # end set_bgp_config

    def reset_bgp_config(self):
        self.routing_instances = {}
        self.bgp_config = None
        self.ri_config = None
        self.policy_config = None
        self.route_targets = set()
        self.bgp_peers = {}
    # ene reset_bgp_config

    def delete_bgp_config(self):
        if self.bgp_config is None:
            return
        self.reset_bgp_config()
        self.send_netconf([], default_operation="none", operation="delete")
    # end delete_config

    def add_bgp_peer(self, router, params):
        self.bgp_peers[router] = params
        self.send_bgp_config()
    # end add_peer

    def delete_bgp_peer(self, router):
        if router in self.bgp_peers:
            del self.bgp_peers[router]
            self.send_bgp_config()
    # end delete_bgp_peer

    def send_bgp_config(self):
        if self.bgp_config is None:
            return
        proto_config = etree.Element("protocols")
        group_config = copy.deepcopy(self.bgp_config)
        bgp = etree.SubElement(proto_config, "bgp")
        bgp.append(group_config)
        for peer, params in self.bgp_peers.items():
            nbr = etree.SubElement(group_config, "neighbor")
            etree.SubElement(nbr, "name").text = peer
            bgp_sessions = params.get('session')
            if bgp_sessions:
                # for now assume only one session
                session_attrs = bgp_sessions[0].get('attributes', [])
                for attr in session_attrs:
                    # For not, only consider the attribute if bgp-router is
                    # not specified
                    if attr.get('bgp_router') is None:
                        self._add_family_etree(nbr, attr)
                        break

        routing_options_config = etree.Element("routing-options")
        etree.SubElement(
            routing_options_config,
            "route-distinguisher-id").text = self.bgp_params['identifier']
        config_list = [proto_config, routing_options_config]
        if self.ri_config:
            config_list.append(self.ri_config)
        for route_target in self.route_targets:
            comm = etree.SubElement(self.policy_config, "community")
            etree.SubElement(comm, 'name').text = route_target.replace(':', '_')
            etree.SubElement(comm, 'members').text = route_target
        if self.policy_config:
            config_list.append(self.policy_config)
        self.send_netconf(config_list)
    # end send_bgp_config

# end PhycalRouterConfig
