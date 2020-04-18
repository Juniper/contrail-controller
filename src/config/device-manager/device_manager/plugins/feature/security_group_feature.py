#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
"""Security group feature implementation."""

from collections import OrderedDict

from abstract_device_api.abstract_device_xsd import *
import netaddr

from .db import AccessControlListDM, LogicalInterfaceDM, SecurityGroupDM, \
    VirtualNetworkDM
from .dm_utils import DMUtils
from .feature_base import FeatureBase


class SecurityGroupFeature(FeatureBase):
    @classmethod
    def feature_name(cls):
        return 'firewall'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_map = None
        self.firewall_config = None
        super(SecurityGroupFeature, self).__init__(
            logger, physical_router, configs)
    # end __init__

    def _get_connected_vn_li_map(self):
        vns = self._get_connected_vns('l2')
        vn_li_map = self._get_vn_li_map('l2')
        for vn in vns:
            vn_li_map.setdefault(vn, [])
        return vn_li_map
    # end _get_connected_vn_li_map

    def _build_l2_evpn_interface_config(self, interfaces, vn):
        ifd_map = {}
        # vpg_map = {}
        for interface in interfaces:
            ifd_map.setdefault(interface.pi_name, []).append(interface)

        for pi_name, interface_list in list(ifd_map.items()):
            untagged = [int(i.port_vlan_tag) for i in interface_list
                        if int(i.vlan_tag) == 0]
            if len(untagged) > 1:
                self._logger.error(
                    "Only one untagged interface is allowed on a ifd %s" %
                    pi_name)
                continue
            tagged = [int(i.vlan_tag) for i in interface_list
                      if int(i.vlan_tag) == 0]
            if self._is_enterprise_style():
                if len(untagged) > 0 and len(tagged) > 0:
                    self._logger.error(
                        "Enterprise style config: Can't have tagged and "
                        "untagged interfaces for same VN on same PI %s" %
                        pi_name)
                    continue
            elif len(set(untagged) & set(tagged)) > 0:
                self._logger.error(
                    "SP style config: Can't have tagged and untagged "
                    "interfaces with same Vlan-id on same PI %s" %
                    pi_name)
                continue
            pi, li_map = self._add_or_lookup_pi(self.pi_map, pi_name)
            # lag = LinkAggrGroup(description="Virtual Port Group : %s" %
            #                                 vpg_map[pi_name])
            # pi.set_link_aggregation_group(lag)
            for interface in interface_list:
                if int(interface.vlan_tag) == 0:
                    is_tagged = False
                    vlan_tag = str(interface.port_vlan_tag)
                else:
                    is_tagged = True
                    vlan_tag = str(interface.vlan_tag)
                unit_name = pi_name + "." + str(interface.unit)
                unit = self._add_or_lookup_li(li_map, unit_name,
                                              interface.unit)
                com = DMUtils.l2_evpn_intf_unit_comment(vn, is_tagged,
                                                        vlan_tag)
                unit.set_comment(com)
                unit.set_is_tagged(is_tagged)
                unit.set_vlan_tag(vlan_tag)
                # attach acls
                self.attach_acls(interface, unit)
    # end build_l2_evpn_interface_config

    def attach_acls(self, interface, unit):
        pr = self._physical_router
        if not pr:
            return

        sg_list = []
        # now the vpg obj is available in interface object as vpg_obj
        vpg_obj = interface.vpg_obj

        # For an enterprise style fabric, VPG has SG refs
        # For SP style fabric, VMI has SG refs
        if self._is_enterprise_style():
            sg_uuids = vpg_obj.security_groups
            for sg in sg_uuids or []:
                sg_obj = SecurityGroupDM.get(sg)
                if sg_obj:
                    sg_list.append(sg_obj)
        else:
            sg_list = vpg_obj.get_attached_sgs(unit.get_vlan_tag(), interface)
        for sg in sg_list or []:
            acls = sg.access_control_lists
            for acl in acls or []:
                acl = AccessControlListDM.get(acl)
                if acl and not acl.is_ingress:
                    self.build_firewall_filters(sg, acl)

        if 'li_uuid' in interface and interface.li_uuid:
            interface = LogicalInterfaceDM.find_by_name_or_uuid(
                interface.li_uuid)
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
        return self._physical_router.fabric_obj.enterprise_style
    # end _is_enterprise_style

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
        self.firewall_config = self.firewall_config or Firewall(
            DMUtils.firewall_comment())
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
                # src_port_match = match.get_src_port()
                term = self.add_filter_term(f, rule_uuid)
                self.add_addr_term(term, dst_addr_match, False,
                                   ether_type_match)
                self.add_addr_term(term, src_addr_match, True,
                                   ether_type_match)
                self.add_port_term(term, dst_port_match, False)
                # source port match is not needed for now (BMS source port)
                # self.add_port_term(term, src_port_match, True)
                self.add_protocol_term(term, protocol_match)
            self.firewall_config.add_firewall_filters(f)
    # end build_firewall_filters

    def add_addr_term(self, term, addr_match, is_src, ether_type_match):
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
        if ether_type_match:
            from_.set_ether_type(ether_type_match)
        subnet_comment = ''
        if netaddr.valid_ipv6(subnet_ip):
            subnet_comment = 'ipv6'
        if is_src:
            from_.add_source_address(Subnet(prefix=subnet_ip,
                                            prefix_len=subnet_len,
                                            comment=subnet_comment))
        else:
            from_.add_destination_address(Subnet(prefix=subnet_ip,
                                                 prefix_len=subnet_len,
                                                 comment=subnet_comment))
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
        ip_prefix = match.get_dst_address().get_subnet().get_ip_prefix()
        ip_p_len = match.get_dst_address().get_subnet().get_ip_prefix_len()
        if (str(ip_prefix) in ["0.0.0.0", "::"]) and (str(ip_p_len) == "0") \
                and (str(match.get_dst_port().get_start_port()) == "0") and \
                (str(match.get_dst_port().get_end_port()) == "65535") and \
                (str(match.get_ethertype()) in ["IPv4", "IPv6"]) and \
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

        return_val = match.get_dst_address() or \
            match.get_dst_port() or match.get_ethertype() or \
            match.get_src_address() or match.get_src_port() or \
            (match.get_protocol() and match.get_protocol() != 'any')
        return return_val

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

    def feature_config(self, **kwargs):
        self.pi_map = OrderedDict()
        feature_config = Feature(name=self.feature_name())

        vn_dict = self._get_connected_vn_li_map()
        for vn_uuid, interfaces in list(vn_dict.items()):
            vn_obj = VirtualNetworkDM.get(vn_uuid)
            self._build_l2_evpn_interface_config(interfaces, vn_obj)

        feature_config.set_firewall(self.firewall_config)
        for pi, li_map in list(self.pi_map.values()):
            pi.set_logical_interfaces(list(li_map.values()))
            feature_config.add_physical_interfaces(pi)
        return feature_config
    # end feature_config
# end SecurityGroupFeature
