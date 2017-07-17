#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of netconf interface for physical router
configuration manager
"""

from db import *
from dm_utils import DMUtils
from juniper_conf import JuniperConf
from juniper_conf import JunosInterface
from device_api.juniper_common_xsd import *

class MxConf(JuniperConf):
    _products = ['mx']

    def __init__(self, logger, params={}):
        self.e2_manager = None
        self._logger = logger
        self.physical_router = params.get("physical_router")
        super(MxConf, self).__init__()
    # end __init__

    @classmethod
    def register(cls):
        mconf = {
              "vendor": cls._vendor,
              "products": cls._products,
              "class": cls
            }
        return super(MxConf, cls).register(mconf)
    # end register

    @classmethod
    def is_product_supported(cls, name):
        for product in cls._products:
            if name.lower().startswith(product.lower()):
                return True
        return False
    # end is_product_supported

    def add_pnf_logical_interface(self, junos_interface):
        if not self.interfaces_config:
            self.interfaces_config = Interfaces(comment=DMUtils.interfaces_comment())
        family = Family(inet=FamilyInet([Address(name=junos_interface.ip)]))
        unit = Unit(name=junos_interface.unit, vlan_id=junos_interface.vlan_tag, family=family)
        interface = Interface(name=junos_interface.ifd_name, unit=unit)
        self.interfaces_config.add_interface(interface)
    # end add_pnf_logical_interface

    def config_pnf_logical_interface(self):
        pnf_dict = {}
        pnf_ris = set()
        # make it fake for now
        # sholud save to the database, the allocation
        self.vlan_alloc = {"max": 1}
        self.ip_alloc = {"max": -1}
        self.li_alloc = {}

        for pi_uuid in self.physical_router.physical_interfaces:
            pi = PhysicalInterfaceDM.get(pi_uuid)
            if pi is None:
                continue
            for pi_pi_uuid in pi.physical_interfaces:
                pi_pi = PhysicalInterfaceDM.get(pi_pi_uuid)
                for pi_vmi_uuid in pi_pi.virtual_machine_interfaces:
                    allocate_li = False
                    pi_vmi = VirtualMachineInterfaceDM.get(pi_vmi_uuid)
                    if (pi_vmi is None or
                            pi_vmi.service_instance is None or
                            pi_vmi.service_interface_type is None):
                        continue
                    if pi_vmi.routing_instances:
                        for ri_id in pi_vmi.routing_instances:
                            ri_obj = RoutingInstanceDM.get(ri_id)
                            if ri_obj and ri_obj.routing_instances and ri_obj.service_chain_address:
                                pnf_ris.add(ri_obj)
                                # If this service is on a service chain, we need allocate
                                # a logic interface for its VMI
                                allocate_li = True

                    if allocate_li:
                        resources = self.physical_router.allocate_pnf_resources(pi_vmi)
                        if (not resources or
                                not resources["ip_address"] or
                                not resources["vlan_id"] or
                                not resources["unit_id"]):
                            self._logger.error(
                                "Cannot allocate PNF resources for "
                                "Virtual Machine Interface" + pi_vmi_uuid)
                            return
                        logical_interface = JunosInterface(
                            pi.name + '.' + resources["unit_id"],
                            "l3", resources["vlan_id"], resources["ip_address"])
                        self.add_pnf_logical_interface(
                            logical_interface)
                        lis = pnf_dict.setdefault(
                            pi_vmi.service_instance,
                            {"left": [], "right": [],
                                "mgmt": [], "other": []}
                        )[pi_vmi.service_interface_type]
                        lis.append(logical_interface)

        return (pnf_dict, pnf_ris)
    # end

    def add_pnf_vrfs(self, first_vrf, pnf_dict, pnf_ris):
        is_first_vrf = False
        is_left_first_vrf = False
        for ri_obj in pnf_ris:
            if ri_obj in first_vrf:
                is_first_vrf = True
            else:
                is_first_vrf = False
            export_set = copy.copy(ri_obj.export_targets)
            import_set = copy.copy(ri_obj.import_targets)
            for ri2_id in ri_obj.routing_instances:
                ri2 = RoutingInstanceDM.get(ri2_id)
                if ri2 is None:
                    continue
                import_set |= ri2.export_targets

            pnf_inters = set()
            static_routes = self.physical_router.compute_pnf_static_route(ri_obj, pnf_dict)
            if_type = ""
            for vmi_uuid in ri_obj.virtual_machine_interfaces:
                vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
                if vmi_obj.service_instance is not None:
                    si_obj = ServiceInstanceDM.get(vmi_obj.service_instance)
                    if_type = vmi_obj.service_interface_type
                    pnf_li_inters = pnf_dict[
                        vmi_obj.service_instance][if_type]
                    if if_type == 'left' and is_first_vrf:
                        is_left_first_vrf = True
                    else:
                        is_left_first_vrf = False

                    for pnf_li in pnf_li_inters:
                        pnf_inters.add(pnf_li)

            if pnf_inters:
                vrf_name = self.physical_router.get_pnf_vrf_name(
                    si_obj, if_type, is_left_first_vrf)
                vrf_interfaces = pnf_inters
                ri_conf = { 'ri_name': vrf_name }
                ri_conf['import_targets'] = import_set
                ri_conf['export_targets'] = export_set
                ri_conf['interfaces'] = vrf_interfaces
                ri_conf['static_routes'] = static_routes
                ri_conf['no_vrf_table_label'] = True
                self.add_routing_instance(ri_conf)
    # end add_pnf_vrfs

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
            ri.set_comment(DMUtils.vn_ri_comment(vn, is_l2, is_l2_l3, is_nat, router_external))
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
            family = Family()
            if has_ipv4_prefixes:
                family.set_inet(FamilyInet(unicast=''))
            if has_ipv6_prefixes:
                family.set_inet6(FamilyInet6(unicast=''))
            if has_ipv4_prefixes or has_ipv6_prefixes:
                auto_export = AutoExport(family=family)
                ri_opt.set_auto_export(auto_export)
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
        ps.add_term(Term(name="t1", then=then))
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
        ps.add_term(term)
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
                subnet = lo_ip
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
                addr.set_comment(DMUtils.lo0_ip_comment(subnet))
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

    def set_route_targets_config(self):
        if self.policy_config is None:
            self.policy_config = PolicyOptions(comment=DMUtils.policy_options_comment())
        for route_target in self.route_targets:
            comm = CommunityType(name=DMUtils.make_community_name(route_target),
                                 members=route_target)
            self.policy_config.add_community(comm)
    # end set_route_targets_config

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        if self.physical_router.router_mode == 'non-contrail':
            return self.push_e2_conf()
        if is_delete:
            return self.send_conf(is_delete=True)
        self.build_bgp_config()
        vn_dict = self.get_vn_li_map()
        self.physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l2_l3', 'irb', False)
        self.physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l3', 'lo0', True)
        vn_irb_ip_map = self.physical_router.get_vn_irb_ip_map()

        first_vrf = []
        pnfs = self.config_pnf_logical_interface()
        pnf_dict = pnfs[0]
        pnf_ris = pnfs[1]

        for vn_id, interfaces in vn_dict.items():
            vn_obj = VirtualNetworkDM.get(vn_id)
            if (vn_obj is None or
                    vn_obj.get_vxlan_vni() is None or
                    vn_obj.vn_network_id is None):
                continue
            export_set = None
            import_set = None
            for ri_id in vn_obj.routing_instances:
                # Find the primary RI by matching the name
                ri_obj = RoutingInstanceDM.get(ri_id)
                if ri_obj is None:
                    continue
                if ri_obj.fq_name[-1] == vn_obj.fq_name[-1]:
                    vrf_name_l2 = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                   vn_obj.vn_network_id, 'l2')
                    vrf_name_l3 = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                   vn_obj.vn_network_id, 'l3')
                    export_set = copy.copy(ri_obj.export_targets)
                    import_set = copy.copy(ri_obj.import_targets)
                    for ri2_id in ri_obj.routing_instances:
                        ri2 = RoutingInstanceDM.get(ri2_id)
                        if ri2 in pnf_ris:
                            first_vrf.append(ri2)
                        if ri2 is None:
                            continue
                        import_set |= ri2.export_targets

                    if vn_obj.get_forwarding_mode() in ['l2', 'l2_l3']:
                        irb_ips = None
                        if vn_obj.get_forwarding_mode() == 'l2_l3':
                            irb_ips = vn_irb_ip_map['irb'].get(vn_id, [])

                        ri_conf = { 'ri_name': vrf_name_l2, 'vn': vn_obj }
                        ri_conf['is_l2'] = True
                        ri_conf['is_l2_l3'] = (vn_obj.get_forwarding_mode() == 'l2_l3')
                        ri_conf['import_targets'] = import_set
                        ri_conf['export_targets'] = export_set
                        ri_conf['prefixes'] = vn_obj.get_prefixes()
                        ri_conf['gateways'] = irb_ips
                        ri_conf['router_external'] = vn_obj.router_external
                        ri_conf['interfaces'] = interfaces
                        ri_conf['vni'] = vn_obj.get_vxlan_vni()
                        ri_conf['network_id'] = vn_obj.vn_network_id
                        ri_conf['highest_enapsulation_priority'] = \
                                  GlobalVRouterConfigDM.global_encapsulation_priority
                        self.add_routing_instance(ri_conf)

                    if vn_obj.get_forwarding_mode() in ['l3', 'l2_l3']:
                        interfaces = []
                        lo0_ips = None
                        if vn_obj.get_forwarding_mode() == 'l2_l3':
                            interfaces = [
                                 JunosInterface(
                                'irb.' + str(vn_obj.vn_network_id),
                                'l3', 0)]
                        else:
                            lo0_ips = vn_irb_ip_map['lo0'].get(vn_id, [])
                        ri_conf = { 'ri_name': vrf_name_l3, 'vn': vn_obj }
                        ri_conf['is_l2_l3'] = (vn_obj.get_forwarding_mode() == 'l2_l3')
                        ri_conf['import_targets'] = import_set
                        ri_conf['export_targets'] = export_set
                        ri_conf['prefixes'] = vn_obj.get_prefixes()
                        ri_conf['router_external'] = vn_obj.router_external
                        ri_conf['interfaces'] = interfaces
                        ri_conf['gateways'] = lo0_ips
                        ri_conf['network_id'] = vn_obj.vn_network_id
                        self.add_routing_instance(ri_conf)
                    break

            if (export_set is not None and
                    self.physical_router.is_junos_service_ports_enabled() and
                    len(vn_obj.instance_ip_map) > 0):
                service_port_ids = DMUtils.get_service_ports(vn_obj.vn_network_id)
                if self.physical_router.is_service_port_id_valid(service_port_ids[0]) == False:
                    self._logger.error("DM can't allocate service interfaces for "
                                       "(vn, vn-id)=(%s,%s)" % (
                        vn_obj.fq_name,
                        vn_obj.vn_network_id))
                else:
                    vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                 vn_obj.vn_network_id, 'l3', True)
                    interfaces = []
                    service_ports = self.physical_router.junos_service_ports.get(
                        'service_port')
                    interfaces.append(
                        JunosInterface(
                            service_ports[0] + "." + str(service_port_ids[0]),
                            'l3', 0))
                    interfaces.append(
                        JunosInterface(
                            service_ports[0] + "." + str(service_port_ids[1]),
                            'l3', 0))
                    ri_conf = { 'ri_name': vrf_name, 'vn': vn_obj }
                    ri_conf['import_targets'] = import_set
                    ri_conf['interfaces'] = interfaces
                    ri_conf['fip_map'] = vn_obj.instance_ip_map
                    ri_conf['network_id'] = vn_obj.vn_network_id
                    ri_conf['restrict_proxy_arp'] = vn_obj.router_external
                    self.add_routing_instance(ri_conf)
        # Add PNF ri configuration
        self.add_pnf_vrfs(first_vrf, pnf_dict, pnf_ris)
        self.set_as_config()
        self.set_route_targets_config()
        self.set_bgp_group_config()
        return self.send_conf()
    # end push_conf

    def push_e2_conf(self, is_delete=False):
        if not self.e2_manager:
            self.e2_manager = MXE2Services(self._logger)
        import pdb;pdb.set_trace()
        self.e2_manager.config_e2_services(self)
        self.build_e2_router_config()
        self.build_e2_telemetry_config()
        return self.send_e2_conf()
    # end push_e2_conf

    def get_service_status(self, service_params={}):
        if not self.e2_manager:
            self.e2_manager = MXE2Services(self._logger)
        self._service_type = service_params.get("service_type")
        self._circuit_id = service_params.get("circuit_id")
        self._neigbor_id = service_params.get("neigbor_id")

        service_status_obj = self.e2_manager.get_service_status(
                                          self._service_type,
                                          self._circuit_id,
                                          self._neigbor_id)
        return service_status_obj
    #end get_service_status

# end MxConf

class MXE2Services(object):
    _l2ckt_errors = {
        'EI': 'encapsulation invalid',
        'NP': 'interface h/w not present',
        'MM': 'mtu mismatch',
        'Dn': 'down',
        'EM': 'encapsulation mismatch',
        'VC-Dn': 'Virtual circuit Down',
        'CM': 'control-word mismatch',
        'Up': 'operational',
        'VM': 'vlan id mismatch',
        'CF': 'Call admission control failure',
        'OL': 'no outgoing label',
        'IB': 'TDM incompatible bitrate',
        'NC': 'intf encaps not CCC/TCC',
        'TM': 'TDM misconfiguration ',
        'BK': 'Backup Connection',
        'ST': 'Standby Connection',
        'CB': 'rcvd cell-bundle size bad',
        'SP': 'Static Pseudowire',
        'LD': 'local site signaled down',
        'RS': 'remote site standby',
        'RD': 'remote site signaled down',
        'HS': 'Hot-standby Connection',
        'XX': 'unknown'
    }

    _l2vpn_errors = {
        'EI': 'encapsulation invalid',
        'NC': 'interface encapsulation not CCC/TCC/VPLS',
        'EM': 'encapsulation mismatch',
        'WE': 'interface and instance encaps not same',
        'VC-Dn': 'Virtual circuit down',
        'NP': 'interface hardware not present',
        'CM': 'control-word mismatch',
        '->': 'only outbound connection is up',
        'CN': 'circuit not provisioned',
        '<-': 'only inbound connection is up',
        'OR': 'out of range',
        'Up': 'operational',
        'OL': 'no outgoing label',
        'Dn': 'down',
        'LD': 'local site signaled down',
        'CF': 'call admission control failure',
        'RD': 'remote site signaled down',
        'SC': 'local and remote site ID collision',
        'LN': 'local site not designated',
        'LM': 'local site ID not minimum designated',
        'RN': 'remote site not designated',
        'RM': 'remote site ID not minimum designated',
        'XX': 'unknown connection status',
        'IL': 'no incoming label',
        'MM': 'MTU mismatch',
        'MI': 'Mesh-Group ID not available',
        'BK': 'Backup connection',
        'ST': 'Standby connection',
        'PF': 'Profile parse failure',
        'PB': 'Profile busy',
        'RS': 'remote site standby',
        'SN': 'Static Neighbor',
        'LB': 'Local site not best-site',
        'RB': 'Remote site not best-site',
        'VM': 'VLAN ID mismatch',
        'HS': 'Hot-standby Connection',
    }
    def __init__(self, logger):
        self._logger = logger
        #end __init__

    def config_e2_services(self, obj):
        status = False
        node_role = None
        service_exists = False
        service_index = 0
        return
        if 'role'in obj.physical_router.physical_router_property:
            node_role = obj.physical_router.physical_router_property['role']
        obj._logger.info("Total services on %s is =%d" %
                (obj.physical_router.name, \
                len(obj.physical_router.service_endpoints)))
        ps_config_dict={}
        ps_intf_list=[]
        ps_circuit_id_dict = {}
        for sindex, sep_uuid in enumerate(obj.physical_router.service_endpoints):
            service_exists = True
            prot_configured = False
            service_type = None
            peer_pr_entry = None
            peer_sep_entry = None
            service_fabric = False
            peer_router_id = 0
            peer_router_lpbk_id = 0
            local_site_id = 0
            remote_site_id = 0
            peer_phy_circuit_id = 0
            sep = ServiceEndpointDM.get(sep_uuid)
            if sep is None:
                obj._logger.info("SEP is NULL for node=%s" %
                                  (obj.physical_router.name))
                continue
            local_site_id = sep.site_id
            pr_uuid = sep.physical_router
            pr_entry = PhysicalRouterDM.get(pr_uuid)
            if pr_entry is None:
                obj._logger.info("PR is NULL for node=%s" %
                                  (obj.physical_router.name))
                continue
            if 'as_number'in pr_entry.physical_router_property:
                as_number = pr_entry.physical_router_property['as_number']
            router_id = pr_entry.physical_router_id
            router_lpbk_id = pr_entry.physical_router_loopback_id
            phy_circuit_id = pr_entry.next_index
            for scm_id in sep.service_connection_modules:
                phy_mtu = 0
                li_mtu  = 0
                no_control_word = False
                scm    = ServiceConnectionModuleDM.get(scm_id)
                if scm is None:
                    obj._logger.info("SCM is NULL for node=%s, sep=%s" %
                                      (obj.physical_router.name, sep.name))
                    continue
                for skey, sval in scm.service_connection_info.iteritems():
                    if 'set_config' in skey:
                        set_config = sval
                    if 'service_type' in skey:
                        service_type = sval
                    if 'resources' in skey:
                        mtu_present = False
                        cw_present = False
                        for res_entry in sval:
                            if 'mtu' in res_entry.values():
                                mtu_present = True
                            if 'control-word' in res_entry.values():
                                cw_present = True
                            for res_key, res_value in res_entry.iteritems():
                                if 'rvalue' in res_key and mtu_present == True:
                                    scm.mtu = res_value
                                    phy_mtu = scm.mtu
                                    mtu_present = False
                                if 'rvalue' in res_key and cw_present == True:
                                    scm.no_control_word = True
                                    no_control_word = True
                                    cw_present = False
                if set_config == False:
                    obj._logger.info("Skipping sep=%s on node=%s, set_config=%d" %
                                      (sep.name, obj.physical_router.name, set_config))
                    continue
                peer_seps = scm.service_endpoints
                peer_sep_circuit_id = None
                if peer_seps is not None:
                    peer_seps = list(peer_seps)
                    for peer_sep_uuid  in peer_seps:
                        if peer_sep_uuid == sep_uuid:
                            continue;
                        peer_sep_entry = ServiceEndpointDM.get(peer_sep_uuid)
                        remote_site_id = peer_sep_entry.site_id
                        peer_pr_uuid = peer_sep_entry.physical_router
                        peer_pr_entry = PhysicalRouterDM.get(peer_pr_uuid)
                        if peer_pr_entry is not None:
                            peer_phy_circuit_id = peer_pr_entry.next_index
                            peer_router_id = peer_pr_entry.physical_router_id
                            peer_router_lpbk_id = peer_pr_entry.physical_router_loopback_id

                if phy_circuit_id > peer_phy_circuit_id:
                    scm_circuit_id = phy_circuit_id
                else:
                    scm_circuit_id = peer_phy_circuit_id
                vmi_id = sep.virtual_machine_interface
                vmi    = VirtualMachineInterfaceDM.get(vmi_id)
                if vmi is None:
                    continue
                li_id = vmi.logical_interface
                li    = LogicalInterfaceDM.get(li_id)
                if li is None:
                    continue
                obj._logger.info("Service config,pr=%s, role=%s, sep=%s, service-type=%s" %
                                  (obj.physical_router.name, node_role, sep.name, service_type))
                if service_type == 'fabric-interface' and service_fabric == False:
                    ps_config = None
                    self.add_e2_phy_logical_interface(obj, li.name, \
                                   li.vlan_tag, service_type, node_role, \
                                   phy_mtu, li_mtu, ps_config)
                    self.add_e2_lo0_config(node_role, router_lpbk_id)
                    self.add_e2_routing_options(node_role, router_id)
                    service_fabric == True
                elif service_type == 'vpws-l2ckt':
                    ps_config = None
                    self.add_e2_phy_logical_interface(obj, li.name,
                            li.vlan_tag, service_type, node_role, phy_mtu,
                            li_mtu, ps_config)
                    if scm.circuit_id == 0:
                        service_index = scm_circuit_id + 1
                        scm.circuit_id = service_index
                        pr_entry.next_index = service_index
                        if peer_pr_entry is not None:
                            peer_pr_entry.next_index = service_index
                    else :
                        service_index = scm.circuit_id
                    self.add_e2_services_l2ckt_config_xml(obj, node_role,
                            li.name, li.vlan_tag, service_index, \
                            peer_router_id, no_control_word)
                elif service_type == 'vpws-l2vpn':
                    ps_config = None
                    self.add_e2_phy_logical_interface(obj, li.name,
                            li.vlan_tag, service_type, node_role, phy_mtu, \
                            li_mtu, ps_config)
                    if scm.circuit_id == 0:
                        service_index = scm_circuit_id + 1
                        scm.circuit_id = service_index
                        pr_entry.next_index = service_index
                        if peer_pr_entry is not None:
                            peer_pr_entry.next_index = service_index
                    else:
                        service_index = scm.circuit_id
                    if local_site_id == 0 and remote_site_id == 0:
                        local_site_id = 1
                        remote_site_id = 2
                        sep.site_id = local_site_id
                        if peer_sep_entry is not None:
                            peer_sep_entry.site_id = remote_site_id
                    # local service endpoint is created, while remote is not yet
                    elif local_site_id != 0:
                        if peer_sep_entry is not None and remote_site_id == 0:
                            remote_site_id = local_site_id + 1
                            peer_sep_entry.site_id = remote_site_id
                    elif remote_site_id != 0:
                        if  local_site_id == 0:
                            local_site_id = remote_site_id + 1
                            sep_entry.site_id = local_site_id
                    self.add_e2_services_l2vpn_config_xml(obj, \
                            node_role, li.name, li.vlan_tag, as_number, \
                            service_index, local_site_id, remote_site_id)
                elif service_type == 'vpws-evpn':
                    self.add_e2_chassis_config()
                    vn_obj = VirtualNetworkDM.get(vmi.virtual_network)
                    ifname = li.name.split('.')
                    if 'ps' in ifname[0] and ifname[0] in ps_config_dict:
                        ps_config = ps_config_dict[ifname[0]]
                    elif 'ps' in ifname[0] and not ifname[0] in ps_intf_list:
                        ps_config = None
                        li_name = ifname[0] + '.0'
                        ps_config = self.add_e2_phy_logical_interface(obj, \
                                li_name, 0, service_type, node_role, phy_mtu, \
                                li_mtu, ps_config)
                    else:
                        ps_config = None
                    ps_config = self.add_e2_phy_logical_interface(obj,
                            li.name, li.vlan_tag, service_type, node_role,
                            phy_mtu, li_mtu, ps_config)
                    if 'ps' in ifname[0] and not ifname[0] in ps_config_dict:
                        ps_config_dict[ifname[0]] = ps_config
                    if vn_obj.circuit_id != 0:
                        scm.circuit_id = vn_obj.circuit_id
                    if scm.circuit_id == 0:
                        service_index = scm_circuit_id + 1
                        scm.circuit_id = service_index
                        pr_entry.next_index = service_index
                        if peer_pr_entry is not None:
                            peer_pr_entry.next_index = service_index
                    else :
                        service_index = scm.circuit_id
                    if vn_obj.circuit_id == 0:
                        vn_obj.circuit_id = service_index
                    if not ifname[0] in ps_intf_list:
                        li_name = ifname[0] + '.0'
                        self.add_e2_services_pwht_config_mx_xml(obj, node_role,
                          li_name, li.vlan_tag, service_index, peer_router_id,
                          no_control_word)
                        if not ps_intf_list:
                            ps_intf_list = [ifname[0]]
                        else:
                            ps_intf_list.append(ifname[0])
    # end config_e2_services

    def _get_interface_unit_config_xml(self, ifl_unit, li_vlan_tag):
        unit_config = etree.Element("unit")
        etree.SubElement(unit_config, "name").text = ifl_unit
        if li_vlan_tag == 0:
            etree.SubElement(unit_config, "encapsulation").text = "ethernet-ccc"
        else:
            etree.SubElement(unit_config, "vlan-id").text = str(li_vlan_tag)
            unit_family = etree.SubElement(unit_config, "family")
            etree.SubElement(unit_family, "inet")
        return unit_config
    # end _get_interface_unit_config_xml

    def _get_interface_config_xml(self, ifd_name, ifl_unit, li_vlan_tag, \
                                  service_type, phy_mtu, li_mtu):
        interface_config = etree.Element("interface")
        etree.SubElement(interface_config, "name").text = ifd_name
        if li_vlan_tag == 0:
            etree.SubElement(interface_config, "encapsulation").text = "ethernet-ccc"
            if phy_mtu != 0:
                etree.SubElement(interface_config, "mtu").text = str(phy_mtu)
            unit = etree.SubElement(interface_config, "unit")
            etree.SubElement(unit, "name").text = ifl_unit
            family = etree.SubElement(unit, "family")
            etree.SubElement(family, "ccc")
        else:
            etree.SubElement(interface_config, "flexible-vlan-tagging")
            if phy_mtu != 0:
                etree.SubElement(interface_config, "mtu").text = str(phy_mtu)
            etree.SubElement(interface_config, "encapsulation").text = "flexible-ethernet-services"
            unit = etree.SubElement(interface_config, "unit")
            etree.SubElement(unit, "name").text = ifl_unit
            etree.SubElement(unit, "encapsulation").text = "vlan-ccc"
            etree.SubElement(unit, "vlan-id").text = str(li_vlan_tag)
        return interface_config
    # end _get_interface_config_xml

    def _get_interface_fabric0_config_xml(self, ifl_unit, node_role):
        fabric_config = etree.Element("interface")
        etree.SubElement(fabric_config, "name").text = "ae0"
        unit = etree.SubElement(fabric_config, "unit")
        etree.SubElement(unit, "name").text = ifl_unit
        family = etree.SubElement(unit, "family")
        inet_family = etree.SubElement(family, "inet")
        address = etree.SubElement(inet_family, "address")
        if node_role == 'access':
            etree.SubElement(address, "name").text = "2.2.2.1/24"
        else:
            etree.SubElement(address, "name").text = "2.2.2.2/24"
        family = etree.SubElement(unit, "family")
        etree.SubElement(family, "mpls")
        return fabric_config
    # end _get_interface_fabric0_config_xml

    def _get_interface_fabric_child_config_xml(self, phy_name):
        fabric_child = etree.Element("interface")
        #etree.SubElement(fabric_child, "name").text = "ge-0/0/1"
        etree.SubElement(fabric_child, "name").text = phy_name
        if_config = etree.SubElement(fabric_child, "gigether-options")
        ad = etree.SubElement(if_config, "ieee-802.3ad")
        etree.SubElement(ad, "bundle").text = "ae0"
        return fabric_child
    # end _get_interface_fabric_child_config_xml

    def _get_interface_lo0_config_xml(self, node_role, router_lpbk_id):
        lo0_config = etree.Element("interface")
        etree.SubElement(lo0_config, "name").text = "lo0"
        unit = etree.SubElement(lo0_config, "unit")
        etree.SubElement(unit, "name").text = "0"
        family = etree.SubElement(unit, "family")
        inet_family = etree.SubElement(family, "inet")
        address = etree.SubElement(inet_family, "address")
        if node_role == 'access':
            if router_lpbk_id == 0:
                etree.SubElement(address, "name").text = "100.100.100.100/32"
            else:
                etree.SubElement(address, "name").text =  router_lpbk_id + "/32"
        else:
            if router_lpbk_id == 0:
                etree.SubElement(address, "name").text = "200.200.200.200/32"
            else:
                etree.SubElement(address, "name").text =  router_lpbk_id + "/32"
        etree.SubElement(address, "primary")
        return lo0_config
    # end _get_interface_lo0_config_xml

    def _get_routing_options_config_xml(self, node_role, router_id):
        routing_options = etree.Element("routing-options")
        if node_role == 'access':
            if router_id == 0:
                lo0_ip = "100.100.100.100/32"
                lo0_ipaddr = lo0_ip.split('/', 1)
                router_id = lo0_ipaddr[0]
        else:
            if router_id == 0:
                lo0_ip = "200.200.200.200/32"
                lo0_ipaddr = lo0_ip.split('/', 1)
                router_id = lo0_ipaddr[0]
        etree.SubElement(routing_options, "router-id").text = router_id
        return routing_options
    # end _get_routing_options_config_xml

    def _get_chassis_config_xml(self):
        chassis_config = etree.Element("chassis")
        ps = etree.SubElement(chassis_config, "pseudowire-service")
        etree.SubElement(ps, "device-count").text = "2"
        fp = etree.SubElement(chassis_config, "fpc")
        etree.SubElement(fp, "name").text = "0"
        pic = etree.SubElement(fp, "pic")
        etree.SubElement(pic, "name").text = "0"
        ts = etree.SubElement(pic, "tunnel-services")
        etree.SubElement(ts, "bandwidth").text = "10g"
        return chassis_config
    # end _get_chassis_config_xml

    def add_l2ckt_protocol_config_xml(self, obj, circuit_id, neighbor, \
                                      li_name, li_vlan_tag,\
                                      no_control_word):
        l2ckt_cfg = etree.Element("l2circuit")
        l2ckt = etree.SubElement(l2ckt_cfg, "neighbor")
        etree.SubElement(l2ckt, "name").text = neighbor
        l2ckt_intf = etree.SubElement(l2ckt, "interface")
        etree.SubElement(l2ckt_intf, "name").text = li_name
        etree.SubElement(l2ckt_intf, "virtual-circuit-id").text = str(circuit_id)
        if no_control_word == True:
            etree.SubElement(l2ckt_intf, "no-control-word")
        if obj.e2_services_prot_config is not None:
            obj.e2_services_prot_config.append(l2ckt_cfg)
        else:
            routing_cfg = etree.Element("protocols")
            routing_cfg.append(l2ckt_cfg)
            obj.e2_services_prot_config = routing_cfg
    # end add_l2ckt_protocol_config_xml

    def add_l2vpn_protocol_config_xml(self, obj, li_name, rd, vrf_target,
                                      li_vlan_tag, site_name, site_id,
                                      remote_site_id):
        l2vpn = etree.Element("instance")
        etree.SubElement(l2vpn, "name").text = site_name
        #etree.SubElement(l2vpn, "name").text = "l2vpn"
        etree.SubElement(l2vpn, "instance-type").text = "l2vpn"
        l2vpn_intf = etree.SubElement(l2vpn, "interface")
        etree.SubElement(l2vpn_intf, "name").text = li_name
        l2vpn_rd = etree.SubElement(l2vpn, "route-distinguisher")
        etree.SubElement(l2vpn_rd, "rd-type").text = rd
        l2vpn_vt = etree.SubElement(l2vpn, "vrf-target")
        etree.SubElement(l2vpn_vt, "community").text = 'target:' + vrf_target
        l2vpn_pr = etree.SubElement(l2vpn, "protocols")
        l2vpn_info = etree.SubElement(l2vpn_pr, "l2vpn")
        if li_vlan_tag != 0:
            etree.SubElement(l2vpn_info, "encapsulation-type").text = "ethernet-vlan"
        else:
            etree.SubElement(l2vpn_info, "encapsulation-type").text = "ethernet"
        site_info = etree.SubElement(l2vpn_info, "site")
        etree.SubElement(site_info, "name").text = site_name
        etree.SubElement(site_info, "site-identifier").text = str(site_id)
        site_intf = etree.SubElement(site_info, "interface")
        etree.SubElement(site_intf, "name").text = li_name
        etree.SubElement(site_intf, "remote-site-id").text = str(remote_site_id)

        if obj.e2_services_ri_config is not None:
            obj.e2_services_ri_config.append(l2vpn)
        else:
            routing_inst = etree.Element("routing-instances")
            routing_inst.append(l2vpn)
            obj.e2_services_ri_config = routing_inst
    # end add_l2vpn_protocol_config_xml

    def _get_interface_ps_config_xml(self, ifname):
        ps_config = etree.Element("interface")
        etree.SubElement(ps_config, "name").text = ifname
        anp = etree.SubElement(ps_config, "anchor-point")
        etree.SubElement(anp, "interface-name").text = "lt-0/0/0"
        etree.SubElement(ps_config, "flexible-vlan-tagging")
        return ps_config
    # end _get_interface_ps_config_xml

    def add_pwht_config_xml(self, obj, circuit_id, neighbor, li_name):
        #Transport and l2ckt
        if 'ps' in li_name and '.0' in li_name:
            l2ckt_cfg = etree.Element("l2circuit")
            l2ckt = etree.SubElement(l2ckt_cfg, "neighbor")
            etree.SubElement(l2ckt, "name").text = neighbor
            l2ckt_intf = etree.SubElement(l2ckt, "interface")
            etree.SubElement(l2ckt_intf, "name").text = li_name
            etree.SubElement(l2ckt_intf, "virtual-circuit-id").text = str(circuit_id)
            if obj.e2_services_prot_config is not None:
                obj.e2_services_prot_config.append(l2ckt_cfg)
            else:
                routing_cfg = etree.Element("protocols")
                routing_cfg.append(l2ckt_cfg)
                obj.e2_services_prot_config = routing_cfg
            return
        #Services
    # end add_pwht_config_xml

    def add_ldp_protocol_config_xml(self, obj, interfaces):
        ldp_cfg = etree.Element("ldp")
        etree.SubElement(ldp_cfg, "track-igp-metric")
        etree.SubElement(ldp_cfg, "deaggregate")
        for interface in interfaces:
            ldp_intf = etree.SubElement(ldp_cfg, "interface")
            etree.SubElement(ldp_intf, "name").text = interface
        if obj.e2_services_prot_config is not None:
            obj.e2_services_prot_config.append(ldp_cfg)
        else:
            routing_cfg = etree.Element("protocols")
            routing_cfg.append(ldp_cfg)
            obj.e2_services_prot_config = routing_cfg
    # end add_ldp_protocol_config_xml

    def add_mpls_protocol_config_xml(self, obj, interfaces, lsp_name, neighbor):
        mpls_cfg = etree.Element("mpls")
        if lsp_name is not None:
            mpls_lsp = etree.SubElement(mpls_cfg, "label-switched-path")
            etree.SubElement(mpls_lsp, "name").text = lsp_name
            etree.SubElement(mpls_lsp, "to").text = neighbor
        for interface in interfaces:
            mpls_intf = etree.SubElement(mpls_cfg, "interface")
            etree.SubElement(mpls_intf, "name").text = interface
        if obj.e2_services_prot_config is not None:
            obj.e2_services_prot_config.append(mpls_cfg)
        else:
            routing_cfg = etree.Element("protocols")
            routing_cfg.append(mpls_cfg)
            obj.e2_services_prot_config = routing_cfg
    # end add_mpls_protocol_config_xml

    def add_ospf_protocol_config_xml(self, obj, interfaces, area):
        ospf_cfg = etree.Element("ospf")
        etree.SubElement(ospf_cfg, "traffic-engineering")
        ospf_area = etree.SubElement(ospf_cfg, "area")
        etree.SubElement(ospf_area, "name").text = "0.0.0.0"
        for interface in interfaces:
            ospf_intf = etree.SubElement(ospf_area, "interface")
            etree.SubElement(ospf_intf, "name").text = interface
            if interface.startswith("lo0"):
               #interface = interface.replace("fi","ae")
               etree.SubElement(ospf_intf, "passive")
        if obj.e2_services_prot_config is not None:
            obj.e2_services_prot_config.append(ospf_cfg)
        else:
            routing_cfg = etree.Element("protocols")
            routing_cfg.append(ospf_cfg)
            obj.e2_services_prot_config = routing_cfg
    # end add_ospf_protocol_config_xml

    def add_e2_routing_options(self, obj, node_role, router_id):
        if obj.e2_routing_config is None:
            obj.e2_routing_config = self._get_routing_options_config_xml(\
                node_role, router_id)
    # end add_e2_routing_options

    def add_e2_chassis_config(self, obj):
        if obj.e2_chassis_config is None:
            obj.e2_chassis_config = self._get_chassis_config_xml()
    # end add_e2_chassis_config

    def add_e2_phy_logical_interface(self, obj, li_name, li_vlan_tag, \
                                     service_type, node_role, phy_mtu, \
                                     li_mtu, ps_config):
        if service_type == 'vpws-evpn' and node_role != 'access':
            ps_config = self.add_e2_phy_logical_interface_pwht_mx(obj, li_name,\
                    li_vlan_tag, service_type, node_role, phy_mtu, li_mtu,\
                    ps_config)
        else:
            self.add_e2_phy_logical_interface_mx(obj, li_name, li_vlan_tag, \
                    service_type, node_role, phy_mtu, li_mtu)
        return ps_config
    # end add_e2_phy_logical_interface

    def add_e2_phy_logical_interface_pwht_mx(self, li_name, li_vlan_tag, \
            service_type, node_role, phy_mtu, li_mtu, ps_config):
        ifname = li_name.split('.')
        ps_unit_config = self._get_interface_unit_config_xml(ifname[1], \
                                                             li_vlan_tag)
        if ps_config is not None:
            #ps_config.insert(0, ps_unit_config)
            ps_config.append(ps_unit_config)
        else:
            ps_config = self._get_interface_ps_config_xml(ifname[0])
            ps_config.append(ps_unit_config)
            if self.e2_phy_intf_config is not None:
                self.e2_phy_intf_config.insert(0, ps_config)
            else:
                intf_cfg = etree.Element("interfaces")
                intf_cfg.append(ps_config)
                self.e2_phy_intf_config = intf_cfg
        return ps_config

    # end add_e2_phy_logical_interface_pwht_mx

    def add_e2_phy_logical_interface_mx(self, obj, li_name, li_vlan_tag, \
                                        service_type, node_role, phy_mtu, \
                                        li_mtu):
        ifparts = li_name.split('.')
        ifd_name = ifparts[0]
        ifl_unit = ifparts[1]
        li1_config = li2_config = li_config = None
        if service_type == 'fabric':
            li1_config = self._get_interface_fabric_child_config_xml(ifd_name)
            li2_config = self._get_interface_fabric0_config_xml(ifl_unit, \
                                                                node_role)
        else:
            li_config = self._get_interface_config_xml(ifd_name, ifl_unit, \
                    li_vlan_tag, service_type, phy_mtu, li_mtu)
        if obj.e2_phy_intf_config is not None:
            if li_config is not None:
                obj.e2_phy_intf_config.insert(0, li_config)
            else:
                obj.e2_phy_intf_config.insert(0, li1_config)
                obj.e2_phy_intf_config.insert(0, li2_config)
        else:
            if li_config is not None:
                intf_cfg = etree.Element("interfaces")
                intf_cfg.append(li_config)
                obj.e2_phy_intf_config = intf_cfg
            else:
                intf_cfg = etree.Element("interfaces")
                intf_cfg.append(li1_config)
                intf_cfg.append(li2_config)
                obj.e2_phy_intf_config = intf_cfg
    # end add_e2_phy_logical_interface_mx

    def add_e2_lo0_config(self, obj, node_role, router_lpbk_id):
        li_config = self._get_interface_lo0_config_xml(node_role,
                                                       router_lpbk_id)
        if obj.e2_phy_intf_config is not None:
            obj.e2_phy_intf_config.insert(0, li_config)
        else:
            obj.e2_phy_intf_config.append(li_config)
    # end add_e2_lo0_config

    def add_e2_fabric_adjacency(self, obj, prot_conf):
        interfaces = ['ae0.0','lo0.0']
        area = 0
        if prot_conf != True:
            self.add_ospf_protocol_config_xml(obj, interfaces, area)
            self.add_ldp_protocol_config_xml(obj, interfaces)
    # end add_e2_fabric_adjacency

    def add_e2_services_l2ckt_config_xml(self, node_role, li_name, \
            li_vlan_tag, sindex, peer_router_id, no_control_word):
        self.add_e2_services_l2ckt_config_mx_xml(node_role, li_name, \
            li_vlan_tag, sindex, peer_router_id, no_control_word)
    # end add_e2_services_l2ckt_config_xml

    def add_e2_services_l2ckt_config_mx_xml(self, obj, node_role, li_name,
                                            li_vlan_tag, sindex,
                                            peer_router_id,
                                            no_control_word):
        circuit_id = sindex
        neighbor = peer_router_id
        if node_role == 'access':
            if peer_router_id == 0:
                neighbor = "200.200.200.200"
            self.add_l2ckt_protocol_config_xml(obj, circuit_id, neighbor, \
                    li_name, li_vlan_tag, no_control_word)
        elif node_role == 'provider-edge':
            if peer_router_id == 0:
                neighbor = "100.100.100.100"
            self.add_l2ckt_protocol_config_xml(obj, circuit_id, neighbor, \
                    li_name, li_vlan_tag, no_control_word)
    # end add_e2_services_l2ckt_config_mx_xml

    def add_e2_services_pwht_config_mx_xml(self, obj, node_role, li_name, \
                                           li_vlan_tag, sindex, \
                                           peer_router_id, no_control_word):
        circuit_id = sindex
        neighbor = peer_router_id
        if node_role == 'access':
            if peer_router_id == 0:
                neighbor = "200.200.200.200"
            self.add_l2ckt_protocol_config_xml(obj, circuit_id, neighbor,\
                    li_name, li_vlan_tag, no_control_word)
        else:
            if peer_router_id == 0:
                neighbor = "100.100.100.100"
            self.add_pwht_config_xml(obj, circuit_id, neighbor, li_name)
    # end add_e2_services_pwht_config_mx_xml

    def add_e2_services_l2ckt_config_mx_xml(self, obj, node_role, li_name, \
                                            li_vlan_tag, sindex, \
                                            peer_router_id, no_control_word):
        circuit_id = sindex
        neighbor = peer_router_id
        if node_role == 'access':
            if peer_router_id == 0:
                neighbor = "200.200.200.200"
            self.add_l2ckt_protocol_config_xml(obj, circuit_id, neighbor, \
                    li_name, li_vlan_tag, no_control_word)
        elif node_role == 'provider-edge':
            if peer_router_id == 0:
                neighbor = "100.100.100.100"
            self.add_l2ckt_protocol_config_xml(obj, circuit_id, neighbor, \
                    li_name, li_vlan_tag, no_control_word)
    # end add_e2_services_l2ckt_config_mx_xml

    def add_e2_services_l2vpn_config_xml(self, obj, node_role, li_name,
                                         li_vlan_tag, as_number, circuit_id
                                         local_site_id, remote_site_id):
        #rd is lo0.0 ipaddress:circuit_id or AS number:circuit_id
        rd = str(as_number) + ":" + str(circuit_id)
        # vrf_target is AS number:circuit_id
        vrf_target = str(as_number) + ":" + str(circuit_id)
        # site name is unique per endpoint
        site_name = "l2vpn" + str(circuit_id)
        self.add_l2vpn_protocol_config_xml(obj, li_name, rd, vrf_target,
                                           li_vlan_tag, site_name,
                                           local_site_id, remote_site_id)
    # end add_e2_services_l2vpn_config_xml

    def get_service_status(self, service_type, circuit_id, neigbor_id):
        if service_type == 'vpws-l2ckt' or service_type == 'vpws-evpn':
            #Fetch the l2circuit summary
            service_status_info = self.get_l2ckt_status(circuit_id, neigbor_id)
        elif service_type == 'vpws-l2vpn':
            #Fetch the l2vpn summary
            site_name = "l2vpn" + str(circuit_id)
            service_status_info = self.get_l2vpn_status(1, site_name)
        elif service_type == 'fabric-interface':
            #Fetch the fabric status
            service_status_info = {}
            service_status_info['service-status'] = 'Up'
        else:
            self._logger.error("could not fetch service status for type %s" % (
                                  service_type))
            service_status_info = {}

        return service_status_info

    def get_l2ckt_status(self, circuit_id, neigbor_id):
        service_status_info = {}
        service_status_info['service-status-reason'] = "Operational failure, no connections found"
        try:
            rpc_command = """
            <get-l2ckt-connection-information>
                <neighbor>dummy</neighbor>
                <summary/>
            </get-l2ckt-connection-information>"""
            rpc_command = rpc_command.replace('dummy', neigbor_id)
            l2ckt_sum = self.service_request_rpc(rpc_command)
            up_count = l2ckt_sum.xpath('//l2circuit-connection-information/l2circuit-neighbor/connections-summary/vc-up-count')[0].text
            down_count = l2ckt_sum.xpath('//l2circuit-connection-information/l2circuit-neighbor/connections-summary/vc-down-count')[0].text
            total_count = int(up_count) + int(down_count)

            rpc_command = """
            <get-l2ckt-connection-information>
                <neighbor>dummy</neighbor>
            </get-l2ckt-connection-information>"""
            rpc_command = rpc_command.replace('dummy', neigbor_id)
            res = self.service_request_rpc(rpc_command)

            neigh_id = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/neighbor-address')[0].text
            if total_count == 1 and int(up_count) > 0 :
                rem_neigh_id = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/connection/remote-pe')[0].text
            else:
                rem_neigh_id = "None"
            count = 0
            up_count = 0
            while count < total_count:
               service_circuit_id_block = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/connection/connection-id')[count].text
               circuit_id_str = service_circuit_id_block[service_circuit_id_block.find("(")+1:service_circuit_id_block.find(")")]
               service_circuit_id = re.search(r'\d+', circuit_id_str).group()
                service_status = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/connection/connection-status')[count].text
                if int(service_circuit_id) == circuit_id:
                    service_status_info = {}
                    service_status_reason = None
                    if service_status != 'Up':
                        service_status_reason = self._l2ckt_errors[service_status]
                        service_intf_name = service_circuit_id_block.split('(')[0]
                    else:
                        service_intf_name = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/connection/local-interface/interface-name')[up_count].text
                        service_intf_status = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/connection/local-interface/interface-status')[up_count].text
                        service_status_info['service-intf-status']   = service_intf_status
                    service_status_info['service-status']        = service_status
                    service_status_info['neighbor-id']           = neigh_id
                    service_status_info['remote-neighbor-id']    = neigh_id
                    service_status_info['service-intf-name']     = service_intf_name
                    if service_status_reason is not None:
                        service_status_info['service-status-reason'] = service_status_reason
                    return service_status_info
                if service_status == 'Up':
                    up_count += 1
                count += 1
            return service_status_info
        except:
            self._logger.error("error: could not fetch service status for %s: circuit-id %s" % (
                self.name, str(circuit_id)))
            return service_status_info

    def get_l2vpn_status(self, site_id, site_name):
        service_status_info = {}
        service_status_info['service-status-reason'] = "Operational failure, no connections found"
        try:
            rpc_command = """
            <get-l2vpn-connection-information>
                <instance>dummy</instance>
                <summary/>
            </get-l2vpn-connection-information>"""
            rpc_command = rpc_command.replace('dummy', site_name)
            l2vpn_sum = self.service_request_rpc(rpc_command)
            up_count = l2vpn_sum.xpath('//l2vpn-connection-information/instance/reference-site/connections-summary/vc-up-count')[0].text
            down_count = l2vpn_sum.xpath('//l2vpn-connection-information/instance/reference-site/connections-summary/vc-down-count')[0].text
            total_count = int(up_count) + int(down_count)
            #Now fetch the l2vpn information.
            service_data = new_ele('get-l2vpn-connection-information')
            rpc_command = """
            <get-l2vpn-connection-information>
                <instance>dummy</instance>
            </get-l2vpn-connection-information>"""
            rpc_command = rpc_command.replace('dummy', site_name)
            res = self.service_request_rpc(rpc_command)
            if total_count == 1 and int(up_count) > 0 :
                rem_neigh_id = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/remote-pe')[0].text
            else:
                rem_neigh_id = "None"
            count = 0
            while count < total_count:
               service_site_id_blk = res.xpath('//l2vpn-connection-information/instance/reference-site/local-site-id')[count].text
               site_id_str = service_site_id_blk.split()[0]
                if site_id_str == site_name:
                    service_status_info = {}
                    #service_site_id = int(service_site_id) - 1
                    service_status = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/connection-status')[count].text
                    service_status_reason = None
                    if service_status != 'Up':
                        service_status_reason = self._l2vpn_errors[service_status]
                        #service_intf_name = service_circuit_id_block.split('(')[0]
                        service_intf_name = "None"
                    else:
                        last_changed = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/last-change')[count].text
                        up_transitions = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/up-transitions')[count].text
                        service_intf_name = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/local-interface/interface-name')[count].text
                        service_intf_status = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/local-interface/interface-status')[count].text
                        service_status_info['service-intf-status']   = service_intf_status
                        service_status_info['last-change']           = last_changed
                        service_status_info['up-transitions']        = up_transitions
                        service_status_info['service-intf-name']     = service_intf_name
                    service_status_info['service-status']            = service_status
                    service_status_info['neighbor-id']               = rem_neigh_id
                    service_status_info['site-name']                 = site_name
                    service_status_info['site-id']                   = site_id
                    if service_status_reason is not None:
                        service_status_info['service-status-reason'] = service_status_reason
                        return service_status_info
                count += 1
            return service_status_info
        except:
            self._logger.error("error: could not fetch service status for %s : site-name %s" % (
                self.name, site_name))
            return service_status_info

# end MXE2Services

