#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of netconf interface for QFX physical router
configuration manager
"""

from builtins import str
import copy

import abc
from device_api.juniper_common_xsd import *

from .db import AccessControlListDM, LogicalInterfaceDM, LogicalRouterDM, \
    PhysicalInterfaceDM, RoutingInstanceDM, VirtualMachineInterfaceDM, \
    VirtualNetworkDM
from .dm_utils import DMUtils
from .juniper_conf import JuniperConf
from .juniper_conf import JunosInterface

class QfxConf(JuniperConf):

    _FAMILY_MAP = {
        'route-target': '',
        'e-vpn': FamilyEvpn(signaling='')
    }

    @classmethod
    def is_product_supported(cls, name, role):
        if role and role.lower().startswith('e2-'):
            return False
        for product in cls._products or []:
            if name.lower().startswith(product.lower()):
                return True
        return False
    # end is_product_supported

    def __init__(self):
        super(QfxConf, self).__init__()
        self.evpn = None
        self.global_switch_options_config = None
        self.vlans_config = None
    # end __init__

    def is_spine(self):
        if self.physical_router.physical_router_role == 'spine':
            return True
        return False
    # end is_spine

    def initialize(self):
        self.evpn = None
        self.global_switch_options_config = None
        self.chassis_config = None
        self.vlans_config = None
        self.irb_interfaces = []
        self.internal_vn_ris = []
        super(QfxConf, self).initialize()
    # end initialize

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
                self._logger.info("DM does not support address family: %s on QFX" % fam)
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
                        int_ri.add_interface(Interface(name=irb_name))
    # end set_internal_vn_irb_config

    def add_irb_config(self, ri_conf):
        vn = ri_conf.get("vn")
        is_l2 = ri_conf.get("is_l2", False)
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        gateways = ri_conf.get("gateways", [])
        vni = ri_conf.get("vni", None)
        network_id = ri_conf.get("network_id", None)
        interfaces_config = self.interfaces_config or \
                               Interfaces(comment=DMUtils.interfaces_comment())
        self.interfaces_config = interfaces_config
        irb_intf = Interface(name='irb', gratuitous_arp_reply='')
        interfaces_config.add_interface(irb_intf)
        self._logger.info("Vn=" + vn.name + ", IRB: " +  str(gateways) + ", pr=" + self.physical_router.name)
        if gateways is not None:
            intf_unit = Unit(name=str(network_id),
                                     comment=DMUtils.vn_irb_comment(vn, False, is_l2_l3))
            irb_intf.add_unit(intf_unit)
            if self.is_spine():
                intf_unit.set_proxy_macip_advertisement('')
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
    # end add_irb_config

    # lo0 interface in RI for route lookup to happen for Inter VN traffic
    # qfx10k pfe limitation
    def add_bogus_lo0(self, ri, network_id, vn):
        if not self.is_spine():
            return
        interfaces_config = self.interfaces_config or \
                               Interfaces(comment=DMUtils.interfaces_comment())
        ifl_num = str(1000 + int(network_id))
        lo_intf = Interface(name="lo0")
        interfaces_config.add_interface(lo_intf)
        intf_unit = Unit(name=ifl_num, comment=DMUtils.l3_bogus_lo_intf_comment(vn))
        lo_intf.add_unit(intf_unit)
        family = Family()
        intf_unit.set_family(family)
        inet = FamilyInet()
        family.set_inet(inet)
        addr = Address()
        inet.add_address(addr)
        lo_ip = "127.0.0.1/32"
        addr.set_name(lo_ip)
        ri.add_interface(Interface(name="lo0." + ifl_num))
        self.interfaces_config = interfaces_config
    # end add_bogus_lo0

    '''
     ri_name: routing instance name to be configured on mx
     is_l2:  a flag used to indicate routing instance type, i.e : l2 or l3
     is_l2_l3:  VN forwarding mode is of type 'l2_l3' or not
     import/export targets: routing instance import, export targets
     prefixes: for l3 vrf static routes
     gateways: for l2 evpn
     interfaces: logical interfaces to be part of vrf
     network_id : this is used for configuraing irb interfaces
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
        interfaces = ri_conf.get("interfaces", [])
        vni = ri_conf.get("vni", None)
        network_id = ri_conf.get("network_id", None)
        is_internal_vn = True if '_contrail_lr_internal_vn_' in vn.name else False

        self.routing_instances[ri_name] = ri_conf
        ri_config = None
        policy_config = self.policy_config or \
                       PolicyOptions(comment=DMUtils.policy_options_comment())
        ri = None
        ri_opt = None
        ri_config = self.ri_config or \
                   RoutingInstances(comment=DMUtils.routing_instances_comment())
        ri = Instance(name=ri_name)
        if not is_l2:
            ri_config.add_instance(ri)
            ri.set_vrf_import(DMUtils.make_import_name(ri_name))
            ri.set_vrf_export(DMUtils.make_export_name(ri_name))

        has_ipv6_prefixes = DMUtils.has_ipv6_prefixes(prefixes)
        has_ipv4_prefixes = DMUtils.has_ipv4_prefixes(prefixes)

        if not is_l2:
            if ri_opt is None:
                ri_opt = RoutingInstanceRoutingOptions()
                ri.set_routing_options(ri_opt)

            ri.set_instance_type("vrf")
            for interface in interfaces:
                ri.add_interface(Interface(name=interface.name))
            family = Family()
            if has_ipv4_prefixes:
                family.set_inet(FamilyInet(unicast=''))
            if has_ipv6_prefixes:
                family.set_inet6(FamilyInet6(unicast=''))
            if has_ipv4_prefixes or has_ipv6_prefixes:
                auto_export = AutoExport(family=family)
                ri_opt.set_auto_export(auto_export)

        if is_internal_vn:
            self.internal_vn_ris.append(ri)
            self.add_bogus_lo0(ri, network_id, vn)

        if self.is_spine() and is_l2_l3:
            self.add_irb_config(ri_conf)
            self.attach_irb(ri_conf, ri)

        lr_uuid = None
        if is_internal_vn:
            lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(ri_name)

        # add policies for export route targets
        if self.is_spine():
            ps = PolicyStatement(name=DMUtils.make_export_name(ri_name))
            ps.set_comment(DMUtils.vn_ps_comment(vn, "Export"))
            then = Then()
            ps.add_term(Term(name="t1", then=then))
            for route_target in export_targets:
                comm = Community(add='',
                             community_name=DMUtils.make_community_name(route_target))
                then.add_community(comm)
                then.set_accept('')
            policy_config.add_policy_statement(ps)
            self.add_to_global_switch_opts(DMUtils.make_export_name(ri_name), False)

        # add policies for import route targets
        ps = PolicyStatement(name=DMUtils.make_import_name(ri_name))
        ps.set_comment(DMUtils.vn_ps_comment(vn, "Import"))

        # add term switch policy
        from_ = From()
        term = Term(name=DMUtils.get_switch_policy_name(), fromxx=from_)
        ps.add_term(term)
        from_.add_community(DMUtils.get_switch_policy_name())
        term.set_then(Then(accept=''))

        from_ = From()
        term = Term(name="t1", fromxx=from_)
        ps.add_term(term)
        for route_target in import_targets:
            from_.add_community(DMUtils.make_community_name(route_target))
            if not is_internal_vn:
                self.add_vni_option(vni or network_id, route_target)
        term.set_then(Then(accept=''))
        policy_config.add_policy_statement(ps)
        self.add_to_global_switch_opts(DMUtils.make_import_name(ri_name), True)

        # add L2 EVPN and BD config
        interfaces_config = self.interfaces_config
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            # add vlan config
            vlan_conf = self.add_vlan_config(ri_name, vni, is_l2_l3, "irb." + str(network_id))
            interfaces_config = self.interfaces_config or Interfaces(comment=DMUtils.interfaces_comment())
            self.build_l2_evpn_interface_config(interfaces_config,
                                              interfaces, vn, vlan_conf)

        if (not is_l2 and (vni is not None or (is_internal_vn and lr_uuid)) and \
                self.is_family_configured(self.bgp_params, "e-vpn")):
            evpn = self.build_evpn_config(int_vn = is_internal_vn)
            if evpn:
                ri.set_protocols(RoutingInstanceProtocols(evpn=evpn))
                if is_internal_vn and lr_uuid:
                    ip_prefix_support = IpPrefixSupport()
                    #ip_prefix_support.set_forwarding_mode("symmetric")
                    ip_prefix_support.set_encapsulation("vxlan")
                    ip_prefix_support.set_vni(str(vni))
                    ip_prefix_support.set_advertise("direct-nexthop")
                    evpn.set_ip_prefix_support(ip_prefix_support)
                else:
                    ri.set_vtep_source_interface("lo0.0")
            if not is_internal_vn:
                #add vlans
                self.add_ri_vlan_config(ri, vni)


        if (not is_l2 and not is_l2_l3 and gateways):
            interfaces_config = self.interfaces_config or \
                               Interfaces(comment=DMUtils.interfaces_comment())
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

        self.policy_config = policy_config
        self.interfaces_config = interfaces_config
        self.route_targets |= import_targets | export_targets
        self.ri_config = ri_config
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
            ethernet = FamilyEthernet()
            efilter = EthernetFilter()
            for fname in filter_list:
                efilter.add_input_list(fname)
            ethernet.set_filter(efilter)
            unit.set_family(Family(ethernet_switching=ethernet))
    # end attach_acls

    def build_l2_evpn_interface_config(self, interfaces_config, interfaces, vn, vlan_conf):
        ifd_map = {}
        for interface in interfaces:
            ifd_map.setdefault(interface.ifd_name, []).append(interface)

        for ifd_name, interface_list in list(ifd_map.items()):
            intf = Interface(name=ifd_name)
            interfaces_config.add_interface(intf)
            intf.set_flexible_vlan_tagging('')
            intf.set_encapsulation("extended-vlan-bridge")
            if interface_list[0].is_untagged():
                if (len(interface_list) > 1):
                    self._logger.error(
                        "invalid logical interfaces config for ifd %s" % (
                            ifd_name))
                    continue
                unit = Unit(name=interface_list[0].unit,
                                   comment=DMUtils.l2_evpn_intf_unit_comment(vn, False),
                                   vlan_id="4094")
                # attach acls
                self.attach_acls(interface_list[0], unit)
                intf.add_unit(unit)
                intf.set_native_vlan_id("4094")
                vlan_conf.add_interface(Interface(name=ifd_name + ".0"))
            else:
                for interface in interface_list:
                    unit = Unit(name=interface.unit,
                               comment=DMUtils.l2_evpn_intf_unit_comment(vn,
                                                     True, interface.vlan_tag),
                               vlan_id=str(interface.vlan_tag))
                    # attach acls
                    self.attach_acls(interface, unit)
                    intf.add_unit(unit)
                    vlan_conf.add_interface(Interface(name=ifd_name + "." + str(interface.unit)))
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
        self.evpn.set_extended_vni_list('all')
        if not self.is_spine():
            self.evpn.set_multicast_mode("ingress-replication")
        if not self.proto_config:
            self.proto_config = Protocols(comment=DMUtils.protocols_comment())
        self.proto_config.set_evpn(self.evpn)
    # end init_evpn_config

    def add_vni_option(self, vni, vrf_target):
        if not self.evpn:
            self.init_evpn_config()
        vni_options = self.evpn.get_vni_options()
        if not vni_options:
            vni_options = VniOptions()
            self.evpn.set_extended_vni_list("all")
        vni_options.add_vni(Vni(name=str(vni), vrf_target=VniTarget(community=vrf_target)))
        self.evpn.set_vni_options(vni_options)

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
        if self.policy_config is None:
            self.policy_config = PolicyOptions(comment=DMUtils.policy_options_comment())
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
        if not self.vlans_config:
            self.vlans_config = Vlans(comment=DMUtils.vlans_comment())
        vxlan = VXLan(vni=vni)
        vlan = Vlan(name=vrf_name[1:], vxlan=vxlan)
        if is_l2_l3 and self.is_spine():
            if not irb_intf:
                self._logger.error("Missing irb interface config l3 vlan: %s" % vrf_name)
            else:
                vlan.set_vlan_id(str(vni))
                vlan.set_l3_interface(irb_intf)
        self.vlans_config.add_vlan(vlan)
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

    # def build_esi_config(self):
    #     pr = self.physical_router
    #     if not pr or self.is_spine():
    #         return
    #     if not self.interfaces_config:
    #         self.interfaces_config = Interfaces(comment=DMUtils.interfaces_comment())
    #     for pi_uuid in pr.physical_interfaces:
    #         pi = PhysicalInterfaceDM.get(pi_uuid)
    #         if not pi or not pi.esi or pi.esi == "0" or pi.get_parent_ae_id():
    #             continue
    #         esi_conf = Esi(identifier=pi.esi, all_active='')
    #         intf = Interface(name=pi.name, esi=esi_conf)
    #         self.interfaces_config.add_interface(intf)
    #     # add ae interfaces
    #     # self.ae_id_map should have all esi => ae_id mapping
    #     for esi, ae_id in self.physical_router.ae_id_map.items():
    #         esi_conf = Esi(identifier=esi, all_active='')
    #         intf = Interface(name="ae" + str(ae_id), esi=esi_conf)
    #         self.interfaces_config.add_interface(intf)
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
                # ae_id = pi.get_parent_ae_id()
                # if ae_id and li.physical_interface:
                #     _, unit= li.name.split('.')
                #     ae_name = "ae" + str(ae_id) + "." + unit
                #     vn_dict.setdefault(vn_id, []).append(
                #            JunosInterface(ae_name, li.li_type, li.vlan_tag))
                #     continue
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
        interfaces_config = self.interfaces_config or \
                    Interfaces(comment=DMUtils.interfaces_comment())
        # self.ae_id_map should have all esi => ae_id mapping
        # esi_map should have esi => interface memberships
        for esi, ae_id in list(self.physical_router.ae_id_map.items()):
            # config ae interface
            ae_name = "ae" + str(ae_id)
            intf = Interface(name=ae_name)
            interfaces_config.add_interface(intf)
            priority = DMUtils.lacp_system_priority()
            system_id = esi[-17:] #last 17 charcaters from esi for ex: 00:00:00:00:00:05
            lacp = Lacp(active='', system_priority=priority, \
                          system_id=system_id, admin_key=1)
            intf.set_aggregated_ether_options(AggregatedEtherOptions(lacp=lacp))
            # associate 'ae' membership
            pi_list = esi_map.get(esi)
            for pi in pi_list or []:
                intf = Interface(name=pi.name)
                interfaces_config.add_interface(intf)
                etherOptions = EtherOptions(ieee_802_3ad=Ieee802(bundle=ae_name))
                intf.set_gigether_options(etherOptions)
        self.interfaces_config = interfaces_config
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
            from_.add_ip_source_address(str(subnet_ip) + "/" + str(subnet_len))
        else:
            from_.add_ip_destination_address(str(subnet_ip) + "/" + str(subnet_len))
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
            from_.add_source_port(port_str)
        else:
            from_.add_destination_port(port_str)
    # end add_port_term

    def add_filter_term(self, ff, name):
        term = Term()
        term.set_name(name)
        ff.add_term(term)
        term.set_then(Then(accept=''))
        return term

    def add_protocol_term(self, term, protocol_match):
        if not protocol_match or protocol_match == 'any':
            return None
        from_ = term.get_from() or From()
        term.set_from(from_)
        from_.set_ip_protocol(protocol_match)
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
            # allow arp ether type always
            self.add_ether_type_term(f, 'arp')
            # allow dhcp/dns always
            self.add_dns_dhcp_terms(f)
            default_term = self.add_filter_term(f, "default-term")
            self.add_addr_term(default_term, dst_addr_match, False)
            self.add_addr_term(default_term, src_addr_match, True)
            self.add_port_term(default_term, dst_port_match, False)
            # source port match is not needed for now (BMS source port)
            #self.add_port_term(default_term, src_port_match, True)
            self.add_protocol_term(default_term, protocol_match)
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
            if acl and not acl.is_ingress:
                fnames = self.get_firewall_filters(sg, acl)
                filter_names += fnames
        return filter_names
    # end get_configured_filters

    def build_ri_config(self):
        # if not self.is_spine():
        #     esi_map = self.get_ae_alloc_esi_map()
        #     self.physical_router.evaluate_ae_id_map(esi_map)
        #     self.build_ae_config(esi_map)
        vn_dict = self.get_vn_li_map()
        vn_irb_ip_map = None
        if self.is_spine():
            self.physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l2_l3', 'irb', False)
            self.physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l3', 'lo0', True)
            vn_irb_ip_map = self.physical_router.get_vn_irb_ip_map()

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

                        ri_conf = { 'ri_name': vrf_name_l2, 'vn': vn_obj }
                        ri_conf['is_l2'] = True
                        ri_conf['is_l2_l3'] = (vn_obj.get_forwarding_mode() == 'l2_l3')
                        ri_conf['import_targets'] = import_set
                        if self.is_spine():
                            ri_conf['export_targets'] = export_set
                        ri_conf['prefixes'] = vn_obj.get_prefixes()
                        ri_conf['gateways'] = irb_ips
                        ri_conf['interfaces'] = interfaces
                        ri_conf['vni'] = vn_obj.get_vxlan_vni()
                        ri_conf['network_id'] = vn_obj.vn_network_id
                        self.add_routing_instance(ri_conf)

                    is_internal_vn = True if '_contrail_lr_internal_vn_' in vn_obj.name else False
                    if vn_obj.get_forwarding_mode() in ['l3'] and self.is_l3_supported(vn_obj):
                        interfaces = []
                        lo0_ips = vn_irb_ip_map['lo0'].get(vn_id, [])
                        ri_conf = { 'ri_name': vrf_name_l3, 'vn': vn_obj }
                        ri_conf['is_l2'] = False
                        ri_conf['is_l2_l3'] = False
                        ri_conf['import_targets'] = import_set
                        ri_conf['export_targets'] = export_set
                        ri_conf['prefixes'] = vn_obj.get_prefixes()
                        ri_conf['interfaces'] = interfaces
                        if is_internal_vn:
                            ri_conf['vni'] = vn_obj.get_vxlan_vni(is_internal_vn = is_internal_vn)
                        ri_conf['gateways'] = lo0_ips
                        ri_conf['network_id'] = vn_obj.vn_network_id
                        self.add_routing_instance(ri_conf)
                    break
        return
    # end build_ri_config

    def set_qfx_common_config(self):
        self.build_bgp_config()
        self.build_ri_config()
        self.set_internal_vn_irb_config()
        self.init_evpn_config()
        self.build_firewall_config()
        self.init_global_switch_opts()
        self.set_resolve_bgp_route_target_family_config()
        # self.build_esi_config()
        self.set_route_targets_config()
        self.set_route_distinguisher_config()
    # end set_qfx_common_config

# end QfxConf
