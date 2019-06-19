#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of abstract config generation for
storm control feature
"""

import db
from abstract_device_api.abstract_device_xsd import *
from collections import OrderedDict
from feature_base import FeatureBase

class StormControlFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'storm-control'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_map = None
        super(StormControlFeature, self).__init__(logger, physical_router, configs)
    # end __init__

    def _build_storm_control_interface_config(self, interfaces, feature_config):
        interface_map = OrderedDict()
        for interface in interfaces:
            interface_map.setdefault(interface.pi_name, []).append(interface)

        for pi_name, interface_list in interface_map.items():
            _, li_map = self._add_or_lookup_pi(self.pi_map, pi_name)
            for interface in interface_list:
                unit = self._add_or_lookup_li(li_map, interface.li_name, interface.unit)
                # attach port profiles
                self._attach_port_profiles(unit, feature_config)

    def _attach_port_profiles(self, unit, feature_config):
        pp_list = []
        pr = self._physical_router
        for vpg_uuid in pr.virtual_port_groups or []:
            vpg_obj = VirtualPortGroupDM.get(vpg_uuid)
            if not vpg_obj:
                continue

            pp_list_temp = vpg_obj.get_attached_port_profiles(unit.get_vlan_tag())
            for pp in pp_list_temp:
                if pp not in pp_list:
                    pp_list.append(pp)

        for pp in pp_list or []:
            sc_uuid = pp.storm_control_profile
            scp = db.StormControlProfile.get(sc_uuid)
            if scp:
                self._build_storm_control_config(scp, feature_config)
                unit.set_storm_control_profile(scp)

    def _build_storm_control_config(self, scp, feature_config):
        sc = StormControl(name=scp.name)
        params = scp.storm_control_params
        traffic_type = []
        if params:
            sc.set_bandwidth_percent(params.get('bandwidth_percent'))
            if params.get('no-broadcast'):
                traffic_type.append('no_broadcast')
            if params.get('no-multicast'):
                traffic_type.append('no_multicast')
            if params.get('no-registered-multicast'):
                traffic_type.append('no_registered_multicast')
            if params.get('no-unregistered-multicast'):
                traffic_type.append('no_unregistered_multicast')
            if params.get('no-unknown-unicast'):
                traffic_type.append('no_unknown_unicast')
            sc.set_traffic_type(traffic_type)
            sc.set_recovery_timeout(params.get('recovery_timeout'))
            sc.set_actions(params.get('storm_control_actions'))
        feature_config.add_storm_control(sc)

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

        vn_dict = self._get_connected_vn_li_map()
        for vn_uuid, interfaces in vn_dict.items():
            self._build_storm_control_interface_config(interfaces,
                                                       feature_config)

        for pi, li_map in self.pi_map.values():
            pi.set_logical_interfaces(li_map.values())
            feature_config.add_physical_interfaces(pi)

        return feature_config
    # end feature_config

# end StormControlFeature
