#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of netconf interface for QFX physical router
configuration manager
"""

from db import *
from dm_utils import DMUtils
from juniper_conf import JuniperConf
from device_api.juniper_common_xsd import *

class QfxConf(JuniperConf):

    _FAMILY_MAP = {
        'route-target': '',
        'e-vpn': FamilyEvpn(signaling='')
    }

    @classmethod
    def is_product_supported(cls, name, role):
        if role and role.lower().startswith('e2-'):
            return False
        if name.lower() in cls._products:
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
        if self.physical_router.physical_router_role == 'Spine':
            return True
        return False
    # end is_spine

    def set_evpn_default_gateway_params(self, evpn_proto):
        if self.is_spine():
            evpn_proto.set_default_gateway("no-gateway-community")
        else:
            evpn_proto.set_default_gateway("do-not-advertise")

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

        self.routing_instances[ri_name] = ri_conf
        ri_config = self.ri_config or RoutingInstances(comment=DMUtils.routing_instances_comment())
        policy_config = self.policy_config or PolicyOptions(comment=DMUtils.policy_options_comment())
        ri = Instance(name=ri_name)

        ri_config.add_instance(ri)
        ri_opt = None

        # for both l2 and l3
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
        else:
            ri.set_instance_type("virtual-switch")

        # add policies for export route targets
        ps = PolicyStatement(name=DMUtils.make_export_name(ri_name))
        ps.set_comment(DMUtils.vn_ps_comment(vn, "Export"))
        then = Then()
        ps.add_term(Term(name="t1", then=then))
        for route_target in export_targets:
            comm = Community(add='',
                             community_name=DMUtils.make_community_name(route_target))
            then.add_community(comm)
            then.set_accept('')
            self.add_vni_option(network_id, DMUtils.make_community_name(route_target))
        policy_config.add_policy_statement(ps)
        self.add_to_global_switch_opts(DMUtils.make_export_name(ri_name), False)

        # add policies for import route targets
        ps = PolicyStatement(name=DMUtils.make_import_name(ri_name))
        ps.set_comment(DMUtils.vn_ps_comment(vn, "Import"))
        from_ = From()
        term = Term(name="t1", fromxx=from_)
        ps.add_term(term)
        for route_target in import_targets:
            from_.add_community(DMUtils.make_community_name(route_target))
            self.add_vni_option(network_id, DMUtils.make_community_name(route_target))
        term.set_then(Then(accept=''))
        ps.set_then(Then(reject=''))
        policy_config.add_policy_statement(ps)
        self.add_to_global_switch_opts(DMUtils.make_import_name(ri_name), True)

        # add vlan config
        if is_l2 and vni and self.is_family_configured(self.bgp_params, "e-vpn"):
            self.add_vlan_config(ri_name, vni)

        # add L2 EVPN and BD config
        interfaces_config = self.interfaces_config
        proto_config = self.proto_config
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            ri.set_vtep_source_interface("lo0.0")
            evpn = Evpn(encapsulation='vxlan', extended_vni_list='all')
            self.set_evpn_default_gateway_params(evpn)
            ri.set_protocols(RoutingInstanceProtocols(evpn=evpn))

            interfaces_config = self.interfaces_config or Interfaces(comment=DMUtils.interfaces_comment())
            if is_l2_l3:
                irb_intf = Interface(name='irb', gratuitous_arp_reply='')
                interfaces_config.add_interface(irb_intf)
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

        self.policy_config = policy_config
        self.proto_config = proto_config
        self.interfaces_config = interfaces_config
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

    def init_evpn_config(self):
        self.evpn = Evpn(encapsulation='vxlan', extended_vni_list='all')
        self.set_evpn_default_gateway_params(self.evpn)
        self.proto_config.set_evpn(self.evpn)

    def add_vni_option(self, vn_id, vrf_target):
        vni_options = self.evpn.get_vni_options()
        if not vni_options:
            vni_options = VniOptions()
            self.evpn.set_vni_options(vni_options)
        vni_options.add_vni(Vni(name=str(vn_id), vrf_target=VniTarget(community=vrf_target)))

    def init_global_switch_opts(self):
        if self.global_switch_options_config is None:
            self.global_switch_options_config = SwitchOptions(comment=DMUtils.switch_options_comment())
        self.global_switch_options_config.set_vtep_source_interface("lo0.0")
    # end init_global_switch_opts

    def add_to_global_switch_opts(self, policy, is_import):
        if is_import:
            self.global_switch_options_config.add_vrf_import(policy)
        else:
            self.global_switch_options_config.add_vrf_export(policy)
    # end add_to_global_switch_opts

    def set_route_targets_config(self):
        if self.policy_config is None:
            self.policy_config = PolicyOptions(comment=DMUtils.policy_options_comment())
        for route_target in self.route_targets:
            comm = CommunityType(name=DMUtils.make_community_name(route_target),
                                 members=route_target)
            self.policy_config.add_community(comm)
    # end set_route_targets_config

    def add_vlan_config(self, vrf_name, vni, is_l3=False, irb_intf=None):
        if not self.vlans_config:
            self.vlans_config = Vlans(comment=DMUtils.vlans_comment())
        vxlan = VXLan(vni=str(vni))
        vlan = Vlan(name=vrf_name, vlan_id=str(vni), vxlan=vxlan)
        if is_l3:
            if not irb_intf:
                self._logger.error("Missing irb interface config l3 vlan: %s" % vrf_name)
            else:
                vlan.set_l3_interface(irb_intf)
        self.vlans_config.add_vlan(vlan)
    # end add_vlan_config

    # Product Specific configuration, called from parent class
    def add_product_specific_config(self, groups):
        groups.set_switch_options(self.global_switch_options_config)
        if self.vlans_config:
            groups.set_vlans(self.vlans_config)
    # end add_product_specific_config

    def check_vn_is_allowed(self,  vn_obj):
        return True
    # end check_vn_is_allowed

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
            vn_dict.setdefault(vn_id, []).append(
                JunosInterface(li.name, li.li_type, li.vlan_tag))
        return vn_dict
    # end

    def build_ri_config(self):
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
            if not self.check_vn_is_allowed(vn_obj):
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
                        ri_conf['interfaces'] = interfaces
                        ri_conf['vni'] = vn_obj.get_vxlan_vni()
                        ri_conf['network_id'] = vn_obj.vn_network_id
                        self.add_routing_instance(ri_conf)

                    if vn_obj.get_forwarding_mode() in ['l3']:
                        interfaces = []
                        lo0_ips = vn_irb_ip_map['lo0'].get(vn_id, [])
                        ri_conf = { 'ri_name': vrf_name_l3, 'vn': vn_obj }
                        ri_conf['is_l2'] = False
                        ri_conf['is_l2_l3'] = False
                        ri_conf['import_targets'] = import_set
                        ri_conf['export_targets'] = export_set
                        ri_conf['prefixes'] = vn_obj.get_prefixes()
                        ri_conf['interfaces'] = interfaces
                        ri_conf['gateways'] = lo0_ips
                        ri_conf['network_id'] = vn_obj.vn_network_id
                        self.add_routing_instance(ri_conf)
                    break
        return
    # end build_ri_config

    def set_qfx_common_config(self):
        self.build_bgp_config()
        self.init_evpn_config()
        self.init_global_switch_opts()
        self.build_ri_config()
        self.set_route_targets_config()
        self.set_product_specific_config()
    # end set_qfx_common_config

# end QfxConf
