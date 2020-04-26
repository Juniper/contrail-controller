#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""L2 Gateway Feature Implementation."""

from builtins import str
from collections import OrderedDict

from abstract_device_api.abstract_device_xsd import *

from .db import LogicalRouterDM, PhysicalInterfaceDM, VirtualNetworkDM, \
    VirtualPortGroupDM
from .dm_utils import DMUtils
from .feature_base import FeatureBase

import gevent # noqa


class L2GatewayFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'l2-gateway'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_map = None
        super(L2GatewayFeature, self).__init__(logger, physical_router,
                                               configs)
    # end __init__

    def _build_ri_config(self, vn, ri_name, ri_obj, interfaces, export_targets,
                         import_targets, feature_config):
        gevent.idle()
        encapsulation_priorities = self._get_encapsulation_priorities()
        highest_encapsulation = encapsulation_priorities[0]
        network_id = vn.vn_network_id
        vxlan_id = vn.get_vxlan_vni()
        is_master_vn = False
        if vn.logical_router is None:
            # try updating logical router incase of DM restart
            vn.set_logical_router(vn.fq_name[-1])
        lr_uuid = vn.logical_router
        if lr_uuid:
            lr = LogicalRouterDM.get(lr_uuid)
            if lr:
                if lr.is_master == True:
                    is_master_vn = True

        ri = RoutingInstance(
            name=ri_name, virtual_network_mode='l2',
            export_targets=export_targets, import_targets=import_targets,
            virtual_network_id=str(network_id), vxlan_id=str(vxlan_id),
            is_public_network=vn.router_external, is_master=is_master_vn)

        ri.set_virtual_network_id(str(network_id))
        ri.set_vxlan_id(str(vxlan_id))
        ri.set_is_public_network(vn.router_external)

        vlan = None
        if highest_encapsulation == 'VXLAN':
            ri.set_routing_instance_type('virtual-switch')
            vlan = Vlan(name=DMUtils.make_bridge_name(vxlan_id),
                        vxlan_id=vxlan_id)
            vlan.set_comment(DMUtils.vn_bd_comment(vn, 'VXLAN'))
            desc = "Virtual Network - %s" % vn.name
            vlan.set_description(desc)
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
        vpg_map = {}

        for interface in interfaces:
            interface_map.setdefault(interface.pi_name, []).append(interface)
            vpg_map[interface.pi_name] = interface.vpg_obj.name

        for pi_name, interface_list in list(interface_map.items()):
            untagged = set([int(i.port_vlan_tag) for i in interface_list if
                            int(i.vlan_tag) == 0])
            if len(untagged) > 1:
                self._logger.error(
                    "Only one untagged interface is allowed on a PI %s" %
                    pi_name)
                continue
            tagged = [int(i.vlan_tag) for i in interface_list
                      if int(i.vlan_tag) != 0]
            if self._is_enterprise_style(self._physical_router):
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
            if not pi_name.startswith('ae'):
                # Handle only the regular interfaces here.
                lag = LinkAggrGroup(description="Virtual Port Group : %s" %
                                                vpg_map[pi_name])
                pi.set_link_aggregation_group(lag)

            for interface in interface_list:
                if int(interface.vlan_tag) == 0:
                    is_tagged = False
                    vlan_tag = str(interface.port_vlan_tag)
                else:
                    is_tagged = True
                    vlan_tag = str(interface.vlan_tag)
                unit = self._add_or_lookup_li(li_map, interface.li_name,
                                              interface.unit)
                unit.set_comment(DMUtils.l2_evpn_intf_unit_comment(
                    vn, is_tagged, vlan_tag))
                unit.set_is_tagged(is_tagged)
                unit.set_vlan_tag(vlan_tag)
                if vlan:
                    vlan.set_vlan_id(int(vlan_tag))
                    self._add_ref_to_list(vlan.get_interfaces(),
                                          interface.li_name)
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

        self.build_vpg_config()
        vn_dict = self._get_connected_vn_li_map()
        for vn_uuid, interfaces in list(vn_dict.items()):
            vn_obj = VirtualNetworkDM.get(vn_uuid)
            ri_obj = self._get_primary_ri(vn_obj)
            if ri_obj is None:
                continue
            ri_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                            vn_obj.vn_network_id, 'l2')
            export_targets, import_targets = self._get_export_import_targets(
                vn_obj, ri_obj)
            ri = self._build_ri_config(
                vn_obj, ri_name, ri_obj, interfaces,
                export_targets, import_targets, feature_config)
            feature_config.add_routing_instances(ri)

        for pi, li_map in list(self.pi_map.values()):
            pi.set_logical_interfaces(list(li_map.values()))
            feature_config.add_physical_interfaces(pi)

        return feature_config
    # end push_conf

    def build_vpg_config(self):
        multi_homed = False
        pr = self._physical_router
        if not pr:
            return
        for vpg_uuid in pr.virtual_port_groups or []:
            ae_link_members = {}
            vpg_obj = VirtualPortGroupDM.get(vpg_uuid)
            if not vpg_obj:
                continue
            if not vpg_obj.virtual_machine_interfaces:
                continue
            esi = vpg_obj.esi
            for pi_uuid in vpg_obj.physical_interfaces or []:
                if pi_uuid not in pr.physical_interfaces:
                    multi_homed = True
                    continue
                ae_id = vpg_obj.pi_ae_map.get(pi_uuid)
                if ae_id is not None:
                    ae_intf_name = 'ae' + str(ae_id)
                    pi = PhysicalInterfaceDM.get(pi_uuid)
                    if not pi:
                        continue
                    if ae_intf_name in ae_link_members:
                        ae_link_members[ae_intf_name].append(pi.name)
                    else:
                        ae_link_members[ae_intf_name] = []
                        ae_link_members[ae_intf_name].append(pi.name)

            for ae_intf_name, link_members in list(ae_link_members.items()):
                self._logger.info("LAG obj_uuid: %s, link_members: %s, "
                                  "name: %s" % (vpg_uuid, link_members,
                                                ae_intf_name))
                lag = LinkAggrGroup(lacp_enabled=True,
                                    link_members=link_members,
                                    description="Virtual Port Group : %s" %
                                                vpg_obj.name)
                intf, _ = self._add_or_lookup_pi(self.pi_map, ae_intf_name,
                                                 'lag')
                intf.set_link_aggregation_group(lag)
                intf.set_comment("ae interface")

                # check if esi needs to be set
                if multi_homed:
                    intf.set_ethernet_segment_identifier(esi)
                    multi_homed = False

    # end build_vpg_config

# end L2GatewayFeature
