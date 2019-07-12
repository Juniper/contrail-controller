#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of abstract config generation for overlay bgp feature
"""

import db
from abstract_device_api.abstract_device_xsd import *
from collections import OrderedDict
from dm_utils import DMUtils
from feature_base import FeatureBase
from netaddr import IPNetwork, IPAddress

class VnInterconnectFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'vn-interconnect'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_map = None
        super(VnInterconnectFeature, self).__init__(logger, physical_router, configs)
    # end __init__

    def _dhcp_server_exists(self, lr):
        return lr.dhcp_relay_servers
    # end _dhcp_server_exists

    def _is_dhcp_server_in_same_network(self, dhcp_server_list, vn_list):
        # dhcp_server_vn_map = {}

        for vn in vn_list:
            # dhcp_server_vn_map[vn] = []
            for dhcp_ip in dhcp_server_list:
                vn_obj = db.VirtualNetworkDM.get(vn)
                ip_prefixes = vn_obj.get_prefixes()
                for ip_prefix in ip_prefixes:
                    if IPAddress(dhcp_ip) in IPNetwork(ip_prefix):
                        # dhcp_server_vn_map[vn].append(dhcp_ip)
                        return True
        return False
        # return dhcp_server_vn_map
    # end _is_dhcp_server_in_same_network

    def _get_interconnect_vn_map(self):
        vn_map = {}
        dhcp_servers = {}
        for lr_id in self._physical_router.logical_routers or []:
            lr = db.LogicalRouterDM.get(lr_id)
            if not lr or not lr.virtual_network or \
                    not self._is_valid_vn(lr.virtual_network, 'l3'):
                continue
            vn_list = lr.get_connected_networks(include_internal=False)
            vn_map[lr.virtual_network] = [vn for vn in vn_list if self._is_valid_vn(vn, 'l3')]
            if self._dhcp_server_exists(lr):
                dhcp_servers[lr.virtual_network] = lr.dhcp_relay_servers
        return vn_map, dhcp_servers
    # end _get_interconnect_vn_map

    def _build_dhcp_relay_config(self, lr, dhcp_server_list, in_network):
        forwarding_options = ForwardingOptions()
        dhcp_relay = DhcpRelay()
        dhcp_relay.set_comment("Dhcp relay for logical router %s" % lr)
        dhcp_relay.set_dhcp_relay_group("DHCP_RELAY_GRP_"+lr)
        dhcp_relay.set_in_network(in_network)

        for dhcp_server in dhcp_server_list:
            ip_address = IpAddress(address=dhcp_server)
            dhcp_relay.add_dhcp_server_ips(ip_address)

        forwarding_options.add_dhcp_relay(dhcp_relay)
        return forwarding_options
    # end _build_dhcp_relay_config

    def _build_ri_config(self, vn, ri_name, ri_obj, export_targets,
            import_targets, vn_list):
        encapsulation_priorities = self._get_encapsulation_priorities()
        network_id = vn.vn_network_id
        vxlan_id = vn.get_vxlan_vni(is_internal_vn=True)

        ri = RoutingInstance(name=ri_name, virtual_network_mode='l3',
            export_targets=export_targets, import_targets=import_targets,
            virtual_network_id=str(network_id), vxlan_id=str(vxlan_id),
            is_public_network=vn.router_external, routing_instance_type='vrf',
            virtual_network_is_internal=True)

        _, li_map = self._add_or_lookup_pi(self.pi_map, 'lo0', 'loopback')
        lo0_unit = 1000 + int(network_id)
        lo0_li = self._add_or_lookup_li(li_map, 'lo0.'+str(lo0_unit), lo0_unit)
        self._add_ip_address(lo0_li, '127.0.0.1')
        self._add_ref_to_list(ri.get_loopback_interfaces(), lo0_li.get_name())

        for connected_vn_uuid in vn_list:
            connected_vn = db.VirtualNetworkDM.get(connected_vn_uuid)
            irb_name = 'irb.' + str(connected_vn.vn_network_id)
            self._add_ref_to_list(ri.get_routing_interfaces(), irb_name)
        return ri
    # end _build_ri_config

    def feature_config(self, **kwargs):
        self.pi_map = OrderedDict()
        feature_config = Feature(name=self.feature_name())
        vn_map, dhcp_servers = self._get_interconnect_vn_map()

        for internal_vn, vn_list in vn_map.iteritems():
            vn_obj = db.VirtualNetworkDM.get(internal_vn)
            ri_obj = self._get_primary_ri(vn_obj)
            if ri_obj is None:
                continue
            ri_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                            vn_obj.vn_network_id, 'l3')
            export_targets, import_targets = self._get_export_import_targets(vn_obj, ri_obj)

            if dhcp_servers[internal_vn]:
                in_network = self._is_dhcp_server_in_same_network(
                    dhcp_servers[internal_vn], vn_list)
                fwd_options = self._build_dhcp_relay_config(
                    vn_obj.logical_router, dhcp_servers[internal_vn],
                    in_network)

            ri = self._build_ri_config(vn_obj, ri_name, ri_obj, export_targets,
                import_targets, vn_list)

            ri.set_forwarding_options(fwd_options)
            feature_config.add_routing_instances(ri)

        for pi, li_map in self.pi_map.values():
            pi.set_logical_interfaces(li_map.values())
            feature_config.add_physical_interfaces(pi)

        return feature_config
    # end feature_config

# end VnInterconnectFeature
