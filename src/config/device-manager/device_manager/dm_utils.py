#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains  utility methods used by device manager module
"""

class DMUtils(object):

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
            return vrf_name[:123] + '-nat'
        return vrf_name[:127]
    #end make_vrf_name

    @staticmethod
    def get_network_gateways(ipam_refs=[]):
        gateways = {}
        for ipam_ref in ipam_refs or []:
            for subnet in ipam_ref['attr'].get('ipam_subnets', []):
                prefix = subnet['subnet']['ip_prefix']
                prefix_len = subnet['subnet']['ip_prefix_len']
                gateways[prefix + '/' + str(prefix_len)] = \
                    subnet.get('default_gateway', '')
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
        return service_name[:23]
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
        return 'redirect_to_' + ri_name[:46] + '_vrf'
    # end make_private_vrf_filter_name

    @staticmethod
    def make_public_vrf_filter_name(inet_type):
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
        return 'term-' + ri_name[:59]
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
        return str(1000 + int(network_id))
    # end compute_lo0_unit_number

    @staticmethod
    def get_ipv6_prefixes(prefixes=[]):
        return [prefix for prefix in prefixes or [] if ':' in prefix]
    # end get_ipv6_prefixes

    @staticmethod
    def get_ipv4_prefixes(prefixes=[]):
        return [prefix for prefix in prefixes or [] if ':' not in prefix]
    # end get_ipv4_prefixes

    @staticmethod
    def has_ipv6_prefixes(prefixes=[]):
        return any(':' in prefix for prefix in prefixes or [])
    # end has_ipv6_prefixes

    @staticmethod
    def has_ipv4_prefixes(prefixes=[]):
        return any(':' not in prefix for prefix in prefixes or [])
    # end has_ipv4_prefixes

# end DMUtils
