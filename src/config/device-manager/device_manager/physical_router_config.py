#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of inetconf interface for physical router
configuration manager
"""

from ncclient import manager
from ncclient.xml_ import new_ele
import copy
import time
import datetime
from cStringIO import StringIO
from dm_utils import DMUtils
from device_api.juniper_common_xsd import *

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
        'route-target': '',
        'inet-vpn': FamilyInetVpn(unicast=''),
        'inet6-vpn': FamilyInet6Vpn(unicast=''),
        'e-vpn': FamilyEvpn(signaling='')
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

    def get_xml_data(self, config):
        xml_data = StringIO()
        config.export_xml(xml_data, 1)
        xml_str = xml_data.getvalue()
        return xml_str.replace("comment>", "junos:comment>", -1)
    # end get_xml_data

    def build_netconf_config(self, groups, operation='replace'):
        groups.set_name("__contrail__")
        configuraion = Configuration(groups=groups)
        groups.set_operation(operation)
        apply_groups = ApplyGroups(name="__contrail__")
        configuraion.set_apply_groups(apply_groups)
        if operation == "delete":
            apply_groups.set_operation(operation)
        conf = config(configuration=configuraion)
        return conf

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
                new_config = self.build_netconf_config(new_config, operation)
                config_str = self.get_xml_data(new_config)
                self._logger.info("\nsend netconf message: %s\n" % config_str)
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

    def get_device_config(self):
        try:
            with manager.connect(host=self.management_ip, port=22,
                                 username=self.user_creds['username'],
                                 password=self.user_creds['password'],
                                 timeout=10,
                                 device_params = {'name':'junos'},
                                 unknown_host_cb=lambda x, y: True) as m:
                sw_info = new_ele('get-software-information')
                res = m.rpc(sw_info)
                pname = res.xpath('//software-information/product-name')[0].text
                pmodel = res.xpath('//software-information/product-model')[0].text
                ele = res.xpath("//software-information/package-information"
                                "[name='junos-version']")[0]
                jversion = ele.find('comment').text
                dev_conf = {}
                dev_conf['product-name'] = pname
                dev_conf['product-model'] = pmodel
                dev_conf['software-version'] = jversion
                return dev_conf
        except Exception as e:
            if self._logger:
                self._logger.error("could not fetch config from router %s: %s" % (
                                          self.management_ip, e.message))
        return {}

    def add_pnf_logical_interface(self, junos_interface):

        if not self.interfaces_config:
            self.interfaces_config = Interfaces(comment=DMUtils.interfaces_comment())
        family = Family(inet=FamilyInet([Address(name=junos_interface.ip)]))
        unit = Unit(name=junos_interface.unit, vlan_id=junos_interface.vlan_tag, family=family)
        interface = Interface(name=junos_interface.ifd_name, unit=unit)
        self.interfaces_config.add_interface(interface)
    # end add_pnf_logical_interface

    def add_lo0_unit_0_interface(self, loopback_ip=''):
        if not loopback_ip:
            return
        if not self.interfaces_config:
            self.interfaces_config = Interfaces(comment=DMUtils.interfaces_comment())
        lo_intf = Interface(name="lo0")
        self.interfaces_config.add_interface(lo_intf)
        fam_inet = FamilyInet(address=[Address(name=loopback_ip + "/32",
                                                   primary='', preferred='')])
        intf_unit = Unit(name="0", family=Family(inet=fam_inet),
                             comment=DMUtils.lo0_unit_0_comment())
        lo_intf.add_unit(intf_unit)
    # end add_lo0_unit_0_interface

    def add_static_routes(self, parent, static_routes):
        static_config = parent.get_static()
        if not static_config:
            static_config = Static()
            parent.set_static(static_config)
        for dest, next_hops in static_routes.items():
            route_config = Route(name=dest)
            for next_hop in next_hops:
                next_hop_str = next_hop.get("next-hop")
                preference = next_hop.get("preference")
                if not next_hop_str:
                    continue
                if preference:
                    route_config.set_qualified_next_hop(QualifiedNextHop(
                                     name=next_hop_str, preference=str(preference)))
                else:
                    route_config.set_next_hop(next_hop_str)
            static_config.add_route(route_config)
    # end add_static_routes

    def add_dynamic_tunnels(self, tunnel_source_ip,
                             ip_fabric_nets, bgp_router_ips):
        dynamic_tunnel = DynamicTunnel(name=DMUtils.dynamic_tunnel_name(self.get_asn()),
                                       source_address=tunnel_source_ip, gre='')
        if ip_fabric_nets is not None:
            for subnet in ip_fabric_nets.get("subnet", []):
                dest_net = subnet['ip_prefix'] + '/' + str(subnet['ip_prefix_len'])
                dynamic_tunnel.add_destination_networks(
                    DestinationNetworks(name=dest_net,
                                        comment=DMUtils.ip_fabric_subnet_comment()))

        for r_name, bgp_router_ip in bgp_router_ips.items():
            dynamic_tunnel.add_destination_networks(
                DestinationNetworks(name=bgp_router_ip + '/32',
                                    comment=DMUtils.bgp_router_subnet_comment(r_name)))

        dynamic_tunnels = DynamicTunnels()
        dynamic_tunnels.add_dynamic_tunnel(dynamic_tunnel)
        if self.global_routing_options_config is None:
            self.global_routing_options_config = RoutingOptions(comment=DMUtils.routing_options_comment())
        self.global_routing_options_config.set_dynamic_tunnels(dynamic_tunnels)
    # end add_dynamic_tunnels

    def add_inet_public_vrf_filter(self, forwarding_options_config,
                                         firewall_config, inet_type):
        fo = Family()
        inet_filter = InetFilter(input=DMUtils.make_public_vrf_filter_name(inet_type))
        if inet_type == 'inet6':
            fo.set_inet6(FamilyInet6(filter=inet_filter))
        else:
            fo.set_inet(FamilyInet(filter=inet_filter))
        forwarding_options_config.add_family(fo)

        f = FirewallFilter(name=DMUtils.make_public_vrf_filter_name(inet_type))
        f.set_comment(DMUtils.public_vrf_filter_comment())
        ff = firewall_config.get_family()
        if not ff:
            ff = FirewallFamily()
            firewall_config.set_family(ff)
        if inet_type == 'inet6':
            inet6 = ff.get_inet6()
            if not inet6:
                inet6 = FirewallInet()
                ff.set_inet6(inet6)
            inet6.add_filter(f)
        else:
            inet = ff.get_inet()
            if not inet:
                inet = FirewallInet()
                ff.set_inet(inet)
            inet.add_filter(f)

        term = Term(name="default-term", then=Then(accept=''))
        f.add_term(term)
        return f
    # end add_inet_public_vrf_filter

    def add_inet_filter_term(self, ri_name, prefixes, inet_type):
        if inet_type == 'inet6':
            prefixes = DMUtils.get_ipv6_prefixes(prefixes)
        else:
            prefixes = DMUtils.get_ipv4_prefixes(prefixes)

        from_ = From()
        for prefix in prefixes:
            from_.add_destination_address(prefix)
        then_ = Then()
        then_.add_routing_instance(ri_name)
        return Term(name=DMUtils.make_vrf_term_name(ri_name),
                                        fromxx=from_, then=then_)
    # end add_inet_filter_term

    def add_ibgp_export_policy(self, params):
        if params.get('address_families') is None:
            return
        families = params['address_families'].get('family', [])
        if not families:
            return
        if self.policy_config is None:
            self.policy_config = PolicyOptions(comment=DMUtils.policy_options_comment())
        ps = PolicyStatement(name=DMUtils.make_ibgp_export_policy_name())
        self.policy_config.add_policy_statement(ps)
        ps.set_comment(DMUtils.ibgp_export_policy_comment())
        vpn_types = []
        for family in ['inet-vpn', 'inet6-vpn']:
            if family in families:
                vpn_types.append(family)
        for vpn_type in vpn_types:
            is_v6 = True if vpn_type == 'inet6-vpn' else False
            term = Term(name=DMUtils.make_ibgp_export_policy_term_name(is_v6))
            ps.set_term(term)
            then = Then()
            from_ = From()
            term.set_from(from_)
            term.set_then(then)
            from_.set_family(DMUtils.get_inet_family_name(is_v6))
            then.set_next_hop(NextHop(selfxx=''))
    # end add_ibgp_export_policy

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
     highest_enapsulation_priority: highest encapsulation configured
    '''

    def add_routing_instance(self, ri_conf):
        ri_name = ri_conf.get("ri_name")
        vn = ri_conf.get("vn")
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
        highest_enapsulation_priority = \
                  ri_conf.get("highest_enapsulation_priority") or "MPLSoGRE"

        self.routing_instances[ri_name] = ri_conf
        ri_config = self.ri_config or RoutingInstances(comment=DMUtils.routing_instances_comment())
        policy_config = self.policy_config or PolicyOptions(comment=DMUtils.policy_options_comment())
        ri = Instance(name=ri_name)
        if vn:
            is_nat = True if fip_map else False
            ri.set_comment(DMUtils.vn_ri_comment(vn, is_l2, is_l2_l3, is_nat))
        ri_config.add_instance(ri)
        ri_opt = None
        if router_external and is_l2 == False:
            ri_opt = RoutingInstanceRoutingOptions(
                         static=Static(route=[Route(name="0.0.0.0/0",
                                                    next_table="inet.0",
                                                    comment=DMUtils.public_vrf_route_comment())]))
            ri.set_routing_options(ri_opt)

        # for both l2 and l3
        ri.set_vrf_import(DMUtils.make_import_name(ri_name))
        ri.set_vrf_export(DMUtils.make_export_name(ri_name))

        has_ipv6_prefixes = DMUtils.has_ipv6_prefixes(prefixes)
        has_ipv4_prefixes = DMUtils.has_ipv4_prefixes(prefixes)

        if not is_l2:
            if ri_opt is None:
                ri_opt = RoutingInstanceRoutingOptions()
                ri.set_routing_options(ri_opt)
            if prefixes and fip_map is None:
                static_config = ri_opt.get_static()
                if not static_config:
                    static_config = Static()
                    ri_opt.set_static(static_config)
                rib_config_v6 = None
                static_config_v6 = None
                for prefix in prefixes:
                    if ':' in prefix and not rib_config_v6:
                        static_config_v6 = Static()
                        rib_config_v6 = RIB(name=ri_name + ".inet6.0")
                        rib_config_v6.set_static(static_config_v6)
                        ri_opt.set_rib(rib_config_v6)
                    if ':' in prefix:
                        static_config_v6.add_route(Route(name=prefix, discard=''))
                    else:
                        static_config.add_route(Route(name=prefix, discard=''))
                    if router_external:
                        self.add_to_global_ri_opts(prefix)

            ri.set_instance_type("vrf")
            if not no_vrf_table_label:
                ri.set_vrf_table_label('')  # only for l3
            if fip_map is None:
                for interface in interfaces:
                    ri.add_interface(Interface(name=interface.name))
            if static_routes:
                self.add_static_routes(ri_opt, static_routes)
            if has_ipv4_prefixes:
                ri_opt.set_auto_export(AutoExport(family=Family(inet=FamilyInet(unicast=''))))
            if has_ipv6_prefixes:
                ri_opt.set_auto_export(AutoExport(family=Family(inet6=FamilyInet6(unicast=''))))
        else:
            if highest_enapsulation_priority == "VXLAN":
                ri.set_instance_type("virtual-switch")
            elif highest_enapsulation_priority in ["MPLSoGRE", "MPLSoUDP"]:
                ri.set_instance_type("evpn")

        if fip_map is not None:
            if ri_opt is None:
                ri_opt = RoutingInstanceRoutingOptions()
                ri.set_routing_options(ri_opt)
            static_config = ri_opt.get_static()
            if not static_config:
                static_config = Static()
                ri_opt.set_static(static_config)
            static_config.add_route(Route(name="0.0.0.0/0",
                                          next_hop=interfaces[0].name,
                                          comment=DMUtils.fip_ingress_comment()))
            ri.add_interface(Interface(name=interfaces[0].name))

            public_vrf_ips = {}
            for pip in fip_map.values():
                if pip["vrf_name"] not in public_vrf_ips:
                    public_vrf_ips[pip["vrf_name"]] = set()
                public_vrf_ips[pip["vrf_name"]].add(pip["floating_ip"])

            for public_vrf, fips in public_vrf_ips.items():
                ri_public = Instance(name=public_vrf)
                ri_config.add_instance(ri_public)
                ri_public.add_interface(Interface(name=interfaces[1].name))

                ri_opt = RoutingInstanceRoutingOptions()
                ri_public.set_routing_options(ri_opt)
                static_config = Static()
                ri_opt.set_static(static_config)

                for fip in fips:
                    static_config.add_route(Route(name=fip + "/32",
                                                  next_hop=interfaces[1].name,
                                                  comment=DMUtils.fip_egress_comment()))

        # add policies for export route targets
        ps = PolicyStatement(name=DMUtils.make_export_name(ri_name))
        ps.set_comment(DMUtils.vn_ps_comment(vn, "Export"))
        then = Then()
        ps.set_term(Term(name="t1", then=then))
        for route_target in export_targets:
            comm = Community(add='',
                             community_name=DMUtils.make_community_name(route_target))
            then.add_community(comm)
        if fip_map is not None:
            # for nat instance
            then.set_reject('')
        else:
            then.set_accept('')
        policy_config.add_policy_statement(ps)

        # add policies for import route targets
        ps = PolicyStatement(name=DMUtils.make_import_name(ri_name))
        ps.set_comment(DMUtils.vn_ps_comment(vn, "Import"))
        from_ = From()
        term = Term(name="t1", fromxx=from_)
        ps.set_term(term)
        for route_target in import_targets:
            from_.add_community(DMUtils.make_community_name(route_target))
        term.set_then(Then(accept=''))
        ps.set_then(Then(reject=''))
        policy_config.add_policy_statement(ps)

        # add firewall config for public VRF
        forwarding_options_config = self.forwarding_options_config
        firewall_config = self.firewall_config
        if router_external and is_l2 == False:
            forwarding_options_config = (self.forwarding_options_config or
                                           ForwardingOptions(DMUtils.forwarding_options_comment()))
            firewall_config = self.firewall_config or Firewall(DMUtils.firewall_comment())
            if has_ipv4_prefixes and not self.inet4_forwarding_filter:
                #create single instance inet4 filter
                self.inet4_forwarding_filter = self.add_inet_public_vrf_filter(
                                                       forwarding_options_config,
                                                       firewall_config, "inet")
            if has_ipv6_prefixes and not self.inet6_forwarding_filter:
                #create single instance inet6 filter
                self.inet6_forwarding_filter = self.add_inet_public_vrf_filter(
                                                       forwarding_options_config,
                                                       firewall_config, "inet6")
            if has_ipv4_prefixes:
                #add terms to inet4 filter
                term = self.add_inet_filter_term(ri_name, prefixes, "inet4")
                # insert before the last term
                terms = self.inet4_forwarding_filter.get_term()
                terms = [term] + (terms or [])
                self.inet4_forwarding_filter.set_term(terms)
            if has_ipv6_prefixes:
                #add terms to inet6 filter
                term = self.add_inet_filter_term(ri_name, prefixes, "inet6")
                # insert before the last term
                terms = self.inet6_forwarding_filter.get_term()
                terms = [term] + (terms or [])
                self.inet6_forwarding_filter.set_term(terms)

        if fip_map is not None:
            firewall_config = firewall_config or Firewall(DMUtils.firewall_comment())
            f = FirewallFilter(name=DMUtils.make_private_vrf_filter_name(ri_name))
            f.set_comment(DMUtils.vn_firewall_comment(vn, "private"))
            ff = firewall_config.get_family()
            if not ff:
                ff = FirewallFamily()
                firewall_config.set_family(ff)
            inet = ff.get_inet()
            if not inet:
                inet = FirewallInet()
                ff.set_inet(inet)
            inet.add_filter(f)

            term = Term(name=DMUtils.make_vrf_term_name(ri_name))
            from_ = From()
            for fip_user_ip in fip_map.keys():
                from_.add_source_address(fip_user_ip)
            term.set_from(from_)
            term.set_then(Then(routing_instance=[ri_name]))
            f.add_term(term)

            term = Term(name="default-term", then=Then(accept=''))
            f.add_term(term)

            interfaces_config = self.interfaces_config or Interfaces(comment=DMUtils.interfaces_comment())
            irb_intf = Interface(name="irb")
            interfaces_config.add_interface(irb_intf)

            intf_unit = Unit(name=str(network_id),
                             comment=DMUtils.vn_irb_fip_inet_comment(vn))
            if restrict_proxy_arp:
                intf_unit.set_proxy_arp(ProxyArp(restricted=''))
            inet = FamilyInet()
            inet.set_filter(InetFilter(input=DMUtils.make_private_vrf_filter_name(ri_name)))
            intf_unit.set_family(Family(inet=inet))
            irb_intf.add_unit(intf_unit)

        # add L2 EVPN and BD config
        bd_config = None
        interfaces_config = self.interfaces_config
        proto_config = self.proto_config
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            ri.set_vtep_source_interface("lo0.0")
            if highest_enapsulation_priority == "VXLAN":
                bd_config = BridgeDomains()
                ri.set_bridge_domains(bd_config)
                bd = Domain(name=DMUtils.make_bridge_name(vni), vlan_id='none', vxlan=VXLan(vni=vni))
                bd.set_comment(DMUtils.vn_bd_comment(vn, "VXLAN"))
                bd_config.add_domain(bd)
                for interface in interfaces:
                     bd.add_interface(Interface(name=interface.name))
                if is_l2_l3:
                    # network_id is unique, hence irb
                    bd.set_routing_interface("irb." + str(network_id))
                ri.set_protocols(RoutingInstanceProtocols(
                               evpn=Evpn(encapsulation='vxlan', extended_vni_list='all')))
            elif highest_enapsulation_priority in ["MPLSoGRE", "MPLSoUDP"]:
                ri.set_vlan_id('none')
                if is_l2_l3:
                    # network_id is unique, hence irb
                    ri.set_routing_interface("irb." + str(network_id))
                evpn = Evpn()
                evpn.set_comment(DMUtils.vn_evpn_comment(vn, highest_enapsulation_priority))
                for interface in interfaces:
                     evpn.add_interface(Interface(name=interface.name))
                ri.set_protocols(RoutingInstanceProtocols(evpn=evpn))

            interfaces_config = self.interfaces_config or Interfaces(comment=DMUtils.interfaces_comment())
            if is_l2_l3:
                irb_intf = Interface(name='irb', gratuitous_arp_reply='')
                interfaces_config.add_interface(irb_intf)
                if gateways is not None:
                    intf_unit = Unit(name=str(network_id),
                                     comment=DMUtils.vn_irb_comment(vn, False, is_l2_l3))
                    irb_intf.add_unit(intf_unit)
                    family = Family()
                    intf_unit.set_family(family)
                    inet = None
                    inet6 = None
                    for (irb_ip, gateway) in gateways:
                        if ':' in irb_ip:
                            if not inet6:
                                inet6 = FamilyInet6()
                                family.set_inet6(inet6)
                            addr = Address()
                            inet6.add_address(addr)
                        else:
                            if not inet:
                                inet = FamilyInet()
                                family.set_inet(inet)
                            addr = Address()
                            inet.add_address(addr)
                        addr.set_name(irb_ip)
                        addr.set_comment(DMUtils.irb_ip_comment(irb_ip))
                        if len(gateway) and gateway != '0.0.0.0':
                            addr.set_virtual_gateway_address(gateway)

            self.build_l2_evpn_interface_config(interfaces_config, interfaces, vn)

        if (not is_l2 and not is_l2_l3 and gateways):
            interfaces_config = self.interfaces_config or Interfaces(comment=DMUtils.interfaces_comment())
            ifl_num = str(1000 + int(network_id))
            lo_intf = Interface(name="lo0")
            interfaces_config.add_interface(lo_intf)
            intf_unit = Unit(name=ifl_num, comment=DMUtils.l3_lo_intf_comment(vn))
            lo_intf.add_unit(intf_unit)
            family = Family()
            intf_unit.set_family(family)
            inet = None
            inet6 = None
            for (lo_ip, _) in gateways:
                (ip, _) = lo_ip.split('/')
                if ':' in lo_ip:
                    if not inet6:
                        inet6 = FamilyInet6()
                        family.set_inet6(inet6)
                    addr = Address()
                    inet6.add_address(addr)
                    lo_ip = ip + '/' + '128'
                else:
                    if not inet:
                        inet = FamilyInet()
                        family.set_inet(inet)
                    addr = Address()
                    inet.add_address(addr)
                    lo_ip = ip + '/' + '32'
                addr.set_name(lo_ip)
                addr.set_comment(DMUtils.lo0_ip_comment(lo_ip))
            ri.add_interface(Interface(name="lo0." + ifl_num,
                                       comment=DMUtils.lo0_ri_intf_comment(vn)))

        # fip services config
        services_config = self.services_config
        if fip_map is not None:
            services_config = self.services_config or Services()
            services_config.set_comment(DMUtils.services_comment())
            service_name = DMUtils.make_services_set_name(ri_name)
            service_set = ServiceSet(name=service_name)
            service_set.set_comment(DMUtils.service_set_comment(vn))
            services_config.add_service_set(service_set)
            nat_rule = NATRules(name=service_name + "-sn-rule")
            service_set.add_nat_rules(NATRules(name=DMUtils.make_snat_rule_name(ri_name),
                                               comment=DMUtils.service_set_nat_rule_comment(vn, "SNAT")))
            service_set.add_nat_rules(NATRules(name=DMUtils.make_dnat_rule_name(ri_name),
                                               comment=DMUtils.service_set_nat_rule_comment(vn, "DNAT")))
            next_hop_service = NextHopService(inside_service_interface = interfaces[0].name,
                                              outside_service_interface = interfaces[1].name)
            service_set.set_next_hop_service(next_hop_service)

            nat = NAT(allow_overlapping_nat_pools='')
            nat.set_comment(DMUtils.nat_comment())
            services_config.add_nat(nat)
            snat_rule = Rule(name=DMUtils.make_snat_rule_name(ri_name),
                             match_direction="input")
            snat_rule.set_comment(DMUtils.snat_rule_comment())
            nat.add_rule(snat_rule)
            dnat_rule = Rule(name=DMUtils.make_dnat_rule_name(ri_name),
                             match_direction="output")
            dnat_rule.set_comment(DMUtils.dnat_rule_comment())
            nat.add_rule(dnat_rule)

            for pip, fip_vn in fip_map.items():
                fip = fip_vn["floating_ip"]
                term = Term(name=DMUtils.make_ip_term_name(pip))
                snat_rule.set_term(term)
                # private ip
                from_ = From(source_address=[pip + "/32"])
                term.set_from(from_)
                # public ip
                then_ = Then()
                term.set_then(then_)
                translated = Translated(source_prefix=fip + "/32",
                                        translation_type=TranslationType(basic_nat44=''))
                then_.set_translated(translated)

                term = Term(name=DMUtils.make_ip_term_name(fip))
                dnat_rule.set_term(term)

                # public ip
                from_ = From(destination_address=[fip + "/32"])
                term.set_from(from_)
                # private ip
                then_ = Then()
                term.set_then(then_)
                translated = Translated(destination_prefix=pip + "/32",
                                        translation_type=TranslationType(dnat_44=''))
                then_.set_translated(translated)

            interfaces_config = self.interfaces_config or Interfaces(comment=DMUtils.interfaces_comment())
            si_intf = Interface(name=interfaces[0].ifd_name,
                                comment=DMUtils.service_ifd_comment())
            interfaces_config.add_interface(si_intf)

            intf_unit = Unit(name=interfaces[0].unit,
                             comment=DMUtils.service_intf_comment("Ingress"))
            si_intf.add_unit(intf_unit)
            family = Family(inet=FamilyInet())
            intf_unit.set_family(family)
            intf_unit.set_service_domain("inside")

            intf_unit = Unit(name=interfaces[1].unit,
                             comment=DMUtils.service_intf_comment("Egress"))
            si_intf.add_unit(intf_unit)
            family = Family(inet=FamilyInet())
            intf_unit.set_family(family)
            intf_unit.set_service_domain("outside")

        self.forwarding_options_config = forwarding_options_config
        self.firewall_config = firewall_config
        self.policy_config = policy_config
        self.proto_config = proto_config
        self.interfaces_config = interfaces_config
        self.services_config = services_config
        self.route_targets |= import_targets | export_targets
        self.ri_config = ri_config
    # end add_routing_instance

    def build_l2_evpn_interface_config(self, interfaces_config, interfaces, vn=None):
        ifd_map = {}
        for interface in interfaces:
            ifd_map.setdefault(interface.ifd_name, []).append(interface)

        for ifd_name, interface_list in ifd_map.items():
            intf = Interface(name=ifd_name)
            interfaces_config.add_interface(intf)
            if interface_list[0].is_untagged():
                if (len(interface_list) > 1):
                    self._logger.error(
                        "invalid logical interfaces config for ifd %s" % (
                            ifd_name))
                    continue
                intf.set_encapsulation("ethernet-bridge")
                intf.add_unit(Unit(name=interface_list[0].unit,
                                   comment=DMUtils.l2_evpn_intf_unit_comment(vn, False),
                                   family=Family(bridge='')))
            else:
                intf.set_flexible_vlan_tagging('')
                intf.set_encapsulation("flexible-ethernet-services")
                for interface in interface_list:
                    intf.add_unit(Unit(name=interface.unit,
                               comment=DMUtils.l2_evpn_intf_unit_comment(vn,
                                                     True, interface.vlan_tag),
                               encapsulation='vlan-bridge',
                               vlan_id=str(interface.vlan_tag)))
    # end build_l2_evpn_interface_config

    def set_global_routing_options(self, bgp_params):
        router_id = bgp_params.get('identifier') or bgp_params.get('address')
        if router_id:
            if not self.global_routing_options_config:
                self.global_routing_options_config = RoutingOptions(comment=DMUtils.routing_options_comment())
            self.global_routing_options_config.set_router_id(router_id)
    # end set_global_routing_options

    def add_to_global_ri_opts(self, prefix):
        if not prefix:
            return
        if self.global_routing_options_config is None:
            self.global_routing_options_config = RoutingOptions(comment=DMUtils.routing_options_comment())
        static_config = Static()
        if ':' in prefix:
            rib_config_v6 = RIB(name='inet6.0')
            rib_config_v6.set_static(static_config)
            self.global_routing_options_config.add_rib(rib_config_v6)
        else:
            self.global_routing_options_config.add_static(static_config)
        static_config.add_route(Route(name=prefix, discard=''))
    # end add_to_global_ri_opts

    def is_family_configured(self, params, family_name):
        if params is None or params.get('address_families') is None:
            return False
        families = params['address_families'].get('family', [])
        if family_name in families:
            return True
        return False

    def add_families(self, parent, params):
        if params.get('address_families') is None:
            return
        families = params['address_families'].get('family', [])
        if not families:
            return
        family_etree = Family()
        parent.set_family(family_etree)
        for family in families:
            fam = family.replace('-', '_')
            if family in ['e-vpn', 'e_vpn']:
                fam = 'evpn'
            if family in self._FAMILY_MAP:
                getattr(family_etree, "set_" + fam)(self._FAMILY_MAP[family])
            else:
                getattr(family_etree, "set_" + fam)('')
    # end add_families

    def add_bgp_auth_config(self, bgp_config, bgp_params):
        if bgp_params.get('auth_data') is None:
            return
        keys = bgp_params['auth_data'].get('key_items', [])
        if len(keys) > 0:
            bgp_config.set_authentication_key(keys[0].get('key'))

    def add_bgp_hold_time_config(self, bgp_config, bgp_params):
        if bgp_params.get('hold_time') is None:
            return
        bgp_config.set_hold_time(bgp_params.get('hold_time'))

    def set_bgp_config(self, params, bgp_obj):
        self.bgp_params = params
        self.bgp_obj = bgp_obj
    # end set_bgp_config

    def _get_bgp_config_xml(self, external=False):
        if self.bgp_params is None:
            return None
        bgp_group = BgpGroup()
        bgp_group.set_comment(DMUtils.bgp_group_comment(self.bgp_obj))
        if external:
            bgp_group.set_name(DMUtils.make_bgp_group_name(self.get_asn(), True))
            bgp_group.set_type('external')
            bgp_group.set_multihop('')
        else:
            bgp_group.set_name(DMUtils.make_bgp_group_name(self.get_asn(), False))
            bgp_group.set_type('internal')
            self.add_ibgp_export_policy(self.bgp_params)
            bgp_group.set_export(DMUtils.make_ibgp_export_policy_name())
        bgp_group.set_local_address(self.bgp_params['address'])
        self.add_families(bgp_group, self.bgp_params)
        self.add_bgp_auth_config(bgp_group, self.bgp_params)
        self.add_bgp_hold_time_config(bgp_group, self.bgp_params)
        return bgp_group
    # end _get_bgp_config_xml

    def reset_bgp_config(self):
        self.routing_instances = {}
        self.bgp_params = None
        self.bgp_obj = None
        self.ri_config = None
        self.interfaces_config = None
        self.services_config = None
        self.policy_config = None
        self.firewall_config = None
        self.inet4_forwarding_filter = None
        self.inet6_forwarding_filter = None
        self.forwarding_options_config = None
        self.global_routing_options_config = None
        self.proto_config = None
        self.route_targets = set()
        self.bgp_peers = {}
        self.external_peers = {}

    # ene reset_bgp_config

    def delete_bgp_config(self):
        self.reset_bgp_config()
        self.send_netconf(Groups(), default_operation="none", operation="delete")
    # end delete_config

    def add_bgp_peer(self, router, params, attr, external, peer):
        peer_data = {}
        peer_data['params'] = params
        peer_data['attr'] = attr
        peer_data['obj'] = peer
        if external:
            self.external_peers[router] = peer_data
        else:
            self.bgp_peers[router] = peer_data
    # end add_peer

    def _get_neighbor_config_xml(self, bgp_config, peers):
        for peer, peer_data in peers.items():
            obj = peer_data.get('obj')
            params = peer_data.get('params', {})
            attr = peer_data.get('attr', {})
            nbr = BgpGroup(name=peer)
            nbr.set_comment(DMUtils.bgp_group_comment(obj))
            bgp_config.add_neighbor(nbr)
            bgp_sessions = attr.get('session')
            if bgp_sessions:
                # for now assume only one session
                session_attrs = bgp_sessions[0].get('attributes', [])
                for session_attr in session_attrs:
                    # For not, only consider the attribute if bgp-router is
                    # not specified
                    if session_attr.get('bgp_router') is None:
                        self.add_families(nbr, session_attr)
                        self.add_bgp_auth_config(nbr, session_attr)
                        break
            peer_as = params.get('local_autonomous_system') or params.get('autonomous_system')
            nbr.set_peer_as(peer_as)
    # end _get_neighbor_config_xml

    def get_asn(self):
        return self.bgp_params.get('local_autonomous_system') or self.bgp_params.get('autonomous_system')

    def set_as_config(self):
        if self.global_routing_options_config is None:
            self.global_routing_options_config = RoutingOptions(comment=DMUtils.routing_options_comment())
        self.global_routing_options_config.set_route_distinguisher_id(self.bgp_params['identifier'])
        self.global_routing_options_config.set_autonomous_system(str(self.get_asn()))
    # end set_as_config

    def set_route_targets_config(self):
        if self.policy_config is None:
            self.policy_config = PolicyOptions(comment=DMUtils.policy_options_comment())
        for route_target in self.route_targets:
            comm = CommunityType(name=DMUtils.make_community_name(route_target),
                                 members=route_target)
            self.policy_config.add_community(comm)
    # end set_route_targets_config

    def set_bgp_group_config(self):
        bgp_config = self._get_bgp_config_xml()
        if bgp_config is None:
            return False
        if self.proto_config is None:
            self.proto_config = Protocols(comment=DMUtils.protocols_comment())
        bgp = Bgp()
        self.proto_config.set_bgp(bgp)
        bgp.add_group(bgp_config)
        self._get_neighbor_config_xml(bgp_config, self.bgp_peers)
        if self.external_peers is not None:
            ext_grp_config = self._get_bgp_config_xml(True)
            bgp.add_group(ext_grp_config)
            self._get_neighbor_config_xml(ext_grp_config, self.external_peers)
        return True
    # end set_bgp_group_config

    def send_bgp_config(self):
        if not self.set_bgp_group_config():
            return 0

        self.set_as_config()
        self.set_route_targets_config()

        groups = Groups()
        groups.set_comment(DMUtils.groups_comment())
        groups.set_routing_instances(self.ri_config)
        groups.set_interfaces(self.interfaces_config)
        groups.set_services(self.services_config)
        groups.set_policy_options(self.policy_config)
        groups.set_firewall(self.firewall_config)
        groups.set_forwarding_options(self.forwarding_options_config)
        groups.set_routing_options(self.global_routing_options_config)
        groups.set_protocols(self.proto_config)
        return self.send_netconf(groups)
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
