#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of abstract config generation for overlay bgp feature
"""

import db
from abstract_device_api.abstract_device_xsd import *
from attrdict import AttrDict
from collections import OrderedDict
from dm_utils import DMUtils
from feature_base import FeatureBase

class L2GatewayFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'l2-gateway'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_map = None
        super(L2GatewayFeature, self).__init__(logger, physical_router, configs)
    # end __init__

    def _build_ri_config(self, vn, ri_name, ri_obj, interfaces, export_targets,
            import_targets, feature_config):
        encapsulation_priorities = self._get_encapsulation_priorities()
        highest_encapsulation = encapsulation_priorities[0]
        network_id = vn.vn_network_id
        vxlan_id = vn.get_vxlan_vni()

        ri = RoutingInstance(name=ri_name, virtual_network_mode='l2',
            export_targets=export_targets, import_targets=import_targets,
            virtual_network_id=str(network_id), vxlan_id=str(vxlan_id),
            is_public_network=vn.router_external)

        ri.set_virtual_network_id(str(network_id))
        ri.set_vxlan_id(str(vxlan_id))
        ri.set_is_public_network(vn.router_external)

        vlan = None
        if highest_encapsulation == 'VXLAN':
            ri.set_routing_instance_type('virtual-switch')
            vlan = Vlan(name=DMUtils.make_bridge_name(vxlan_id), vxlan_id=vxlan_id)
            vlan.set_comment(DMUtils.vn_bd_comment(vn, 'VXLAN'))
            feature_config.add_vlans(vlan)
            for interface in interfaces:
                self._add_ref_to_list(vlan.get_interfaces(), interface.li_name)
        elif highest_encapsulation in ['MPLSoGRE', 'MPLSoUDP']:
            ri.set_routing_instance_type('evpn')

        self._build_l2_evpn_interface_config(interfaces, vn, vlan)

        return ri
    # end _build_ri_config

    def _build_l2_evpn_interface_config(self, interfaces, vn, vlan):
        interface_map = OrderedDict()
        for interface in interfaces:
            interface_map.setdefault(interface.pi_name, []).append(interface)

        for pi_name, interface_list in interface_map.items():
            if len(interface_list) > 1 and \
                    any(intf.is_untagged() for intf in interface_list):
                self._logger.error(
                    "Invalid logical interface config for PI %s" % pi_name)
                continue
            _, li_map = self._add_or_lookup_pi(self.pi_map, pi_name)
            for interface in interface_list:
                if interface.is_untagged():
                    is_tagged = False
                    vlan_tag = str(interface.port_vlan_tag)
                else:
                    is_tagged = True
                    vlan_tag = str(interface.vlan_tag)
                unit = self._add_or_lookup_li(li_map, interface.li_name, interface.unit)
                unit.set_comment(DMUtils.l2_evpn_intf_unit_comment(vn,
                    is_tagged, vlan_tag))
                unit.set_is_tagged(is_tagged)
                unit.set_vlan_tag(vlan_tag)
                if vlan:
                    self._add_ref_to_list(vlan.get_interfaces(), interface.li_name)
    # end _build_l2_evpn_interface_config

    def _get_connected_vn_li_map(self):
        vns = self._get_connected_vns('l2')
        vn_li_map = self._get_vn_li_map('l2')
        for vn in vns:
            vn_li_map.setdefault(vn, [])
        return vn_li_map
    # end _get_connected_vn_li_map

    def feature_config(self, **kwargs):
        self.pi_map = OrderedDict()
        feature_config = Feature(name=self.feature_name())
        if not self._is_evpn(self._physical_router):
            return feature_config

        vn_dict = self._get_connected_vn_li_map()
        for vn_uuid, interfaces in vn_dict.items():
            vn_obj = db.VirtualNetworkDM.get(vn_uuid)
            ri_obj = self._get_primary_ri(vn_obj)
            if ri_obj is None:
                continue
            ri_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                            vn_obj.vn_network_id, 'l2')
            export_targets, import_targets = self._get_export_import_targets(vn_obj, ri_obj)
            ri = self._build_ri_config(vn_obj, ri_name, ri_obj, interfaces,
                export_targets, import_targets, feature_config)
            feature_config.add_routing_instances(ri)

        for pi, li_map in self.pi_map.values():
            pi.set_logical_interfaces(li_map.values())
            feature_config.add_physical_interfaces(pi)

        return feature_config
    # end push_conf

# end L2GatewayFeature
