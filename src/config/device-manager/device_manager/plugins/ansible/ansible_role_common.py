#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains base implementation of both spines and leafs
"""

from db import *
from dm_utils import DMUtils
from ansible_conf import AnsibleConf
from ansible_conf import JunosInterface
from abstract_device_api.abstract_device_xsd import *
import abc


class AnsibleRoleCommon(AnsibleConf):

    @classmethod
    def is_role_supported(cls, role):
        if role and role.lower().startswith('e2-'):
            return False
        for _role in cls._roles or []:
            if role.lower().startswith(_role.lower()):
                return True
        return False
    # end is_role_supported

    def __init__(self, logger, params={}):
        super(AnsibleRoleCommon, self).__init__(logger, params)
    # end __init__

    def is_gateway(self):
        if self.physical_router.routing_bridging_roles:
            gateway_roles = [r for r in self.physical_router.routing_bridging_roles if 'Gateway' in r]
            if gateway_roles:
                return True
        return False
    # end is_spine

    def underlay_config(self, is_delete=False):
        self._logger.info("underlay config start: %s(%s)\n" %
                          (self.physical_router.name,
                           self.physical_router.uuid))
        if not is_delete:
            self.build_underlay_bgp()
        self.send_conf(is_delete=is_delete, retry=False)
        self._logger.info("underlay config end: %s(%s)\n" %
                          (self.physical_router.name,
                           self.physical_router.uuid))
    # end underlay_config

    def initialize(self):
        super(AnsibleRoleCommon, self).initialize()
        self.irb_interfaces = []
        self.internal_vn_ris = []
    # end initialize

    def attach_irb(self, ri_conf, ri):
        if not self.is_gateway():
            return
        is_l2 = ri_conf.get("is_l2", False)
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        vni = ri_conf.get("vni", None)
        network_id = ri_conf.get("network_id", None)
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            if is_l2_l3:
                self.irb_interfaces.append("irb." + str(network_id))
    # end attach_irb

    def set_internal_vn_irb_config(self):
        if self.internal_vn_ris and self.irb_interfaces:
            for int_ri in self.internal_vn_ris:
                lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(int_ri.name)
                lr = LogicalRouterDM.get(lr_uuid)
                if not lr:
                    continue
                vn_list = lr.get_connected_networks(include_internal=False)
                for vn in vn_list:
                    vn_obj = VirtualNetworkDM.get(vn)
                    irb_name = "irb." + str(vn_obj.vn_network_id)
                    if irb_name in self.irb_interfaces:
                        int_ri.add_routing_interfaces(LogicalInterface(name=irb_name))
    # end set_internal_vn_irb_config

    def add_irb_config(self, ri_conf):
        vn = ri_conf.get("vn")
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        gateways = ri_conf.get("gateways", [])
        network_id = ri_conf.get("network_id", None)
        self.interfaces_config = self.interfaces_config or []
        irb_intf = PhysicalInterface(name='irb', interface_type='irb')
        self.interfaces_config.append(irb_intf)
        self._logger.info("Vn=" + vn.name + ", IRB: " + str(gateways) + ", pr="
                          + self.physical_router.name)
        if gateways is not None:
            intf_unit = LogicalInterface(
                name='irb.' + str(network_id), unit=network_id,
                comment=DMUtils.vn_irb_comment(vn, False, is_l2_l3))
            irb_intf.add_logical_interfaces(intf_unit)
            for (irb_ip, gateway) in gateways:
                intf_unit.add_ip_list(irb_ip)
                if len(gateway) and gateway != '0.0.0.0':
                    intf_unit.set_gateway(gateway)
    # end add_irb_config

    # lo0 interface in RI for route lookup to happen for Inter VN traffic
    # qfx10k pfe limitation
    def add_bogus_lo0(self, ri, network_id, vn):
        self.interfaces_config = self.interfaces_config or []
        ifl_num = 1000 + int(network_id)
        lo_intf = PhysicalInterface(name="lo0", interface_type='loopback')
        self.interfaces_config.append(lo_intf)
        intf_unit = LogicalInterface(
            name="lo0." + str(ifl_num), unit=ifl_num,
            comment=DMUtils.l3_bogus_lo_intf_comment(vn))
        intf_unit.add_ip_list("127.0.0.1")
        lo_intf.add_logical_interfaces(intf_unit)
        ri.add_loopback_interfaces(LogicalInterface(name="lo0." + str(ifl_num)))
    # end add_bogus_lo0

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
            from_.add_destination_address(self.get_subnet_for_cidr(prefix))
        then_ = Then()
        then_.add_routing_instance(ri_name)
        return Term(name=DMUtils.make_vrf_term_name(ri_name),
                    fromxx=from_, then=then_)
    # end add_inet_filter_term

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
        is_internal_vn = True if '_contrail_lr_internal_vn_' in vn.name else False
        highest_encapsulation_priority = \
            ri_conf.get("highest_encapsulation_priority") or "MPLSoGRE"

        self.ri_config = self.ri_config or []
        ri = RoutingInstance(name=ri_name)
        if vn:
            is_nat = True if fip_map else False
            ri.set_comment(DMUtils.vn_ri_comment(vn, is_l2, is_l2_l3, is_nat,
                                                 router_external))
        self.ri_config.append(ri)

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
                for interface in interfaces:
                    ri.add_interfaces(LogicalInterface(name=interface.name))
                if prefixes:
                    for prefix in prefixes:
                        ri.add_static_routes(self.get_route_for_cidr(prefix))
                        ri.add_prefixes(self.get_subnet_for_cidr(prefix))
        else:
            if highest_encapsulation_priority == "VXLAN":
                ri.set_routing_instance_type("virtual-switch")
            elif highest_encapsulation_priority in ["MPLSoGRE", "MPLSoUDP"]:
                ri.set_routing_instance_type("evpn")

        if is_internal_vn:
            self.internal_vn_ris.append(ri)
        if is_internal_vn or router_external:
            self.add_bogus_lo0(ri, network_id, vn)

        if self.is_gateway() and is_l2_l3:
            self.add_irb_config(ri_conf)
            self.attach_irb(ri_conf, ri)

        if fip_map is not None:
            ri.add_interfaces(LogicalInterface(name=interfaces[0].name))

            public_vrf_ips = {}
            for pip in fip_map.values():
                if pip["vrf_name"] not in public_vrf_ips:
                    public_vrf_ips[pip["vrf_name"]] = set()
                public_vrf_ips[pip["vrf_name"]].add(pip["floating_ip"])

            for public_vrf, fips in public_vrf_ips.items():
                ri.add_interfaces(LogicalInterface(name=interfaces[1].name))
                floating_ips = []
                for fip in fips:
                    ri.add_static_routes(
                        Route(prefix=fip,
                              prefix_len=32,
                              next_hop=interfaces[1].name,
                              comment=DMUtils.fip_egress_comment()))
                    floating_ips.append(FloatingIpMap(floating_ip=fip + "/32"))
                ri.add_floating_ip_list(FloatingIpList(
                    public_routing_instance=public_vrf,
                    floating_ips=floating_ips))

        # add firewall config for public VRF
        if router_external and is_l2 is False:
            self.firewall_config = self.firewall_config or Firewall(
                comment=DMUtils.firewall_comment())
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
            self.firewall_config = self.firewall_config or Firewall(
                comment=DMUtils.firewall_comment())
            f = FirewallFilter(
                name=DMUtils.make_private_vrf_filter_name(ri_name))
            f.set_comment(DMUtils.vn_firewall_comment(vn, "private"))
            self.firewall_config.add_firewall_filters(f)

            term = Term(name=DMUtils.make_vrf_term_name(ri_name))
            from_ = From()
            for fip_user_ip in fip_map.keys():
                from_.add_source_address(self.get_subnet_for_cidr(fip_user_ip))
            term.set_from(from_)
            term.set_then(Then(routing_instance=[ri_name]))
            f.add_terms(term)

            irb_intf = PhysicalInterface(name='irb', interface_type='irb')
            self.interfaces_config.append(irb_intf)
            intf_unit = LogicalInterface(
                name="irb." + str(network_id), unit=network_id,
                comment=DMUtils.vn_irb_fip_inet_comment(vn))
            irb_intf.add_logical_interfaces(intf_unit)
            intf_unit.set_family("inet")
            intf_unit.add_firewall_filters(
                DMUtils.make_private_vrf_filter_name(ri_name))
            ri.add_routing_interfaces(intf_unit)

        if gateways is not None:
            for (ip, gateway) in gateways:
                ri.add_gateways(GatewayRoute(
                    ip_address=self.get_subnet_for_cidr(ip),
                    gateway=self.get_subnet_for_cidr(gateway)))

        # add L2 EVPN and BD config
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            vlan = None
            if highest_encapsulation_priority == "VXLAN":
                self.vlans_config = self.vlans_config or []
                vlan = Vlan(name=DMUtils.make_bridge_name(vni), vxlan_id=vni)
                vlan.set_comment(DMUtils.vn_bd_comment(vn, "VXLAN"))
                self.vlans_config.append(vlan)
                for interface in interfaces:
                    vlan.add_interfaces(LogicalInterface(name=interface.name))
                if is_l2_l3:
                    # network_id is unique, hence irb
                    irb_intf = "irb." + str(network_id)
                    vlan.add_interfaces(LogicalInterface(name=irb_intf))
            elif highest_encapsulation_priority in ["MPLSoGRE", "MPLSoUDP"]:
                self.init_evpn_config(highest_encapsulation_priority)
                self.evpn.set_comment(
                    DMUtils.vn_evpn_comment(vn, highest_encapsulation_priority))
                for interface in interfaces:
                    self.evpn.add_interfaces(LogicalInterface(name=interface.name))

            self.interfaces_config = self.interfaces_config or []
            self.build_l2_evpn_interface_config(self.interfaces_config,
                                                interfaces, vn, vlan)

        if (not is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            self.init_evpn_config()
            if not is_internal_vn:
                # add vlans
                self.add_ri_vlan_config(ri_name, vni)

        if (not is_l2 and not is_l2_l3 and gateways):
            self.interfaces_config = self.interfaces_config or []
            ifl_num = 1000 + int(network_id)
            lo_intf = PhysicalInterface(name="lo0", interface_type='loopback')
            self.interfaces_config.append(lo_intf)
            intf_unit = LogicalInterface(name="lo0." + str(ifl_num),
                                         unit=ifl_num,
                                         comment=DMUtils.l3_lo_intf_comment(vn))
            lo_intf.add_logical_interfaces(intf_unit)
            for (lo_ip, _) in gateways:
                subnet = lo_ip
                (ip, _) = lo_ip.split('/')
                if ':' in lo_ip:
                    lo_ip = ip + '/' + '128'
                else:
                    lo_ip = ip + '/' + '32'
                intf_unit.add_ip_list(lo_ip)
            ri.add_loopback_interfaces(LogicalInterface(
                name="lo0." + str(ifl_num),
                comment=DMUtils.lo0_ri_intf_comment(vn)))

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

            for pip, fip_vn in fip_map.items():
                fip = fip_vn["floating_ip"]
                # private ip
                snat_rule.add_source_addresses(self.get_subnet_for_cidr(pip))
                # public ip
                snat_rule.add_source_prefixes(self.get_subnet_for_cidr(fip))

                # public ip
                dnat_rule.add_destination_addresses(
                    self.get_subnet_for_cidr(fip))
                # private ip
                dnat_rule.add_destination_prefixes(
                    self.get_subnet_for_cidr(pip))

            intf_unit = LogicalInterface(
                name=interfaces[0].name,
                unit=interfaces[0].unit,
                comment=DMUtils.service_intf_comment("Ingress"))
            intf_unit.set_family("inet")
            ri.add_service_interfaces(intf_unit)

            intf_unit = LogicalInterface(
                name=interfaces[1].name,
                unit=interfaces[1].unit,
                comment=DMUtils.service_intf_comment("Egress"))
            intf_unit.set_family("inet")
            ri.add_service_interfaces(intf_unit)

        for target in import_targets:
            ri.add_import_targets(target)

        for target in export_targets:
            ri.add_export_targets(target)
    # end add_routing_instance

    def attach_acls(self, interface, unit):
        pi_list = []
        esi_map = self.get_ae_alloc_esi_map()
        for esi, ae_id in self.physical_router.ae_id_map.items():
            ae_name = "ae" + str(ae_id)
            if_name, if_unit = interface.name.split('.')
            if ae_name == if_name:
                pi_list = esi_map.get(esi)
                if pi_list:
                    self._logger.info("attach acls on AE intf:%s, link_member:%s, unit:%s" %
                                      (ae_name, pi_list[0].name, if_unit))
                    li_name = pi_list[0].name + '.' + if_unit
                    break

        if not pi_list and not interface.li_uuid:
            return

        interface = LogicalInterfaceDM.find_by_name_or_uuid(interface.li_uuid)
        if not interface:
            interface = LogicalInterfaceDM.find_by_name_or_uuid(li_name)
            if not interface:
                return

        sg_list = interface.get_attached_sgs()
        filter_list = []
        for sg in sg_list:
            flist = self.get_configured_filters(sg)
            filter_list += flist
        if filter_list:
            for fname in filter_list:
                unit.add_firewall_filters(fname)
    # end attach_acls

    def build_l2_evpn_interface_config(self, interfaces_config, interfaces, vn,
                                       vlan_conf):
        ifd_map = {}
        for interface in interfaces:
            ifd_map.setdefault(interface.ifd_name, []).append(interface)

        for ifd_name, interface_list in ifd_map.items():
            intf = PhysicalInterface(name=ifd_name)
            interfaces_config.append(intf)
            if interface_list[0].is_untagged():
                if len(interface_list) > 1:
                    self._logger.error(
                        "invalid logical interfaces config for ifd %s" % (
                            ifd_name))
                    continue
                unit_name = ifd_name + "." + str(interface_list[0].unit)
                unit = LogicalInterface(
                    name=unit_name,
                    unit=interface_list[0].unit,
                    comment=DMUtils.l2_evpn_intf_unit_comment(vn, False),
                    is_tagged=False,
                    vlan_tag="4094")
                # attach acls
                self.attach_acls(interface_list[0], unit)
                intf.add_logical_interfaces(unit)
                if vlan_conf:
                    vlan_conf.add_interfaces(LogicalInterface(name=unit_name))
            else:
                for interface in interface_list:
                    unit_name = ifd_name + "." + str(interface.unit)
                    unit = LogicalInterface(
                        name=unit_name,
                        unit=interface.unit,
                        comment=DMUtils.l2_evpn_intf_unit_comment(
                            vn, True, interface.vlan_tag),
                        is_tagged=True,
                        vlan_tag=str(interface.vlan_tag))
                    # attach acls
                    self.attach_acls(interface, unit)
                    intf.add_logical_interfaces(unit)
                    if vlan_conf:
                        vlan_conf.add_interfaces(LogicalInterface(
                            name=unit_name))
    # end build_l2_evpn_interface_config

    def init_evpn_config(self, encapsulation='vxlan'):
        if not self.ri_config:
            # no vn config then no need to configure evpn
            return
        if self.evpn:
            # evpn init done
            return
        self.evpn = Evpn(encapsulation=encapsulation)
    # end init_evpn_config

    def add_vlan_config(self, vrf_name, vni, is_l2_l3=False, irb_intf=None):
        self.vlans_config = self.vlans_config or []
        vlan = Vlan(name=vrf_name[1:], vxlan_id=vni)
        if is_l2_l3:
            if not irb_intf:
                self._logger.error("Missing irb interface config l3 vlan: %s" % vrf_name)
            else:
                vlan.set_vlan_id(vni)
                vlan.add_interfaces(LogicalInterface(name=irb_intf))
        self.vlans_config.append(vlan)
        return vlan
    # end add_vlan_config

    def add_ri_vlan_config(self, vrf_name, vni):
        self.vlans_config = self.vlans_config or []
        self.vlans_config.append(Vlan(name=vrf_name[1:], vlan_id=vni, vxlan_id=vni))
    # end add_ri_vlan_config

    def build_esi_config(self):
        pr = self.physical_router
        if not pr:
            return
        self.interfaces_config = self.interfaces_config or []
        for pi_uuid in pr.physical_interfaces:
            pi = PhysicalInterfaceDM.get(pi_uuid)
            if not pi or not pi.esi or pi.esi == "0" or pi.get_parent_ae_id():
                continue
            intf = PhysicalInterface(name=pi.name,
                                     ethernet_segment_identifier=pi.esi)
            self.interfaces_config.append(intf)
    # end build_esi_config

    def build_lag_config(self):
        pr = self.physical_router
        if not pr:
            return
        self.interfaces_config = self.interfaces_config or []
        for lag_uuid in pr.link_aggregation_groups or []:
            link_members = []
            lag_obj = LinkAggregationGroupDM.get(lag_uuid)
            if not lag_obj:
                continue
            for pi_uuid in lag_obj.physical_interfaces or []:
                pi = PhysicalInterfaceDM.get(pi_uuid)
                if not pi:
                    continue
                if pi.interface_type != 'lag':
                   link_members.append(pi.name)
                else:
                   ae_intf_name = pi.name

            self._logger.info("LAG obj_uuid: %s, link_members: %s, name: %s" %
                              (lag_uuid, link_members, ae_intf_name))
            lag = LinkAggrGroup(lacp_enabled=lag_obj.lacp_enabled,
                                link_members=link_members)
            intf = PhysicalInterface(name=ae_intf_name,
                                     interface_type='lag',
                                     link_aggregation_group=lag)
            self.interfaces_config.append(intf)
    # end build_lag_config

    def get_vn_li_map(self):
        pr = self.physical_router
        vn_list = []
        # get all logical router connected networks
        for lr_id in pr.logical_routers or []:
            lr = LogicalRouterDM.get(lr_id)
            if not lr:
                continue
            vn_list += lr.get_connected_networks(include_internal=True)

        vn_dict = {}
        for vn_id in vn_list:
            vn_dict[vn_id] = []

        for vn_id in pr.virtual_networks:
            vn_dict[vn_id] = []

        li_set = pr.logical_interfaces
        for pi_uuid in pr.physical_interfaces:
            pi = PhysicalInterfaceDM.get(pi_uuid)
            if pi is None:
                continue
            li_set |= pi.logical_interfaces
        for li_uuid in li_set:
            li = LogicalInterfaceDM.get(li_uuid)
            if li is None:
                continue
            vmi_id = li.virtual_machine_interface
            vmi = VirtualMachineInterfaceDM.get(vmi_id)
            if vmi is None:
                continue
            vn_id = vmi.virtual_network
            if li.physical_interface:
                pi = PhysicalInterfaceDM.get(li.physical_interface)
                ae_id = pi.get_parent_ae_id()
                if ae_id and li.physical_interface:
                    _, unit= li.name.split('.')
                    ae_name = "ae" + str(ae_id) + "." + unit
                    vn_dict.setdefault(vn_id, []).append(
                           JunosInterface(ae_name, li.li_type, li.vlan_tag))
                    continue
            vn_dict.setdefault(vn_id, []).append(
                JunosInterface(li.name, li.li_type, li.vlan_tag, li_uuid=li.uuid))
        return vn_dict
    # end

    def get_vn_associated_physical_interfaces(self):
        pr = self.physical_router
        li_set = set()
        pi_list = []
        for pi_uuid in pr.physical_interfaces:
            pi = PhysicalInterfaceDM.get(pi_uuid)
            if pi is None or not pi.esi or pi.esi == "0":
                continue
            if self.has_vmi(pi.logical_interfaces):
                pi_list.append(pi)
        return pi_list
    # end get_vn_associated_physical_interfaces

    def has_vmi(self, li_set):
        if not li_set:
            return False
        for li_uuid in li_set:
            li = LogicalInterfaceDM.get(li_uuid)
            if not li or not li.virtual_machine_interface \
                or not VirtualMachineInterfaceDM.get(li.virtual_machine_interface):
                continue
            return True
        return False
    # end has_vmi

    def get_ae_alloc_esi_map(self):
        pi_list = self.get_vn_associated_physical_interfaces()
        esi_map = {}
        for pi in pi_list:
            if not pi.name.startswith("ae") and pi.esi:
                esi_map.setdefault(pi.esi, []).append(pi)
        return esi_map
    # end get_ae_alloc_esi_map

    def build_ae_config(self, esi_map):
        self.interfaces_config = self.interfaces_config or []
        # self.ae_id_map should have all esi => ae_id mapping
        # esi_map should have esi => interface memberships
        for esi, ae_id in self.physical_router.ae_id_map.items():
            # config ae interface
            ae_name = "ae" + str(ae_id)
            # associate 'ae' membership
            pi_list = esi_map.get(esi)
            link_members = []
            for pi in pi_list or []:
                 link_members.append(pi.name)

            lag = LinkAggrGroup(link_members=link_members)
            intf = PhysicalInterface(name=ae_name,
                                     ethernet_segment_identifier=esi,
                                     link_aggregation_group=lag)
            self.interfaces_config.append(intf)
    # end build_ae_config

    def add_addr_term(self, term, addr_match, is_src):
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
    # end add_addr_term

    def add_port_term(self, term, port_match, is_src):
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
    # end add_port_term

    def add_protocol_term(self, term, protocol_match):
        if not protocol_match or protocol_match == 'any':
            return None
        from_ = term.get_from() or From()
        term.set_from(from_)
        from_.set_ip_protocol(protocol_match)
    # end add_protocol_term

    def add_filter_term(self, ff, name):
        term = Term()
        term.set_name(name)
        ff.add_terms(term)
        term.set_then(Then(accept_or_reject=True))
        return term

    def add_dns_dhcp_terms(self, ff):
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
    # end add_dns_dhcp_terms

    def add_ether_type_term(self, ff, ether_type_match):
        if not ether_type_match:
            return None
        term = Term()
        from_ = From()
        term.set_from(from_)
        term.set_name("ether-type")
        from_.set_ether_type(ether_type_match.lower())
        term.set_then(Then(accept_or_reject=True))
        ff.add_terms(term)

    # end add_ether_type_term

    def build_firewall_filters(self, sg, acl, is_egress=False):
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
        self.firewall_config = self.firewall_config or\
                               Firewall(DMUtils.firewall_comment())
        for rule in rules:
            if not self.has_terms(rule):
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
            self.add_ether_type_term(f, 'arp')
            # allow dhcp/dns always
            self.add_dns_dhcp_terms(f)
            for rule in rules:
                if not self.has_terms(rule):
                    continue
                match = rule.get_match_condition()
                if not match:
                    continue
                rule_uuid = rule.get_rule_uuid()
                dst_addr_match = match.get_dst_address()
                dst_port_match = match.get_dst_port()
                ether_type_match = match.get_ethertype()
                protocol_match = match.get_protocol()
                src_addr_match = match.get_src_address()
                src_port_match = match.get_src_port()
                term = self.add_filter_term(f, rule_uuid)
                self.add_addr_term(term, dst_addr_match, False)
                self.add_addr_term(term, src_addr_match, True)
                self.add_port_term(term, dst_port_match, False)
                # source port match is not needed for now (BMS source port)
                #self.add_port_term(term, src_port_match, True)
                self.add_protocol_term(term, protocol_match)
            self.firewall_config.add_firewall_filters(f)
    # end build_firewall_filters

    def build_firewall_config(self):
        sg_list = LogicalInterfaceDM.get_sg_list()
        for sg in sg_list or []:
            acls = sg.access_control_lists
            for acl in acls or []:
                acl = AccessControlListDM.get(acl)
                if acl and not acl.is_ingress:
                    self.build_firewall_filters(sg, acl)
    # end build_firewall_config

    def is_default_sg(self, match):
        if (not match.get_dst_address()) or \
           (not match.get_dst_port()) or \
           (not match.get_ethertype()) or \
           (not match.get_src_address()) or \
           (not match.get_src_port()) or \
           (not match.get_protocol()):
            return False
        if not match.get_dst_address().get_subnet():
            return False
        if ((str(match.get_dst_address().get_subnet().get_ip_prefix()) == "0.0.0.0") or \
            (str(match.get_dst_address().get_subnet().get_ip_prefix()) == "::")) and \
           (str(match.get_dst_address().get_subnet().get_ip_prefix_len()) == "0") and \
           (str(match.get_dst_port().get_start_port()) == "0") and \
           (str(match.get_dst_port().get_end_port()) == "65535") and \
           ((str(match.get_ethertype()) == "IPv4") or \
            (str(match.get_ethertype()) == "IPv6")) and \
           (not match.get_src_address().get_subnet()) and \
           (not match.get_src_address().get_subnet_list()) and \
           (str(match.get_src_port().get_start_port()) == "0") and \
           (str(match.get_src_port().get_end_port()) == "65535") and \
           (str(match.get_protocol()) == "any"):
            return True
        return False
    # end is_default_sg

    def has_terms(self, rule):
        match = rule.get_match_condition()
        if not match:
            return False
        # return False if it is default SG, no filter is applied
        if self.is_default_sg(match):
            return False
        return match.get_dst_address() or match.get_dst_port() or \
              match.get_ethertype() or match.get_src_address() or match.get_src_port() or \
              (match.get_protocol() and match.get_protocol() != 'any')

    def get_firewall_filters(self, sg, acl, is_egress=False):
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
            if not self.has_terms(rule):
                continue
            match = rule.get_match_condition()
            if not match:
                continue
            rule_uuid = rule.get_rule_uuid()
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
    # end get_firewall_filters

    def get_configured_filters(self, sg):
        if not sg:
            return []
        filter_names = []
        acls = sg.access_control_lists
        for acl in acls or []:
            acl = AccessControlListDM.get(acl)
            if acl and not acl.is_ingress:
                fnames = self.get_firewall_filters(sg, acl)
                filter_names += fnames
        return filter_names
    # end get_configured_filters

    def build_ri_config(self):
        esi_map = self.get_ae_alloc_esi_map()
        self.physical_router.evaluate_ae_id_map(esi_map)
        self.build_ae_config(esi_map)

        vn_dict = self.get_vn_li_map()
        vn_irb_ip_map = None
        if self.is_gateway():
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
                        if vn_obj.get_forwarding_mode() == 'l2_l3' and self.is_gateway():
                            irb_ips = vn_irb_ip_map['irb'].get(vn_id, [])

                        ri_conf = {'ri_name': vrf_name_l2, 'vn': vn_obj,
                                   'is_l2': True, 'is_l2_l3': (
                                        vn_obj.get_forwarding_mode() == 'l2_l3'),
                                   'import_targets': import_set,
                                   'export_targets': export_set,
                                   'prefixes': vn_obj.get_prefixes(),
                                   'gateways': irb_ips,
                                   'router_external': vn_obj.router_external,
                                   'interfaces': interfaces,
                                   'vni': vn_obj.get_vxlan_vni(),
                                   'network_id': vn_obj.vn_network_id,
                                   'highest_encapsulation_priority':
                                       GlobalVRouterConfigDM.
                                           global_encapsulation_priority}
                        self.add_routing_instance(ri_conf)

                    if vn_obj.get_forwarding_mode() in ['l3', 'l2_l3']:
                        interfaces = []
                        lo0_ips = None
                        if vn_obj.get_forwarding_mode() == 'l2_l3':
                            interfaces = [
                                 JunosInterface(
                                'irb.' + str(vn_obj.vn_network_id),
                                'l3', 0)]
                        elif self.is_gateway():
                            lo0_ips = vn_irb_ip_map['lo0'].get(vn_id, [])
                        is_internal_vn = True if '_contrail_lr_internal_vn_' in vn_obj.name else False
                        ri_conf = {'ri_name': vrf_name_l3, 'vn': vn_obj,
                                   'is_l2': False,
                                   'is_l2_l3': vn_obj.get_forwarding_mode() ==
                                               'l2_l3',
                                   'import_targets': import_set,
                                   'export_targets': export_set,
                                   'prefixes': vn_obj.get_prefixes(),
                                   'router_external': vn_obj.router_external,
                                   'interfaces': interfaces,
                                   'gateways': lo0_ips,
                                   'network_id': vn_obj.vn_network_id}
                        if is_internal_vn:
                            ri_conf['vni'] = vn_obj.get_vxlan_vni(is_internal_vn = is_internal_vn)
                        self.add_routing_instance(ri_conf)
                    break

            if export_set and\
                    self.physical_router.is_junos_service_ports_enabled() and\
                    len(vn_obj.instance_ip_map) > 0:
                service_port_ids = DMUtils.get_service_ports(
                    vn_obj.vn_network_id)
                if not self.physical_router \
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
                    service_ports = self.physical_router.junos_service_ports.\
                        get('service_port')
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
                    self.add_routing_instance(ri_conf)
        return
    # end build_ri_config

    def set_common_config(self):
        self.build_underlay_bgp()
        if not self.ensure_bgp_config():
            return
        self.build_bgp_config()
        self.build_ri_config()
        self.set_internal_vn_irb_config()
        self.init_evpn_config()
        self.build_firewall_config()
        self.build_esi_config()
        self.build_lag_config()
    # end set_common_config

    @staticmethod
    def get_subnet_for_cidr(cidr):
        cidr_parts = cidr.split('/', 1)
        return Subnet(prefix=cidr_parts[0],
                      prefix_len=int(cidr_parts[1]) if len(cidr_parts) > 1
                      else 32)
    # end get_subnet_for_cidr


    @staticmethod
    def get_route_for_cidr(cidr):
        cidr_parts = cidr.split('/', 1)
        return Route(prefix=cidr_parts[0],
                     prefix_len=int(cidr_parts[1]) if len(cidr_parts) > 1
                     else 32)
    # end get_route_for_cidr

# end AnsibleRoleCommon
