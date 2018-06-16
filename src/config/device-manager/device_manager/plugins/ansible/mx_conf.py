#
# Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
#

from db import *
from dm_utils import DMUtils
from juniper_conf import JuniperConf
from juniper_conf import JunosInterface
from device_api.juniper_common_xsd import *
from ansible_conf import AnsibleConf

class MxConf(AnsibleConf):
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

    # TODO: Update as per new roles
    @classmethod
    def is_product_supported(cls, name, role):
        if role and role.lower().startswith('e2-'):
            return False
        for product in cls._products or []:
            if name.lower().startswith(product.lower()):
                return True
        return False
    # end is_product_supported

    def add_inet_public_vrf_filter(self, firewall_config, inet_type):
        firewall_config.set_family(inet_type)
        f = FirewallFilter(name=DMUtils.make_public_vrf_filter_name(inet_type))
        f.set_comment(DMUtils.public_vrf_filter_comment())
        firewall_config.add_firewall_filters(f)
        term = Term(name="default-term", then=Then(accept_or_reject=True))
        f.add_terms(term)
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
     network_id : this is used for configuring irb interfaces
     highest_encapsulation_priority: highest encapsulation configured
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
        highest_encapsulation_priority = \
                  ri_conf.get("highest_encapsulation_priority") or "MPLSoGRE"

        self.routing_instances[ri_name] = ri_conf
        self.ri_config = self.ri_config or []
        self.policy_config = self.policy_config or Policy(comment=DMUtils.policy_options_comment())
        ri = RoutingInstance(name=ri_name)
        if vn:
            is_nat = True if fip_map else False
            ri.set_comment(DMUtils.vn_ri_comment(vn, is_l2, is_l2_l3, is_nat, router_external))
        elif si:
            ri.set_comment(DMUtils.si_ri_comment(si))
        self.ri_config.append(ri)

        if router_external and is_l2 is False:
            ri.add_static_routes(
                Route(prefix="0.0.0.0", prefix_len=0,
                      comment=DMUtils.public_vrf_route_comment()))

        has_ipv6_prefixes = DMUtils.has_ipv6_prefixes(prefixes)
        has_ipv4_prefixes = DMUtils.has_ipv4_prefixes(prefixes)

        ri.set_is_public_network(router_external)
        if not is_l2:
            if prefixes and fip_map is None:
                for prefix in prefixes:
                    prefix_parts = prefix.split('/', 1)
                    ri.add_static_routes(
                        Route(prefix=prefix_parts[0],
                              prefix_len=int(prefix_parts[1])))
                    ri.add_prefixes(
                        Subnet(prefix=prefix_parts[0],
                               prefix_len=int(prefix_parts[1])))

            ri.set_routing_instance_type("vrf")
            if fip_map is None:
                for interface in interfaces:
                    ri.add_interfaces(LogicalInterface(name=interface.name))

        else:
            if highest_encapsulation_priority == "VXLAN":
                ri.set_routing_instance_type("virtual-switch")
            elif highest_encapsulation_priority in ["MPLSoGRE", "MPLSoUDP"]:
                ri.set_routing_instance_type("evpn")

        if fip_map is not None:
            ri.add_static_routes(
                Route(prefix="0.0.0.0", prefix_len=0,
                      next_hop=interfaces[0].name,
                      comment=DMUtils.fip_ingress_comment()))
            ri.add_interfaces(LogicalInterface(name=interfaces[0].name))

            public_vrf_ips = {}
            for pip in fip_map.values():
                if pip["vrf_name"] not in public_vrf_ips:
                    public_vrf_ips[pip["vrf_name"]] = set()
                public_vrf_ips[pip["vrf_name"]].add(pip["floating_ip"])

            for public_vrf, fips in public_vrf_ips.items():
                ri_public = RoutingInstance(name=public_vrf)
                self.ri_config.append(ri_public)
                ri_public.add_interfaces(
                    LogicalInterface(name=interfaces[1].name))

                for fip in fips:
                    ri_public.add_static_routes(
                        Route(prefix=fip,
                              prefix_len=32,
                              next_hop=interfaces[1].name,
                              comment=DMUtils.fip_egress_comment()))

        # add policies for export route targets
        p = PolicyRule(name=DMUtils.make_export_name(ri_name))
        if vn:
            p.set_comment(DMUtils.vn_ps_comment(vn, "Export"))
        elif si:
            p.set_comment(DMUtils.si_ps_comment(si, "Export"))
        then = Then()
        p.add_term(Term(name="t1", then=then))
        for route_target in export_targets:
            then.add_community(
                DMUtils.make_community_name(route_target))
        then.set_accept_or_reject(fip_map is None)
        self.policy_config.add_policy_rule(p)

        # add policies for import route targets
        p = PolicyRule(name=DMUtils.make_import_name(ri_name))
        if vn:
            p.set_comment(DMUtils.vn_ps_comment(vn, "Import"))
        elif si:
            p.set_comment(DMUtils.si_ps_comment(si, "Import"))
        from_ = From()
        p.add_term(Term(name="t1", fromxx=from_))
        for route_target in import_targets:
            from_.add_community(
                DMUtils.make_community_name(route_target))
        term.set_then(Then(accept_or_reject=True))
        p.set_then(Then(accept_or_reject=False))
        self.policy_config.add_policy_rule(p)

        # add firewall config for public VRF
        if router_external and is_l2 is False:
            self.firewall_config = self.firewall_config or Firewall(comment=DMUtils.firewall_comment())
            if has_ipv4_prefixes and not self.inet4_forwarding_filter:
                # create single instance inet4 filter
                self.inet4_forwarding_filter = self.add_inet_public_vrf_filter(
                    self.firewall_config, "inet")
            if has_ipv6_prefixes and not self.inet6_forwarding_filter:
                # create single instance inet6 filter
                self.inet6_forwarding_filter = self.add_inet_public_vrf_filter(
                    self.firewall_config, "inet6")
            if has_ipv4_prefixes:
                # add terms to inet4 filter
                term = self.add_inet_filter_term(ri_name, prefixes, "inet4")
                # insert before the last term
                terms = self.inet4_forwarding_filter.get_terms()
                terms = [term] + (terms or [])
                self.inet4_forwarding_filter.set_terms(terms)
            if has_ipv6_prefixes:
                # add terms to inet6 filter
                term = self.add_inet_filter_term(ri_name, prefixes, "inet6")
                # insert before the last term
                terms = self.inet6_forwarding_filter.get_terms()
                terms = [term] + (terms or [])
                self.inet6_forwarding_filter.set_terms(terms)

        if fip_map is not None:
            self.firewall_config = self.firewall_config or Firewall(comment=DMUtils.firewall_comment())
            f = FirewallFilter(name=DMUtils.make_private_vrf_filter_name(ri_name))
            f.set_comment(DMUtils.vn_firewall_comment(vn, "private"))
            self.firewall_config.add_firewall_filters(f)

            term = Term(name=DMUtils.make_vrf_term_name(ri_name))
            from_ = From()
            for fip_user_ip in fip_map.keys():
                from_.add_source_address(fip_user_ip)
            term.set_from(from_)
            term.set_then(Then(routing_instance=[ri_name]))
            f.add_terms(term)

            term = Term(name="default-term", then=Then(accept_or_reject=True))
            f.add_terms(term)

            intf_unit = LogicalInterface(name=str(network_id),
                                         comment=DMUtils.vn_irb_fip_inet_comment(vn))
            intf_unit.set_family("inet")
            intf_unit.add_firewall_filters(DMUtils.make_private_vrf_filter_name(ri_name))
            self.ri_config.add_routing_interfaces(intf_unit)

        if gateways is not None:
            for (ip, gateway) in gateways:
                ri.add_gateways(GatewayRoute(ip_address=get_subnet_for_cidr(ip),
                                             gateway=get_subnet_for_cidr(gateway)))
        # add L2 EVPN and BD config
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            if highest_encapsulation_priority == "VXLAN":
                self.vlans_config = self.vlans_config or []
                vlan = Vlan(name=DMUtils.make_bridge_name(vni), vlan_id='none',
                            vxlan_id=vni, vlan_or_bridge_domain=False)
                vlan.set_comment(DMUtils.vn_bd_comment(vn, "VXLAN"))
                self.vlans_config.append(vlan)
                for interface in interfaces:
                     vlan.add_interfaces(LogicalInterface(name=interface.name))
            elif highest_encapsulation_priority in ["MPLSoGRE", "MPLSoUDP"]:
                ri.set_vlan_id('none')
                self.evpn_config = self.evpn_config or []
                evpn = Evpn(encapsulation=highest_encapsulation_priority)
                evpn.set_comment(DMUtils.vn_evpn_comment(vn, highest_encapsulation_priority))
                for interface in interfaces:
                     evpn.add_interfaces(LogicalInterface(name=interface.name))

            self.build_l2_evpn_interface_config(self.interfaces_config, interfaces, vn)

        # fip services config
        if fip_map is not None:
            nat_rules = NatRules(allow_overlapping_nat_pools=True)
            nat_rules.set_comment(DMUtils.nat_comment())
            ri.add_nat_rules(nat_rules)
            snat_rule = NatRule(name=DMUtils.make_snat_rule_name(ri_name),
                                direction="input")
            snat_rule.set_comment(DMUtils.snat_rule_comment())
            nat_rules.add_rules(snat_rule)
            dnat_rule = NatRule(name=DMUtils.make_dnat_rule_name(ri_name),
                                direction="output")
            dnat_rule.set_comment(DMUtils.dnat_rule_comment())
            nat_rules.add_rule(dnat_rule)

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
                then_.set_translation_type('basic_nat44')

                term = Term(name=DMUtils.make_ip_term_name(fip))
                dnat_rule.set_term(term)

                # public ip
                from_ = From(destination_address=[fip + "/32"])
                term.set_from(from_)
                # private ip
                then_ = Then()
                term.set_then(then_)
                then_.set_translation_type('dnat_44')

            intf_unit = LogicalInterface(
                name=interfaces[0].unit,
                comment=DMUtils.service_intf_comment("Ingress"))
            intf_unit.set_family("inet")
            ri.add_service_interfaces(intf_unit)

            intf_unit = LogicalInterface(
                name=interfaces[1].unit,
                comment=DMUtils.service_intf_comment("Egress"))
            intf_unit.set_family("inet")
            ri.add_service_interfaces(intf_unit)

        for target in import_targets:
            ri.add_import_targets(target)

        for target in export_targets:
            ri.add_export_targets(target)
    # end add_routing_instance

    def build_l2_evpn_interface_config(self, interfaces_config, interfaces, vn=None):
        ifd_map = {}
        for interface in interfaces:
            ifd_map.setdefault(interface.ifd_name, []).append(interface)

        for ifd_name, interface_list in ifd_map.items():
            intf = PhysicalInterface(name=ifd_name)
            interfaces_config.append(intf)
            if interface_list[0].get_vlan_tag() is None:
                if (len(interface_list) > 1):
                    self._logger.error(
                        "invalid logical interfaces config for ifd %s" % (
                            ifd_name))
                    continue
                intf.add_interfaces(LogicalInterface(
                    name=interface_list[0].unit,
                    comment=DMUtils.l2_evpn_intf_unit_comment(vn, False),
                    family='bridge'))
            else:
                for interface in interface_list:
                    intf.add_interfaces(LogicalInterface(
                        name=interface.unit,
                        comment=DMUtils.l2_evpn_intf_unit_comment(vn, True, interface.vlan_tag),
                        vlan_tag=str(interface.vlan_tag)))
    # end build_l2_evpn_interface_config

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
                        ri_conf['highest_encapsulation_priority'] = \
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
                    self.add_routing_instance(ri_conf)
        self.set_as_config()
        self.set_bgp_group_config()
        return self.send_conf()
    # end push_conf

    @staticmethod
    def get_subnet_for_cidr(cidr):
        cidr_parts = cidr.split('/', 1)
        return Subnet(prefix=cidr_parts[0],
                      prefix_len=int(cidr_parts[1]))

    # end get_subnet_for_cidr

# end PhycalRouterConfig
