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


class PushConfigState(object):
    PUSH_STATE_INIT = 0
    PUSH_STATE_SUCCESS = 1
    PUSH_STATE_RETRY = 2
    REPUSH_INTERVAL = 15
    REPUSH_MAX_INTERVAL = 300
    PUSH_DELAY_PER_KB = 0.01
    PUSH_DELAY_MAX = 100
    PUSH_DELAY_ENABLE = True

    @classmethod
    def set_repush_interval(cls, value):
        cls.REPUSH_INTERVAL = value
    # end set_repush_interval

    @classmethod
    def set_repush_max_interval(cls, value):
        cls.REPUSH_MAX_INTERVAL = value
    # end set_repush_max_interval

    @classmethod
    def set_push_delay_per_kb(cls, value):
        cls.PUSH_DELAY_PER_KB = value
    # end set_push_delay_per_kb

    @classmethod
    def set_push_delay_max(cls, value):
        cls.PUSH_DELAY_MAX = value
    # end set_push_delay_max

    @classmethod
    def set_push_delay_enable(cls, value):
        cls.PUSH_DELAY_ENABLE = value
    # end set_push_delay_enable

    @classmethod
    def get_repush_interval(cls):
        return cls.REPUSH_INTERVAL
    # end set_repush_interval

    @classmethod
    def get_repush_max_interval(cls):
        return cls.REPUSH_MAX_INTERVAL
    # end get_repush_max_interval

    @classmethod
    def get_push_delay_per_kb(cls):
        return cls.PUSH_DELAY_PER_KB
    # end get_push_delay_per_kb

    @classmethod
    def get_push_delay_max(cls):
        return cls.PUSH_DELAY_MAX
    # end get_push_delay_max

    @classmethod
    def get_push_delay_enable(cls):
        return cls.PUSH_DELAY_ENABLE
    # end get_push_delay_enable


# end PushConfigState

class PhysicalRouterConfig(object):
    # mapping from contrail family names to junos
    _FAMILY_MAP = {
        'route-target': '<route-target/>',
        'inet-vpn': '<inet-vpn><unicast/></inet-vpn>',
        'inet6-vpn': '<inet6-vpn><unicast/></inet6-vpn>',
        'e-vpn': '<evpn><signaling/></evpn>'
    }

    def __init__(self, management_ip, user_creds,
                 vendor, product, logger=None):
        self.management_ip = management_ip
        self.user_creds = user_creds
        self.vendor = vendor
        self.product = product
        self.reset_bgp_config()
        self._logger = logger
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        self.commit_stats = {
            'last_commit_time': '',
            'last_commit_duration': '',
            'commit_status_message': '',
            'total_commits_sent_since_up': 0,
        }
    # end __init__

    def update(self, management_ip, user_creds, vendor, product):
        self.management_ip = management_ip
        self.user_creds = user_creds
        self.vendor = vendor
        self.product = product
    # end update

    def get_commit_stats(self):
        return self.commit_stats
    # end get_commit_stats

    def retry(self):
        if self.push_config_state == PushConfigState.PUSH_STATE_RETRY:
            return True
        return False
    # end retry

    def send_netconf(self, new_config, default_operation="merge",
                     operation="replace"):
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        start_time = None
        config_size = 0
        try:
            with manager.connect(host=self.management_ip, port=22,
                                 username=self.user_creds['username'],
                                 password=self.user_creds['password'],
                                 unknown_host_cb=lambda x, y: True) as m:
                add_config = etree.Element(
                    "config",
                    nsmap={"xc": "urn:ietf:params:xml:ns:netconf:base:1.0"})
                config = etree.SubElement(add_config, "configuration")
                config_group = etree.SubElement(
                    config, "groups", operation=operation)
                contrail_group = etree.SubElement(config_group, "name")
                contrail_group.text = "__contrail__"
                if isinstance(new_config, list):
                    for nc in new_config:
                        config_group.append(nc)
                else:
                    config_group.append(new_config)
                if operation == "delete":
                    apply_groups = etree.SubElement(
                        config, "apply-groups", operation=operation)
                else:
                    apply_groups = etree.SubElement(config, "apply-groups")
                apply_groups.text = "__contrail__"
                self._logger.info("\nsend netconf message: %s\n" % (
                    etree.tostring(add_config, pretty_print=True)))
                config_str = etree.tostring(add_config)
                config_size = len(config_str)
                m.edit_config(
                    target='candidate', config=config_str,
                    test_option='test-then-set',
                    default_operation=default_operation)
                self.commit_stats['total_commits_sent_since_up'] += 1
                start_time = time.time()
                m.commit()
                end_time = time.time()
                self.commit_stats['commit_status_message'] = 'success'
                self.commit_stats['last_commit_time'] = \
                    datetime.datetime.fromtimestamp(
                    end_time).strftime('%Y-%m-%d %H:%M:%S')
                self.commit_stats['last_commit_duration'] = str(
                    end_time - start_time)
                self.push_config_state = PushConfigState.PUSH_STATE_SUCCESS
        except Exception as e:
            if self._logger:
                self._logger.error("Router %s: %s" % (self.management_ip,
                                                      e.message))
                self.commit_stats[
                    'commit_status_message'] = 'failed to apply config,\
                                                router response: ' + e.message
                if start_time is not None:
                    self.commit_stats['last_commit_time'] = \
                        datetime.datetime.fromtimestamp(
                            start_time).strftime('%Y-%m-%d %H:%M:%S')
                    self.commit_stats['last_commit_duration'] = str(
                        time.time() - start_time)
                self.push_config_state = PushConfigState.PUSH_STATE_RETRY
        return config_size
    # end send_config

    def add_pnf_logical_interface(self, junos_interface):

        if not self.logical_interface_config:
            self.logical_interface_config = etree.Element('interfaces')
        li_config = etree.fromstring("""
            <interface>
                <name>{physical_interface_name}</name>
                <unit>
                    <name>{logical_interface_id}</name>
                    <vlan-id>{vlan_id}</vlan-id>
                    <family>
                        <inet>
                            <address>{ip}</address>
                        </inet>
                    </family>
                </unit>
            </interface>
        """.format(
            physical_interface_name=junos_interface.ifd_name,
            logical_interface_id=junos_interface.unit,
            vlan_id=junos_interface.vlan_tag,
            ip=junos_interface.ip
        )
        )
        self.logical_interface_config.append(li_config)

    def add_static_routes(self, parent, static_routes):
        static_config = etree.SubElement(parent, "static")
        for dest, next_hops in static_routes.items():
            route_config = etree.SubElement(static_config, "route")
            etree.SubElement(route_config, "name").text = dest
            for next_hop in next_hops:
                next_hop_str = next_hop.get("next-hop")
                preference = next_hop.get("preference")
                if not next_hop_str:
                    continue
                if preference:
                    qualified = etree.SubElement(
                        route_config, "qualified-next-hop")
                    qualified.text = next_hop_str
                    etree.SubElement(
                        qualified, "preference").text = str(preference)
                else:
                    etree.SubElement(
                        route_config, "next-hop").text = next_hop_str

    def add_dynamic_tunnels(self, tunnel_source_ip,
                            ip_fabric_nets, bgp_router_ips):
        self.tunnel_config = etree.Element("routing-options")
        dynamic_tunnels = etree.SubElement(
            self.tunnel_config, "dynamic-tunnels")
        dynamic_tunnel = etree.SubElement(dynamic_tunnels, "dynamic-tunnel")
        etree.SubElement(dynamic_tunnel, "name").text = "__contrail__"
        etree.SubElement(
            dynamic_tunnel, "source-address").text = tunnel_source_ip
        etree.SubElement(dynamic_tunnel, "gre")
        if ip_fabric_nets is not None:
            for subnet in ip_fabric_nets.get("subnet", []):
                dest_network = etree.SubElement(
                    dynamic_tunnel, "destination-networks")
                etree.SubElement(dest_network, "name").text = subnet[
                    'ip_prefix'] + '/' + str(subnet['ip_prefix_len'])
        for bgp_router_ip in bgp_router_ips:
            dest_network = etree.SubElement(
                dynamic_tunnel, "destination-networks")
            etree.SubElement(dest_network, "name").text = bgp_router_ip + '/32'
    # end add_dynamic_tunnels

    def add_inet_public_vrf_filter(self, forwarding_options_config,
                                         firewall_config, inet_type, inet_xml_name):
        fo = etree.SubElement(forwarding_options_config, "family")
        inet = etree.SubElement(fo, inet_xml_name)
        f = etree.SubElement(inet, "filter")
        etree.SubElement(f, "input").text = "redirect_to_public_vrf_filter_" + inet_type
        fc = etree.SubElement(firewall_config, "family")
        inet = etree.SubElement(fc, inet_xml_name)
        f = etree.SubElement(inet, "filter")
        etree.SubElement(f, "name").text = "redirect_to_public_vrf_filter_" + inet_type
        term = etree.SubElement(f, "term")
        etree.SubElement(term, "name").text = "default-term"
        then_ = etree.SubElement(term, "then")
        etree.SubElement(then_, "accept")
        return f
    # end add_inet_public_vrf_filter

    def add_inet_filter_term(self, ri_name, prefixes, inet_type):
        term = etree.Element("term")
        etree.SubElement(term, "name").text = "term-" + ri_name[:59]
        from_ = etree.SubElement(term, "from")
        if inet_type == 'inet6':
            prefixes = [prefix for prefix in prefixes or [] if ':' in prefix]
        else:
            prefixes = [prefix for prefix in prefixes or [] if ':' not in prefix]

        for prefix in prefixes:
            etree.SubElement(from_, "destination-address").text = prefix

        then_ = etree.SubElement(term, "then")
        etree.SubElement(then_, "routing-instance").text = ri_name
        return term
    # end add_inet_filter_term

    '''
     ri_name: routing instance name to be configured on mx
     is_l2:  a flag used to indicate routing instance type, i.e : l2 or l3
     is_l2_l3:  VN forwarding mode is of type 'l2_l3' or not
     import/export targets: routing instance import, export targets
     prefixes: for l3 vrf static routes and for public vrf filter terms
     gateways: for l2 evpn, bug#1395944 
     router_external: this indicates the routing instance configured is for 
                      the public network
     interfaces: logical interfaces to be part of vrf
     fip_map: contrail instance ip to floating-ip map, used for snat & floating ip support
     network_id : this is used for configuraing irb interfaces
     static_routes: this is used for add PNF vrf static routes
     no_vrf_table_label: if this is set to True will not generate vrf table label knob
     restrict_proxy_arp: proxy-arp restriction config is generated for irb interfaces
                         only if vn is external and has fip map
    '''

    def add_routing_instance(self, ri_conf):
        ri_name = ri_conf.get("ri_name")
        is_l2 = ri_conf.get("is_l2", False)
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        import_targets = ri_conf.get("import_targets", set())
        export_targets = ri_conf.get("export_targets", set())
        prefixes = ri_conf.get("prefixes", [])
        gateways = ri_conf.get("gateways", [])
        router_external = ri_conf.get("router_external", False)
        interfaces = ri_conf.get("interfaces", [])
        vni = ri_conf.get("vni", None)
        fip_map = ri_conf.get("fip_map", None)
        network_id = ri_conf.get("network_id", None)
        static_routes = ri_conf.get("static_routes", {})
        no_vrf_table_label = ri_conf.get("no_vrf_table_label", False)
        restrict_proxy_arp = ri_conf.get("restrict_proxy_arp", False)

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
        ri = etree.SubElement(ri_config, "instance")
        etree.SubElement(ri, "name").text = ri_name
        ri_opt = None
        if router_external and is_l2 == False:
            ri_opt = etree.SubElement(ri, "routing-options")
            static_config = etree.SubElement(ri_opt, "static")
            route_config = etree.SubElement(static_config, "route")
            etree.SubElement(route_config, "name").text = "0.0.0.0/0"
            etree.SubElement(route_config, "next-table").text = "inet.0"

        # for both l2 and l3
        etree.SubElement(ri, "vrf-import").text = ri_name + "-import"
        etree.SubElement(ri, "vrf-export").text = ri_name + "-export"

        has_ipv6_prefixes = any(':' in prefix for prefix in prefixes or [])
        has_ipv4_prefixes = any(':' not in prefix for prefix in prefixes or [])

        if not is_l2:
            if ri_opt is None:
                ri_opt = etree.SubElement(ri, "routing-options")
            if prefixes and fip_map is None:
                static_config = etree.SubElement(ri_opt, "static")
                rib_config_v6 = None
                static_config_v6 = None
                for prefix in prefixes:
                    if ':' in prefix and not rib_config_v6:
                        rib_config_v6 = etree.SubElement(ri_opt, "rib")
                        etree.SubElement(rib_config_v6, "name").text = ri_name + ".inet6.0"
                        static_config_v6 = etree.SubElement(rib_config_v6, "static")
                    if ':' in prefix:
                        route_config = etree.SubElement(static_config_v6, "route")
                    else:
                        route_config = etree.SubElement(static_config, "route")
                    etree.SubElement(route_config, "name").text = prefix
                    etree.SubElement(route_config, "discard")
                    if router_external:
                        self.add_to_global_ri_opts(prefix)

            etree.SubElement(ri, "instance-type").text = "vrf"
            if not no_vrf_table_label:
                etree.SubElement(ri, "vrf-table-label")  # only for l3
            if fip_map is None:
                for interface in interfaces:
                    if_element = etree.SubElement(ri, "interface")
                    etree.SubElement(if_element, "name").text = interface.name
            if ri_opt is None:
                ri_opt = etree.SubElement(ri, "routing-options")
            if static_routes:
                self.add_static_routes(ri_opt, static_routes)
            if has_ipv4_prefixes:
                auto_export = """<auto-export>
                                <family><inet><unicast/></inet></family>
                            </auto-export>"""
                ri_opt.append(etree.fromstring(auto_export))
            if has_ipv6_prefixes:
                auto_export = """<auto-export>
                                <family><inet6><unicast/></inet6></family>
                            </auto-export>"""
                ri_opt.append(etree.fromstring(auto_export))
        else:
            etree.SubElement(ri, "instance-type").text = "virtual-switch"

        if fip_map is not None:
            if ri_opt is None:
                ri_opt = etree.SubElement(ri, "routing-options")
            static_config = etree.SubElement(ri_opt, "static")
            route_config = etree.SubElement(static_config, "route")
            etree.SubElement(route_config, "name").text = "0.0.0.0/0"
            etree.SubElement(
                route_config, "next-hop").text = interfaces[0].name
            if_element = etree.SubElement(ri, "interface")
            etree.SubElement(if_element, "name").text = interfaces[0].name
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
                if_element = etree.SubElement(ri_public, "interface")
                etree.SubElement(if_element, "name").text = interfaces[1].name

                for fip in fips:
                    route_config = etree.SubElement(static_config, "route")
                    etree.SubElement(route_config, "name").text = fip + "/32"
                    etree.SubElement(
                        route_config, "next-hop").text = interfaces[1].name

        # add policies for export route targets
        ps = etree.SubElement(policy_config, "policy-statement")
        etree.SubElement(ps, "name").text = ri_name + "-export"
        term = etree.SubElement(ps, "term")
        etree.SubElement(term, "name").text = "t1"
        then = etree.SubElement(term, "then")
        for route_target in export_targets:
            comm = etree.SubElement(then, "community")
            etree.SubElement(comm, "add")
            etree.SubElement(
                comm, "community-name").text = route_target.replace(':', '_')
        if fip_map is not None:
            # for nat instance
            etree.SubElement(then, "reject")
        else:
            etree.SubElement(then, "accept")

        # add policies for import route targets
        ps = etree.SubElement(policy_config, "policy-statement")
        etree.SubElement(ps, "name").text = ri_name + "-import"
        term = etree.SubElement(ps, "term")
        etree.SubElement(term, "name").text = "t1"
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
        if router_external and is_l2 == False:
            forwarding_options_config = (self.forwarding_options_config or
                                           etree.Element("forwarding-options"))
            firewall_config = self.firewall_config or etree.Element("firewall")
            if has_ipv4_prefixes and not self.inet4_forwarding_filter:
                #create single instance inet4 filter
                self.inet4_forwarding_filter = self.add_inet_public_vrf_filter(
                                                       forwarding_options_config,
                                                       firewall_config, "inet4", "inet")
            if has_ipv6_prefixes and not self.inet6_forwarding_filter:
                #create single instance inet6 filter
                self.inet6_forwarding_filter = self.add_inet_public_vrf_filter(
                                                       forwarding_options_config,
                                                       firewall_config, "inet6", "inet6")
            if has_ipv4_prefixes:
                #add terms to inet4 filter
                term = self.add_inet_filter_term(ri_name, prefixes, "inet4")
                # insert after 'name' element but before the last term
                self.inet4_forwarding_filter.insert(1, term)
            if has_ipv6_prefixes:
                #add terms to inet6 filter
                term = self.add_inet_filter_term(ri_name, prefixes, "inet6")
                # insert after 'name' element but before the last term
                self.inet6_forwarding_filter.insert(1, term)

        if fip_map is not None:
            firewall_config = self.firewall_config or etree.Element("firewall")
            fc = etree.SubElement(firewall_config, "family")
            inet = etree.SubElement(fc, "inet")
            f = etree.SubElement(inet, "filter")
            etree.SubElement(
                f, "name").text = "redirect_to_" + ri_name[:46] + "_vrf"
            term = etree.SubElement(f, "term")
            etree.SubElement(term, "name").text = "term-" + ri_name[:59]
            from_ = etree.SubElement(term, "from")
            for fip_user_ip in fip_map.keys():
                etree.SubElement(from_, "source-address").text = fip_user_ip
            then_ = etree.SubElement(term, "then")
            etree.SubElement(then_, "routing-instance").text = ri_name
            term = etree.SubElement(f, "term")
            etree.SubElement(term, "name").text = "default-term"
            then_ = etree.SubElement(term, "then")
            etree.SubElement(then_, "accept")

            interfaces_config = self.interfaces_config or etree.Element(
                "interfaces")
            irb_intf = etree.SubElement(interfaces_config, "interface")
            etree.SubElement(irb_intf, "name").text = "irb"
            intf_unit = etree.SubElement(irb_intf, "unit")
            etree.SubElement(intf_unit, "name").text = str(network_id)
            if restrict_proxy_arp:
                proxy_arp = etree.SubElement(intf_unit, "proxy-arp")
                etree.SubElement(proxy_arp, "restricted")
            family = etree.SubElement(intf_unit, "family")
            inet = etree.SubElement(family, "inet")
            f = etree.SubElement(inet, "filter")
            iput = etree.SubElement(f, "input")
            etree.SubElement(
                iput,
                "filter-name").text = "redirect_to_" + ri_name[:46] + "_vrf"

        # add L2 EVPN and BD config
        bd_config = None
        interfaces_config = self.interfaces_config
        proto_config = self.proto_config
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            etree.SubElement(ri, "vtep-source-interface").text = "lo0.0"
            bd_config = etree.SubElement(ri, "bridge-domains")
            bd = etree.SubElement(bd_config, "domain")
            etree.SubElement(bd, "name").text = "bd-" + str(vni)
            etree.SubElement(bd, "vlan-id").text = 'none'
            vxlan = etree.SubElement(bd, "vxlan")
            etree.SubElement(vxlan, "vni").text = str(vni)
            for interface in interfaces:
                if_element = etree.SubElement(bd, "interface")
                etree.SubElement(if_element, "name").text = interface.name
            if is_l2_l3:
                # network_id is unique, hence irb
                etree.SubElement(
                    bd, "routing-interface").text = "irb." + str(network_id)
            evpn_proto_config = etree.SubElement(ri, "protocols")
            evpn = etree.SubElement(evpn_proto_config, "evpn")
            etree.SubElement(evpn, "encapsulation").text = "vxlan"
            etree.SubElement(evpn, "extended-vni-list").text = "all"

            interfaces_config = self.interfaces_config or etree.Element(
                "interfaces")
            if is_l2_l3:
                irb_intf = etree.SubElement(interfaces_config, "interface")
                etree.SubElement(irb_intf, "name").text = "irb"
                etree.SubElement(irb_intf, "gratuitous-arp-reply")
                if gateways is not None:
                    intf_unit = etree.SubElement(irb_intf, "unit")
                    etree.SubElement(intf_unit, "name").text = str(network_id)
                    family = etree.SubElement(intf_unit, "family")
                    inet = None
                    inet6 = None
                    for (irb_ip, gateway) in gateways:
                        if ':' in irb_ip:
                            if not inet6:
                                inet6 = etree.SubElement(family, "inet6")
                            addr = etree.SubElement(inet6, "address")
                        else:
                            if not inet:
                                inet = etree.SubElement(family, "inet")
                            addr = etree.SubElement(inet, "address")
                        etree.SubElement(addr, "name").text = irb_ip
                        if len(gateway) and gateway != '0.0.0.0':
                            etree.SubElement(
                                addr, "virtual-gateway-address").text = gateway

            lo_intf = etree.SubElement(interfaces_config, "interface")
            etree.SubElement(lo_intf, "name").text = "lo0"
            intf_unit = etree.SubElement(lo_intf, "unit")
            etree.SubElement(intf_unit, "name").text = "0"
            family = etree.SubElement(intf_unit, "family")
            inet = etree.SubElement(family, "inet")
            addr = etree.SubElement(inet, "address")
            etree.SubElement(addr, "name").text = self.bgp_params[
                'address'] + "/32"
            etree.SubElement(addr, "primary")
            etree.SubElement(addr, "preferred")

            self.build_l2_evpn_interface_config(interfaces_config, interfaces)

        # fip services config
        services_config = self.services_config
        if fip_map is not None:
            services_config = self.services_config or etree.Element("services")
            service_name = 'sv-' + ri_name
            # mx has limitation for service-set and nat-rule name length,
            # allowed max 63 chars
            service_name = service_name[:23]
            service_set = etree.SubElement(services_config, "service-set")
            etree.SubElement(service_set, "name").text = service_name
            nat_rule = etree.SubElement(service_set, "nat-rules")
            etree.SubElement(nat_rule, "name").text = service_name + "-sn-rule"
            nat_rule = etree.SubElement(service_set, "nat-rules")
            etree.SubElement(nat_rule, "name").text = service_name + "-dn-rule"
            next_hop_service = etree.SubElement(
                service_set, "next-hop-service")
            etree.SubElement(
                next_hop_service,
                "inside-service-interface").text = interfaces[0].name
            etree.SubElement(
                next_hop_service,
                "outside-service-interface").text = interfaces[1].name

            nat = etree.SubElement(services_config, "nat")
            etree.SubElement(nat, "allow-overlapping-nat-pools")
            snat_rule = etree.SubElement(nat, "rule")
            etree.SubElement(
                snat_rule, "name").text = service_name + "-sn-rule"
            etree.SubElement(snat_rule, "match-direction").text = "input"
            dnat_rule = etree.SubElement(nat, "rule")
            etree.SubElement(
                dnat_rule, "name").text = service_name + "-dn-rule"
            etree.SubElement(dnat_rule, "match-direction").text = "output"

            for pip, fip_vn in fip_map.items():
                fip = fip_vn["floating_ip"]
                term = etree.SubElement(snat_rule, "term")
                etree.SubElement(
                    term, "name").text = "term_" + pip.replace('.', '_')
                from_ = etree.SubElement(term, "from")
                src_addr = etree.SubElement(from_, "source-address")
                # private ip
                etree.SubElement(src_addr, "name").text = pip + "/32"
                then_ = etree.SubElement(term, "then")
                translated = etree.SubElement(then_, "translated")
                etree.SubElement(
                    translated,
                    "source-prefix").text = fip + "/32"  # public ip
                translation_type = etree.SubElement(
                    translated, "translation-type")
                etree.SubElement(translation_type, "basic-nat44")

                term = etree.SubElement(dnat_rule, "term")
                etree.SubElement(
                    term, "name").text = "term_" + fip.replace('.', '_')
                from_ = etree.SubElement(term, "from")
                src_addr = etree.SubElement(from_, "destination-address")
                etree.SubElement(
                    src_addr, "name").text = fip + "/32"  # public ip
                then_ = etree.SubElement(term, "then")
                translated = etree.SubElement(then_, "translated")
                etree.SubElement(
                    translated,
                    "destination-prefix").text = pip + "/32"  # source ip
                translation_type = etree.SubElement(
                    translated, "translation-type")
                etree.SubElement(translation_type, "dnat-44")

            interfaces_config = self.interfaces_config or etree.Element(
                "interfaces")
            si_intf = etree.SubElement(interfaces_config, "interface")
            etree.SubElement(si_intf, "name").text = interfaces[0].ifd_name
            intf_unit = etree.SubElement(si_intf, "unit")
            etree.SubElement(intf_unit, "name").text = interfaces[0].unit
            family = etree.SubElement(intf_unit, "family")
            etree.SubElement(family, "inet")
            etree.SubElement(intf_unit, "service-domain").text = "inside"
            intf_unit = etree.SubElement(si_intf, "unit")
            etree.SubElement(intf_unit, "name").text = interfaces[1].unit
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

    def build_l2_evpn_interface_config(self, interfaces_config, interfaces):
        ifd_map = {}
        for interface in interfaces:
            ifd_map.setdefault(interface.ifd_name, []).append(interface)

        for ifd_name, interface_list in ifd_map.items():
            intf = etree.SubElement(interfaces_config, "interface")
            etree.SubElement(intf, "name").text = ifd_name
            if interface_list[0].is_untagged():
                if (len(interface_list) > 1):
                    self._logger.error(
                        "invalid logical interfaces config for ifd %s" % (
                            ifd_name))
                    continue
                etree.SubElement(
                    intf, "encapsulation").text = "ethernet-bridge"
                intf_unit = etree.SubElement(intf, "unit")
                etree.SubElement(
                    intf_unit, "name").text = interface_list[0].unit
                family = etree.SubElement(intf_unit, "family")
                etree.SubElement(family, "bridge")
            else:
                etree.SubElement(intf, "flexible-vlan-tagging")
                etree.SubElement(
                    intf, "encapsulation").text = "flexible-ethernet-services"
                for interface in interface_list:
                    intf_unit = etree.SubElement(intf, "unit")
                    etree.SubElement(intf_unit, "name").text = interface.unit
                    etree.SubElement(
                        intf_unit, "encapsulation").text = "vlan-bridge"
                    etree.SubElement(
                        intf_unit, "vlan-id").text = str(interface.vlan_tag)
    # end build_l2_evpn_interface_config

    def add_mpls_protocol(self):
        proto_config = self.proto_config or etree.Element("protocols")
        mpls = etree.SubElement(proto_config, "mpls")
        intf = etree.SubElement(mpls, "interface")
        etree.SubElement(intf, "name").text = "all"

    def set_global_routing_options(self, bgp_params):
        if bgp_params['address'] is not None:
            self.global_routing_options_config = etree.Element(
                "routing-options")
            etree.SubElement(
                self.global_routing_options_config,
                "router-id").text = bgp_params['address']
    # end set_global_routing_options

    def add_to_global_ri_opts(self, prefix):
        if not prefix:
            return
        if self.global_routing_options_config is None:
            self.global_routing_options_config = etree.Element(
                "routing-options")
        if ':' in prefix:
            rib_config_v6 = etree.SubElement(self.global_routing_options_config, "rib")
            etree.SubElement(rib_config_v6, "name").text = "inet6.0"
            static_config = etree.SubElement(rib_config_v6, "static")
        else:
            static_config = etree.SubElement(
                            self.global_routing_options_config, "static")
        route_config = etree.SubElement(static_config, "route")
        etree.SubElement(route_config, "name").text = prefix
        etree.SubElement(route_config, "discard")
    # end add_to_global_ri_opts

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
        families = params['address_families'].get('family', [])
        if not families:
            return
        family_etree = etree.SubElement(parent, "family")
        for family in families:
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
            etree.SubElement(
                bgp_config, "authentication-key").text = keys[0].get('key')

    def add_bgp_hold_time_config(self, bgp_config, bgp_params):
        if bgp_params.get('hold_time') is None:
            return
        etree.SubElement(
            bgp_config, "hold-time").text = str(bgp_params.get('hold_time'))

    def set_bgp_config(self, params):
        self.bgp_params = params
    # end set_bgp_config

    def _get_bgp_config_xml(self, external=False):
        if self.bgp_params is None:
            return None
        bgp_config = etree.Element("group", operation="replace")
        if external:
            etree.SubElement(bgp_config, "name").text = "__contrail_external__"
            etree.SubElement(bgp_config, "type").text = "external"
            etree.SubElement(bgp_config, "multihop")
        else:
            etree.SubElement(bgp_config, "name").text = "__contrail__"
            etree.SubElement(bgp_config, "type").text = "internal"
        local_address = etree.SubElement(bgp_config, "local-address")
        local_address.text = self.bgp_params['address']
        self._add_family_etree(bgp_config, self.bgp_params)
        self.add_bgp_auth_config(bgp_config, self.bgp_params)
        self.add_bgp_hold_time_config(bgp_config, self.bgp_params)
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
        self.inet4_forwarding_filter = None
        self.inet6_forwarding_filter = None
        self.forwarding_options_config = None
        self.global_routing_options_config = None
        self.proto_config = None
        self.logical_interface_config = None
        self.route_targets = set()
        self.bgp_peers = {}
        self.external_peers = {}

    # ene reset_bgp_config

    def delete_bgp_config(self):
        self.reset_bgp_config()
        self.send_netconf([], default_operation="none", operation="delete")
    # end delete_config

    def add_bgp_peer(self, router, params, attr, external):
        peer_data = {}
        peer_data['params'] = params
        peer_data['attr'] = attr
        if external:
            self.external_peers[router] = peer_data
        else:
            self.bgp_peers[router] = peer_data
    # end add_peer

    def _get_neighbor_config_xml(self, bgp_config, peers):
        for peer, peer_data in peers.items():
            params = peer_data.get('params', {})
            attr = peer_data.get('attr', {})
            nbr = etree.SubElement(bgp_config, "neighbor")
            etree.SubElement(nbr, "name").text = peer
            bgp_sessions = attr.get('session')
            if bgp_sessions:
                # for now assume only one session
                session_attrs = bgp_sessions[0].get('attributes', [])
                for session_attr in session_attrs:
                    # For not, only consider the attribute if bgp-router is
                    # not specified
                    if session_attr.get('bgp_router') is None:
                        self._add_family_etree(nbr, session_attr)
                        self.add_bgp_auth_config(nbr, session_attr)
                        break
            peer_as = params.get('local_autonomous_system') or params.get('autonomous_system')
            etree.SubElement(nbr, "peer-as").text = str(peer_as)
    # end _get_neighbor_config_xml

    def send_bgp_config(self):
        bgp_config = self._get_bgp_config_xml()
        if bgp_config is None:
            return 0
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
        local_as = self.bgp_params.get('local_autonomous_system') or self.bgp_params.get('autonomous_system')
        etree.SubElement(routing_options_config, "autonomous-system").text = str(local_as)
        config_list = [proto_config, routing_options_config]
        if self.ri_config is not None:
            config_list.append(self.ri_config)
        for route_target in self.route_targets:
            comm = etree.SubElement(self.policy_config, "community")
            etree.SubElement(
                comm, 'name').text = route_target.replace(':', '_')
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
        if self.global_routing_options_config is not None:
            config_list.append(self.global_routing_options_config)
        if self.proto_config is not None:
            config_list.append(self.proto_config)
        if self.logical_interface_config is not None:
            config_list.append(self.logical_interface_config)
        return self.send_netconf(config_list)
    # end send_bgp_config

# end PhycalRouterConfig


class JunosInterface(object):

    def __init__(self, if_name, if_type, if_vlan_tag=0, if_ip=None):
        self.name = if_name
        self.if_type = if_type
        self.vlan_tag = if_vlan_tag
        ifparts = if_name.split('.')
        self.ifd_name = ifparts[0]
        self.unit = ifparts[1]
        self.ip = if_ip
    # end __init__

    def is_untagged(self):
        if not self.vlan_tag:
            return True
        return False
    # end is_untagged

# end JunosInterface
