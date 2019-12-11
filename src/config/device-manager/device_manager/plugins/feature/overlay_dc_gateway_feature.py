#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
Telemetry feature implementation.

This file contains implementation of abstract config generation for
telemetry feature
"""

from builtins import str
from collections import OrderedDict

from abstract_device_api.abstract_device_xsd import *
import db
from dm_utils import DMUtils
from dm_utils import JunosInterface
from feature_base import FeatureBase


class OverlayDcGatewayFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'dc-gateway'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.feature_config = None

        self.irb_interfaces = []
        self.internal_vn_ris = []

        self.ri_map = {}
        self.pi_map = {}

        self.firewall_config = None
        self.vlan_map = {}

        self.is_pr_evpn = None
        self.evpn = None

        super(OverlayDcGatewayFeature, self).__init__(logger, physical_router,
                                                      configs)
    # end __init__

    def _set_internal_vn_irb_config(self):
        if self.internal_vn_ris and self.irb_interfaces:
            for int_ri in self.internal_vn_ris:
                lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(
                    int_ri.name)
                lr = db.LogicalRouterDM.get(lr_uuid)
                if not lr:
                    continue
                vn_list = lr.get_connected_networks(include_internal=False)
                for vn in vn_list:
                    vn_obj = db.VirtualNetworkDM.get(vn)
                    if vn_obj is None or vn_obj.vn_network_id is None:
                        continue
                    irb_name = "irb." + str(vn_obj.vn_network_id)
                    if irb_name in self.irb_interfaces:
                        self._add_ref_to_list(int_ri.get_routing_interfaces(),
                                              irb_name)
    # end _set_internal_vn_irb_config

    def _set_default_pi(self, name, interface_type=None):
        if name in self.pi_map:
            pi, li_map = self.pi_map[name]
            if interface_type:
                pi.set_interface_type(interface_type)
        else:
            pi = PhysicalInterface(name=name, interface_type=interface_type)
            li_map = {}
            self.pi_map[name] = (pi, li_map)
        return pi, li_map
    # end _set_default_pi

    def set_default_li(self, li_map, name, unit):
        if name in li_map:
            li = li_map[name]
        else:
            li = LogicalInterface(name=name, unit=unit)
            li_map[name] = li
        return li
    # end _set_default_li

    # lo0 interface in RI for route lookup to happen for Inter VN traffic
    # qfx10k pfe limitation
    def _add_bogus_lo0(self, ri, network_id, vn):
        ifl_num = 1000 + int(network_id)
        lo_intf, li_map = self._set_default_pi('lo0', 'loopback')
        intf_name = 'lo0.' + str(ifl_num)
        intf_unit = self.set_default_li(li_map, intf_name, ifl_num)
        intf_unit.set_comment(DMUtils.l3_bogus_lo_intf_comment(vn))
        self._add_ip_address(intf_unit, "127.0.0.1")
        self._add_ref_to_list(ri.get_loopback_interfaces(), intf_name)
    # end _add_bogus_lo0

    def _add_irb_config(self, ri_conf):
        vn = ri_conf.get("vn")
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        gateways = ri_conf.get("gateways", [])
        network_id = ri_conf.get("network_id", None)
        irb_intf, li_map = self.set_default_pi('irb', 'irb')
        self._logger.info("Vn=" + vn.name + ", IRB: " + str(gateways) +
                          ", pr=" + self._physical_router.name)
        if gateways is not None:
            intf_unit = self.set_default_li(li_map,
                                            'irb.' + str(network_id),
                                            network_id)
            if vn.has_ipv6_subnet is True:
                intf_unit.set_is_virtual_router(True)
            intf_unit.set_comment(DMUtils.vn_irb_comment(vn, False, is_l2_l3))
            for (irb_ip, gateway) in gateways:
                if len(gateway) and gateway != '0.0.0.0':
                    intf_unit.set_gateway(gateway)
                    self._add_ip_address(intf_unit, irb_ip, gateway=gateway)
                else:
                    self._add_ip_address(intf_unit, irb_ip)
    # end _add_irb_config

    def _is_bgp_evpn(self):
        if self.is_pr_evpn is None:
            self.is_pr_evpn = self._is_evpn(self._physical_router)
        return self.is_pr_evpn

    def _attach_irb(self, ri_conf, ri):
        if not self._is_gateway():
            return
        is_l2 = ri_conf.get("is_l2", False)
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        vni = ri_conf.get("vni", None)
        network_id = ri_conf.get("network_id", None)
        if (is_l2 and vni is not None and
                self._is_bgp_evpn()):
            if is_l2_l3:
                self.irb_interfaces.append("irb." + str(network_id))
    # end _attach_irb

    def _add_inet_public_vrf_filter(self, firewall_config, inet_type):
        firewall_config.set_family(inet_type)
        f = FirewallFilter(name=DMUtils.make_public_vrf_filter_name(inet_type))
        f.set_comment(DMUtils.public_vrf_filter_comment())
        firewall_config.add_firewall_filters(f)
        term = Term(name="default-term", then=Then(accept_or_reject=True))
        f.add_terms(term)
        return f
    # end _add_inet_public_vrf_filter

    def _init_evpn_config(self, encapsulation='vxlan'):
        if not self.ri_map:
            # no vn config then no need to configure evpn
            return
        if self.evpn:
            # evpn init done
            return
        self.evpn = Evpn(encapsulation=encapsulation)
    # end _init_evpn_config

    def _add_inet_filter_term(self, ri_name, prefixes, inet_type):
        if inet_type == 'inet6':
            prefixes = DMUtils.get_ipv6_prefixes(prefixes)
        else:
            prefixes = DMUtils.get_ipv4_prefixes(prefixes)

        from_ = From()
        for prefix in prefixes:
            from_.add_destination_address(self.get_subnet_for_cidr(prefix))
        then_ = Then()
        then_.add_routing_instance(ri_name)
        return Term(name=DMUtils.make_vrf_term_name(ri_name),
                    fromxx=from_, then=then_)
    # end _add_inet_filter_term

    def _is_default_sg(self, match):
        if (not match.get_dst_address()) or \
           (not match.get_dst_port()) or \
           (not match.get_ethertype()) or \
           (not match.get_src_address()) or \
           (not match.get_src_port()) or \
           (not match.get_protocol()):
            return False
        if not match.get_dst_address().get_subnet():
            return False
        if ((str(match.get_dst_address().get_subnet().get_ip_prefix()) ==
             "0.0.0.0") or
            (str(match.get_dst_address().get_subnet().get_ip_prefix()) ==
             "::")) and \
           (str(match.get_dst_address().get_subnet().get_ip_prefix_len()) ==
            "0") and \
           (str(match.get_dst_port().get_start_port()) == "0") and \
           (str(match.get_dst_port().get_end_port()) == "65535") and \
           ((str(match.get_ethertype()) == "IPv4") or
            (str(match.get_ethertype()) == "IPv6")) and \
           (not match.get_src_address().get_subnet()) and \
           (not match.get_src_address().get_subnet_list()) and \
           (str(match.get_src_port().get_start_port()) == "0") and \
           (str(match.get_src_port().get_end_port()) == "65535") and \
           (str(match.get_protocol()) == "any"):
            return True
        return False
    # end _is_default_sg

    def _add_addr_term(self, term, addr_match, is_src):
        if not addr_match:
            return None
        subnet = addr_match.get_subnet()
        if not subnet:
            return None
        subnet_ip = subnet.get_ip_prefix()
        subnet_len = subnet.get_ip_prefix_len()
        if not subnet_ip or not subnet_len:
            return None
        from_ = term.get_from() or From()
        term.set_from(from_)
        if is_src:
            from_.add_source_address(Subnet(prefix=subnet_ip,
                                            prefix_len=subnet_len))
        else:
            from_.add_destination_address(Subnet(prefix=subnet_ip,
                                                 prefix_len=subnet_len))
    # end _add_addr_term

    def _add_port_term(self, term, port_match, is_src):
        if not port_match:
            return None
        start_port = port_match.get_start_port()
        end_port = port_match.get_end_port()
        if not start_port or not end_port:
            return None
        port_str = str(start_port) + "-" + str(end_port)
        from_ = term.get_from() or From()
        term.set_from(from_)
        if is_src:
            from_.add_source_ports(port_str)
        else:
            from_.add_destination_ports(port_str)
    # end _add_port_term

    def _add_protocol_term(self, term, protocol_match):
        if not protocol_match or protocol_match == 'any':
            return None
        from_ = term.get_from() or From()
        term.set_from(from_)
        from_.set_ip_protocol(protocol_match)
    # end _add_protocol_term

    def _add_filter_term(self, ff, name):
        term = Term()
        term.set_name(name)
        ff.add_terms(term)
        term.set_then(Then(accept_or_reject=True))
        return term
    # end _add_filter_term

    def _add_dns_dhcp_terms(self, ff):
        port_list = [67, 68, 53]
        term = Term()
        term.set_name("allow-dns-dhcp")
        from_ = From()
        from_.set_ip_protocol("udp")
        term.set_from(from_)
        for port in port_list:
            from_.add_source_ports(str(port))
        term.set_then(Then(accept_or_reject=True))
        ff.add_terms(term)
    # end _add_dns_dhcp_terms

    def _add_ether_type_term(self, ff, ether_type_match):
        if not ether_type_match:
            return None
        term = Term()
        from_ = From()
        term.set_from(from_)
        term.set_name("ether-type")
        from_.set_ether_type(ether_type_match.lower())
        term.set_then(Then(accept_or_reject=True))
        ff.add_terms(term)
    # end _add_ether_type_term

    def _has_terms(self, rule):
        match = rule.get_match_condition()
        if not match:
            return False
        # return False if it is default SG, no filter is applied
        if self._is_default_sg(match):
            return False
        return (match.get_dst_address() or match.get_dst_port() or
                match.get_ethertype() or match.get_src_address() or
                match.get_src_port() or (match.get_protocol() and
                                         match.get_protocol() != 'any'))
    # end _has_terms

    def _build_firewall_filters(self, sg, acl, is_egress=False):
        acl_rule_present = False
        if not sg or not acl or not acl.vnc_obj:
            return
        acl = acl.vnc_obj
        entries = acl.get_access_control_list_entries()
        if not entries:
            return
        rules = entries.get_acl_rule() or []
        if not rules:
            return
        self.firewall_config = self.firewall_config or Firewall(
            DMUtils.firewall_comment())
        for rule in rules:
            if not self._has_terms(rule):
                continue
            match = rule.get_match_condition()
            if not match:
                continue
            acl_rule_present = True
            break

        if acl_rule_present:
            filter_name = DMUtils.make_sg_firewall_name(sg.name, acl.uuid)
            f = FirewallFilter(name=filter_name)
            f.set_comment(DMUtils.make_sg_firewall_comment(sg.name, acl.uuid))
            # allow arp ether type always
            self._add_ether_type_term(f, 'arp')
            # allow dhcp/dns always
            self._add_dns_dhcp_terms(f)
            for rule in rules:
                if not self._has_terms(rule):
                    continue
                match = rule.get_match_condition()
                if not match:
                    continue
                rule_uuid = rule.get_rule_uuid()
                dst_addr_match = match.get_dst_address()
                dst_port_match = match.get_dst_port()
                # ether_type_match = match.get_ethertype()
                protocol_match = match.get_protocol()
                src_addr_match = match.get_src_address()
                # src_port_match = match.get_src_port()
                term = self._add_filter_term(f, rule_uuid)
                self._add_addr_term(term, dst_addr_match, False)
                self._add_addr_term(term, src_addr_match, True)
                self._add_port_term(term, dst_port_match, False)
                # source port match is not needed for now (BMS source port)
                # self._add_port_term(term, src_port_match, True)
                self._add_protocol_term(term, protocol_match)
            self.firewall_config.add_firewall_filters(f)
    # end _build_firewall_filters

    def _get_firewall_filters(self, sg, acl, is_egress=False):
        acl_rule_present = False
        if not sg or not acl or not acl.vnc_obj:
            return []
        acl = acl.vnc_obj
        entries = acl.get_access_control_list_entries()
        if not entries:
            return []
        rules = entries.get_acl_rule() or []
        if not rules:
            return []
        filter_names = []
        for rule in rules:
            if not self._has_terms(rule):
                continue
            match = rule.get_match_condition()
            if not match:
                continue
            # rule_uuid = rule.get_rule_uuid()
            ether_type_match = match.get_ethertype()
            if not ether_type_match:
                continue
            if 'ipv6' in ether_type_match.lower():
                continue
            acl_rule_present = True
            break

        if acl_rule_present:
            filter_name = DMUtils.make_sg_firewall_name(sg.name, acl.uuid)
            filter_names.append(filter_name)
        return filter_names
    # end _get_firewall_filters

    def _get_configured_filters(self, sg):
        if not sg:
            return []
        filter_names = []
        acls = sg.access_control_lists
        for acl in acls or []:
            acl = db.AccessControlListDM.get(acl)
            if acl and not acl.is_ingress:
                fnames = self._get_firewall_filters(sg, acl)
                filter_names += fnames
        return filter_names
    # end _get_configured_filters

    def _attach_acls(self, interface, unit):
        pr = self._physical_router
        if not pr:
            return

        sg_list = []
        for vpg_uuid in pr.virtual_port_groups or []:
            vpg_obj = db.VirtualPortGroupDM.get(vpg_uuid)
            if not vpg_obj:
                continue

            sg_list_temp = vpg_obj.get_attached_sgs(unit.get_vlan_tag(),
                                                    interface)
            for sg in sg_list_temp:
                if sg not in sg_list:
                    sg_list.append(sg)

        for sg in sg_list or []:
            acls = sg.access_control_lists
            for acl in acls or []:
                acl = db.AccessControlListDM.get(acl)
                if acl and not acl.is_ingress:
                    self._build_firewall_filters(sg, acl)

        if interface.li_uuid:
            interface = db.LogicalInterfaceDM.find_by_name_or_uuid(
                interface.li_uuid)
            if interface:
                sg_list += interface.get_attached_sgs()

        filter_list = []
        for sg in sg_list:
            flist = self._get_configured_filters(sg)
            filter_list += flist
        if filter_list:
            for fname in filter_list:
                unit.add_firewall_filters(fname)
    # end _attach_acls

    def _build_l2_evpn_interface_config(self, interfaces, vn, vlan_conf):
        ifd_map = {}
        for interface in interfaces:
            ifd_map.setdefault(interface.ifd_name, []).append(interface)

        for ifd_name, interface_list in list(ifd_map.items()):
            untagged = [int(i.port_vlan_tag) for i in interface_list
                        if i.is_untagged()]
            if len(untagged) > 1:
                self._logger.error(
                    "Only one untagged interface is allowed on a ifd %s" %
                    ifd_name)
                continue
            tagged = [int(i.vlan_tag) for i in interface_list
                      if not i.is_untagged()]
            if self._is_enterprise_style(self._physical_router):
                if len(untagged) > 0 and len(tagged) > 0:
                    self._logger.error(
                        "Enterprise style config: Can't have tagged and "
                        "untagged interfaces for same VN on same PI %s" %
                        ifd_name)
                    continue
            elif len(set(untagged) & set(tagged)) > 0:
                self._logger.error(
                    "SP style config: Can't have tagged and untagged "
                    "interfaces with same Vlan-id on same PI %s" %
                    ifd_name)
                continue
            _, li_map = self._set_default_pi(ifd_name)
            for interface in interface_list:
                if interface.is_untagged():
                    is_tagged = False
                    vlan_tag = str(interface.port_vlan_tag)
                else:
                    is_tagged = True
                    vlan_tag = str(interface.vlan_tag)
                unit_name = ifd_name + "." + str(interface.unit)
                unit = self._set_default_li(li_map, unit_name, interface.unit)
                unit.set_comment(DMUtils.l2_evpn_intf_unit_comment(
                    vn, is_tagged, vlan_tag))
                unit.set_is_tagged(is_tagged)
                unit.set_vlan_tag(vlan_tag)
                # attach acls
                self._attach_acls(interface, unit)
                if vlan_conf:
                    self._add_ref_to_list(vlan_conf.get_interfaces(),
                                          unit_name)
    # end _build_l2_evpn_interface_config

    def _add_ri_vlan_config(self, vrf_name, vni):
        vlan = Vlan(name=vrf_name[1:], vxlan_id=vni)
        self.vlan_map[vlan.get_name()] = vlan
    # end _add_ri_vlan_config

    def _add_routing_instance(self, ri_conf):
        ri_name = ri_conf.get("ri_name")
        vn = ri_conf.get("vn")
        is_l2 = ri_conf.get("is_l2", False)
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        import_targets = ri_conf.get("import_targets", set())
        export_targets = ri_conf.get("export_targets", set())
        prefixes = ri_conf.get("prefixes", [])
        gateways = ri_conf.get("gateways", [])
        router_external = ri_conf.get("router_external", False)
        # connected_dci_network = ri_conf.get("connected_dci_network")
        interfaces = ri_conf.get("interfaces", [])
        vni = ri_conf.get("vni", None)
        fip_map = ri_conf.get("fip_map", None)
        network_id = ri_conf.get("network_id", None)
        is_internal_vn = True if '_contrail_lr_internal_vn_' in vn.name else \
            False
        encapsulation_priorities = \
            ri_conf.get("encapsulation_priorities") or ["MPLSoGRE"]
        highest_encapsulation = encapsulation_priorities[0]

        ri = RoutingInstance(name=ri_name)

        if vn:
            is_nat = True if fip_map else False
            ri.set_comment(DMUtils.vn_ri_comment(vn, is_l2, is_l2_l3, is_nat,
                                                 router_external))
            if is_internal_vn:
                lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(
                    ri_name)
                lr = db.LogicalRouterDM.get(lr_uuid)
                if lr:
                    ri.set_description("__contrail_%s_%s" % (lr.name, lr_uuid))

        self.ri_map[ri_name] = ri

        ri.set_virtual_network_id(str(network_id))
        ri.set_vxlan_id(str(vni))
        ri.set_virtual_network_is_internal(is_internal_vn)
        ri.set_is_public_network(router_external)
        if is_l2_l3:
            ri.set_virtual_network_mode('l2-l3')
        elif is_l2:
            ri.set_virtual_network_mode('l2')
        else:
            ri.set_virtual_network_mode('l3')

        has_ipv6_prefixes = DMUtils.has_ipv6_prefixes(prefixes)
        has_ipv4_prefixes = DMUtils.has_ipv4_prefixes(prefixes)

        if not is_l2:
            ri.set_routing_instance_type("vrf")
            if fip_map is None:
                if not is_internal_vn:
                    for interface in interfaces:
                        self._add_ref_to_list(ri.get_interfaces(),
                                              interface.name)
                    if prefixes:
                        for prefix in prefixes:
                            ri.add_static_routes(self._get_route_for_cidr(
                                prefix))
                            ri.add_prefixes(self._get_subnet_for_cidr(prefix))
        else:
            if highest_encapsulation == "VXLAN":
                ri.set_routing_instance_type("virtual-switch")
            elif highest_encapsulation in ["MPLSoGRE", "MPLSoUDP"]:
                ri.set_routing_instance_type("evpn")

        if is_internal_vn:
            self.internal_vn_ris.append(ri)

        if is_internal_vn or router_external:
            self._add_bogus_lo0(ri, network_id, vn)

        if self.is_gateway() and is_l2_l3 and not is_internal_vn:
            self._add_irb_config(ri_conf)
            self._attach_irb(ri_conf, ri)

        if fip_map is not None:
            self._add_ref_to_list(ri.get_interfaces(), interfaces[0].name)

            public_vrf_ips = {}
            for pip in list(fip_map.values()):
                if pip["vrf_name"] not in public_vrf_ips:
                    public_vrf_ips[pip["vrf_name"]] = set()
                public_vrf_ips[pip["vrf_name"]].add(pip["floating_ip"])

            for public_vrf, fips in list(public_vrf_ips.items()):
                ri_public = RoutingInstance(name=public_vrf)
                self.ri_map[public_vrf] = ri_public
                self._add_ref_to_list(ri_public.get_interfaces(),
                                      interfaces[1].name)
                floating_ips = []
                for fip in fips:
                    ri_public.add_static_routes(
                        Route(prefix=fip,
                              prefix_len=32,
                              next_hop=interfaces[1].name,
                              comment=DMUtils.fip_egress_comment()))
                    floating_ips.append(FloatingIpMap(floating_ip=fip + "/32"))
                ri_public.add_floating_ip_list(FloatingIpList(
                    public_routing_instance=public_vrf,
                    floating_ips=floating_ips))

        # add firewall config for public VRF
        if router_external and is_l2 is False:
            self.firewall_config = self.firewall_config or Firewall(
                comment=DMUtils.firewall_comment())
            if has_ipv4_prefixes and not self.inet4_forwarding_filter:
                # create single instance inet4 filter
                self.inet4_forwarding_filter = \
                    self._add_inet_public_vrf_filter(
                        self.firewall_config, "inet")
            if has_ipv6_prefixes and not self.inet6_forwarding_filter:
                # create single instance inet6 filter
                self.inet6_forwarding_filter = \
                    self._add_inet_public_vrf_filter(
                        self.firewall_config, "inet6")
            if has_ipv4_prefixes:
                # add terms to inet4 filter
                term = self._add_inet_filter_term(ri_name, prefixes, "inet4")
                # insert before the last term
                terms = self.inet4_forwarding_filter.get_terms()
                terms = [term] + (terms or [])
                self.inet4_forwarding_filter.set_terms(terms)
            if has_ipv6_prefixes:
                # add terms to inet6 filter
                term = self._add_inet_filter_term(ri_name, prefixes, "inet6")
                # insert before the last term
                terms = self.inet6_forwarding_filter.get_terms()
                terms = [term] + (terms or [])
                self.inet6_forwarding_filter.set_terms(terms)

        # add firewall config for DCI Network
        if fip_map is not None:
            self.firewall_config = self.firewall_config or Firewall(
                comment=DMUtils.firewall_comment())
            f = FirewallFilter(
                name=DMUtils.make_private_vrf_filter_name(ri_name))
            f.set_comment(DMUtils.vn_firewall_comment(vn, "private"))
            self.firewall_config.add_firewall_filters(f)

            term = Term(name=DMUtils.make_vrf_term_name(ri_name))
            from_ = From()
            for fip_user_ip in list(fip_map.keys()):
                from_.add_source_address(self._get_subnet_for_cidr(
                    fip_user_ip))
            term.set_from(from_)
            term.set_then(Then(routing_instance=[ri_name]))
            f.add_terms(term)

            irb_intf, li_map = self._set_default_pi('irb', 'irb')
            intf_name = 'irb.' + str(network_id)
            intf_unit = self._set_default_li(li_map, intf_name, network_id)
            intf_unit.set_comment(DMUtils.vn_irb_fip_inet_comment(vn))
            intf_unit.set_family("inet")
            intf_unit.add_firewall_filters(
                DMUtils.make_private_vrf_filter_name(ri_name))
            self._add_ref_to_list(ri.get_routing_interfaces(), intf_name)

        if gateways is not None:
            for (ip, gateway) in gateways:
                ri.add_gateways(GatewayRoute(
                    ip_address=self._get_subnet_for_cidr(ip),
                    gateway=self._get_subnet_for_cidr(gateway)))

        # add L2 EVPN and BD config
        if (is_l2 and vni is not None and
                self._is_bgp_evpn()):
            vlan = None
            if highest_encapsulation == "VXLAN":
                if not is_internal_vn:
                    vlan = Vlan(name=DMUtils.make_bridge_name(vni),
                                vxlan_id=vni)
                    vlan.set_comment(DMUtils.vn_bd_comment(vn, "VXLAN"))
                    desc = "Virtual Network - %s" % vn.name
                    vlan.set_description(desc)
                    self.vlan_map[vlan.get_name()] = vlan
                    for interface in interfaces:
                        self._add_ref_to_list(vlan.get_interfaces(),
                                              interface.name)
                    if is_l2_l3 and self.is_gateway():
                        # network_id is unique, hence irb
                        irb_intf = "irb." + str(network_id)
                        self._add_ref_to_list(vlan.get_interfaces(), irb_intf)
            elif highest_encapsulation in ["MPLSoGRE", "MPLSoUDP"]:
                self._init_evpn_config(highest_encapsulation)
                self.evpn.set_comment(DMUtils.vn_evpn_comment(
                    vn, highest_encapsulation))
                for interface in interfaces:
                    self._add_ref_to_list(self.evpn.get_interfaces(),
                                          interface.name)

            self._build_l2_evpn_interface_config(interfaces, vn, vlan)

        if (not is_l2 and vni is not None and
                self._is_bgp_evpn() == True):
            self._init_evpn_config()
            if not is_internal_vn:
                # add vlans
                self._add_ri_vlan_config(ri_name, vni)

        if (not is_l2 and not is_l2_l3 and gateways):
            ifl_num = 1000 + int(network_id)
            lo_intf, li_map = self._set_default_pi('lo0', 'loopback')
            intf_name = 'lo0.' + str(ifl_num)
            intf_unit = self._set_default_li(li_map, intf_name, ifl_num)
            intf_unit.set_comment(DMUtils.l3_lo_intf_comment(vn))
            for (lo_ip, _) in gateways:
                # subnet = lo_ip
                (ip, _) = lo_ip.split('/')
                if ':' in lo_ip:
                    lo_ip = ip + '/' + '128'
                else:
                    lo_ip = ip + '/' + '32'
                self._add_ip_address(intf_unit, lo_ip)
            self._add_ref_to_list(ri.get_loopback_interfaces(), intf_name)

        # fip services config
        if fip_map is not None:
            nat_rules = NatRules(allow_overlapping_nat_pools=True,
                                 name=DMUtils.make_services_set_name(ri_name),
                                 comment=DMUtils.service_set_comment(vn))
            ri.set_nat_rules(nat_rules)
            snat_rule = NatRule(
                name=DMUtils.make_snat_rule_name(ri_name),
                comment=DMUtils.service_set_nat_rule_comment(vn, "SNAT"),
                direction="input", translation_type="basic-nat44")
            snat_rule.set_comment(DMUtils.snat_rule_comment())
            nat_rules.add_rules(snat_rule)
            dnat_rule = NatRule(
                name=DMUtils.make_dnat_rule_name(ri_name),
                comment=DMUtils.service_set_nat_rule_comment(vn, "DNAT"),
                direction="output", translation_type="dnat-44")
            dnat_rule.set_comment(DMUtils.dnat_rule_comment())
            nat_rules.add_rules(dnat_rule)
            nat_rules.set_inside_interface(interfaces[0].name)
            nat_rules.set_outside_interface(interfaces[1].name)

            for pip, fip_vn in list(fip_map.items()):
                fip = fip_vn["floating_ip"]
                # private ip
                snat_rule.add_source_addresses(self._get_subnet_for_cidr(pip))
                # public ip
                snat_rule.add_source_prefixes(self._get_subnet_for_cidr(fip))

                # public ip
                dnat_rule.add_destination_addresses(
                    self._get_subnet_for_cidr(fip))
                # private ip
                dnat_rule.add_destination_prefixes(
                    self._get_subnet_for_cidr(pip))

            self._add_ref_to_list(ri.get_ingress_interfaces(),
                                  interfaces[0].name)
            self._add_ref_to_list(ri.get_egress_interfaces(),
                                  interfaces[1].name)

        for target in import_targets:
            self._add_to_list(ri.get_import_targets(), target)

        for target in export_targets:
            self._add_to_list(ri.get_export_targets(), target)
        # self.feature_config.add_routing_instances(ri)
    # end _add_routing_instance

    def _build_ri_config(self):
        vn_dict = self._get_vn_junointerface_map()
        self._physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()),
                                                     'l2_l3', 'irb', False)
        self._physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()),
                                                     'l3', 'lo0', True)
        vn_irb_ip_map = self._physical_router.get_vn_irb_ip_map()
        isMxRouter = self._physical_router.is_junos_service_ports_enabled()

        for vn_id, interfaces in self._get_sorted_key_value_pairs(vn_dict):
            vn_obj = db.VirtualNetworkDM.get(vn_id)
            if (vn_obj is None or
                vn_obj.get_vxlan_vni() is None or
                vn_obj.vn_network_id is None or
                (vn_obj.router_external == False and
                 isMxRouter == False)):
                continue

            export_set = None
            import_set = None

            for ri_id in vn_obj.routing_instances:
                # Find the primary RI by matching the name
                ri_obj = db.RoutingInstanceDM.get(ri_id)
                if ri_obj is None:
                    continue
                if ri_obj.fq_name[-1] == vn_obj.fq_name[-1]:
                    vrf_name_l2 = DMUtils.make_vrf_name(
                        vn_obj.fq_name[-1], vn_obj.vn_network_id, 'l2')
                    vrf_name_l3 = DMUtils.make_vrf_name(
                        vn_obj.fq_name[-1], vn_obj.vn_network_id, 'l3')
                    if vn_obj.route_targets:
                        export_set = \
                            vn_obj.route_targets & ri_obj.export_targets
                        import_set = \
                            vn_obj.route_targets & ri_obj.import_targets
                    else:
                        export_set = copy.copy(ri_obj.export_targets)
                        import_set = copy.copy(ri_obj.import_targets)
                    for ri2_id in ri_obj.routing_instances:
                        ri2 = db.RoutingInstanceDM.get(ri2_id)
                        if ri2 is None:
                            continue
                        import_set |= ri2.export_targets

                    if vn_obj.get_forwarding_mode() in ['l2', 'l2_l3']:
                        irb_ips = None
                        if vn_obj.get_forwarding_mode() == 'l2_l3' and \
                                self._is_gateway():
                            irb_ips = vn_irb_ip_map['irb'].get(vn_id, [])

                        ri_conf = {'ri_name': vrf_name_l2, 'vn': vn_obj,
                                   'is_l2': True,
                                   'is_l2_l3': (
                                       vn_obj.get_forwarding_mode() ==
                                       'l2_l3'),
                                   'import_targets': import_set,
                                   'export_targets': export_set,
                                   'prefixes': vn_obj.get_prefixes(),
                                   'gateways': irb_ips,
                                   'router_external': vn_obj.router_external,
                                   'interfaces': interfaces,
                                   'vni': vn_obj.get_vxlan_vni(),
                                   'network_id': vn_obj.vn_network_id,
                                   'encapsulation_priorities':
                                   db.GlobalVRouterConfigDM.
                                       global_encapsulation_priorities}
                        self._add_routing_instance(ri_conf)

                    if vn_obj.get_forwarding_mode() in ['l3', 'l2_l3'] and\
                            self._is_gateway():
                        interfaces = []
                        lo0_ips = None
                        if vn_obj.get_forwarding_mode() == 'l2_l3':
                            interfaces = [JunosInterface(
                                'irb.' + str(vn_obj.vn_network_id),
                                'l3', 0)]
                        else:
                            lo0_ips = vn_irb_ip_map['lo0'].get(vn_id, [])
                        is_internal_vn = True if '_contrail_lr_internal_vn_' \
                                                 in vn_obj.name else False
                        ri_conf = {'ri_name': vrf_name_l3, 'vn': vn_obj,
                                   'is_l2': False,
                                   'is_l2_l3':
                                       vn_obj.get_forwarding_mode() ==
                                       'l2_l3',
                                   'import_targets': import_set,
                                   'export_targets': export_set,
                                   'prefixes': vn_obj.get_prefixes(),
                                   'router_external': vn_obj.router_external,
                                   'interfaces': interfaces,
                                   'gateways': lo0_ips,
                                   'network_id': vn_obj.vn_network_id}
                        if is_internal_vn:
                            ri_conf['vni'] = vn_obj.get_vxlan_vni(
                                is_internal_vn=is_internal_vn)
                            lr_uuid = \
                                DMUtils.extract_lr_uuid_from_internal_vn_name(
                                    vrf_name_l3)
                            lr = db.LogicalRouterDM.get(lr_uuid)
                            if lr:
                                ri_conf['router_external'] = \
                                    lr.logical_router_gateway_external
                            if lr.data_center_interconnect:
                                ri_conf['connected_dci_network'] = \
                                    lr.data_center_interconnect
                                dci_uuid = lr.data_center_interconnect
                                dci = db.DataCenterInterconnectDM.get(dci_uuid)
                                lr_vn_list = dci.get_connected_lr_internal_vns(
                                    exclude_lr=lr.uuid) if dci else []
                                for lr_vn in lr_vn_list:
                                    exports, imports = \
                                        lr_vn.get_route_targets()
                                    if imports:
                                        ri_conf['import_targets'] |= imports
                                    if exports:
                                        ri_conf['export_targets'] |= exports
                        self._add_routing_instance(ri_conf)
                    break

            if export_set and\
                    isMxRouter == True and\
                    len(vn_obj.instance_ip_map) > 0:
                service_port_ids = DMUtils.get_service_ports(
                    vn_obj.vn_network_id)
                if not self._physical_router \
                        .is_service_port_id_valid(service_port_ids[0]):
                    self._logger.error("DM can't allocate service interfaces"
                                       " for (vn, vn-id)=(%s,%s)" %
                                       (vn_obj.fq_name,
                                        vn_obj.vn_network_id))
                else:
                    vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                     vn_obj.vn_network_id,
                                                     'l3', True)
                    interfaces = []
                    service_ports = self._physical_router.\
                        junos_service_ports.get('service_port')
                    interfaces.append(
                        JunosInterface(
                            service_ports[0] + "." + str(service_port_ids[0]),
                            'l3', 0))
                    interfaces.append(
                        JunosInterface(
                            service_ports[0] + "." + str(service_port_ids[1]),
                            'l3', 0))
                    ri_conf = {'ri_name': vrf_name, 'vn': vn_obj,
                               'import_targets': import_set,
                               'interfaces': interfaces,
                               'fip_map': vn_obj.instance_ip_map,
                               'network_id': vn_obj.vn_network_id,
                               'restrict_proxy_arp': vn_obj.router_external}
                    self._add_routing_instance(ri_conf)
        return
    # end _build_ri_config

    def _build_firewall_config(self):
        pr = self._physical_router
        if not pr:
            return
        sg_list = []

        if db.LogicalInterfaceDM.get_sg_list():
            sg_list += db.LogicalInterfaceDM.get_sg_list()

        for sg in sg_list or []:
            acls = sg.access_control_lists
            for acl in acls or []:
                acl = db.AccessControlListDM.get(acl)
                if acl and not acl.is_ingress:
                    self._build_firewall_filters(sg, acl)

    # end _build_firewall_config

    def feature_config(self, **kwargs):
        self.feature_config = Feature(name=self.feature_name())
        self._build_ri_config()
        self._set_internal_vn_irb_config()
        self._build_firewall_config()

        pis = []
        for pi, li_map in self._get_values_sorted_by_key(self.pi_map):
            pi.set_logical_interfaces(self._get_values_sorted_by_key(li_map))
            pis.append(pi)

        self.feature_config.set_routing_instances(
            self._get_values_sorted_by_key(
                self.ri_map))
        self.feature_config.set_physical_interfaces(pis)
        if self.evpn:
            self.feature_config.set_evpn(self.evpn)

        self.feature_config.set_firewall(self.firewall_config)
        self.feature_config.set_vlans(self._get_values_sorted_by_key(
            self.vlan_map))

        return self.feature_config
    # end feature_config

# end OverlayDcGatewayFeature
