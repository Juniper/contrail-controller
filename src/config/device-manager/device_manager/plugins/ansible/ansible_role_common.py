#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains base implementation of both spines and leafs
"""

from builtins import str

from abstract_device_api.abstract_device_xsd import *
import copy
import gevent

from .db import AccessControlListDM, DataCenterInterconnectDM, \
    GlobalVRouterConfigDM, InstanceIpDM, LogicalInterfaceDM, \
    LogicalRouterDM, NetworkIpamDM, PhysicalInterfaceDM, \
    PhysicalRouterDM, PortDM, PortTupleDM, RoutingInstanceDM, \
    ServiceApplianceDM, ServiceApplianceSetDM, ServiceInstanceDM, \
    ServiceTemplateDM, TagDM, VirtualMachineInterfaceDM, \
    VirtualNetworkDM, VirtualPortGroupDM
from .dm_utils import DMUtils
from .ansible_conf import AnsibleConf
from .ansible_conf import JunosInterface


class AnsibleRoleCommon(AnsibleConf):

    @classmethod
    def is_role_supported(cls, role):
        if not role or not cls._roles:
            return False
        for _role in cls._roles:
            if role.lower() == _role.lower():
                return True
        return False
    # end is_role_supported

    def __init__(self, logger, params={}):
        super(AnsibleRoleCommon, self).__init__(logger, params)
    # end __init__

    def _any_rb_role_matches(self, sub_str):
        if self.physical_router.routing_bridging_roles and sub_str:
            rb_roles = [r.lower() for r in self.physical_router.routing_bridging_roles]
            for rb_role in rb_roles:
                if sub_str.lower() in rb_role:
                    return True
        return False
    # end _any_rb_role_matches

    def is_gateway(self):
        return self._any_rb_role_matches('gateway')
    # end is_gateway

    def is_service_chained(self):
        return self._any_rb_role_matches('servicechain')
    # end is_service_chained

    def is_dci_gateway(self):
        return self._any_rb_role_matches('DCI-Gateway')
    # end is_dci_gateway

    def initialize(self):
        super(AnsibleRoleCommon, self).initialize()
        self.irb_interfaces = []
        self.internal_vn_ris = []
        self.dci_vn_ris = []
        self.dci_forwarding_filter = {}
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

    def set_dci_vn_irb_config(self):
        if self.dci_vn_ris and self.irb_interfaces:
            for int_ri in self.dci_vn_ris:
                dci_uuid = DMUtils.extract_dci_uuid_from_internal_vn_name(int_ri.name)
                dci = DataCenterInterconnectDM.get(dci_uuid)
                if not dci or not dci.virtual_network:
                    continue
                vn = dci.virtual_network
                vn_obj = VirtualNetworkDM.get(vn)
                if vn_obj is None or vn_obj.vn_network_id is None:
                    continue
                irb_name = "irb." + str(vn_obj.vn_network_id)
                if irb_name in self.irb_interfaces:
                    self.add_ref_to_list(int_ri.get_routing_interfaces(), irb_name)
    # end set_dci_vn_irb_config

    def set_internal_vn_irb_config(self):
        if self.internal_vn_ris and self.irb_interfaces:
            for int_ri in self.internal_vn_ris:
                gevent.idle()
                lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(int_ri.name)
                lr = LogicalRouterDM.get(lr_uuid)
                if not lr:
                    continue
                vn_list = lr.get_connected_networks(include_internal=False,
                                                    pr_uuid=
                                                    self.physical_router.uuid)
                for vn in vn_list:
                    vn_obj = VirtualNetworkDM.get(vn)
                    if vn_obj is None or vn_obj.vn_network_id is None:
                        continue
                    irb_name = "irb." + str(vn_obj.vn_network_id)
                    if irb_name in self.irb_interfaces:
                        self.add_ref_to_list(int_ri.get_routing_interfaces(), irb_name)
    # end set_internal_vn_irb_config

    def add_irb_config(self, ri_conf):
        vn = ri_conf.get("vn")
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        gateways = ri_conf.get("gateways", [])
        network_id = ri_conf.get("network_id", None)
        irb_intf, li_map = self.set_default_pi('irb', 'irb')
        self._logger.debug("Vn=" + vn.name + ", IRB: " + str(gateways) + ", pr="
                          + self.physical_router.name)
        intf_unit = self.set_default_li(li_map,
                                        'irb.' + str(network_id),
                                        network_id)
        if gateways:
            if vn.has_ipv6_subnet is True:
                intf_unit.set_is_virtual_router(True)
            intf_unit.set_comment(DMUtils.vn_irb_comment(vn, False, is_l2_l3))
            for (irb_ip, gateway) in gateways:
                if len(gateway) and gateway != '0.0.0.0':
                    intf_unit.set_gateway(gateway)
                    self.add_ip_address(intf_unit, irb_ip, gateway=gateway)
                else:
                    self.add_ip_address(intf_unit, irb_ip)
    # end add_irb_config

    # lo0 interface in RI for route lookup to happen for Inter VN traffic
    # qfx10k pfe limitation
    def add_bogus_lo0(self, ri, network_id, vn):
        ifl_num = 1000 + int(network_id)
        lo_intf, li_map = self.set_default_pi('lo0', 'loopback')
        intf_name = 'lo0.' + str(ifl_num)
        intf_unit = self.set_default_li(li_map, intf_name, ifl_num)
        intf_unit.set_comment(DMUtils.l3_bogus_lo_intf_comment(vn))
        self.add_ip_address(intf_unit, "127.0.0.1")
        self.add_ref_to_list(ri.get_loopback_interfaces(), intf_name)
    # end add_bogus_lo0

    def add_inet_vrf_filter(self, firewall_config, vrf_name):
        firewall_config.set_family("inet")
        f = FirewallFilter(name=DMUtils.make_private_vrf_filter_name(vrf_name))
        f.set_comment(DMUtils.vrf_filter_comment(vrf_name))
        firewall_config.add_firewall_filters(f)
        term = Term(name="default-term", then=Then(accept_or_reject=True))
        f.add_terms(term)
        return f
    # end add_inet_vrf_filter

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
        gevent.idle()
        ri_name = ri_conf.get("ri_name")
        vn = ri_conf.get("vn")
        is_l2 = ri_conf.get("is_l2", False)
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        import_targets = ri_conf.get("import_targets", set())
        export_targets = ri_conf.get("export_targets", set())
        prefixes = ri_conf.get("prefixes", [])
        gateways = ri_conf.get("gateways", [])
        router_external = ri_conf.get("router_external", False)
        connected_dci_network = ri_conf.get("connected_dci_network")
        interfaces = ri_conf.get("interfaces", [])
        vni = ri_conf.get("vni", None)
        fip_map = ri_conf.get("fip_map", None)
        network_id = ri_conf.get("network_id", None)
        is_internal_vn = True if '_contrail_lr_internal_vn_' in vn.name else False
        encapsulation_priorities = \
           ri_conf.get("encapsulation_priorities") or ["MPLSoGRE"]
        highest_encapsulation = encapsulation_priorities[0]

        ri = RoutingInstance(name=ri_name)
        is_master_int_vn = False
        if vn:
            is_nat = True if fip_map else False
            ri.set_comment(DMUtils.vn_ri_comment(vn, is_l2, is_l2_l3, is_nat,
                                                 router_external))
            if is_internal_vn:
                lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(
                    ri_name)
            else:
                if vn.logical_router is None:
                    # try updating logical router to handle DM restart
                    # vn.logical_router could be none as sequencing of
                    # locate object calls in device_manager.py
                    vn.set_logical_router(vn.fq_name[-1])
                lr_uuid = vn.logical_router
            if lr_uuid:
                lr = LogicalRouterDM.get(lr_uuid)
                if lr:
                    is_master_int_vn = lr.is_master
                    if is_internal_vn:
                        # set description only for interval VN/VRF
                        ri.set_description("__contrail_%s_%s" % (lr.name, lr_uuid))

            ri.set_is_master(is_master_int_vn)

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
                if router_external or not is_internal_vn:
                    for interface in interfaces:
                        self.add_ref_to_list(ri.get_interfaces(), interface.name)
                    if prefixes:
                        for prefix in prefixes:
                            ri.add_static_routes(self.get_route_for_cidr(prefix))
                            ri.add_prefixes(self.get_subnet_for_cidr(prefix))
        else:
            if highest_encapsulation == "VXLAN":
                ri.set_routing_instance_type("virtual-switch")
            elif highest_encapsulation in ["MPLSoGRE", "MPLSoUDP"]:
                ri.set_routing_instance_type("evpn")

        if is_internal_vn:
            self.internal_vn_ris.append(ri)


        if (is_internal_vn and not is_master_int_vn) or router_external:
            self.add_bogus_lo0(ri, network_id, vn)

        if self.is_gateway() and is_l2_l3 and not is_internal_vn:
            self.add_irb_config(ri_conf)
            self.attach_irb(ri_conf, ri)

        if fip_map is not None:
            self.add_ref_to_list(ri.get_interfaces(), interfaces[0].name)

            public_vrf_ips = {}
            for pip in list(fip_map.values()):
                if pip["vrf_name"] not in public_vrf_ips:
                    public_vrf_ips[pip["vrf_name"]] = set()
                public_vrf_ips[pip["vrf_name"]].add(pip["floating_ip"])

            for public_vrf, fips in list(public_vrf_ips.items()):
                ri_public = RoutingInstance(name=public_vrf)
                self.ri_map[public_vrf] = ri_public
                self.add_ref_to_list(ri_public.get_interfaces(), interfaces[1].name)
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
            term_ri_name = ri.get_name()
            # Routing instance name is set to description for internal vns
            # in the template. Routing instance name in the firewall
            # filter is required to match with routing-instance name 
            # in the template. This is used only by mx for fip-snat.
            if ri.get_virtual_network_is_internal():
                term_ri_name = ri.get_description()
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
                term = self.add_inet_filter_term(
                    term_ri_name, prefixes, "inet4")
                # insert before the last term
                terms = self.inet4_forwarding_filter.get_terms()
                terms = [term] + (terms or [])
                self.inet4_forwarding_filter.set_terms(terms)
            if has_ipv6_prefixes:
                # add terms to inet6 filter
                term = self.add_inet_filter_term(
                    term_ri_name, prefixes, "inet6")
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
                from_.add_source_address(self.get_subnet_for_cidr(fip_user_ip))
            term.set_from(from_)
            term.set_then(Then(routing_instance=[ri_name]))
            f.add_terms(term)

            irb_intf, li_map = self.set_default_pi('irb', 'irb')
            intf_name = 'irb.' + str(network_id)
            intf_unit = self.set_default_li(li_map, intf_name, network_id)
            intf_unit.set_comment(DMUtils.vn_irb_fip_inet_comment(vn))
            intf_unit.set_family("inet")
            intf_unit.add_firewall_filters(
                DMUtils.make_private_vrf_filter_name(ri_name))
            self.add_ref_to_list(ri.get_routing_interfaces(), intf_name)

        if gateways is not None:
            for (ip, gateway) in gateways:
                ri.add_gateways(GatewayRoute(
                    ip_address=self.get_subnet_for_cidr(ip),
                    gateway=self.get_subnet_for_cidr(gateway)))

        # add L2 EVPN and BD config
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            vlan = None
            if highest_encapsulation == "VXLAN":
                if not is_internal_vn:
                    vlan = Vlan(name=DMUtils.make_bridge_name(vni), vxlan_id=vni)
                    vlan.set_comment(DMUtils.vn_bd_comment(vn, "VXLAN"))
                    desc = "Virtual Network - %s" % vn.name
                    vlan.set_description(desc)
                    self.vlan_map[vlan.get_name()] = vlan
                    for interface in interfaces:
                        self.add_ref_to_list(vlan.get_interfaces(), interface.name)
                    if is_l2_l3 and self.is_gateway():
                        # network_id is unique, hence irb
                        irb_intf = "irb." + str(network_id)
                        self.add_ref_to_list(vlan.get_interfaces(), irb_intf)
            elif highest_encapsulation in ["MPLSoGRE", "MPLSoUDP"]:
                self.init_evpn_config(highest_encapsulation)
                self.evpn.set_comment(
                      DMUtils.vn_evpn_comment(vn, highest_encapsulation))
                for interface in interfaces:
                    self.add_ref_to_list(self.evpn.get_interfaces(), interface.name)

            self.build_l2_evpn_interface_config(interfaces, vn, vlan)

        if (not is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            self.init_evpn_config()
            if not is_internal_vn:
                # add vlans
                self.add_ri_vlan_config(ri_name, vni)

        if (not is_l2 and not is_l2_l3 and gateways):
            ifl_num = 1000 + int(network_id)
            lo_intf, li_map = self.set_default_pi('lo0', 'loopback')
            intf_name = 'lo0.' + str(ifl_num)
            intf_unit = self.set_default_li(li_map, intf_name, ifl_num)
            intf_unit.set_comment(DMUtils.l3_lo_intf_comment(vn))
            for (lo_ip, _) in gateways:
                subnet = lo_ip
                (ip, _) = lo_ip.split('/')
                if ':' in lo_ip:
                    lo_ip = ip + '/' + '128'
                else:
                    lo_ip = ip + '/' + '32'
                self.add_ip_address(intf_unit, lo_ip)
            self.add_ref_to_list(ri.get_loopback_interfaces(), intf_name)

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
                snat_rule.add_source_addresses(self.get_subnet_for_cidr(pip))
                # public ip
                snat_rule.add_source_prefixes(self.get_subnet_for_cidr(fip))

                # public ip
                dnat_rule.add_destination_addresses(
                    self.get_subnet_for_cidr(fip))
                # private ip
                dnat_rule.add_destination_prefixes(
                    self.get_subnet_for_cidr(pip))

            self.add_ref_to_list(ri.get_ingress_interfaces(), interfaces[0].name)
            self.add_ref_to_list(ri.get_egress_interfaces(), interfaces[1].name)

        for target in import_targets:
            self.add_to_list(ri.get_import_targets(), target)

        for target in export_targets:
            self.add_to_list(ri.get_export_targets(), target)
    # end add_routing_instance

    def attach_acls(self, interface, unit):
        pr = self.physical_router
        if not pr:
            return

        sg_list = []
        # now the vpg obj is available in interface object as vpg_obj
        vpg_obj = interface.vpg_obj
        sg_list_temp = vpg_obj.get_attached_sgs(unit.get_vlan_tag(),
                                                    interface)
        for sg in sg_list_temp:
            if sg not in sg_list:
                sg_list.append(sg)

        for sg in sg_list or []:
            acls = sg.access_control_lists
            for acl in acls or []:
                acl = AccessControlListDM.get(acl)
                if acl and not acl.is_ingress:
                    self.build_firewall_filters(sg, acl)

        if interface.li_uuid:
            interface = LogicalInterfaceDM.find_by_name_or_uuid(interface.li_uuid)
            if interface:
                sg_list += interface.get_attached_sgs()

        filter_list = []
        for sg in sg_list:
            flist = self.get_configured_filters(sg)
            filter_list += flist
        if filter_list:
            for fname in filter_list:
                unit.add_firewall_filters(fname)
    # end attach_acls

    def _is_enterprise_style(self):
        return self.physical_router.fabric_obj.enterprise_style
    # end _is_enterprise_style

    def build_l2_evpn_interface_config(self, interfaces, vn, vlan_conf):
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
            if self._is_enterprise_style():
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
            _, li_map = self.set_default_pi(ifd_name)
            for interface in interface_list:
                if interface.is_untagged():
                    is_tagged = False
                    vlan_tag = str(interface.port_vlan_tag)
                else:
                    is_tagged = True
                    vlan_tag = str(interface.vlan_tag)
                unit_name = ifd_name + "." + str(interface.unit)
                unit = self.set_default_li(li_map, unit_name, interface.unit)
                unit.set_comment(DMUtils.l2_evpn_intf_unit_comment(vn,
                    is_tagged, vlan_tag))
                unit.set_is_tagged(is_tagged)
                unit.set_vlan_tag(vlan_tag)
                # attach acls
                self.attach_acls(interface, unit)
                if vlan_conf:
                    self.add_ref_to_list(vlan_conf.get_interfaces(),
                        unit_name)
    # end build_l2_evpn_interface_config

    def init_evpn_config(self, encapsulation='vxlan'):
        if not self.ri_map:
            # no vn config then no need to configure evpn
            return
        if self.evpn:
            # evpn init done
            return
        self.evpn = Evpn(encapsulation=encapsulation)
    # end init_evpn_config

    def add_vlan_config(self, vrf_name, vni, is_l2_l3=False, irb_intf=None):
        vlan = Vlan(name=vrf_name[1:], vxlan_id=vni)
        if is_l2_l3 and self.is_gateway():
            if not irb_intf:
                self._logger.error("Missing irb interface config l3 vlan: %s" % vrf_name)
            else:
                vlan.set_vlan_id(vni)
                self.add_ref_to_list(vlan.get_interfaces(), irb_intf)
        self.vlan_map[vlan.get_name()] = vlan
        return vlan
    # end add_vlan_config

    def add_ri_vlan_config(self, vrf_name, vni):
        vlan = Vlan(name=vrf_name[1:], vxlan_id=vni)
        self.vlan_map[vlan.get_name()] = vlan
    # end add_ri_vlan_config

    def build_vpg_config(self):
        multi_homed = False
        ae_link_members = {}

        pr = self.physical_router
        if not pr:
            return
        for vpg_uuid in pr.virtual_port_groups or []:
            ae_link_members = {}
            vpg_obj = VirtualPortGroupDM.get(vpg_uuid)
            if not vpg_obj:
                continue
            if not vpg_obj.virtual_machine_interfaces:
                continue
            esi = vpg_obj.esi
            for pi_uuid in vpg_obj.physical_interfaces or []:
                if pi_uuid not in pr.physical_interfaces:
                    multi_homed = True
                    continue
                ae_id = vpg_obj.pi_ae_map.get(pi_uuid)
                if ae_id is not None:
                    ae_intf_name = 'ae' + str(ae_id)
                    pi = PhysicalInterfaceDM.get(pi_uuid)
                    if not pi:
                        continue
                    if ae_intf_name in ae_link_members:
                        ae_link_members[ae_intf_name].append(pi.name)
                    else:
                        ae_link_members[ae_intf_name] = []
                        ae_link_members[ae_intf_name].append(pi.name)
                else:
                    pi_obj = PhysicalInterfaceDM.get(pi_uuid)
                    if pi_obj:
                        lag = LinkAggrGroup(description="Virtual Port Group : %s" %
                                                        vpg_obj.name)
                        intf, _ = self.set_default_pi(pi_obj.name, 'regular')
                        intf.set_link_aggregation_group(lag)

            for ae_intf_name, link_members in list(ae_link_members.items()):
                self._logger.info("LAG obj_uuid: %s, link_members: %s, name: %s" %
                                  (vpg_uuid, link_members, ae_intf_name))
                lag = LinkAggrGroup(lacp_enabled=True,
                                    link_members=link_members,
                                    description="Virtual Port Group : %s" %
                                                vpg_obj.name)
                intf, _ = self.set_default_pi(ae_intf_name, 'lag')
                intf.set_link_aggregation_group(lag)
                intf.set_comment("ae interface")

                #check if esi needs to be set
                if multi_homed:
                    intf.set_ethernet_segment_identifier(esi)
                    multi_homed = False
    # end build_vpg_config

    def get_vn_li_map(self):
        pr = self.physical_router
        vn_list = []
        # get all logical router connected networks
        for lr_id in pr.logical_routers or []:
            lr = LogicalRouterDM.get(lr_id)
            if not lr:
                continue
            vn_list += lr.get_connected_networks(include_internal=True,
                                                 pr_uuid=pr.uuid)

        vn_dict = {}
        for vn_id in vn_list:
            vn_dict[vn_id] = []

        for vn_id in pr.virtual_networks:
            vn_dict[vn_id] = []
            vn = VirtualNetworkDM.get(vn_id)
            if vn and vn.router_external:
                vn_list = vn.get_connected_private_networks()
                for pvn in vn_list or []:
                    vn_dict[pvn] = []

        for vpg_uuid in pr.virtual_port_groups or []:
            vpg_obj = VirtualPortGroupDM.get(vpg_uuid)
            if not vpg_obj:
                continue
            vpg_interfaces = vpg_obj.physical_interfaces
            for vmi_uuid in vpg_obj.virtual_machine_interfaces:
                vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
                if not vmi_obj:
                    continue
                vn_id = vmi_obj.virtual_network or None
                if vn_id:
                    vn = VirtualNetworkDM.get(vn_id)
                    vlan_tag = vmi_obj.vlan_tag
                    port_vlan_tag = vmi_obj.port_vlan_tag
                    for pi_uuid in vpg_interfaces:
                        if pi_uuid in pr.physical_interfaces:
                            ae_id = vpg_obj.pi_ae_map.get(pi_uuid)
                            if ae_id is not None and vlan_tag is not None:
                                ae_name = "ae" + str(ae_id) + "." + str(vlan_tag)
                                vn_dict.setdefault(vn_id, []).append(
                                    JunosInterface(ae_name, 'l2', vlan_tag, port_vlan_tag=port_vlan_tag, vpg_obj=vpg_obj))
                                break
                            else:
                                pi_obj = PhysicalInterfaceDM.get(pi_uuid)
                                if pi_obj:
                                    li_name = pi_obj.name + "." + str(vlan_tag)
                                    vn_dict.setdefault(vn_id, []).append(
                                    JunosInterface(li_name, 'l2', vlan_tag, port_vlan_tag=port_vlan_tag, vpg_obj=vpg_obj))
                                    break
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
        pr = self.physical_router
        if not pr:
            return
        sg_list = []

        if LogicalInterfaceDM.get_sg_list():
            sg_list += LogicalInterfaceDM.get_sg_list()

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
        vn_dict = self.get_vn_li_map()
        self.physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l2_l3', 'irb', False)
        self.physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l3', 'lo0', True)
        vn_irb_ip_map = self.physical_router.get_vn_irb_ip_map()

        for vn_id, interfaces in self.get_sorted_key_value_pairs(vn_dict):
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
                    if vn_obj.route_targets:
                        export_set = vn_obj.route_targets & ri_obj.export_targets
                        import_set = vn_obj.route_targets & ri_obj.import_targets
                    else:
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
                                   'prefixes': vn_obj.get_prefixes(self.physical_router.uuid),
                                   'gateways': irb_ips,
                                   'router_external': vn_obj.router_external,
                                   'interfaces': interfaces,
                                   'vni': vn_obj.get_vxlan_vni(),
                                   'network_id': vn_obj.vn_network_id,
                                   'encapsulation_priorities':
                                       GlobalVRouterConfigDM.
                                           global_encapsulation_priorities}
                        self.add_routing_instance(ri_conf)

                    if vn_obj.get_forwarding_mode() in ['l3', 'l2_l3'] and self.is_gateway():
                        interfaces = []
                        lo0_ips = None
                        if vn_obj.get_forwarding_mode() == 'l2_l3':
                            interfaces = [
                                 JunosInterface(
                                'irb.' + str(vn_obj.vn_network_id),
                                'l3', 0)]
                        else:
                            lo0_ips = vn_irb_ip_map['lo0'].get(vn_id, [])
                        is_internal_vn = True if '_contrail_lr_internal_vn_' in vn_obj.name else False
                        ri_conf = {'ri_name': vrf_name_l3, 'vn': vn_obj,
                                   'is_l2': False,
                                   'is_l2_l3': vn_obj.get_forwarding_mode() ==
                                               'l2_l3',
                                   'import_targets': import_set,
                                   'export_targets': export_set,
                                   'prefixes': vn_obj.get_prefixes(self.physical_router.uuid),
                                   'router_external': vn_obj.router_external,
                                   'interfaces': interfaces,
                                   'gateways': lo0_ips,
                                   'network_id': vn_obj.vn_network_id}
                        if is_internal_vn:
                            lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(vrf_name_l3)
                            lr = LogicalRouterDM.get(lr_uuid)
                            if lr and not lr.is_master:
                                ri_conf['vni'] = vn_obj.get_vxlan_vni(is_internal_vn = is_internal_vn)
                                ri_conf['router_external'] = lr.logical_router_gateway_external
                                if lr.data_center_interconnect:
                                    ri_conf['connected_dci_network'] = lr.data_center_interconnect
                                    dci_uuid = lr.data_center_interconnect
                                    dci = DataCenterInterconnectDM.get(dci_uuid)
                                    lr_vn_list = dci.get_connected_lr_internal_vns(exclude_lr=lr.uuid, pr_uuid=self.physical_router.uuid) if dci else []
                                    for lr_vn in lr_vn_list:
                                        exports, imports = lr_vn.get_route_targets()
                                        if imports:
                                            ri_conf['import_targets'] |= imports
                                        if exports:
                                            ri_conf['export_targets'] |= exports
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

    def build_service_chain_ri_config(self, si_name, left_vrf_info, right_vrf_info):
        #left vrf
        vn_obj = VirtualNetworkDM.get(left_vrf_info.get('vn_id'))
        if vn_obj:

            vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                             vn_obj.vn_network_id, 'l3')

            if self.ri_map.get(vrf_name):
                left_ri = self.ri_map.get(vrf_name)
            else:
                left_ri = RoutingInstance(name=vrf_name)
                self.ri_map[vrf_name] = left_ri

            self.add_ref_to_list(left_ri.get_routing_interfaces(), 'irb.'+left_vrf_info.get('left_svc_unit'))

            if left_vrf_info.get('srx_left_interface') and left_vrf_info.get('loopback_ip'):
                protocols = RoutingInstanceProtocols()
                bgp_name = si_name + '_left'
                peer_bgp_name = bgp_name + '_' + left_vrf_info.get(
                     'srx_left_interface')

                peer_bgp = Bgp(name=peer_bgp_name,
                               autonomous_system=left_vrf_info.get('peer'),
                               ip_address=left_vrf_info.get('srx_left_interface'))

                bgp = Bgp(name=bgp_name,
                          type_="external",
                          autonomous_system=left_vrf_info.get('local'))
                bgp.add_peers(peer_bgp)
                bgp.set_comment('PNF-Service-Chaining')
                protocols.add_bgp(bgp)

                pimrp = PimRp(ip_address=left_vrf_info.get('loopback_ip'))
                pim = Pim(name=si_name + '_left')
                pim.set_rp(pimrp)
                pim.set_comment('PNF-Service-Chaining')
                protocols.add_pim(pim)
                left_ri.add_protocols(protocols)

        #create new service chain ri for vni targets
        for vn in left_vrf_info.get('tenant_vn') or []:
            vn_obj = VirtualNetworkDM.get(vn)
            if vn_obj:
                vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                             vn_obj.vn_network_id, 'l3')
                if self.ri_map.get(vrf_name):
                    ri = self.ri_map.get(vrf_name)

                    vni_ri_left = RoutingInstance(name=si_name + '_service_chain_left')
                    self.ri_map[si_name + '_service_chain_left'] = vni_ri_left

                    vni_ri_left.set_comment('PNF-Service-Chaining')
                    vni_ri_left.set_routing_instance_type("virtual-switch")
                    vni_ri_left.set_vxlan_id(left_vrf_info.get('left_svc_unit'))

                    for target in ri.get_export_targets():
                        self.add_to_list(vni_ri_left.get_export_targets(), target)

        #right vrf
        vn_obj = VirtualNetworkDM.get(right_vrf_info.get('vn_id'))
        if vn_obj:
            vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                             vn_obj.vn_network_id, 'l3')

            if self.ri_map.get(vrf_name):
                right_ri = self.ri_map.get(vrf_name)
            else:
                right_ri = RoutingInstance(name=vrf_name)
                self.ri_map[vrf_name] = right_ri

            self.add_ref_to_list(right_ri.get_routing_interfaces(), 'irb.'+right_vrf_info.get('right_svc_unit'))

            if right_vrf_info.get('srx_right_interface') and left_vrf_info.get('loopback_ip'):
                protocols = RoutingInstanceProtocols()
                bgp_name = si_name + '_right'
                peer_bgp_name = bgp_name + '_' + right_vrf_info.get('srx_right_interface')

                peer_bgp = Bgp(name=peer_bgp_name,
                               autonomous_system=right_vrf_info.get('peer'),
                               ip_address=right_vrf_info.get(
                                   'srx_right_interface'))

                bgp = Bgp(name=bgp_name,
                          type_="external",
                          autonomous_system=right_vrf_info.get('local'))
                bgp.add_peers(peer_bgp)
                bgp.set_comment('PNF-Service-Chaining')
                protocols.add_bgp(bgp)

                pimrp = PimRp(ip_address=left_vrf_info.get('loopback_ip'))
                pim = Pim(name=si_name + '_right')
                pim.set_rp(pimrp)
                pim.set_comment('PNF-Service-Chaining')
                protocols.add_pim(pim)
                right_ri.add_protocols(protocols)

        #create new service chain ri for vni targets
        for vn in right_vrf_info.get('tenant_vn') or []:
            vn_obj = VirtualNetworkDM.get(vn)
            if vn_obj:
                vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                             vn_obj.vn_network_id, 'l3')
                if self.ri_map.get(vrf_name):
                    ri = self.ri_map.get(vrf_name)

                    vni_ri_right = RoutingInstance(name=si_name + '_service_chain_right')
                    self.ri_map[si_name + '_service_chain_right'] = vni_ri_right

                    vni_ri_right.set_comment('PNF-Service-Chaining')
                    vni_ri_right.set_routing_instance_type("virtual-switch")
                    vni_ri_right.set_vxlan_id(right_vrf_info.get('right_svc_unit'))

                    for target in ri.get_export_targets():
                        self.add_to_list(vni_ri_right.get_export_targets(), target)


    def build_service_chain_irb_bd_config(self, svc_app_obj, left_right_params):
        left_fq_name = left_right_params['left_qfx_fq_name']
        right_fq_name = left_right_params['right_qfx_fq_name']
        left_svc_vlan = left_right_params['left_svc_vlan']
        right_svc_vlan = left_right_params['right_svc_vlan']
        left_svc_unit = left_right_params['left_svc_unit']
        right_svc_unit = left_right_params['right_svc_unit']

        for pnf_pi, intf_type in list(svc_app_obj.physical_interfaces.items()):
            pnf_pi_obj = PhysicalInterfaceDM.get(pnf_pi)
            for spine_pi in pnf_pi_obj.physical_interfaces:
                if spine_pi not in self.physical_router.physical_interfaces:
                    continue
                spine_pi_obj = PhysicalInterfaceDM.get(spine_pi)
                pr = self.physical_router
                if spine_pi_obj.fq_name == left_fq_name:
                    li_name = spine_pi_obj.name + '.' + left_svc_vlan
                    li_fq_name = spine_pi_obj.fq_name + [li_name.replace(":", "_")]
                    li_obj = LogicalInterfaceDM.find_by_fq_name(li_fq_name)
                    if li_obj:
                        #irb creation
                        iip_obj = InstanceIpDM.get(li_obj.instance_ip)
                        if iip_obj:
                            irb_addr = iip_obj.instance_ip_address
                            irb_unit = left_svc_unit
                            left_irb_intf, li_map = self.set_default_pi('irb',
                                                                    'irb')
                            intf_unit = self.set_default_li(li_map,
                                'irb.' + str(irb_unit),
                                irb_unit)
                            self.add_ip_address(intf_unit, irb_addr+'/29')
                            #build BD config
                            left_bd_vlan = Vlan(name=DMUtils.make_bridge_name(left_svc_unit),
                                        vxlan_id=left_svc_unit)
                            left_bd_vlan.set_vlan_id(left_svc_vlan)
                            left_bd_vlan.set_description("PNF-Service-Chaining")
                            left_bd_vlan.set_comment("PNF-Service-Chaining")
                            self.add_ref_to_list(
                                left_bd_vlan.get_interfaces(), 'irb.' + str(irb_unit))
                            self.vlan_map[left_bd_vlan.get_name()] = left_bd_vlan
                            #create logical interfaces for the aggregated interfaces
                            left_svc_intf, li_map = self.set_default_pi(
                                spine_pi_obj.name, 'service')
                            left_svc_intf_unit = self.set_default_li(li_map,
                                                          left_fq_name[-1] + '.0',
                                                          "0")
                            left_svc_intf_unit.set_comment("PNF-Service-Chaining")
                            left_svc_intf_unit.set_family("ethernet-switching")
                            vlan_left = Vlan(name="bd-"+left_svc_unit)
                            left_svc_intf_unit.add_vlans(vlan_left)

                if spine_pi_obj.fq_name == right_fq_name:
                    li_name = spine_pi_obj.name + '.' + right_svc_vlan
                    li_fq_name = spine_pi_obj.fq_name + [li_name.replace(":", "_")]
                    li_obj = LogicalInterfaceDM.find_by_fq_name(li_fq_name)
                    if li_obj:
                        #irb creation
                        iip_obj = InstanceIpDM.get(li_obj.instance_ip)
                        if iip_obj:
                            irb_addr = iip_obj.instance_ip_address
                            irb_unit = right_svc_unit
                            right_irb_intf, li_map = self.set_default_pi(
                                'irb', 'irb')
                            intf_unit = self.set_default_li(li_map,
                                'irb.' + str(irb_unit),
                                irb_unit)
                            self.add_ip_address(intf_unit, irb_addr+'/29')
                            #build BD config
                            right_bd_vlan = Vlan(
                                name=DMUtils.make_bridge_name(right_svc_unit),
                                vxlan_id=right_svc_unit)
                            right_bd_vlan.set_vlan_id(right_svc_vlan)
                            right_bd_vlan.set_description("PNF-Service-Chaining")
                            right_bd_vlan.set_comment("PNF-Service-Chaining")
                            self.add_ref_to_list(
                                right_bd_vlan.get_interfaces(), 'irb.' + str(irb_unit))
                            self.vlan_map[right_bd_vlan.get_name()] = right_bd_vlan
                            #create logical interfaces for the aggregated interfaces
                            right_svc_intf, li_map = self.set_default_pi(
                                spine_pi_obj.name, 'service')
                            right_svc_intf_unit = self.set_default_li(li_map,
                                                          right_fq_name[-1] + '.0',
                                                          "0")
                            right_svc_intf_unit.set_comment("PNF-Service-Chaining")
                            right_svc_intf_unit.set_family(
                                "ethernet-switching")
                            vlan_right = Vlan(name="bd-"+right_svc_unit)
                            right_svc_intf_unit.add_vlans(vlan_right)

    def build_service_chain_required_params(self, left_vrf_info, right_vrf_info):
        if left_vrf_info.get('left_svc_unit') and \
           right_vrf_info.get('right_svc_unit'):
            return True

        return False

    def build_service_chaining_config(self):
        pr = self.physical_router
        if not pr:
            return

        if self.is_service_chained():
            if not pr.port_tuples:
                return

            for pt in pr.port_tuples:
                left_vrf_info = {}
                right_vrf_info = {}
                left_interface = ''
                right_interface = ''
                left_right_params = {}

                pt_obj = PortTupleDM.get(pt)

                if not pt_obj:
                    continue

                if not pt_obj.logical_routers:
                    continue

                si_obj = ServiceInstanceDM.get(pt_obj.svc_instance)

                #get unit for irb interface
                left_svc_unit = si_obj.left_svc_unit
                right_svc_unit = si_obj.right_svc_unit

                left_svc_vlan = si_obj.left_svc_vlan
                right_svc_vlan = si_obj.right_svc_vlan

                left_vrf_info['peer'] = si_obj.left_svc_asns[0]
                left_vrf_info['local'] = si_obj.left_svc_asns[1]
                right_vrf_info['peer'] = si_obj.right_svc_asns[0]
                right_vrf_info['local'] = si_obj.right_svc_asns[1]

                #get left LR and right LR
                for lr_uuid in pt_obj.logical_routers:
                    lr_obj = LogicalRouterDM.get(lr_uuid)
                    if pr.uuid not in lr_obj.physical_routers:
                        continue
                    if pt_obj.left_lr == lr_uuid:
                        left_vrf_info['vn_id'] = lr_obj.virtual_network
                        left_vrf_info[
                            'tenant_vn'] = lr_obj.get_connected_networks(
                            include_internal=False,
                            pr_uuid=pr.uuid)
                    if pt_obj.right_lr == lr_uuid:
                        right_vrf_info[
                            'vn_id'] = lr_obj.virtual_network
                        right_vrf_info[
                            'tenant_vn'] = lr_obj.get_connected_networks(
                            include_internal=False,
                            pr_uuid=pr.uuid)

                st_obj = ServiceTemplateDM.get(si_obj.service_template)
                sas_obj = ServiceApplianceSetDM.get(st_obj.service_appliance_set)
                svc_app_obj = ServiceApplianceDM.get(
                    list(sas_obj.service_appliances)[0])

                # get the left and right interfaces
                for kvp in svc_app_obj.kvpairs or []:
                    if kvp.get('key') == "left-attachment-point":
                        left_interface = kvp.get('value')
                    if kvp.get('key') == "right-attachment-point":
                        right_interface = kvp.get('value')

                left_fq_name = left_interface.split(':')
                right_fq_name = right_interface.split(':')

                left_right_params['left_qfx_fq_name'] = left_fq_name
                left_right_params['right_qfx_fq_name'] = right_fq_name
                left_right_params['left_svc_vlan'] = left_svc_vlan
                left_right_params['right_svc_vlan'] = right_svc_vlan
                left_right_params['left_svc_unit'] = left_svc_unit
                left_right_params['right_svc_unit'] = right_svc_unit

                left_vrf_info['left_svc_unit'] = left_svc_unit
                right_vrf_info['right_svc_unit'] = right_svc_unit

                #get left srx and right srx interface IPs
                for pnf_pi, intf_type in list(svc_app_obj.physical_interfaces.items()):
                    if intf_type.get('interface_type') == "left":
                        pnf_pi_obj = PhysicalInterfaceDM.get(pnf_pi)
                        pnf_pr = pnf_pi_obj.physical_router
                        pnf_pr_obj = PhysicalRouterDM.get(pnf_pr)
                        for li in pnf_pi_obj.logical_interfaces:
                            li_obj = LogicalInterfaceDM.get(li)
                            if li_obj:
                                vlan = li_obj.fq_name[-1].split('.')[-1]
                                if left_svc_vlan == vlan:
                                    instance_ip = InstanceIpDM.get(
                                        li_obj.instance_ip)
                                    if instance_ip:
                                        left_vrf_info['srx_left_interface'] = \
                                            instance_ip.instance_ip_address
                    if intf_type.get('interface_type') == "right":
                        pnf_pi_obj = PhysicalInterfaceDM(pnf_pi)
                        for li in pnf_pi_obj.logical_interfaces:
                            li_obj = LogicalInterfaceDM.get(li)
                            if li_obj:
                                vlan = li_obj.fq_name[-1].split('.')[-1]
                                if right_svc_vlan == vlan:
                                    instance_ip = InstanceIpDM.get(
                                        li_obj.instance_ip)
                                    if instance_ip:
                                        right_vrf_info['srx_right_interface'] = \
                                            instance_ip.instance_ip_address

                if si_obj.rp_ip_addr:
                    #get rendezvous point IP addr from SI object, passed as
                    #user input
                    left_vrf_info['loopback_ip'] = si_obj.rp_ip_addr
                else:
                    #get rendezvous point IP address as srx loopback IP
                    li_name = "lo0." + left_svc_vlan
                    li_fq_name = pnf_pr_obj.fq_name + ['lo0', li_name]
                    li_obj = LogicalInterfaceDM.find_by_fq_name(li_fq_name)
                    if li_obj:
                        instance_ip = InstanceIpDM.get(li_obj.instance_ip)
                        if instance_ip:
                            left_vrf_info['loopback_ip'] = \
                                instance_ip.instance_ip_address
                self._logger.debug("PR: %s left_vrf_info: %s right_vrf_info: %s " % (pr.name,
                                                                                     left_vrf_info,
                                                                                     right_vrf_info))
                # Make sure all required parameters are present before creating the
                # abstract config
                if self.build_service_chain_required_params(left_vrf_info,
                                                            right_vrf_info):
                    self.build_service_chain_irb_bd_config(svc_app_obj, left_right_params)
                    self.build_service_chain_ri_config(si_obj.name,
                                                       left_vrf_info,
                                                       right_vrf_info)

    def build_server_config(self):
        pr = self.physical_router
        if not pr:
            return
        pi_tag = []
        for pi in pr.physical_interfaces:
            pi_obj = PhysicalInterfaceDM.get(pi)
            if not pi_obj:
                continue
            if pi_obj.port:
                port = PortDM.get(pi_obj.port)
                pi_info = {'pi': pi, 'tag': port.tags}
                pi_tag.append(pi_info)

        if not pi_tag:
            return

        for pi_info in pi_tag:
            for tag in pi_info.get('tag') or []:
                tag_obj = TagDM.get(tag)
                for vn in tag_obj.virtual_networks or []:
                    vn_obj = VirtualNetworkDM.get(vn)
                    for net_ipam in vn_obj.network_ipams or []:
                        net_ipam_obj = NetworkIpamDM.get(net_ipam)
                        for server_info in net_ipam_obj.server_discovery_params:
                            dhcp_relay_server = None
                            if server_info.get('dhcp_relay_server'):
                                dhcp_relay_server = server_info.get(
                                    'dhcp_relay_server')[0]
                            vlan_tag = server_info.get('vlan_tag') or 4094
                            default_gateway = server_info.get(
                                'default_gateway') + '/' + str(
                                server_info.get('ip_prefix_len'))

                            #create irb interface
                            irb_intf, li_map = self.set_default_pi('irb', 'irb')
                            irb_intf.set_comment("Underlay Infra BMS Access")
                            irb_intf_unit = self.set_default_li(li_map,
                                'irb.' + str(vlan_tag),
                                vlan_tag)
                            self.add_ip_address(irb_intf_unit, default_gateway)

                            # create LI
                            pi_obj = PhysicalInterfaceDM.get(pi_info.get('pi'))
                            access_intf, li_map = self.set_default_pi(
                                        pi_obj.name, 'regular')
                            access_intf.set_comment("Underlay Infra BMS Access")
                            access_intf_unit = self.set_default_li(li_map,
                                                          pi_obj.fq_name[-1]+ '.' +
                                                                   str(0), 0)
                            access_intf_unit.add_vlans(Vlan(
                                name=tag_obj.name+"_vlan"))
                            access_intf_unit.set_family("ethernet-switching")
                            if vlan_tag == 4094:
                                access_intf_unit.set_is_tagged(False)
                            else:
                                access_intf_unit.set_is_tagged(True)

                            #create Vlan
                            vlan = Vlan(name=tag_obj.name+"_vlan")
                            vlan.set_vlan_id(vlan_tag)
                            vlan.set_comment("Underlay Infra BMS Access")
                            vlan.set_l3_interface('irb.' + str(vlan_tag))
                            self.vlan_map[vlan.get_name()] = vlan

                            #set dhcp relay info
                            if dhcp_relay_server:
                                self.forwarding_options_config = self.forwarding_options_config or ForwardingOptions()
                                dhcp_relay = DhcpRelay()
                                dhcp_relay.set_comment("Underlay Infra BMS Access")
                                dhcp_relay.set_dhcp_relay_group(
                                    "SVR_RELAY_GRP_"+tag_obj.name)

                                ip_address = IpAddress(address=dhcp_relay_server)
                                dhcp_relay.add_dhcp_server_ips(ip_address)
                                self.add_ref_to_list(dhcp_relay.get_interfaces(), 'irb.' + str(vlan_tag))
                                self.forwarding_options_config.add_dhcp_relay(dhcp_relay)


    def set_common_config(self):
        if self.physical_router.underlay_managed:
            self.build_underlay_bgp()
        if not self.ensure_bgp_config():
            return
        self.build_server_config()
        self.build_bgp_config()
        self.build_ri_config()
        self.set_internal_vn_irb_config()
        self.set_internal_vn_routed_vn_config()
        self.set_dci_vn_irb_config()
        self.init_evpn_config()
        self.build_firewall_config()
        self.build_vpg_config()
        self.build_service_chaining_config()
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

    def set_internal_vn_routed_vn_config(self):
        if self.internal_vn_ris:
            for int_ri in self.internal_vn_ris:
                lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(
                    int_ri.name)
                lr = LogicalRouterDM.get(lr_uuid)
                if not lr:
                    continue
                vn_list = lr.get_connected_networks(include_internal=False,
                                                    pr_uuid=
                                                    self.physical_router.uuid)
                self.physical_router.set_routing_vn_proto_in_ri(
                    int_ri, self.routing_policies, vn_list)
    # end set_internal_vn_routed_vn_config
# end AnsibleRoleCommon
