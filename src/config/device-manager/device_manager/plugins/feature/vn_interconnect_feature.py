#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""VN Interconnect Feature Implementation."""

from builtins import str
from collections import OrderedDict

from abstract_device_api.abstract_device_xsd import *
import db
from dm_utils import DMUtils
from feature_base import FeatureBase
from netaddr import IPAddress, IPNetwork

import gevent # noqa


class VnInterconnectFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'vn-interconnect'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_map = None
        super(VnInterconnectFeature, self).__init__(
            logger, physical_router, configs)
    # end __init__

    def _dhcp_server_exists(self, lr):
        return lr.dhcp_relay_servers
    # end _dhcp_server_exists

    def _is_dhcp_server_in_same_network(self, dhcp_server_list, vn_list):
        for vn in vn_list:
            for dhcp_ip in dhcp_server_list:
                vn_obj = db.VirtualNetworkDM.get(vn)
                ip_prefixes = vn_obj.get_prefixes(self._physical_router.uuid)
                for ip_prefix in ip_prefixes:
                    if IPAddress(dhcp_ip) in IPNetwork(ip_prefix):
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
            vn_list = lr.get_connected_networks(include_internal=False,
                                                pr_uuid=self.
                                                _physical_router.uuid)
            vn_map[lr.virtual_network] = [
                vn for vn in vn_list if self._is_valid_vn(vn, 'l3')]
            if self._dhcp_server_exists(lr):
                dhcp_servers[lr.virtual_network] = lr.dhcp_relay_servers
        return vn_map, dhcp_servers
    # end _get_interconnect_vn_map

    def _build_dhcp_relay_config(self, lr, dhcp_server_list, in_network):
        forwarding_options = ForwardingOptions()
        dhcp_relay = DhcpRelay()
        dhcp_relay.set_comment("Dhcp relay for logical router %s" % lr)
        dhcp_relay.set_dhcp_relay_group("DHCP_RELAY_GRP_" + lr)
        dhcp_relay.set_in_network(in_network)

        for dhcp_server in dhcp_server_list:
            ip_address = IpAddress(address=dhcp_server)
            dhcp_relay.add_dhcp_server_ips(ip_address)

        forwarding_options.add_dhcp_relay(dhcp_relay)
        return forwarding_options
    # end _build_dhcp_relay_config

    def _build_ri_config(self, vn, ri_obj, lr_obj, vn_list, is_master_int_vn):
        gevent.idle()
        network_id = vn.vn_network_id
        vxlan_id = vn.get_vxlan_vni(is_internal_vn=True)

        if lr_obj:
            ri_name = "__contrail_%s_%s" % (lr_obj.name,
                                            vn.logical_router)

        export_targets, import_targets = self._get_export_import_targets(
            vn, ri_obj)

        if not is_master_int_vn:
            # create routing instance of type vrf
            ri = RoutingInstance(
                name=ri_name, description=ri_name,
                virtual_network_mode='l3',
                export_targets=export_targets, import_targets=import_targets,
                virtual_network_id=str(network_id), vxlan_id=str(vxlan_id),
                is_public_network=lr_obj.logical_router_gateway_external,
                routing_instance_type='vrf', virtual_network_is_internal=True,
                is_master=False)

            _, li_map = self._add_or_lookup_pi(self.pi_map, 'lo0', 'loopback')
            lo0_unit = 1000 + int(network_id)
            lo0_li = self._add_or_lookup_li(
                li_map, 'lo0.' + str(lo0_unit), lo0_unit)
            self._add_ip_address(lo0_li, '127.0.0.1')
            self._add_ref_to_list(ri.get_loopback_interfaces(),
                                  lo0_li.get_name())
        else:
            # create routing instance of type master, which represents inet.0
            # setting is_public_network to false as per review comment - 57282
            ri = RoutingInstance(
                name=ri_name, description=ri_name,
                virtual_network_mode='l3',
                is_public_network=False,
                routing_instance_type='master',
                virtual_network_is_internal=True, is_master=True)

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

        for internal_vn, vn_list in list(vn_map.items()):
            vn_obj = db.VirtualNetworkDM.get(internal_vn)
            ri_obj = self._get_primary_ri(vn_obj)
            if ri_obj is None:
                continue

            lr_obj = db.LogicalRouterDM.get(vn_obj.logical_router)
            if lr_obj:
                is_master_int_vn = lr_obj.is_master

            ri = self._build_ri_config(vn_obj, ri_obj, lr_obj, vn_list,
                                       is_master_int_vn)

            fip_map = vn_obj.instance_ip_map
            if lr_obj.logical_router_gateway_external:
                prefixes = vn_obj.get_prefixes(self._physical_router.uuid)
                if not fip_map:
                    if prefixes:
                        for prefix in prefixes:
                            ri.add_static_routes(
                                self._get_route_for_cidr(prefix))
                            ri.add_prefixes(
                                self._get_subnet_for_cidr(prefix))

            if dhcp_servers.get(internal_vn):
                in_network = self._is_dhcp_server_in_same_network(
                    dhcp_servers[internal_vn], vn_list)
                fwd_options = self._build_dhcp_relay_config(
                    vn_obj.logical_router, dhcp_servers[internal_vn],
                    in_network)
                ri.set_forwarding_options(fwd_options)
                rib_group_name = 'external_vrf_' + internal_vn
                ri.set_rib_group(rib_group_name)

            routing_policies = []
            self._physical_router.set_routing_vn_proto_in_ri(
                ri, routing_policies, vn_list)
            if len(routing_policies) > 0:
                feature_config.set_routing_policies(routing_policies)

            feature_config.add_routing_instances(ri)

        for pi, li_map in list(self.pi_map.values()):
            pi.set_logical_interfaces(list(li_map.values()))
            feature_config.add_physical_interfaces(pi)

        return feature_config

# end VnInterconnectFeature
