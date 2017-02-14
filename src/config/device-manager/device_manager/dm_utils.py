#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains  utility methods used by device manager module
"""

from netaddr import *

class DMUtils(object):

    MAX_VRF_NAME_LENGTH = 127
    MAX_SERVICE_SET_NAME_LENGTH = 23
    MAX_FILTER_NAME_LENGTH = 63
    MAX_FILTER_TERM_NAME_LENGTH = 63
    LO0_INTF_UNIT_START_ID = 1000

    @staticmethod
    def make_vrf_name(name, network_id, vrf_type, is_nat=False):
        vrf_name = ''
        if not vrf_type:
            vrf_name = '_contrail_' + \
                str(network_id) + '_' + name
        else:
            vrf_name = '_contrail_' + vrf_type + '_' + \
                str(network_id) + '_' + name
        # mx has limitation for vrf name, allowed max 127 chars
        if is_nat:
            return vrf_name[:DMUtils.MAX_VRF_NAME_LENGTH - 4] + '-nat'
        return vrf_name[:DMUtils.MAX_VRF_NAME_LENGTH]
    #end make_vrf_name

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
        return rt_name.replace(':', '_')
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
        ri_prefix = ri_name[:DMUtils.MAX_FILTER_NAME_LENGTH - len('redirect_to_') - len('_vrf')]
        return 'redirect_to_' + ri_prefix + '_vrf'
    # end make_private_vrf_filter_name

    @staticmethod
    def make_public_vrf_filter_name(inet_type):
        if inet_type == 'inet':
            inet_type = 'inet4'
        return 'redirect_to_public_vrf_filter_' + inet_type
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
    def make_bgp_group_name(is_external=False):
        if is_external:
            return '__contrail_external__'
        return '__contrail__'
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
            fw_mode = "L2-L3"
        if not is_nat:
            return "/* Virtual Network: %s, UUID: %s, VRF Type: %s, Forwarding Mode: %s */"%(
                                                  vn.fq_name[-1], vn.uuid, vrf_type, fwd_mode)
        else:
            return "/* Virtual Network: %s, UUID: %s, VRF Type: %s (NAT), Forwarding Mode: %s*/"%(
                                                  vn.fq_name[-1], vn.uuid, vrf_type, fwd_mode)

    @staticmethod
    def bgp_group_comment(bgp_obj):
        return "/* BGP Router: %s, UUID: %s */"%(bgp_obj.name, bgp_obj.uuid)

    @staticmethod
    def public_vrf_filter_comment():
        return "/* Public VRF Filter for Floating IPs*/"

    @staticmethod
    def vn_ps_comment(vn, target_type):
        return "/* Virtual Network: %s, UUID: %s, Route Targets Type: %s */"%(vn.fq_name[-1], vn.uuid, target_type)

    @staticmethod
    def vn_firewall_comment(vn, mode):
        return "/* Virtual Network: %s, UUID: %s, Filter Type: %s */"%(vn.fq_name[-1], vn.uuid, mode)

    @staticmethod
    def vn_bd_comment(vn, encap):
        return "/* Virtual Network: %s, UUID: %s, Encapsulation: %s */"%(vn.fq_name[-1], vn.uuid, encap)

    @staticmethod
    def vn_evpn_comment(vn, encap):
        return "/* Virtual Network: %s, UUID: %s, Encapsulation: %s */"%(vn.fq_name[-1], vn.uuid, encap)

    @staticmethod
    def vn_irb_comment(vn, vrf_type):
        return "/* Virtual Network: %s, UUID: %s, VRF Type: %s */"%(vn.fq_name[-1], vn.uuid, vrf_type)

    @staticmethod
    def service_set_comment(vn):
        return "/* Virtual Network: %s, UUID: %s, VRF Type: %s */"%(vn.fq_name[-1], vn.uuid)

    @staticmethod
    def service_set_nat_rule_comment(vn, nat_type):
        return "/* %s Rules for Virtual Network: %s, UUID: %sType: %s */"%(nat_type, vn.fq_name[-1], vn.uuid)

    @staticmethod
    def nat_comment():
        return "/* Network Address Transalation Rules for SNAT/Floating IPs */"

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
        return "/* Firwalls Configuration */"

    @staticmethod
    def interfaces_comment():
        return "/* Interfaces Configuration */"

    @staticmethod
    def protocols_comment():
        return "/* Protocols Configuration */"

    @staticmethod
    def routing_instances_comment():
        return "/* Routing Instances Confguration */"

    @staticmethod
    def services_comment():
        return "/* Services Config */"

# end DMUtils
