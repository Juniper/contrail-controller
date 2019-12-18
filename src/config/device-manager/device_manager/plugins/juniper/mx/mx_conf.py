#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of netconf interface for physical router
configuration manager
"""

from builtins import str
import copy

from device_api.juniper_common_xsd import *

from .db import GlobalVRouterConfigDM, PhysicalInterfaceDM, \
    RoutingInstanceDM, ServiceInstanceDM, VirtualMachineInterfaceDM, \
    VirtualNetworkDM
from .dm_utils import DMUtils
from .juniper_conf import JuniperConf
from .juniper_conf import JunosInterface


class MxConf(JuniperConf):
    _products = ['mx', 'vmx']

    def __init__(self, logger, params={}):
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
    def is_product_supported(cls, name, role):
        if role and role.lower().startswith('e2-'):
            return False
        for product in cls._products or []:
            if name.lower().startswith(product.lower()):
                return True
        return False
    # end is_product_supported

    def add_pnf_logical_interface(self, junos_interface):
        if not self.interfaces_config:
            self.interfaces_config = Interfaces(comment=DMUtils.interfaces_comment())
        family = Family(inet=FamilyInet(address=[Address(name=junos_interface.ip)]))
        unit = Unit(name=junos_interface.unit, vlan_id=junos_interface.vlan_tag, family=family)
        interface = Interface(name=junos_interface.ifd_name)
        interface.add_unit(unit)
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
                ri_conf['si'] = si_obj
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
        for dest, next_hops in list(static_routes.items()):
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
        si = ri_conf.get("si")
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
        elif si:
            ri.set_comment(DMUtils.si_ri_comment(si))
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
            for pip in list(fip_map.values()):
                if pip["vrf_name"] not in public_vrf_ips:
                    public_vrf_ips[pip["vrf_name"]] = set()
                public_vrf_ips[pip["vrf_name"]].add(pip["floating_ip"])

            for public_vrf, fips in list(public_vrf_ips.items()):
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
        if vn:
            ps.set_comment(DMUtils.vn_ps_comment(vn, "Export"))
        elif si:
            ps.set_comment(DMUtils.si_ps_comment(si, "Export"))
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
        if vn:
            ps.set_comment(DMUtils.vn_ps_comment(vn, "Import"))
        elif si:
            ps.set_comment(DMUtils.si_ps_comment(si, "Import"))
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
            for fip_user_ip in list(fip_map.keys()):
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

            for pip, fip_vn in list(fip_map.items()):
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

        for ifd_name, interface_list in list(ifd_map.items()):
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
            comm = CommunityType(name=DMUtils.make_community_name(route_target))
            comm.add_members(route_target)
            self.policy_config.add_community(comm)
    # end set_route_targets_config

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        if is_delete:
            return self.send_conf(is_delete=True)
        if not self.ensure_bgp_config():
            return 0
        self.build_bgp_config()
        vn_dict = self.get_vn_li_map()
        self.physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l2_l3', 'irb', False)
        self.physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l3', 'lo0', True)
        vn_irb_ip_map = self.physical_router.get_vn_irb_ip_map()

        first_vrf = []
        pnfs = self.config_pnf_logical_interface()
        pnf_dict = pnfs[0]
        pnf_ris = pnfs[1]

        for vn_id, interfaces in list(vn_dict.items()):
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

# end PhycalRouterConfig
