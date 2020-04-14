#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#
# This file contains  utility methods used by device manager module
#

from builtins import object
from builtins import str

from bitarray import bitarray
from netaddr import IPNetwork


class PushConfigState(object):
    PUSH_MODE = 0  # Global Push Mode - 0: python plugin, 1 : ansible plugin
    PUSH_STATE_INIT = 0
    PUSH_STATE_SUCCESS = 1
    PUSH_STATE_RETRY = 2
    PUSH_STATE_FAILED = 3
    PUSH_STATE_IN_PROGRESS = 4
    REPUSH_INTERVAL = 15
    REPUSH_MAX_INTERVAL = 300
    PUSH_DELAY_PER_KB = 0.01
    PUSH_DELAY_MAX = 100
    PUSH_DELAY_ENABLE = True

    @classmethod
    def set_push_mode(cls, value):
        cls.PUSH_MODE = value
    # end set_push_mode

    @classmethod
    def is_push_mode_ansible(cls):
        if cls.PUSH_MODE == 1:
            return True
        return False
    # end is_push_mode_ansible

    @classmethod
    def set_repush_interval(cls, value):
        cls.REPUSH_INTERVAL = value
    # end set_repush_interval

    @classmethod
    def set_repush_max_interval(cls, value):
        cls.REPUSH_MAX_INTERVAL = value
    # end set_repush_max_interval

    @classmethod
    def set_push_delay_per_kb(cls, value):
        cls.PUSH_DELAY_PER_KB = value
    # end set_push_delay_per_kb

    @classmethod
    def set_push_delay_max(cls, value):
        cls.PUSH_DELAY_MAX = value
    # end set_push_delay_max

    @classmethod
    def set_push_delay_enable(cls, value):
        cls.PUSH_DELAY_ENABLE = value
    # end set_push_delay_enable

    @classmethod
    def get_repush_interval(cls):
        return cls.REPUSH_INTERVAL
    # end set_repush_interval

    @classmethod
    def get_repush_max_interval(cls):
        return cls.REPUSH_MAX_INTERVAL
    # end get_repush_max_interval

    @classmethod
    def get_push_delay_per_kb(cls):
        return cls.PUSH_DELAY_PER_KB
    # end get_push_delay_per_kb

    @classmethod
    def get_push_delay_max(cls):
        return cls.PUSH_DELAY_MAX
    # end get_push_delay_max

    @classmethod
    def get_push_delay_enable(cls):
        return cls.PUSH_DELAY_ENABLE
    # end get_push_delay_enable

# end PushConfigState


class DMUtils(object):

    MAX_VRF_NAME_LENGTH = 127
    MAX_SERVICE_SET_NAME_LENGTH = 23
    MAX_FILTER_NAME_LENGTH = 63
    MAX_FILTER_TERM_NAME_LENGTH = 63
    MAX_SG_NAME_LENGTH = 17
    LO0_INTF_UNIT_START_ID = 1000

    @staticmethod
    def contrail_prefix(name=''):
        return "_contrail_" + name

    @staticmethod
    def sanitize_name(name):
        if name:
            return name.replace(' ', '_', -1)
        return name

    @staticmethod
    def make_vrf_name(name, network_id, vrf_type, is_nat=False):
        name = DMUtils.sanitize_name(name)
        vrf_name = DMUtils.contrail_prefix(name)
        network_id_str = '-' + str(network_id)
        nat_postfix = '-nat'
        vrf_type_str = '-' + str(vrf_type)
        # mx has limitation for vrf name, allowed max 127 chars
        if not vrf_type:
            if is_nat:
                post_len = len(network_id_str) + len(nat_postfix)
                return vrf_name[:DMUtils.MAX_VRF_NAME_LENGTH -
                                post_len] + network_id_str + nat_postfix
            post_len = len(network_id_str)
            return vrf_name[:DMUtils.MAX_VRF_NAME_LENGTH -
                            post_len] + network_id_str
        else:
            if is_nat:
                post_len = len(network_id_str) + \
                    len(nat_postfix) + len(vrf_type_str)
                return vrf_name[:DMUtils.MAX_VRF_NAME_LENGTH -
                                post_len] + vrf_type_str + \
                    network_id_str + nat_postfix
            post_len = len(network_id_str) + len(vrf_type_str)
            return vrf_name[:DMUtils.MAX_VRF_NAME_LENGTH -
                            post_len] + vrf_type_str + network_id_str
    # end make_vrf_name

    @staticmethod
    def dynamic_tunnel_name(asn):
        return DMUtils.contrail_prefix() + "asn-" + str(asn)

    @staticmethod
    def get_network_gateways(ipam_refs=[]):
        gateways = {}
        has_ipv6_prefix = False
        for ipam_ref in ipam_refs or []:
            for subnet in ipam_ref['attr'].get('ipam_subnets', []):
                prefix = '0.0.0.0'
                prefix_len = 0
                if 'subnet' in subnet:
                    prefix = subnet['subnet']['ip_prefix']
                    prefix_len = subnet['subnet']['ip_prefix_len']
                if has_ipv6_prefix is False and IPNetwork(prefix).version == 6:
                    has_ipv6_prefix = True
                gateways[prefix + '/' + str(prefix_len)] = \
                    {"default_gateway": subnet.get('default_gateway', ''),
                     "subnet_uuid": subnet.get('subnet_uuid')}
        return (gateways, has_ipv6_prefix)
    # end get_network_gateways

    @staticmethod
    def get_server_discovery_parameters(subnets=[]):
        server_discovery_params = []
        for subnet in subnets:
            server_discovery_params.append(
                {"vlan_tag": subnet.get('vlan_tag', ''),
                 "dhcp_relay_server": subnet.get('dhcp_relay_server', []),
                 "default_gateway": subnet.get('default_gateway'),
                 "ip_prefix_len": subnet.get('subnet', {}).get(
                    'ip_prefix_len', 32)})
        return server_discovery_params
    # end get_server_discovery_parameters

    @staticmethod
    def make_export_name(ri_name):
        return ri_name + '-' + 'export'
    # end make_export_name

    @staticmethod
    def make_import_name(ri_name):
        return ri_name + '-' + 'import'
    # end make_import_name

    @staticmethod
    def make_community_name(rt_name):
        return DMUtils.contrail_prefix(rt_name.replace(':', '_'))
    # end make_community_name

    @staticmethod
    def get_service_ports(network_id):
        service_port_id = 2 * network_id - 1
        return (service_port_id, service_port_id + 1)
    # end find_service_ports

    @staticmethod
    def make_services_set_name(ri_name):
        service_name = 'sv-' + ri_name
        return service_name[:DMUtils.MAX_SERVICE_SET_NAME_LENGTH]
    # end make_services_set_name

    @staticmethod
    def make_snat_rule_name(ri_name):
        return DMUtils.make_services_set_name(ri_name) + '-sn-rule'
    # end make_snat_rule_name

    @staticmethod
    def make_dnat_rule_name(ri_name):
        return DMUtils.make_services_set_name(ri_name) + '-dn-rule'
    # end make_dnat_rule_name

    @staticmethod
    def make_private_vrf_filter_name(ri_name):
        ri_prefix = ri_name[:DMUtils.MAX_FILTER_NAME_LENGTH -
                            len('redirect-to-') - len('-vrf')]
        return 'redirect-to-' + ri_prefix + '-vrf'
    # end make_private_vrf_filter_name

    @staticmethod
    def make_public_vrf_filter_name(inet_type):
        if inet_type == 'inet':
            inet_type = 'inet4'
        return DMUtils.contrail_prefix('redirect-to-public-vrfs-' + inet_type)
    # end make_public_filter_name

    @staticmethod
    def map_public_vrf_inet_type_to_xml(inet_type):
        if inet_type == 'inet4':
            return 'inet'
        return inet_type
    # end map_public_vrf_inet_type_to_xml

    @staticmethod
    def make_ip_term_name(ip):
        return 'term_' + ip.replace('.', '_')
    # end make_ip_term_name

    @staticmethod
    def make_vrf_term_name(ri_name):
        ri_prefix = ri_name[:DMUtils.MAX_FILTER_TERM_NAME_LENGTH -
                            len('term-')]
        return 'term-' + ri_prefix
    # end make_vrf_term_name

    @staticmethod
    def make_underlay_bgp_group_name(asn, intf_name, is_external=False):
        name = "underlay_asn_%s_%s" % (intf_name, str(asn))
        if is_external:
            name = name + "_external"
        return DMUtils.contrail_prefix(name)
    # end make_bgp_group_name

    @staticmethod
    def make_bgp_group_name(asn, is_external=False, is_RR=False):
        name = DMUtils.contrail_prefix() + "asn-" + str(asn)
        if is_external:
            name = name + "-external"
        if is_RR:
            name = name + "-rr"
        return name
    # end make_bgp_group_name

    @staticmethod
    def get_dynamic_tunnel_name():
        return '__contrail__'
    # end get_dynamic_tunnel_name

    @staticmethod
    def make_bridge_name(vni):
        return "bd-" + str(vni)
    # end make_bridge_name

    @staticmethod
    def compute_lo0_unit_number(network_id):
        return str(DMUtils.LO0_INTF_UNIT_START_ID + int(network_id))
    # end compute_lo0_unit_number

    @staticmethod
    def get_ipv6_prefixes(prefixes=[]):
        return [prefix for prefix in prefixes or []
                if IPNetwork(prefix).version == 6]
    # end get_ipv6_prefixes

    @staticmethod
    def get_ipv4_prefixes(prefixes=[]):
        return [prefix for prefix in prefixes or []
                if IPNetwork(prefix).version == 4]
    # end get_ipv4_prefixes

    @staticmethod
    def is_ipv6_ll_subnet(ipv6_address):
        if IPNetwork(ipv6_address).version == 6:
            if ipv6_address.split('::', 1)[0] == 'fe80':
                return True
        return False

    @staticmethod
    def has_ipv6_prefixes(prefixes=[]):
        return any(IPNetwork(prefix).version == 6 for prefix in prefixes or [])
    # end has_ipv6_prefixes

    @staticmethod
    def has_ipv4_prefixes(prefixes=[]):
        return any(IPNetwork(prefix).version == 4 for prefix in prefixes or [])
    # end has_ipv4_prefixes

    @staticmethod
    def get_ip_cs_column_name(ip_type):
        m = {
            "irb": "ip_address",
            "lo0": "lo0_ip_address"
        }
        return m.get(ip_type)
    # end get_ip_cs_column_name

    @staticmethod
    def get_ip_used_for_str(cs_name):
        m = {
            "ip_address": "irb",
            "lo0_ip_address": "lo0"
        }
        return m.get(cs_name)
    # end get_ip_used_for_str

    @staticmethod
    def groups_comment():
        return "/* Contrail Generated Group Config */"

    @staticmethod
    def si_ri_comment(si):
        return "/* Service Instance: %s, UUID: %s */" % (
            si.fq_name[-1], si.uuid)

    @staticmethod
    def vn_ri_comment(vn, is_l2, is_l2_l3, is_nat, router_external):
        vrf_type = "L3"
        fwd_mode = "L3"
        vn_type = "Private"
        if is_l2:
            vrf_type = "L2"
            fwd_mode = "L2"
        if is_l2_l3:
            fwd_mode = "L2-L3"
        if router_external:
            vn_type = "Public"
        if not is_nat:
            return "/* %s Virtual Network: %s, UUID: %s, VRF Type: %s," \
                " Forwarding Mode: %s */" % (vn_type, vn.fq_name[-1], vn.uuid,
                                             vrf_type, fwd_mode)
        return "/* Virtual Network: %s, UUID: %s, VRF Type: %s (NAT)," \
            " Forwarding Mode: %s */" % (vn.fq_name[-1], vn.uuid,
                                         vrf_type, fwd_mode)

    @staticmethod
    def bgp_group_comment(bgp_obj):
        return "/* overlay_bgp: BGP Router: %s, UUID: %s */" %\
               (bgp_obj.name, bgp_obj.uuid)

    @staticmethod
    def routing_policy_comment(rp_obj):
        return "/* routing_policy: %s, UUID: %s */" % \
               (rp_obj.name, rp_obj.uuid)

    @staticmethod
    def dci_bgp_group_comment(bgp):
        return "/* overlay_bgp: DCI BGP Router: %s */" % (bgp.name)

    @staticmethod
    def public_vrf_filter_comment():
        return "/* fip: Public VRF Filter for Floating IPs */"

    @staticmethod
    def vrf_filter_comment(ri_name):
        return "/* VRF Filter for Virtual Network : " + ri_name + " */"

    @staticmethod
    def vn_ps_comment(vn, target_type):
        return "/* Virtual Network: %s, UUID: %s," \
            " Route Targets Type: %s */" % (
                vn.fq_name[-1], vn.uuid, target_type)

    @staticmethod
    def si_ps_comment(si, target_type):
        return "/* Service Instance: %s, UUID: %s," \
            " Route Targets Type: %s */" % (
                si.fq_name[-1], si.uuid, target_type)

    @staticmethod
    def vn_firewall_comment(vn, mode):
        return "/* fip: Virtual Network: %s, UUID: %s," \
            " Filter Type: %s */" %\
            (vn.fq_name[-1], vn.uuid, mode)

    @staticmethod
    def vn_bd_comment(vn, encap):
        return "/* Virtual Network: %s, UUID: %s, Encapsulation: %s */" % (
            vn.fq_name[-1], vn.uuid, encap)

    @staticmethod
    def vn_evpn_comment(vn, encap):
        return "/* Virtual Network: %s, UUID: %s, Encapsulation: %s */" % (
            vn.fq_name[-1], vn.uuid, encap)

    @staticmethod
    def vn_irb_comment(vn, is_l2, is_l2_l3):
        vrf_type = "L3"
        if is_l2:
            vrf_type = "L2"
        if is_l2_l3:
            vrf_type = "L2-L3"
        return "/* Virtual Network: %s, UUID: %s, VRF Type: %s */" % (
            vn.fq_name[-1], vn.uuid, vrf_type)

    @staticmethod
    def service_set_comment(vn):
        return "/* Virtual Network: %s, UUID: %s */" % (
            vn.fq_name[-1], vn.uuid)

    @staticmethod
    def service_set_nat_rule_comment(vn, nat_type):
        return "/* %s Rules for Virtual Network: %s, UUID: %s */" % (
            nat_type, vn.fq_name[-1], vn.uuid)

    @staticmethod
    def nat_comment():
        return "/* Network Address Translation Rules for SNAT/Floating IPs */"

    @staticmethod
    def snat_rule_comment():
        return "/* Traffic Inbound Rule */"

    @staticmethod
    def dnat_rule_comment():
        return "/* Traffic Outbound Rule */"

    @staticmethod
    def routing_options_comment():
        return "/* Global Routing Options */"

    @staticmethod
    def switch_options_comment():
        return "/* Global Switch Options */"

    @staticmethod
    def policy_options_comment():
        return "/* Policy Options */"

    @staticmethod
    def forwarding_options_comment():
        return "/* Forwarding Options */"

    @staticmethod
    def firewall_comment():
        return "/* Firewalls Configuration */"

    @staticmethod
    def make_sg_name(sg_name):
        return sg_name.strip().replace(' ', '-')[:DMUtils.MAX_SG_NAME_LENGTH]

    @staticmethod
    def make_sg_filter_name(sg_name, ether_match, rule_uuid):
        return "sg-filter-" + ether_match + "-" + \
               DMUtils.make_sg_name(sg_name) + "-" + rule_uuid

    @staticmethod
    def sg_firewall_comment(sg_name, ether_type_match, rule_uuid):
        return "/* Firewall Filter for : Ether Type: " + ether_type_match + \
               ", Security Group: " + DMUtils.make_sg_name(sg_name) +\
               ", Rule UUID: " + rule_uuid

    @staticmethod
    def make_sg_firewall_name(sg_name, acl_uuid):
        return "sg-filter-" + DMUtils.make_sg_name(sg_name) + \
               "-" + acl_uuid

    @staticmethod
    def make_sg_firewall_comment(sg_name, acl_uuid):
        return "/* Firewall Filter for : Security Group: " + \
               DMUtils.make_sg_name(sg_name) + \
               ", ACL UUID: " + acl_uuid

    @staticmethod
    def interfaces_comment():
        return "/* Interfaces Configuration */"

    @staticmethod
    def protocols_comment():
        return "/* Protocols Configuration */"

    @staticmethod
    def routing_instances_comment():
        return "/* Routing Instances Configuration */"

    @staticmethod
    def services_comment():
        return "/* Services Config */"

    @staticmethod
    def vn_irb_fip_inet_comment(vn):
        return "/* fip: Routing Interface For Floating IPs, " \
               "Virtual Network: %s, UUID: %s */" % (vn.fq_name[-1], vn.uuid)

    @staticmethod
    def l2_evpn_intf_unit_comment(vn, is_tagged, tag=None):
        if is_tagged:
            return "/* L2 EVPN Tagged Interface, Virtual Network: %s, "\
                "UUID: %s, VLAN Tag: %s */" % (vn.fq_name[-1],
                                               vn.uuid, str(tag))
        return "/* L2 EVPN Untagged Interface, Virtual Network: %s, "\
               "UUID: %s */" % (vn.fq_name[-1], vn.uuid)

    @staticmethod
    def l3_lo_intf_comment(vn):
        return "/* L3 Gateway Interface, Virtual Network: %s, "\
               "UUID: %s */" % (vn.fq_name[-1], vn.uuid)

    @staticmethod
    def l3_bogus_lo_intf_comment(vn):
        return "/* Bogus lo0 intf (PFE limitation), Virtual Network: %s, "\
               "UUID: %s */" % (vn.fq_name[-1], vn.uuid)

    @staticmethod
    def service_ifd_comment():
        return "/* Service Interface */"

    @staticmethod
    def service_intf_comment(direction):
        return "/* Service %s Interface */" % (direction)

    @staticmethod
    def irb_ip_comment(irb_ip):
        ip = IPNetwork(irb_ip)
        return "/* Allocated IPv%s Address from Subnet: %s/%s */" % (
            str(ip.version), ip.network, str(ip.prefixlen))

    @staticmethod
    def lo0_ip_comment(lo_ip):
        ip = IPNetwork(lo_ip)
        return "/* Allocated IPv%s Address from Subnet: %s/%s */" % (
            str(ip.version), ip.network, str(ip.prefixlen))

    @staticmethod
    def lo0_ri_intf_comment(vn):
        return "/* Routing Interface for lo0 IPs of L3 Virtual Network: %s, "\
               "UUID: %s */" % (vn.fq_name[-1], vn.uuid)

    @staticmethod
    def lo0_unit_0_comment():
        return "/* Router Loopback Interface */"

    @staticmethod
    def ip_fabric_subnet_comment():
        return "/* IP Fabric Subnet */"

    @staticmethod
    def bgp_router_subnet_comment(name):
        return "/* BGP Router : %s */" % (name)

    @staticmethod
    def public_vrf_route_comment():
        return "/* Static Route for Public L3 VRF */"

    @staticmethod
    def fip_ingress_comment():
        return "/* Static Route for Floating IP ingress */"

    @staticmethod
    def fip_egress_comment():
        return "/* Static Route for Floating IP egress */"

    @staticmethod
    def make_ibgp_export_policy_name():
        return "_contrail_ibgp_export_policy"

    @staticmethod
    def ibgp_export_policy_comment():
        return "/* iBGP Export Policy */"

    @staticmethod
    def vlans_comment():
        return "/* Vlans Configuration */"

    @staticmethod
    def make_ibgp_export_policy_term_name(is_v6=False):
        if is_v6:
            return "inet6-vpn"
        return "inet-vpn"

    @staticmethod
    def get_inet_family_name(is_v6=False):
        if is_v6:
            return "inet6-vpn"
        return "inet-vpn"

    @staticmethod
    def ip_clos_comment():
        return "underlay_ip_clos"

    @classmethod
    def get_lr_internal_vn_prefix(cls):
        return '__contrail_lr_internal_vn_'
    # end get_lr_internal_vn_prefix

    @classmethod
    def get_lr_internal_vn_name(cls, uuid):
        return cls.get_lr_internal_vn_prefix() + uuid + '__'
    # end get_lr_internal_vn_name

    @classmethod
    def extract_lr_uuid_from_internal_vn_name(cls, name):
        (_, uuid) = name.split(cls.get_lr_internal_vn_prefix())
        (uuid, _) = uuid.split('__')
        return uuid
    # end extract_lr_uuid_from_internal_vn_name

    @classmethod
    def extract_lr_uuid_from_ri_name(cls, name):
        uuid = name.split('_')
        if len(uuid) > 0:
            return uuid[-1]
        return ''
    # end extract_lr_uuid_from_internal_vn_name

    @classmethod
    def get_dci_rib_group_name(cls, dci):
        return DMUtils.contrail_prefix() + 'rib_' + dci.name + '_' + dci.uuid
    # end get_dci_rib_group_name

    @classmethod
    def get_dci_rib_group_comment(cls, dci):
        return "/* DataCenter InterConnect: %s, UUID: %s */" % (
            dci.name, dci.uuid)
    # end get_dci_rib_group_comment

    @classmethod
    def get_dci_rib_rp_name(cls, dci):
        return DMUtils.contrail_prefix() + 'rp_rib_' + dci.name

    @classmethod
    def get_dci_rib_rp_comment(cls, dci):
        return "/* DataCenter InterConnect: %s, UUID: %s RP from VNs */" % (
            dci.name, dci.uuid)

    @classmethod
    def get_dci_vrf_rp_name(cls, dci):
        return DMUtils.contrail_prefix() + 'rp_vrf_' + dci.name

    @classmethod
    def get_dci_vrf_rp_comment(cls, dci):
        return DMUtils.contrail_prefix() + 'rp_vrf_' + dci.name

    @classmethod
    def get_dci_vrf_community_name(cls, dci):
        return DMUtils.contrail_prefix() + 'rp_inter_' + dci.name

    @classmethod
    def get_pr_dci_bgp_group(cls, pr_name, dci_uuid):
        return DMUtils.contrail_prefix() + 'dci_' + pr_name + '_' + dci_uuid
    # end get_pr_dci_bgp_group

    @classmethod
    def get_switch_policy_name(cls):
        return "_contrail_switch_policy_"
    # end get_switch_policy_name

    @classmethod
    def switch_export_policy_comment(cls):
        return "L2 Switch Global Export Policy"
    # end switch_export_policy_comment

    @classmethod
    def get_switch_export_policy_name(cls):
        return "_contrail_switch_export_policy_"
    # end get_switch_export_policy_name

    @classmethod
    def get_switch_export_community_name(cls):
        return "_contrail_switch_export_community_"
    # end get_switch_export_community_name

    @classmethod
    def get_switch_vrf_import(cls, asn):
        return "target:" + str(asn) + ":1"
    # end get_switch_vrf_import

    @classmethod
    def get_max_ae_device_count(cls):
        return 128
    # end get_max_ae_device_count

    @classmethod
    def lacp_system_priority(cls):
        return 100
    # end lacp_system_priority

# end DMUtils


class DMIndexer(object):
    ALLOC_INCREMENT = 1
    ALLOC_DECREMENT = -1

    def __init__(self, max_count, order=1):
        """Initialize index_allocation, order, max_count etc."""
        self.max_count = max_count
        self.allocation_order = order
        self.index_allocator = bitarray([0] * max_count)
    # end __init__

    def reserve_index(self, index):
        if self.allocation_order == self.ALLOC_DECREMENT:
            self.index_allocator[self.max_count - 1 - index] = 1
        else:
            self.index_allocator[index] = 1
    # end reserve_index

    def free_index(self, index):
        if self.allocation_order == self.ALLOC_DECREMENT:
            self.index_allocator[self.max_count - 1 - index] = 1
        else:
            self.index_allocator[index] = 0
    # end free_index

    def find_next_available_index(self):
        try:
            if self.allocation_order == self.ALLOC_INCREMENT:
                return self.index_allocator.index(0)
            elif self.allocation_order == self.ALLOC_DECREMENT:
                index = self.index_allocator.index(0)
                return self.max_count - 1 - index
        except ValueError:
            return -1
        return -1
    # end find_next_available_index
