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
import time
import datetime

class PhysicalRouterConfig(object):
    # mapping from contrail family names to junos
    _FAMILY_MAP = {
        'route-target': '<route-target/>',
        'inet-vpn': '<inet-vpn><unicast/></inet-vpn>',
        'inet6-vpn': '<inet6-vpn><unicast/></inet6-vpn>',
        'e-vpn': '<evpn><signaling/></evpn>'
    }

    def __init__(self, management_ip, user_creds, vendor, product, vnc_managed, logger=None):
        self.management_ip = management_ip
        self.user_creds = user_creds
        self.vendor = vendor
        self.product = product
        self.vnc_managed = vnc_managed
        self.reset_bgp_config()
        self._logger = logger
        self.commit_stats = {   
                                'netconf_enabled':False,
                                'netconf_enabled_status':'',
                                'last_commit_time': '',
                                'last_commit_duration': '',
                                'commit_status_message': '',
                                'total_commits_sent_since_up': 0,
                            }
        self.bgp_config_sent = False
    # end __init__

    def update(self, management_ip, user_creds, vendor, product, vnc_managed):
        self.management_ip = management_ip
        self.user_creds = user_creds
        self.vendor = vendor
        self.product = product
        self.vnc_managed = vnc_managed
    # end update

    def get_commit_stats():
        return self.commit_stats
    #end get_commit_stats

    def send_netconf(self, new_config, default_operation="merge",
                     operation="replace"):
        if (self.vendor is None or self.product is None or
               self.vendor.lower() != "juniper" or self.product.lower() != "mx"):
            self._logger.info("auto configuraion of physical router is not supported \
                on the configured vendor family, ip: %s, not pushing netconf message" % (self.management_ip))
            self.commit_stats['netconf_enabled'] = False
            self.commit_stats['netconf_enabled_status'] = "netconf configuraion is not supported on this vendor/product family"
            return
        if (self.vnc_managed is None or self.vnc_managed == False):
            self._logger.info("vnc managed property must be set for a physical router to get auto \
                configured, ip: %s, not pushing netconf message" % (self.management_ip))
            self.commit_stats['netconf_enabled'] = False
            self.commit_stats['netconf_enabled_status'] = "netconf auto configuraion is not enabled on this physical router"
            return
        self.commit_stats['netconf_enabled'] = True
        self.commit_stats['netconf_enabled_status'] = ''
        try:
            with manager.connect(host=self.management_ip, port=22,
                                 username=self.user_creds['username'],
                                 password=self.user_creds['password'],
                                 unknown_host_cb=lambda x, y: True) as m:
                add_config = etree.Element(
                    "config",
                    nsmap={"xc": "urn:ietf:params:xml:ns:netconf:base:1.0"})
                config = etree.SubElement(add_config, "configuration")
                config_group = etree.SubElement(config, "groups", operation=operation)
                contrail_group = etree.SubElement(config_group, "name")
                contrail_group.text = "__contrail__"
                if isinstance(new_config, list):
                    for nc in new_config:
                        config_group.append(nc)
                else:
                    config_group.append(new_config)
                if operation == "delete":
                    apply_groups = etree.SubElement(config, "apply-groups", operation=operation)
                else:
                    apply_groups = etree.SubElement(config, "apply-groups")
                apply_groups.text = "__contrail__"
                self._logger.info("\nsend netconf message: %s\n" % (etree.tostring(add_config, pretty_print=True)))
                m.edit_config(
                    target='candidate', config=etree.tostring(add_config),
                    test_option='test-then-set',
                    default_operation=default_operation)
                self.commit_stats['total_commits_sent_since_up'] += 1
                start_time = time.time()
                m.commit()
                end_time = time.time()
                self.commit_stats['commit_status_message'] = 'success'
                self.commit_stats['last_commit_time'] = datetime.datetime.fromtimestamp(end_time).strftime('%Y-%m-%d %H:%M:%S')
                self.commit_stats['last_commit_duration'] = str(end_time - start_time)
        except Exception as e:
            if self._logger:
                self._logger.error("Router %s: %s" % (self.management_ip,
                                                      e.message))
                self.commit_stats['commit_status_message'] = 'failed to apply config, router response: ' + e.message
                self.commit_stats['last_commit_time'] = datetime.datetime.fromtimestamp(end_time).strftime('%Y-%m-%d %H:%M:%S')
                self.commit_stats['last_commit_duration'] = str(time.time() - start_time)
    # end send_config

    def add_dynamic_tunnels(self, tunnel_source_ip, ip_fabric_nets, bgp_router_ips):
        self.tunnel_config = etree.Element("routing-options")
        dynamic_tunnels = etree.SubElement(self.tunnel_config, "dynamic-tunnels")
        dynamic_tunnel = etree.SubElement(dynamic_tunnels, "dynamic-tunnel")
        etree.SubElement(dynamic_tunnel, "name").text = "__contrail__"
        etree.SubElement(dynamic_tunnel, "source-address").text = tunnel_source_ip
        etree.SubElement(dynamic_tunnel, "gre")
        if ip_fabric_nets is not None:
            for subnet in ip_fabric_nets.get("subnet", []): 
                dest_network = etree.SubElement(dynamic_tunnel, "destination-networks")
                etree.SubElement(dest_network, "name").text = subnet['ip_prefix'] + '/' + str(subnet['ip_prefix_len']) 
        for bgp_router_ip in bgp_router_ips:
            dest_network = etree.SubElement(dynamic_tunnel, "destination-networks")
            etree.SubElement(dest_network, "name").text = bgp_router_ip + '/32'
    #end add_dynamic_tunnels

    def add_routing_instance(self, ri_name, import_targets, export_targets,
                             prefixes=[], gateways=[], router_external=False, interfaces=[], vni=None, fip_map=None):
        self.routing_instances[ri_name] = {'import_targets': import_targets,
                                        'export_targets': export_targets,
                                        'prefixes': prefixes,
                                        'gateways': gateways,
                                        'router_external': router_external,
                                        'interfaces': interfaces,
                                        'vni': vni,
                                        'fip_map': fip_map}

        ri_config = self.ri_config or etree.Element("routing-instances")
        policy_config = self.policy_config or etree.Element("policy-options")
        ri = etree.SubElement(ri_config, "instance", operation="replace")
        etree.SubElement(ri, "name").text = ri_name
        if vni is not None:
            etree.SubElement(ri, "instance-type").text = "virtual-switch"
        else:
            etree.SubElement(ri, "instance-type").text = "vrf"
        if vni is None:
            for interface in interfaces:
                if_element = etree.SubElement(ri, "interface")
                etree.SubElement(if_element, "name").text = interface
        etree.SubElement(ri, "vrf-import").text = ri_name + "-import"
        etree.SubElement(ri, "vrf-export").text = ri_name + "-export"
        if vni is None:
            etree.SubElement(ri, "vrf-table-label")
        ri_opt = None
        if prefixes and vni is None:
            ri_opt = etree.SubElement(ri, "routing-options")
            static_config = etree.SubElement(ri_opt, "static")
            for prefix in prefixes:
                route_config = etree.SubElement(static_config, "route")
                etree.SubElement(route_config, "name").text = prefix
                etree.SubElement(route_config, "discard")
            auto_export = "<auto-export><family><inet><unicast/></inet></family></auto-export>"
            ri_opt.append(etree.fromstring(auto_export))

        if router_external and vni is None:
            if ri_opt is None:
                ri_opt = etree.SubElement(ri, "routing-options")
                static_config = etree.SubElement(ri_opt, "static")
            route_config = etree.SubElement(static_config, "route")
            etree.SubElement(route_config, "name").text = "0.0.0.0/0"
            etree.SubElement(route_config, "next-table").text = "inet.0"

        if fip_map is not None:
            if ri_opt is None:
                ri_opt = etree.SubElement(ri, "routing-options")
                static_config = etree.SubElement(ri_opt, "static")
            route_config = etree.SubElement(static_config, "route")
            etree.SubElement(route_config, "name").text = "0.0.0.0/0"
            etree.SubElement(route_config, "next-hop").text = interfaces[0]
            public_vrf_ips = {}
            for pip in fip_map.values():
                if pip["vrf_name"] not in public_vrf_ips:
                    public_vrf_ips[pip["vrf_name"]] = set()
                public_vrf_ips[pip["vrf_name"]].add(pip["floating_ip"])

            for public_vrf, fips in public_vrf_ips.items():
                ri_public = etree.SubElement(ri_config, "instance")
                etree.SubElement(ri_public, "name").text = public_vrf
                ri_opt = etree.SubElement(ri_public, "routing-options")
                static_config = etree.SubElement(ri_opt, "static")

                for fip in fips:
                    route_config = etree.SubElement(static_config, "route")
                    etree.SubElement(route_config, "name").text = fip + "/32"
                    etree.SubElement(route_config, "next-hop").text = interfaces[1]

        # add policies for export route targets
        ps = etree.SubElement(policy_config, "policy-statement")
        etree.SubElement(ps, "name").text = ri_name + "-export"
        term = etree.SubElement(ps, "term")
        etree.SubElement(term, "name").text= "t1"
        then = etree.SubElement(term, "then")
        for route_target in export_targets:
            comm = etree.SubElement(then, "community")
            etree.SubElement(comm, "add")
            etree.SubElement(comm, "community-name").text = route_target.replace(':', '_') 
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

        # add firewall config for public VRF
        forwarding_options_config = self.forwarding_options_config
        firewall_config = self.firewall_config
        if router_external:
            forwarding_options_config = self.forwarding_options_config or etree.Element("forwarding-options")
            fo = etree.SubElement(forwarding_options_config, "family")
            inet  = etree.SubElement(fo, "inet")
            f = etree.SubElement(inet, "filter")
            #mx has limitation for filter names, allowed max 63 chars
            etree.SubElement(f, "input").text = "redirect_to_" + ri_name[:46] + "_vrf"

            firewall_config = self.firewall_config or etree.Element("firewall")
            f = etree.SubElement(firewall_config, "filter")
            etree.SubElement(f, "name").text = "redirect_to_" + ri_name[:46] + "_vrf"
            term = etree.SubElement(f, "term")
            etree.SubElement(term, "name").text= "t1"
            from_ = etree.SubElement(term, "from")
            if prefixes:
                etree.SubElement(from_, "destination-address").text = ';'.join(prefixes)
            then_ = etree.SubElement(term, "then")
            etree.SubElement(then_, "routing-instance").text = ri_name
            term = etree.SubElement(f, "term")
            etree.SubElement(term, "name").text= "t2"
            then_ = etree.SubElement(term, "then")
            etree.SubElement(then_, "accept")

        # add L2 EVPN and BD config
        bd_config = None
        interfaces_config = self.interfaces_config
        proto_config = self.proto_config
        if vni is not None and self.is_family_configured(self.bgp_params, "e-vpn"):
            etree.SubElement(ri, "vtep-source-interface").text = "lo0.0"
            rt_element = etree.SubElement(ri, "vrf-target")
            #fix me, check if this is correct target value for vrf-target
            for route_target in import_targets:
                etree.SubElement(rt_element, "community").text = route_target
            bd_config = etree.SubElement(ri, "bridge-domains")
            bd= etree.SubElement(bd_config, "domain")
            etree.SubElement(bd, "name").text = "bd-" + str(vni)
            etree.SubElement(bd, "vlan-id").text = str(vni)
            vxlan = etree.SubElement(bd, "vxlan")
            etree.SubElement(vxlan, "vni").text = str(vni)
            etree.SubElement(vxlan, "ingress-node-replication")
            for interface in interfaces:
                if_element = etree.SubElement(bd, "interface")
                etree.SubElement(if_element, "name").text = interface
            etree.SubElement(bd, "routing-interface").text = "irb." + str(vni) #vni is unique, hence irb
            evpn_proto_config = etree.SubElement(ri, "protocols")
            evpn = etree.SubElement(evpn_proto_config, "evpn")
            etree.SubElement(evpn, "encapsulation").text = "vxlan"
            etree.SubElement(evpn, "extended-vni-list").text = "all"

            interfaces_config = self.interfaces_config or etree.Element("interfaces")
            irb_intf = etree.SubElement(interfaces_config, "interface")
            etree.SubElement(irb_intf, "name").text = "irb"
            etree.SubElement(irb_intf, "gratuitous-arp-reply")
            if gateways is not None:
                intf_unit = etree.SubElement(irb_intf, "unit")
                etree.SubElement(intf_unit, "name").text = str(vni)
                family = etree.SubElement(intf_unit, "family")
                inet = etree.SubElement(family, "inet")
                for gateway in gateways:
                    addr = etree.SubElement(inet, "address")
                    etree.SubElement(addr, "name").text = gateway + "/24"

            lo_intf = etree.SubElement(interfaces_config, "interface")
            etree.SubElement(lo_intf, "name").text = "lo0"
            intf_unit = etree.SubElement(lo_intf, "unit")
            etree.SubElement(intf_unit, "name").text = "0"
            family = etree.SubElement(intf_unit, "family")
            inet = etree.SubElement(family, "inet")
            addr = etree.SubElement(inet, "address")
            etree.SubElement(addr, "name").text = self.bgp_params['address'] + "/32"
            etree.SubElement(addr, "primary")
            etree.SubElement(addr, "preferred")

            for interface in interfaces:
                intf = etree.SubElement(interfaces_config, "interface")
                intfparts = interface.split(".")
                etree.SubElement(intf, "name").text = intfparts[0] 
                etree.SubElement(intf, "encapsulation").text = "ethernet-bridge" 
                intf_unit = etree.SubElement(intf, "unit")
                etree.SubElement(intf_unit, "name").text = intfparts[1]
                family = etree.SubElement(intf_unit, "family")
                etree.SubElement(family, "bridge")

            proto_config = self.proto_config or etree.Element("protocols")
            mpls = etree.SubElement(proto_config, "mpls")
            intf = etree.SubElement(mpls, "interface")
            etree.SubElement(intf, "name").text = "all"
        #fip services config
        services_config = self.services_config
        if fip_map is not None:
            services_config = self.services_config or etree.Element("services")
            service_name = 'sv-' + ri_name
            #mx has limitation for service-set and nat-rule name length, allowed max 63 chars
            service_name = service_name[:56]
            service_set = etree.SubElement(services_config, "service-set")
            etree.SubElement(service_set, "name").text = service_name
            rule_count = len(fip_map)
            for rule_id in range(0, rule_count):
                nat_rule = etree.SubElement(service_set, "nat-rules")
                etree.SubElement(nat_rule, "name").text = service_name + "-sn-" + str(rule_id)
                nat_rule = etree.SubElement(service_set, "nat-rules")
                etree.SubElement(nat_rule, "name").text = service_name + "-dn-" + str(rule_id)
            next_hop_service = etree.SubElement(service_set, "next-hop-service")
            etree.SubElement(next_hop_service , "inside-service-interface").text = interfaces[0]
            etree.SubElement(next_hop_service , "outside-service-interface").text = interfaces[1]

            nat = etree.SubElement(services_config, "nat")

            rule_id = 0
            for pip, fip_vn in fip_map.items():
                fip = fip_vn["floating_ip"]
                rule = etree.SubElement(nat, "rule")
                etree.SubElement(rule, "name").text = service_name + "-sn-" + str(rule_id)
                etree.SubElement(rule, "match-direction").text = "input"
                term = etree.SubElement(rule, "term")
                etree.SubElement(term, "name").text = "t1"
                from_ = etree.SubElement(term, "from")
                src_addr = etree.SubElement(from_, "source-address")
                etree.SubElement(src_addr, "name").text = pip + "/32"  # private ip
                then_ = etree.SubElement(term, "then")
                translated = etree.SubElement(then_, "translated")
                etree.SubElement(translated , "source-prefix").text = fip + "/32" # public ip
                translation_type = etree.SubElement(translated, "translation-type")
                etree.SubElement(translation_type, "basic-nat44")

                rule = etree.SubElement(nat, "rule")
                etree.SubElement(rule, "name").text = service_name + "-dn-" + str(rule_id)
                etree.SubElement(rule, "match-direction").text = "output"
                term = etree.SubElement(rule, "term")
                etree.SubElement(term, "name").text = "t1"
                from_ = etree.SubElement(term, "from")
                src_addr = etree.SubElement(from_, "destination-address")
                etree.SubElement(src_addr, "name").text = fip + "/32" #public ip
                then_ = etree.SubElement(term, "then")
                translated = etree.SubElement(then_, "translated")
                etree.SubElement(translated , "destination-prefix").text = pip + "/32" #source ip
                translation_type = etree.SubElement(translated, "translation-type")
                etree.SubElement(translation_type, "dnat-44")
                rule_id = rule_id + 1

            interfaces_config = self.interfaces_config or etree.Element("interfaces")
            si_intf = etree.SubElement(interfaces_config, "interface")
            intfparts = interfaces[0].split(".")
            etree.SubElement(si_intf, "name").text = intfparts[0]
            intf_unit = etree.SubElement(si_intf, "unit")
            etree.SubElement(intf_unit, "name").text = interfaces[0].split(".")[1]
            family = etree.SubElement(intf_unit, "family")
            etree.SubElement(family, "inet")
            etree.SubElement(intf_unit, "service-domain").text = "inside"
            intf_unit = etree.SubElement(si_intf, "unit")
            etree.SubElement(intf_unit, "name").text = interfaces[1].split(".")[1]
            family = etree.SubElement(intf_unit, "family")
            etree.SubElement(family, "inet")
            etree.SubElement(intf_unit, "service-domain").text = "outside"

        self.forwarding_options_config = forwarding_options_config
        self.firewall_config = firewall_config
        self.policy_config = policy_config
        self.proto_config = proto_config
        self.interfaces_config = interfaces_config
        self.services_config = services_config
        self.route_targets |= import_targets | export_targets
        self.ri_config = ri_config
    # end add_routing_instance

    def is_family_configured(self, params, family_name):
        if params is None or params.get('address_families') is None:
            return False
        families = params['address_families'].get('family', [])
        if family_name in families:
            return True
        return False

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

    def add_bgp_auth_config(self, bgp_config, bgp_params):
        if bgp_params.get('auth_data') is None:
            return
        keys = bgp_params['auth_data'].get('key_items', [])
        if len(keys) > 0:
            etree.SubElement(bgp_config, "authentication-key").text = keys[0].get('key')

    def add_bgp_hold_time_config(self, bgp_config, bgp_params):
        if bgp_params.get('hold_time') is None:
            return
        etree.SubElement(bgp_config, "hold-time").text = str(bgp_params.get('hold_time'))

    def set_bgp_config(self, params):
        self.bgp_params = params
        if (self.vnc_managed is None or self.vnc_managed == False):
            if self.bgp_config_sent:
                # user must have unset the vnc managed property, so temporaly set it
                # for deleting the existing config
                self.vnc_managed = True
                self.delete_bgp_config()
                self.vnc_managed = False
                return
            return
    # end set_bgp_config

    def _get_bgp_config_xml(self, external=False):
        if self.bgp_params is None:
            return None
        bgp_config = etree.Element("group", operation="replace")
        if external:
            etree.SubElement(bgp_config, "name").text = "__contrail_external__"
            etree.SubElement(bgp_config, "type").text = "external"
        else:
            etree.SubElement(bgp_config, "name").text = "__contrail__"
            etree.SubElement(bgp_config, "type").text = "internal"
        etree.SubElement(bgp_config, "multihop")
        local_address = etree.SubElement(bgp_config, "local-address")
        local_address.text = self.bgp_params['address']
        self._add_family_etree(bgp_config, self.bgp_params)
        self.add_bgp_auth_config(bgp_config, self.bgp_params)
        self.add_bgp_hold_time_config(bgp_config, self.bgp_params)
        etree.SubElement(bgp_config, "keep").text = "all"
        return bgp_config
    # end _get_bgp_config_xml

    def reset_bgp_config(self):
        self.routing_instances = {}
        self.bgp_params = None
        self.ri_config = None
        self.tunnel_config = None
        self.interfaces_config = None
        self.services_config = None
        self.policy_config = None
        self.firewall_config = None
        self.forwarding_options_config = None
        self.proto_config = None
        self.route_targets = set()
        self.bgp_peers = {}
        self.external_peers = {}
    # ene reset_bgp_config

    def delete_bgp_config(self):
        if not self.bgp_config_sent:
            return
        self.reset_bgp_config()
        self.send_netconf([], default_operation="none", operation="delete")
        self.bgp_config_sent = False
    # end delete_config

    def add_bgp_peer(self, router, params, external):
        if external:
            self.external_peers[router] = params
        else:
            self.bgp_peers[router] = params
        self.send_bgp_config()
    # end add_peer

    def delete_bgp_peer(self, router):
        if router in self.bgp_peers:
            del self.bgp_peers[router]
        elif router in self.external_peers:
            del self.external_peers[rotuer]
        else:
            return
        self.send_bgp_config()
    # end delete_bgp_peer

    def _get_neighbor_config_xml(self, bgp_config, peers):
        for peer, params in peers.items():
            nbr = etree.SubElement(bgp_config, "neighbor")
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
                        self.add_bgp_auth_config(nbr, attr)
                        break
            if params.get('autonomous_system') is not None:
                etree.SubElement(nbr, "peer-as").text = str(params.get('autonomous_system'))
    # end _get_neighbor_config_xml

    def send_bgp_config(self):
        bgp_config = self._get_bgp_config_xml()
        if bgp_config is None:
            return
        proto_config = etree.Element("protocols")
        bgp = etree.SubElement(proto_config, "bgp")
        bgp.append(bgp_config)
        self._get_neighbor_config_xml(bgp_config, self.bgp_peers)
        if self.external_peers is not None:
            ext_grp_config = self._get_bgp_config_xml(True)
            bgp.append(ext_grp_config)
            self._get_neighbor_config_xml(ext_grp_config, self.external_peers)

        routing_options_config = etree.Element("routing-options")
        etree.SubElement(
            routing_options_config,
            "route-distinguisher-id").text = self.bgp_params['identifier']
        etree.SubElement(routing_options_config, "autonomous-system").text = \
            str(self.bgp_params.get('autonomous_system'))
        config_list = [proto_config, routing_options_config]
        if self.ri_config is not None:
            config_list.append(self.ri_config)
        for route_target in self.route_targets:
            comm = etree.SubElement(self.policy_config, "community")
            etree.SubElement(comm, 'name').text = route_target.replace(':', '_')
            etree.SubElement(comm, 'members').text = route_target
        if self.tunnel_config is not None:
            config_list.append(self.tunnel_config)
        if self.interfaces_config is not None:
            config_list.append(self.interfaces_config)
        if self.services_config is not None:
            config_list.append(self.services_config)
        if self.policy_config is not None:
            config_list.append(self.policy_config)
        if self.firewall_config is not None:
            config_list.append(self.firewall_config)
        if self.forwarding_options_config is not None:
            config_list.append(self.forwarding_options_config)
        if self.proto_config is not None:
            config_list.append(self.proto_config)
        self.send_netconf(config_list)
        self.bgp_config_sent = True
    # end send_bgp_config

# end PhycalRouterConfig
