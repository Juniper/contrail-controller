#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains  utility methods used by device manager module
"""

from netaddr import IPNetwork

class DMUtils(object):

    MAX_VRF_NAME_LENGTH = 127
    MAX_SERVICE_SET_NAME_LENGTH = 23
    MAX_FILTER_NAME_LENGTH = 63
    MAX_FILTER_TERM_NAME_LENGTH = 63
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
                return vrf_name[:DMUtils.MAX_VRF_NAME_LENGTH - post_len] + network_id_str + nat_postfix
            post_len = len(network_id_str)
            return vrf_name[:DMUtils.MAX_VRF_NAME_LENGTH - post_len] + network_id_str
        else:
            if is_nat:
                post_len = len(network_id_str) + len(nat_postfix) + len(vrf_type_str)
                return vrf_name[:DMUtils.MAX_VRF_NAME_LENGTH - post_len] + vrf_type_str + network_id_str + nat_postfix
            post_len = len(network_id_str) + len(vrf_type_str)
            return vrf_name[:DMUtils.MAX_VRF_NAME_LENGTH - post_len] + vrf_type_str + network_id_str
    #end make_vrf_name

    @staticmethod
    def dynamic_tunnel_name(asn):
        return DMUtils.contrail_prefix() + "asn-" + str(asn)

    @staticmethod
    def get_network_gateways(ipam_refs=[]):
        gateways = {}
        for ipam_ref in ipam_refs or []:
            for subnet in ipam_ref['attr'].get('ipam_subnets', []):
                prefix = '0.0.0.0'
                prefix_len = 0
                if 'subnet' in subnet:
                    prefix = subnet['subnet']['ip_prefix']
                    prefix_len = subnet['subnet']['ip_prefix_len']
                gateways[prefix + '/' + str(prefix_len)] = \
                    {"default_gateway": subnet.get('default_gateway', ''),
                     "subnet_uuid": subnet.get('subnet_uuid')}
        return gateways
    # end get_network_gateways

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
        service_port_id = 2*network_id - 1
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
        ri_prefix = ri_name[:DMUtils.MAX_FILTER_NAME_LENGTH - len('redirect-to-') - len('-vrf')]
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
        ri_prefix = ri_name[:DMUtils.MAX_FILTER_TERM_NAME_LENGTH - len('term-')]
        return 'term-' + ri_prefix
    # end make_vrf_term_name

    @staticmethod
    def make_bgp_group_name(asn, is_external=False):
        name = DMUtils.contrail_prefix() + "asn-" + str(asn)
        if is_external:
            return name + "-external"
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
        return [prefix for prefix in prefixes or [] if IPNetwork(prefix).version == 6]
    # end get_ipv6_prefixes

    @staticmethod
    def get_ipv4_prefixes(prefixes=[]):
        return [prefix for prefix in prefixes or [] if IPNetwork(prefix).version == 4]
    # end get_ipv4_prefixes

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
    def vn_ri_comment(vn, is_l2, is_l2_l3, is_nat):
        vrf_type = "L3"
        fwd_mode = "L3"
        if is_l2:
            vrf_type = "L2"
            fwd_mode = "L2"
        if is_l2_l3:
            fwd_mode = "L2-L3"
        if not is_nat:
            return "/* Virtual Network: %s, UUID: %s, VRF Type: %s," \
                " Forwarding Mode: %s */"%(vn.fq_name[-1], vn.uuid, vrf_type, fwd_mode)
        return "/* Virtual Network: %s, UUID: %s, VRF Type: %s (NAT)," \
            " Forwarding Mode: %s */"%(vn.fq_name[-1], vn.uuid, vrf_type, fwd_mode)

    @staticmethod
    def bgp_group_comment(bgp_obj):
        return "/* BGP Router: %s, UUID: %s */"%(bgp_obj.name, bgp_obj.uuid)

    @staticmethod
    def public_vrf_filter_comment():
        return "/* Public VRF Filter for Floating IPs */"

    @staticmethod
    def vn_ps_comment(vn, target_type):
        return "/* Virtual Network: %s, UUID: %s, Route Targets Type: %s */"%(
                                          vn.fq_name[-1], vn.uuid, target_type)

    @staticmethod
    def vn_firewall_comment(vn, mode):
        return "/* Virtual Network: %s, UUID: %s, Filter Type: %s */"%(vn.fq_name[-1],
                                                                       vn.uuid, mode)

    @staticmethod
    def vn_bd_comment(vn, encap):
        return "/* Virtual Network: %s, UUID: %s, Encapsulation: %s */"%(
                                            vn.fq_name[-1], vn.uuid, encap)

    @staticmethod
    def vn_evpn_comment(vn, encap):
        return "/* Virtual Network: %s, UUID: %s, Encapsulation: %s */"%(
                                            vn.fq_name[-1], vn.uuid, encap)

    @staticmethod
    def vn_irb_comment(vn, is_l2, is_l2_l3):
        vrf_type = "L3"
        if is_l2:
            vrf_type = "L2"
        if is_l2_l3:
            vrf_type = "L2-L3"
        return "/* Virtual Network: %s, UUID: %s, VRF Type: %s */"%(vn.fq_name[-1],
                                                                 vn.uuid, vrf_type)

    @staticmethod
    def service_set_comment(vn):
        return "/* Virtual Network: %s, UUID: %s */"%(vn.fq_name[-1], vn.uuid)

    @staticmethod
    def service_set_nat_rule_comment(vn, nat_type):
        return "/* %s Rules for Virtual Network: %s, UUID: %s */"%(nat_type,
                                                      vn.fq_name[-1], vn.uuid)

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
    def policy_options_comment():
        return "/* Policy Options */"

    @staticmethod
    def forwarding_options_comment():
        return "/* Forwarding Options */"

    @staticmethod
    def firewall_comment():
        return "/* Firewalls Configuration */"

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
        return "/* Routing Interface For Floating IPs, Virtual Network: %s, "\
               "UUID: %s */"%(vn.fq_name[-1], vn.uuid)

    @staticmethod
    def l2_evpn_intf_unit_comment(vn, is_tagged, tag=None):
        if is_tagged:
            return "/* L2 EVPN Tagged Interface, Virtual Network: %s, "\
                   "UUID: %s, VLAN Tag: %s */"%(vn.fq_name[-1], vn.uuid, str(tag))
        return "/* L2 EVPN Untagged Interface, Virtual Network: %s, "\
               "UUID: %s */"%(vn.fq_name[-1], vn.uuid)

    @staticmethod
    def l3_lo_intf_comment(vn):
        return "/* L3 Gateway Interface, Virtual Network: %s, "\
               "UUID: %s */"%(vn.fq_name[-1], vn.uuid)

    @staticmethod
    def service_ifd_comment():
        return "/* Service Interface */"

    @staticmethod
    def service_intf_comment(direction):
        return "/* Service %s Interface */"%(direction)

    @staticmethod
    def irb_ip_comment(irb_ip):
        ip = IPNetwork(irb_ip)
        return "/* Allocated IPv%s Address from Subnet: %s/%s */"%(str(ip.version),
                                                    ip.network, str(ip.prefixlen))

    @staticmethod
    def lo0_ip_comment(lo_ip):
        ip = IPNetwork(lo_ip)
        return "/* Allocated IPv%s Address from Subnet: %s/%s */"%(str(ip.version),
                                                    ip.network, str(ip.prefixlen))

    @staticmethod
    def lo0_ri_intf_comment(vn):
        return "/* Routing Interface for lo0 IPs of L3 Virtual Network: %s, "\
               "UUID: %s */"%(vn.fq_name[-1], vn.uuid)

    @staticmethod
    def lo0_unit_0_comment():
        return "/* Router Loopback Interface */"

    @staticmethod
    def ip_fabric_subnet_comment():
        return "/* IP Fabric Subnet */"

    @staticmethod
    def bgp_router_subnet_comment(name):
        return "/* BGP Router : %s */"%(name)

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
    def make_ibgp_export_policy_term_name(is_v6=False):
        if is_v6:
            return "inet6-vpn"
        return "inet-vpn"

    @staticmethod
    def get_inet_family_name(is_v6=False):
        if is_v6:
            return "inet6-vpn"
        return "inet-vpn"

# end DMUtils
