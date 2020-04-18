#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""Infra BMS Access Feature Implementation."""

from collections import OrderedDict

from abstract_device_api.abstract_device_xsd import *

from .db import NetworkIpamDM, PhysicalInterfaceDM, PortDM, TagDM, \
    VirtualNetworkDM
from .dm_utils import DMUtils
from .feature_base import FeatureBase


class InfraBMSAccessFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'infra-bms-access'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.COMMENT_STR = "Underlay Infra BMS Access"
        self._pi_map = None
        self._forwarding_options_config = None

        super(InfraBMSAccessFeature, self).__init__(
            logger, physical_router, configs)
    # end __init__

    def _build_server_config(self, fconfig):
        pr = self._physical_router
        if not pr:
            return
        pi_tag = []
        for pi in pr.physical_interfaces:
            pi_obj = PhysicalInterfaceDM.get(pi)
            if not pi_obj:
                continue
            if pi_obj.port:
                port = PortDM.get(pi_obj.port)
                pi_info = {'pi': pi, 'tag': port.tags, 'pi_obj': pi_obj}
                pi_tag.append(pi_info)

        if not pi_tag:
            return

        for pi_info in pi_tag:
            add_pi = False
            for tag in pi_info.get('tag') or []:
                tag_obj = TagDM.get(tag)
                for vn in tag_obj.virtual_networks or []:
                    vn_obj = VirtualNetworkDM.get(vn)
                    for net_ipam in vn_obj.network_ipams or []:
                        net_ipam_obj = NetworkIpamDM.get(net_ipam)
                        for server_info in \
                                net_ipam_obj.server_discovery_params:
                            dhcp_relay_server = None
                            if server_info.get('dhcp_relay_server'):
                                dhcp_relay_server = server_info.get(
                                    'dhcp_relay_server')[0]
                            vlan_tag = server_info.get('vlan_tag') or 4094
                            default_gateway = server_info.get(
                                'default_gateway') + '/' + str(
                                server_info.get('ip_prefix_len'))

                            # create irb interface
                            irb_intf, li_map = self._add_or_lookup_pi(
                                self._pi_map, 'irb', 'irb')
                            irb_intf.set_comment(self.COMMENT_STR)
                            li_name = 'irb.' + str(vlan_tag)
                            irb_intf_unit = self._add_or_lookup_li(li_map,
                                                                   li_name,
                                                                   vlan_tag)
                            self._add_ip_address(irb_intf_unit,
                                                 default_gateway)

                            # create LI
                            pi_obj = pi_info.get('pi_obj')
                            add_pi = True
                            access_intf, li_map = self._add_or_lookup_pi(
                                self._pi_map, pi_obj.name, 'regular')
                            access_intf.set_comment(self.COMMENT_STR)
                            li_name = pi_obj.fq_name[-1] + '.' + str(0)
                            access_intf_unit = self._add_or_lookup_li(li_map,
                                                                      li_name,
                                                                      0)
                            access_intf_unit.add_vlans(Vlan(
                                name=tag_obj.name + "_vlan"))
                            access_intf_unit.set_family("ethernet-switching")
                            if vlan_tag == 4094:
                                access_intf_unit.set_is_tagged(False)
                            else:
                                access_intf_unit.set_is_tagged(True)

                            # create Vlan
                            vlan = Vlan(name=tag_obj.name + "_vlan")
                            vlan.set_vlan_id(vlan_tag)
                            vlan.set_comment(self.COMMENT_STR)
                            vlan.set_l3_interface('irb.' + str(vlan_tag))
                            # Add the vlan to feature config.
                            fconfig.add_vlans(vlan)

                            # set dhcp relay info
                            if dhcp_relay_server:
                                self._forwarding_options_config = \
                                    self._forwarding_options_config or \
                                    ForwardingOptions()
                                dhcp_relay = DhcpRelay()
                                dhcp_relay.set_comment(self.COMMENT_STR)
                                dhcp_relay.set_dhcp_relay_group(
                                    "SVR_RELAY_GRP_" + tag_obj.name)

                                ip_address = IpAddress(
                                    address=dhcp_relay_server)
                                dhcp_relay.add_dhcp_server_ips(ip_address)
                                self._add_ref_to_list(
                                    dhcp_relay.get_interfaces(), 'irb.' +
                                                                 str(vlan_tag))
                                self._forwarding_options_config.add_dhcp_relay(
                                    dhcp_relay)
            if add_pi:
                p, lmap = self._pi_map.get(pi_info['pi_obj'].name)
                p.set_logical_interfaces(list(lmap))
                fconfig.add_physical_interfaces(p)

    def feature_config(self, **kwargs):
        feature_config = Feature(name=self.feature_name())
        self._build_server_config(feature_config)

        if self._forwarding_options_config:
            feature_config.set_forwarding_options_config(
                self._forwarding_options_config)

        return feature_config
