#
# Copyright (c) 2020 Juniper Networks, Inc. All rights reserved.
#

"""DC Gateway Feature Implementation."""
from builtins import str
from collections import OrderedDict
import copy

from abstract_device_api.abstract_device_xsd import Feature, Firewall, \
    FirewallFilter, From, NatRule, NatRules, RoutingInstance, Term, Then
import gevent

from .db import GlobalVRouterConfigDM, LogicalRouterDM, PhysicalInterfaceDM, \
    RoutingInstanceDM, VirtualMachineInterfaceDM, VirtualNetworkDM, \
    VirtualPortGroupDM
from .dm_utils import DMUtils
from .feature_base import FeatureBase


class JunosInterface(object):

    def __init__(self, if_name, if_type, if_vlan_tag=0, if_ip=None,
                 li_uuid=None, port_vlan_tag=4094, vpg_obj=None):
        """Initialize JunosInterface init params."""
        self.li_uuid = li_uuid
        self.name = if_name
        self.if_type = if_type
        self.vlan_tag = if_vlan_tag
        ifparts = if_name.split('.')
        self.ifd_name = ifparts[0]
        self.unit = ifparts[1]
        self.ip = if_ip
    # end __init__

    def is_untagged(self):
        return not self.vlan_tag
    # end is_untagged
# end class JunosInterface


class DcGatewayFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'dc-gateway'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.ri_map = {}
        self.firewall_config = None
        self.pi_map = OrderedDict()
        self.inet4_forwarding_filter = None
        self.inet6_forwarding_filter = None
        super(DcGatewayFeature, self).__init__(
            logger, physical_router, configs)
    # end __init__

    def _get_export_import_set(self, vn_obj, ri_obj):
        export_set = None
        import_set = None
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
        return export_set, import_set
    # end _get_export_import_set

    def _add_ri_prefixes(self, vn, router_external, interfaces, prefixes, ri):
        for interface in interfaces:
            self._add_ref_to_list(
                ri.get_interfaces(), interface.name)
        if len(prefixes) < 1:
            return
        # for DC-gateway, skip routed vn prefix for public LR
        routed_vn_prefix = set()
        if vn:
            routed_vn_prefix = vn.get_prefixes(
                pr_uuid=self._physical_router.uuid,
                only_routedvn_prefix=True)
        for prefix in prefixes:
            ri.add_static_routes(
                self._get_route_for_cidr(prefix))
            if router_external and prefix in routed_vn_prefix:
                continue
            ri.add_prefixes(self._get_subnet_for_cidr(prefix))
        # if vn internal then also add rib interfaces since in
        # overlay_networking we use this to filter out irb interfaces to set.
        if router_external and '_contrail_lr_internal_vn_' in vn.name:
            lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(vn.name)
            lr = LogicalRouterDM.get(lr_uuid)
            if lr:
                vn_list = lr.get_connected_networks(
                    include_internal=False,
                    pr_uuid=self._physical_router.uuid)
                for vn in vn_list:
                    vn_obj = VirtualNetworkDM.get(vn)
                    if vn_obj and vn_obj.vn_network_id is not None:
                        irb_name = "irb." + str(vn_obj.vn_network_id)
                        self._add_ref_to_list(
                            ri.get_routing_interfaces(), irb_name)
    # end _add_ri_prefixes

    def _add_inet_public_vrf_filter(cls, firewall_config, inet_type):
        firewall_config.set_family(inet_type)
        f = FirewallFilter(name=DMUtils.make_public_vrf_filter_name(inet_type))
        f.set_comment(DMUtils.public_vrf_filter_comment())
        firewall_config.add_firewall_filters(f)
        term = Term(name="default-term", then=Then(accept_or_reject=True))
        f.add_terms(term)
        return f
    # end _add_inet_public_vrf_filter

    def _add_inet_filter_term(self, ri_name, prefixes, inet_type):
        if inet_type == 'inet6':
            prefixes = DMUtils.get_ipv6_prefixes(prefixes)
        else:
            prefixes = DMUtils.get_ipv4_prefixes(prefixes)
        from_ = From()
        for prefix in prefixes:
            from_.add_destination_address(self._get_subnet_for_cidr(prefix))
        then_ = Then()
        then_.add_routing_instance(ri_name)
        return Term(name=DMUtils.make_vrf_term_name(ri_name),
                    fromxx=from_, then=then_)
    # end _add_inet_filter_term

    def _check_term_exist(self, new_term_name):
        for t in self.inet4_forwarding_filter.get_terms() or []:
            if t.name == new_term_name:
                return True
        return False
    # end _check_term_exist

    def _add_ri_vrf_firewall_config(self, prefixes, ri):
        has_ipv6_prefixes = DMUtils.has_ipv6_prefixes(prefixes)
        has_ipv4_prefixes = DMUtils.has_ipv4_prefixes(prefixes)

        term_ri_name = ri.get_name()
        if ri.get_virtual_network_is_internal():
            term_ri_name = ri.get_description()
        self.firewall_config = self.firewall_config or Firewall(
            comment=DMUtils.firewall_comment())
        if has_ipv4_prefixes and not self.inet4_forwarding_filter:
            # create single instance inet4 filter
            self.inet4_forwarding_filter = self. \
                _add_inet_public_vrf_filter(self.firewall_config, "inet")
        if has_ipv6_prefixes and not self.inet6_forwarding_filter:
            # create single instance inet6 filter
            self.inet6_forwarding_filter = self. \
                _add_inet_public_vrf_filter(self.firewall_config, "inet6")

        if self._check_term_exist(DMUtils.make_vrf_term_name(term_ri_name)):
            return
        if has_ipv4_prefixes:
            # add terms to inet4 filter
            term = self._add_inet_filter_term(
                term_ri_name, prefixes, "inet4")
            # insert before the last term
            terms = self.inet4_forwarding_filter.get_terms()
            terms = [term] + (terms or [])
            self.inet4_forwarding_filter.set_terms(terms)
        if has_ipv6_prefixes:
            # add terms to inet6 filter
            term = self._add_inet_filter_term(
                term_ri_name, prefixes, "inet6")
            # insert before the last term
            terms = self.inet6_forwarding_filter.get_terms()
            terms = [term] + (terms or [])
            self.inet6_forwarding_filter.set_terms(terms)
    # end _add_ri_firewall_config

    def _add_routing_instance(self, ri_conf):
        gevent.idle()
        ri_name = ri_conf.get("ri_name")
        vn = ri_conf.get("vn")
        is_l2 = ri_conf.get("is_l2", False)
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        import_targets = ri_conf.get("import_targets", set())
        export_targets = ri_conf.get("export_targets", set())
        prefixes = ri_conf.get("prefixes") or []
        gateways = ri_conf.get("gateways") or []
        router_external = ri_conf.get("router_external", False)
        interfaces = ri_conf.get("interfaces", [])
        vni = ri_conf.get("vni", None)
        fip_map = ri_conf.get("fip_map", None)
        network_id = ri_conf.get("network_id", None)
        is_internal_vn = True if '_contrail_lr_internal_vn_' in vn.name \
            else False
        encapsulation_priorities = ri_conf.get(
            "encapsulation_priorities") or ["MPLSoGRE"]
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
                        ri.set_description("__contrail_%s_%s" %
                                           (lr.name, lr_uuid))
            ri.set_is_master(is_master_int_vn)

        ri.set_virtual_network_id(str(network_id))
        ri.set_vxlan_id(str(vni))
        ri.set_virtual_network_is_internal(is_internal_vn)
        ri.set_is_public_network(router_external)
        if is_l2_l3:
            ri.set_virtual_network_mode('l2-l3')
        elif is_l2:
            ri.set_virtual_network_mode('l2')
            if highest_encapsulation == "VXLAN":
                ri.set_routing_instance_type("virtual-switch")
            elif highest_encapsulation in ["MPLSoGRE", "MPLSoUDP"]:
                ri.set_routing_instance_type("evpn")
        else:
            ri.set_virtual_network_mode('l3')

        if not is_l2:
            ri.set_routing_instance_type("vrf")
            if fip_map is None and (router_external or not is_internal_vn):
                # add RI prefixes for dc-gateway
                self._add_ri_prefixes(vn, router_external, interfaces,
                                      prefixes, ri)

        if ri.get_routing_instance_type() != 'virtual-switch' and \
                ri.get_virtual_network_mode() != 'l2':
            self.ri_map[ri_name] = ri

        # add irb physical interface and irb vni gateway settings for l2_l3
        if self._is_gateway() and is_l2_l3 and not is_internal_vn:
            __, li_map = self._add_or_lookup_pi(self.pi_map, 'irb', 'irb')
            intf_unit = self._add_or_lookup_li(
                li_map, 'irb.' + str(network_id), network_id)
            if len(gateways) > 0:
                if vn.has_ipv6_subnet is True:
                    intf_unit.set_is_virtual_router(True)
                intf_unit.set_comment(
                    DMUtils.vn_irb_comment(vn, False, is_l2_l3,
                                           router_external))
                for (irb_ip, gateway) in gateways:
                    if len(gateway) and gateway != '0.0.0.0':
                        intf_unit.set_gateway(gateway)
                        self._add_ip_address(intf_unit, irb_ip,
                                             gateway=gateway)
                    else:
                        self._add_ip_address(intf_unit, irb_ip)
            if (is_l2 and vni is not None and
                    self._is_evpn(self._physical_router)):
                irb_name = 'irb.' + str(network_id)
                self._add_ref_to_list(ri.get_routing_interfaces(), irb_name)

        # add firewall config for public VRF
        if router_external and is_l2 is False:
            self._add_ri_vrf_firewall_config(prefixes, ri)

        # add firewall config for DCI Network
        if fip_map is not None:
            self._add_ref_to_list(ri.get_interfaces(), interfaces[0].name)
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

            __, li_map = self._add_or_lookup_pi(self.pi_map, 'irb', 'irb')
            intf_name = 'irb.' + str(network_id)
            intf_unit = self._add_or_lookup_li(li_map, intf_name, network_id)
            intf_unit.set_comment(DMUtils.vn_irb_fip_inet_comment(vn))
            intf_unit.set_family("inet")
            intf_unit.add_firewall_filters(
                DMUtils.make_private_vrf_filter_name(ri_name))
            self._add_ref_to_list(ri.get_routing_interfaces(), intf_name)

            # fip services config
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
                snat_rule.add_source_addresses(self._get_subnet_for_cidr(pip))
                snat_rule.add_source_prefixes(self._get_subnet_for_cidr(fip))
                dnat_rule.add_destination_addresses(
                    self._get_subnet_for_cidr(fip))
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
    # end _add_routing_instance

    def _update_vn_dict_for_external_vn(self, vn_dict, pr):
        # get all extended VN and private VN which has used in BMS fip pool
        for vn_id in pr.virtual_networks:
            vn_dict[vn_id] = []
            vn = VirtualNetworkDM.get(vn_id)
            if vn and vn.router_external:
                # retrieve and add all tenant private vn which has used in BMS
                # with fip pool of external vn
                vn_list = vn.get_connected_private_networks()
                for pvn in vn_list or []:
                    vn_dict[pvn] = []

        # MX snat requires physical interface and firewall config for current
        # PR. get PR's PI used in VPG's VN and its LI interface. Interface has
        # l2 name (ae or PI name), vlan tag, port_vlantag and vpg obj
        for vpg_uuid in pr.virtual_port_groups or []:
            vpg_obj = VirtualPortGroupDM.get(vpg_uuid)
            if not vpg_obj:
                continue
            vpg_interfaces = vpg_obj.physical_interfaces
            for vmi_uuid in vpg_obj.virtual_machine_interfaces:
                vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
                vn = VirtualNetworkDM.get(vmi_obj.virtual_network) if \
                    vmi_obj and vmi_obj.virtual_network is not None else None
                if not vn:
                    continue
                vlan_tag = vmi_obj.vlan_tag
                port_vlan_tag = vmi_obj.port_vlan_tag
                for pi_uuid in vpg_interfaces:
                    if pi_uuid not in pr.physical_interfaces:
                        continue
                    ae_id = vpg_obj.pi_ae_map.get(pi_uuid)
                    if ae_id is not None and vlan_tag is not None:
                        ae_name = "ae" + str(ae_id) + "." + str(vlan_tag)
                        vn_dict.setdefault(vn_id, []).append(
                            JunosInterface(ae_name, 'l2', vlan_tag,
                                           port_vlan_tag=port_vlan_tag,
                                           vpg_obj=vpg_obj))
                        break
                    else:
                        pi_obj = PhysicalInterfaceDM.get(pi_uuid)
                        if pi_obj:
                            li_name = pi_obj.name + "." + str(vlan_tag)
                            vn_dict.setdefault(vn_id, []).append(
                                JunosInterface(li_name, 'l2', vlan_tag,
                                               port_vlan_tag=port_vlan_tag,
                                               vpg_obj=vpg_obj))
                            break
    # end _update_vn_dict_for_external_vn

    def _build_ri_config_for_dc(self):
        pr = self._physical_router
        vn_dict = {}

        # For Pulic LR, add all tenant VN and contrail internal vn in dict
        vn_list = []
        for lr_id in pr.logical_routers or []:
            lr = LogicalRouterDM.get(lr_id)
            if not lr or (lr.logical_router_gateway_external == False) or \
                not lr.virtual_network or \
                    not self._is_valid_vn(lr.virtual_network, 'l3'):
                continue
            vn_obj = VirtualNetworkDM.get(lr.virtual_network)
            if '_contrail_lr_internal_vn_' not in vn_obj.name:
                continue
            ri_obj = self._get_primary_ri(vn_obj)
            if ri_obj is None:
                continue
            lr_obj = LogicalRouterDM.get(vn_obj.logical_router)
            if lr_obj is None or lr_obj.is_master is True:
                continue
            # vn_dict[lr.virtual_network] = []
            vn_list += lr.get_connected_networks(include_internal=True,
                                                 pr_uuid=pr.uuid)
            for vn_id in vn_list:
                vn_dict[vn_id] = []

        if pr.device_family == 'junos':
            # only for Junos MX platform we support fip and snat
            # through external vn
            self._update_vn_dict_for_external_vn(vn_dict, pr)

        if len(vn_dict) > 0:
            # refresh prepared vn's pr.vn_ip_map dictionary for irb and lo0
            pr.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l2_l3', 'irb',
                                      False)
            pr.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l3', 'lo0', True)
            vn_irb_ip_map = pr.get_vn_irb_ip_map()

        for vn_id, interfaces in self._get_sorted_key_value_pairs(vn_dict):
            vn_obj = VirtualNetworkDM.get(vn_id)
            if (vn_obj is None or vn_obj.get_vxlan_vni() is None or
                    vn_obj.vn_network_id is None):
                continue
            export_set = None
            import_set = None
            for ri_id in vn_obj.routing_instances:
                # Find the primary RI by matching the fabric name
                ri_obj = RoutingInstanceDM.get(ri_id)
                if ri_obj is None or ri_obj.fq_name[-1] != vn_obj.fq_name[-1]:
                    continue
                export_set, import_set = self._get_export_import_set(vn_obj,
                                                                     ri_obj)
                if vn_obj.get_forwarding_mode() in ['l2', 'l2_l3']:
                    # create ri config for is_l2 True
                    irb_ips = []
                    if vn_obj.get_forwarding_mode() == 'l2_l3' and \
                            self._is_gateway():
                        irb_ips = vn_irb_ip_map['irb'].get(vn_id, [])
                    vrf_name_l2 = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                        vn_obj.vn_network_id,
                                                        'l2')
                    ri_conf = {'ri_name': vrf_name_l2, 'vn': vn_obj,
                               'is_l2': True, 'is_l2_l3':
                                   (vn_obj.get_forwarding_mode() == 'l2_l3'),
                               'import_targets': import_set,
                               'export_targets': export_set,
                               'prefixes': vn_obj.get_prefixes(pr.uuid),
                               'gateways': irb_ips,
                               'router_external': vn_obj.router_external,
                               'interfaces': interfaces,
                               'vni': vn_obj.get_vxlan_vni(),
                               'network_id': vn_obj.vn_network_id,
                               'encapsulation_priorities':
                                   GlobalVRouterConfigDM.
                                   global_encapsulation_priorities}
                    self._add_routing_instance(ri_conf)

                if vn_obj.get_forwarding_mode() in ['l3', 'l2_l3'] and \
                        self._is_gateway():
                    interfaces = []
                    lo0_ips = []
                    if vn_obj.get_forwarding_mode() == 'l2_l3':
                        interfaces = [
                            JunosInterface(
                                'irb.' + str(vn_obj.vn_network_id),
                                'l3', 0)]
                    else:
                        lo0_ips = vn_irb_ip_map['lo0'].get(vn_id, [])
                    is_internal_vn = True if '_contrail_lr_internal_vn_' in \
                        vn_obj.name else False
                    vrf_name_l3 = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                        vn_obj.vn_network_id,
                                                        'l3')
                    ri_conf = {'ri_name': vrf_name_l3, 'vn': vn_obj,
                               'is_l2': False,
                               'is_l2_l3':
                                   vn_obj.get_forwarding_mode() == 'l2_l3',
                               'import_targets': import_set,
                               'export_targets': export_set,
                               'prefixes': vn_obj.get_prefixes(pr.uuid),
                               'router_external': vn_obj.router_external,
                               'interfaces': interfaces,
                               'gateways': lo0_ips,
                               'network_id': vn_obj.vn_network_id}
                    if is_internal_vn:
                        lr_uuid = DMUtils.\
                            extract_lr_uuid_from_internal_vn_name(vrf_name_l3)
                        lr = LogicalRouterDM.get(lr_uuid)
                        if lr and not lr.is_master:
                            ri_conf['vni'] = vn_obj.get_vxlan_vni(
                                is_internal_vn=is_internal_vn)
                            ri_conf['router_external'] = lr.\
                                logical_router_gateway_external
                            dci = lr.get_interfabric_dci()
                            if dci:
                                ri_conf['connected_dci_network'] = dci.uuid
                                lr_vn_list = dci.\
                                    get_connected_lr_internal_vns(
                                        exclude_lr=lr.uuid, pr_uuid=pr.uuid)
                                for lr_vn in lr_vn_list:
                                    exports, imports = lr_vn.\
                                        get_route_targets()
                                    if imports:
                                        ri_conf['import_targets'] |= imports
                                    if exports:
                                        ri_conf['export_targets'] |= exports
                    self._add_routing_instance(ri_conf)
                break
            # end for ri_id in vn_obj.routing_instances:

            if export_set and \
                    pr.is_junos_service_ports_enabled() and \
                    len(vn_obj.instance_ip_map) > 0:
                service_port_ids = DMUtils.get_service_ports(
                    vn_obj.vn_network_id)
                if not pr \
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
                    service_ports = pr.junos_service_ports. \
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
                    self._add_routing_instance(ri_conf)

    # end _build_ri_config_for_dc

    def feature_config(self, **kwargs):
        self.ri_map = {}
        self.firewall_config = None
        self.pi_map = OrderedDict()
        self.inet4_forwarding_filter = None
        self.inet6_forwarding_filter = None

        feature_config = Feature(name=self.feature_name())
        self._build_ri_config_for_dc()
        feature_config.set_routing_instances(
            self._get_values_sorted_by_key(
                self.ri_map))

        if self.firewall_config is not None:
            feature_config.set_firewall(self.firewall_config)

        for pi, li_map in list(self.pi_map.values()):
            pi.set_logical_interfaces(list(li_map.values()))
            feature_config.add_physical_interfaces(pi)

        return feature_config

# end DcGatewayFeature
