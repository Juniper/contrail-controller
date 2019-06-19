#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of abstract config generation for 
storm control feature
"""

from abstract_device_api.abstract_device_xsd import *
from feature_base import FeatureBase

class StormControlFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'storm-control'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
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

            pp_list_temp = vpg_obj.get_attached_port_profiless(unit.get_vlan_tag())
            for pp in pp_list_temp:
                if pp not in pp_list:
                    pp_list.append(pp)

       for pp in pp_list or []:
            scps = pp.storm_control_profiles
            for scp in scps or []:
                 scp = StormControlProfile.get(scp)
                 if scp:
                     self.build_storm_control_config(scp, feature_config)

        for sg in sg_list:
            flist = self.get_configured_filters(sg)
            filter_list += flist
        if filter_list:
            for fname in filter_list:
                unit.add_firewall_filters(fname)
            
        feature_config.add_storm_control(storm_ctrl)

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
