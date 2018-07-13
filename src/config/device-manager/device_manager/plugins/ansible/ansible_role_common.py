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

    _FAMILY_MAP = {
        'route-target': '',
        'e-vpn': ''
    }

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

    def is_spine(self):
        if self.physical_router.physical_router_role == 'spine':
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
        self.evpn = None
        self.global_switch_options_config = None
        self.chassis_config = None
        self.vlans_config = None
        self.irb_interfaces = []
        self.internal_vn_ris = []
    # end initialize

    def add_families(self, parent, params):
        if params.get('address_families') is None:
            return
        families = params['address_families'].get('family', [])
        if not families:
            return
        for family in families:
            if family in self._FAMILY_MAP:
                parent.add_families(family)
            else:
                self._logger.info("DM does not support address family: %s on "
                                  "QFX" % family)
    # end add_families

    def attach_irb(self, ri_conf, ri):
        if not self.is_spine():
            return
        is_l2 = ri_conf.get("is_l2", False)
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        vni = ri_conf.get("vni", None)
        network_id = ri_conf.get("network_id", None)
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            if is_l2_l3:
                ri.add_routing_interfaces.append(LogicalInterface(
                    name="irb." + str(network_id)))
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
                        int_ri.add_interface(Interface(name=irb_name))
    # end set_internal_vn_irb_config

    def add_irb_config(self, ri_conf):
        vn = ri_conf.get("vn")
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        gateways = ri_conf.get("gateways", [])
        network_id = ri_conf.get("network_id", None)
        self.interfaces_config = self.interfaces_config or []
        irb_intf = PhysicalInterface(name='irb')
        self.interfaces_config.append(irb_intf)
        self._logger.info("Vn=" + vn.name + ", IRB: " + str(gateways) + ", pr="
                          + self.physical_router.name)
        if gateways is not None:
            intf_unit = LogicalInterface(
                name=str(network_id), comment=DMUtils.vn_irb_comment(vn, False,
                                                                     is_l2_l3))
            irb_intf.add_logical_interfaces(intf_unit)
            for (irb_ip, gateway) in gateways:
                ip = IpType(name=irb_ip, comment=DMUtils.irb_ip_comment(irb_ip))
                intf_unit.add_ip_list(ip)
                if len(gateway) and gateway != '0.0.0.0':
                    ip.set_address(gateway)
    # end add_irb_config

    # lo0 interface in RI for route lookup to happen for Inter VN traffic
    # qfx10k pfe limitation
    def add_bogus_lo0(self, ri, network_id, vn):
        if not self.is_spine():
            return
        self.interfaces_config = self.interfaces_config or []
        ifl_num = str(1000 + int(network_id))
        lo_intf = PhysicalInterface(name="lo0")
        self.interfaces_config.append(lo_intf)
        intf_unit = LogicalInterface(
            name=ifl_num, comment=DMUtils.l3_bogus_lo_intf_comment(vn))
        intf_unit.add_ip_list(IpType(address="127.0.0.1"))
        lo_intf.add_logical_interfaces(intf_unit)
        ri.add_loopback_interfaces(LogicalInterface(name="lo0." + ifl_num))
    # end add_bogus_lo0

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

        self.routing_instances[ri_name] = ri_conf
        self.ri_config = self.ri_config or []
        self.policy_config = self.policy_config or\
                             Policy(comment=DMUtils.policy_options_comment())
        ri = RoutingInstance(name=ri_name)
        if vn:
            is_nat = True if fip_map else False
            ri.set_comment(DMUtils.vn_ri_comment(vn, is_l2, is_l2_l3, is_nat,
                                                 router_external))
        self.ri_config.append(ri)

        ri.set_is_public_network(router_external)

        has_ipv6_prefixes = DMUtils.has_ipv6_prefixes(prefixes)
        has_ipv4_prefixes = DMUtils.has_ipv4_prefixes(prefixes)

        if not is_l2:
            ri.set_instance_type("vrf")
            if fip_map is None:
                for interface in interfaces:
                    ri.add_interfaces(LogicalInterface(name=interface.name))
        else:
            if highest_encapsulation_priority == "VXLAN":
                ri.set_routing_instance_type("virtual-switch")
            elif highest_encapsulation_priority in ["MPLSoGRE", "MPLSoUDP"]:
                ri.set_routing_instance_type("evpn")

        if is_internal_vn:
            self.internal_vn_ris.append(ri)
            self.add_bogus_lo0(ri, network_id, vn)

        if self.is_spine() and is_l2_l3:
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
                floating_ips = []
                ri.add_floating_ip_list(FloatingIpList(
                    public_routing_instance=public_vrf,
                    floating_ips=floating_ips))
                for fip in fips:
                    floating_ips.append(FloatingIpMap(floating_ip=fip))

        # add policies for export route targets
        if self.is_spine():
            p = PolicyRule(name=DMUtils.make_export_name(ri_name))
            p.set_comment(DMUtils.vn_ps_comment(vn, "Export"))
            then = Then()
            p.add_term(Term(name="t1", then=then))
            for route_target in export_targets:
                then.add_community(DMUtils.make_community_name(route_target))
            then.set_accept_or_reject(True)
            self.policy_config.add_policy_rule(p)

        # add policies for import route targets
        p = PolicyRule(name=DMUtils.make_import_name(ri_name))
        p.set_comment(DMUtils.vn_ps_comment(vn, "Import"))

        # add term switch policy
        from_ = From()
        term = Term(name=DMUtils.get_switch_policy_name(), fromxx=from_)
        p.add_term(term)
        from_.add_community(DMUtils.get_switch_policy_name())
        term.set_then(Then(accept_or_reject=True))

        from_ = From()
        term = Term(name="t1", fromxx=from_)
        p.add_term(term)
        for route_target in import_targets:
            from_.add_community(DMUtils.make_community_name(route_target))
            if not is_internal_vn:
                self.add_vni_option(vni or network_id, route_target)
        term.set_then(Then(accept_or_reject=True))
        self.policy_config.add_policy_rule(p)

        # add L2 EVPN and BD config
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            # add vlan config
            vlan_conf = self.add_vlan_config(ri_name, vni, is_l2_l3, "irb." + str(network_id))
            self.interfaces_config = self.interfaces_config or []
            self.build_l2_evpn_interface_config(self.interfaces_config,
                                                interfaces, vn, vlan_conf)

        if (not is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            ri.set_vtep_source_interface("lo0.0")
            evpn = self.build_evpn_config()
            if evpn:
                ri.set_protocols(RoutingInstanceProtocols(evpn=evpn))
            #add vlans
            self.add_ri_vlan_config(ri, vni)

        if (not is_l2 and not is_l2_l3 and gateways):
            self.interfaces_config = self.interfaces_config or []
            ifl_num = str(1000 + int(network_id))
            lo_intf = Interface(name="lo0")
            interfaces_config.add_interface(lo_intf)
            intf_unit = Unit(name=ifl_num, comment=DMUtils.l3_lo_intf_comment(vn))
            lo_intf.add_logical_interfaces(intf_unit)
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

        self.route_targets |= import_targets | export_targets
    # end add_routing_instance

    def attach_acls(self, interface, unit):
        if self.is_spine() or not interface.li_uuid:
            return
        interface = LogicalInterfaceDM.find_by_name_or_uuid(interface.li_uuid)
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

    def build_l2_evpn_interface_config(self, interfaces_config, interfaces, vn, vlan_conf):
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
                unit = LogicalInterface(
                    name=interface_list[0].unit,
                    comment=DMUtils.l2_evpn_intf_unit_comment(vn, False),
                    vlan_tag="4094")
                # attach acls
                self.attach_acls(interface_list[0], unit)
                intf.add_logical_interfaces(unit)
                vlan_conf.add_interfaces(LogicalInterface(name=ifd_name + ".0"))
            else:
                for interface in interface_list:
                    unit = LogicalInterface(
                        name=interface.unit,
                        comment=DMUtils.l2_evpn_intf_unit_comment(
                            vn, True, interface.vlan_tag),
                        vlan_tag=str(interface.vlan_tag))
                    # attach acls
                    self.attach_acls(interface, unit)
                    intf.add_logical_interfaces(unit)
                    vlan_conf.add_interfaces(LogicalInterface(name=ifd_name + "." + str(interface.unit)))
    # end build_l2_evpn_interface_config

    @abc.abstractmethod
    def build_evpn_config(self):
        """build evpn config depending on qfx model"""
    # end build_evpn_config

    def init_evpn_config(self):
        if not self.routing_instances:
            # no vn config then no need to configure evpn
            return
        if self.evpn:
            # evpn init done
            return
        self.evpn = self.build_evpn_config()
        if not self.is_spine():
            self.evpn.set_multicast_mode("ingress-replication")
    # end init_evpn_config

    def add_vni_option(self, vni, vrf_target):
        if not self.evpn:
            self.init_evpn_config()

    def init_global_switch_opts(self):
        if self.global_switch_options_config is None:
            self.global_switch_options_config = SwitchOptions(comment=DMUtils.switch_options_comment())
        self.global_switch_options_config.set_vtep_source_interface("lo0.0")
        if not self.routing_instances:
            # no vn config then no need to configure vrf target
            return
        self.global_switch_options_config.add_vrf_target(VniTarget(auto=''))
        switch_options_community = DMUtils.get_switch_vrf_import(self.get_asn())
        self.global_switch_options_config.add_vrf_target(VniTarget(community=switch_options_community))
        self.set_global_export_policy()
    # end init_global_switch_opts

    def set_global_export_policy(self):
        if self.is_spine():
            return

        export_policy = DMUtils.get_switch_export_policy_name()
        ps = PolicyStatement(name=export_policy)
        ps.set_comment(DMUtils.switch_export_policy_comment())

        export_community = DMUtils.get_switch_export_community_name()
        then = Then()
        comm = Community(add='', community_name=export_community)
        then.add_community(comm)
        ps.add_term(Term(name="t1", then=then))

        if not self.policy_config:
            self.policy_config = PolicyOptions(comment=DMUtils.policy_options_comment())
        self.policy_config.add_policy_statement(ps)
        if not self.global_switch_options_config:
            self.global_switch_options_config = SwitchOptions(comment=DMUtils.switch_options_comment())
        self.global_switch_options_config.add_vrf_export(export_policy)
    # end set_global_export_policy

    def add_to_global_switch_opts(self, policy, is_import):
        if not self.global_switch_options_config:
            self.init_global_switch_opts()
        if is_import:
            self.global_switch_options_config.add_vrf_import(policy)
        else:
            self.global_switch_options_config.add_vrf_export(policy)
    # end add_to_global_switch_opts

    def set_route_targets_config(self):
        self.policy_config = self.policy_config or\
                             Policy(comment=DMUtils.policy_options_comment())
        # add export community
        export_comm = CommunityType(name=DMUtils.get_switch_export_community_name())
        for route_target in self.route_targets:
            comm = CommunityType(name=DMUtils.make_community_name(route_target))
            comm.add_members(route_target)
            self.policy_config.add_community(comm)
            # add route-targets to export community
            export_comm.add_members(route_target)
        # if no members, no need to add community
        if export_comm.get_members():
            self.policy_config.add_community(export_comm)
        # add community for switch options
        comm = CommunityType(name=DMUtils.get_switch_policy_name())
        comm.add_members(DMUtils.get_switch_vrf_import(self.get_asn()))
        self.policy_config.add_community(comm)
    # end set_route_targets_config

    def add_vlan_config(self, vrf_name, vni, is_l2_l3=False, irb_intf=None):
        self.vlans_config = self.vlans_config or []
        vlan = Vlan(name=vrf_name[1:], vxlan_id=vni)
        if is_l2_l3 and self.is_spine():
            if not irb_intf:
                self._logger.error("Missing irb interface config l3 vlan: %s" % vrf_name)
            else:
                vlan.set_vlan_id(str(vni))
                vlan.set_l3_interface(irb_intf)
        self.vlans_config.append(vlan)
        return vlan
    # end add_vlan_config

    def add_ri_vlan_config(self, ri, vni):
        vxlan = VXLan(vni=vni)
        vlan = Vlan(name=vrf_name[1:], vlan_id=str(vni), vxlan=vxlan)
        vlans = ri.get_vlans()
        if not vlans:
            vlans = Vlans()
        vlans.add_vlan(vlan)
        ri.set_vlans(vlans)
    # end add_ri_vlan_config

    # Product Specific configuration, called from parent class
    def add_product_specific_config(self, groups):
        groups.set_switch_options(self.global_switch_options_config)
        if self.vlans_config:
            groups.set_vlans(self.vlans_config)
        if self.chassis_config:
            groups.set_chassis(self.chassis_config)
    # end add_product_specific_config

    def set_route_distinguisher_config(self):
        if not self.routing_instances or not self.bgp_params.get('identifier'):
            # no vn config then no need to configure route distinguisher
            return
        if self.global_switch_options_config is None:
            self.global_switch_options_config = SwitchOptions(comment=DMUtils.switch_options_comment())
        self.global_switch_options_config.set_route_distinguisher(
                                 RouteDistinguisher(rd_type=self.bgp_params['identifier'] + ":1"))
    # end set_route_distinguisher_config

    def build_esi_config(self):
        pr = self.physical_router
        if not pr or self.is_spine():
            return
        self.interfaces_config = self.interfaces_config or []
        for pi_uuid in pr.physical_interfaces:
            pi = PhysicalInterfaceDM.get(pi_uuid)
            if not pi or not pi.esi or pi.esi == "0" or pi.get_parent_ae_id():
                continue
            intf = PhysicalInterface(name=pi.name,
                                     ethernet_segment_identifier=pi.esi)
            self.interfaces_config.append(intf)
        # add ae interfaces
        # self.ae_id_map should have all esi => ae_id mapping
        for esi, ae_id in self.physical_router.ae_id_map.items():
            intf = PhysicalInterfaceInterface(name="ae" + str(ae_id),
                                              ethernet_segment_identifier=esi)
            self.interfaces_config.append(intf)
    # end build_esi_config

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

    def is_l2_supported(self, vn):
        """ Check l2 capability """
        return True
    # end is_l2_supported

    @abc.abstractmethod
    def is_l3_supported(self, vn):
        """ Check l3 capability """
        return False
    # end is_l3_supported

    def set_resolve_bgp_route_target_family_config(self):
        """ configure resolution config in global routing options if needed """
        if not self.global_routing_options_config:
            self.global_routing_options_config = RoutingOptions(
                                       comment=DMUtils.routing_options_comment())
        resolve = Resolution(rib=RIB(name="bgp.rtarget.0",
                                       resolution_ribs="inet.0"))
        self.global_routing_options_config.set_resolution(resolve)
    # end set_resolve_bgp_route_target_family_config

    def set_chassis_config(self):
        device_count =  DMUtils.get_max_ae_device_count()
        aggr_devices = AggregatedDevices(Ethernet(device_count=device_count))
        if not self.chassis_config:
            self.chassis_config = Chassis()
        self.chassis_config.set_aggregated_devices(aggr_devices)
    # end set_chassis_config

    def build_ae_config(self, esi_map):
        if esi_map:
            self.set_chassis_config()
        self.interfaces_config = self.interfaces_config or []
        # self.ae_id_map should have all esi => ae_id mapping
        # esi_map should have esi => interface memberships
        for esi, ae_id in self.physical_router.ae_id_map.items():
            # config ae interface
            ae_name = "ae" + str(ae_id)
            intf = PhysicalInterface(name=ae_name)
            self.interfaces_config.append(intf)
            # associate 'ae' membership
            pi_list = esi_map.get(esi)
            for pi in pi_list or []:
                intf = PhysicalInterface(name=pi.name,
                                         ethernet_segment_identifier=ae_name)
                self.interfaces_config.append(intf)
    # end build_ae_config

    def add_addr_term(self, ff, addr_match, is_src):
        if not addr_match:
            return None
        subnet = addr_match.get_subnet()
        if not subnet:
            return None
        subnet_ip = subnet.get_ip_prefix()
        subnet_len = subnet.get_ip_prefix_len()
        if not subnet_ip or not subnet_len:
            return None
        term = Term()
        from_ = From()
        term.set_from(from_)
        if is_src:
            term.set_name("src-addr-prefix")
            src_prefix_list = SourcePrefixList(name=str(subnet_ip) + "/" + str(subnet_len))
            from_.set_source_prefix_list(src_prefix_list)
        else:
            term.set_name("dst-addr-prefix")
            dst_prefix_list = DestinationPrefixList(name=str(subnet_ip) + "/" + str(subnet_len))
            from_.set_destination_prefix_list(dst_prefix_list)
        term.set_then(Then(accept=''))
        ff.add_term(term)
    # end add_addr_term

    def add_port_term(self, ff, port_match, is_src):
        if not port_match:
            return None
        start_port = port_match.get_start_port()
        end_port = port_match.get_end_port()
        if not start_port or not end_port:
            return None
        port_str = str(start_port) + "-" + str(end_port)
        term = Term()
        from_ = From()
        term.set_from(from_)
        if is_src:
            term.set_name("src-port")
            from_.add_source_port(port_str)
        else:
            term.set_name("dst-port")
            from_.add_destination_port(port_str)
        term.set_then(Then(accept=''))
        ff.add_term(term)
    # end add_port_term

    def add_protocol_term(self, ff, protocol_match):
        if not protocol_match or protocol_match == 'any':
            return None
        term = Term()
        from_ = From()
        term.set_from(from_)
        term.set_name("protocol")
        from_.set_ip_protocol(protocol_match)
        term.set_then(Then(accept=''))
        ff.add_term(term)
    # end add_protocol_term

    def add_dns_dhcp_terms(self, ff):
        port_list = [67, 68, 53]
        term = Term()
        term.set_name("allow-dns-dhcp")
        from_ = From()
        from_.set_ip_protocol("udp")
        term.set_from(from_)
        for port in port_list:
            from_.add_source_port(str(port))
        term.set_then(Then(accept=''))
        ff.add_term(term)
    # end add_dns_dhcp_terms

    def add_ether_type_term(self, ff, ether_type_match):
        if not ether_type_match:
            return None
        term = Term()
        from_ = From()
        term.set_from(from_)
        term.set_name("ether-type")
        from_.set_ether_type(ether_type_match.lower())
        term.set_then(Then(accept=''))
        ff.add_term(term)
    # end add_ether_type_term

    def build_firewall_filters(self, sg, acl, is_egress=False):
        if self.is_spine():
            return
        if not sg or not acl or not acl.vnc_obj:
            return
        acl = acl.vnc_obj
        entries = acl.get_access_control_list_entries()
        if not entries:
            return
        rules = entries.get_acl_rule() or []
        if not rules:
            return
        firewall_config = self.firewall_config or Firewall(DMUtils.firewall_comment())
        ff = firewall_config.get_family() or FirewallFamily()
        firewall_config.set_family(ff)
        eswitching = ff.get_ethernet_switching() or FirewallEthernet()
        ff.set_ethernet_switching(eswitching)
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
            filter_name = DMUtils.make_sg_filter_name(sg.name, ether_type_match, rule_uuid)
            f = FirewallFilter(name=filter_name)
            f.set_comment(DMUtils.sg_firewall_comment(sg.name, ether_type_match, rule_uuid))
            self.add_addr_term(f, dst_addr_match, False)
            self.add_addr_term(f, src_addr_match, True)
            self.add_port_term(f, dst_port_match, False)
            self.add_port_term(f, src_port_match, False)
            # allow arp ether type always
            self.add_ether_type_term(f, 'arp')
            self.add_protocol_term(f, protocol_match)
            # allow dhcp/dns always
            self.add_dns_dhcp_terms(f)
            eswitching.add_filter(f)
        if not eswitching.get_filter():
            ff.set_ethernet_switching(None)
        self.firewall_config = firewall_config
    # end build_firewall_filters

    def build_firewall_config(self):
        if self.is_spine():
            return
        sg_list = LogicalInterfaceDM.get_sg_list()
        for sg in sg_list or []:
            acls = sg.access_control_lists
            for acl in acls or []:
                acl = AccessControlListDM.get(acl)
                if acl and acl.is_ingress:
                    self.build_firewall_filters(sg, acl)
    # end build_firewall_config

    def has_terms(self, rule):
        match = rule.get_match_condition()
        if not match or match.get_protocol() == 'any':
            return False
        return match.get_dst_address() or match.get_dst_port() or \
              match.get_ethertype() or match.get_src_address() or match.get_src_port()

    def get_firewall_filters(self, sg, acl, is_egress=False):
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
            filter_name = DMUtils.make_sg_filter_name(sg.name, ether_type_match, rule_uuid)
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
            if acl and acl.is_ingress:
                fnames = self.get_firewall_filters(sg, acl)
                filter_names += fnames
        return filter_names
    # end get_configured_filters

    def build_ri_config(self):
        if not self.is_spine():
            esi_map = self.get_ae_alloc_esi_map()
            self.physical_router.evaluate_ae_id_map(esi_map)
            self.build_ae_config(esi_map)
        vn_dict = self.get_vn_li_map()
        vn_irb_ip_map = None
        if self.is_spine():
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
                    if self.is_spine():
                        for ri2_id in ri_obj.routing_instances:
                            ri2 = RoutingInstanceDM.get(ri2_id)
                            if ri2 is None:
                                continue
                            import_set |= ri2.export_targets

                    if vn_obj.get_forwarding_mode() in ['l2', 'l2_l3']:
                        irb_ips = None
                        if vn_obj.get_forwarding_mode() == 'l2_l3' and self.is_spine():
                            irb_ips = vn_irb_ip_map['irb'].get(vn_id, [])

                        ri_conf = {'ri_name': vrf_name_l2, 'vn': vn_obj,
                                   'is_l2': True, 'is_l2_l3': (
                                        vn_obj.get_forwarding_mode() == 'l2_l3'),
                                   'import_targets': import_set,
                                   'prefixes': vn_obj.get_prefixes(),
                                   'gateways': irb_ips,
                                   'router_external': vn_obj.router_external,
                                   'interfaces': interfaces,
                                   'vni': vn_obj.get_vxlan_vni(),
                                   'network_id': vn_obj.vn_network_id,
                                   'highest_encapsulation_priority':
                                       GlobalVRouterConfigDM.
                                           global_encapsulation_priority}
                        if self.is_spine():
                            ri_conf['export_targets'] = export_set
                        self.add_routing_instance(ri_conf)

                    if vn_obj.get_forwarding_mode() in ['l3'] and self.is_l3_supported(vn_obj):
                        interfaces = []
                        lo0_ips = vn_irb_ip_map['lo0'].get(vn_id, [])
                        ri_conf = {'ri_name': vrf_name_l3, 'vn': vn_obj,
                                   'is_l2': False, 'is_l2_l3': False,
                                   'import_targets': import_set,
                                   'export_targets': export_set,
                                   'prefixes': vn_obj.get_prefixes(),
                                   'router_external': vn_obj.router_external,
                                   'interfaces': interfaces,
                                   'gateways': lo0_ips,
                                   'network_id': vn_obj.vn_network_id}
                        self.add_routing_instance(ri_conf)
                    break

            if export_set and len(vn_obj.instance_ip_map) > 0:
                vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                 vn_obj.vn_network_id,
                                                 'l3', True)
                ri_conf = {'ri_name': vrf_name, 'vn': vn_obj,
                           'import_targets': import_set,
                           'fip_map': vn_obj.instance_ip_map,
                           'network_id': vn_obj.vn_network_id}
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
        self.set_route_targets_config()
    # end set_common_config

# end AnsibleRoleCommon
