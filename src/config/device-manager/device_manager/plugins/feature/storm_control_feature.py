#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
Storm control feature implementation.

This file contains implementation of abstract config generation for
storm control feature
"""

from builtins import str
from collections import OrderedDict

from abstract_device_api.abstract_device_xsd import *

from .db import StormControlProfileDM, VirtualPortGroupDM
from .feature_base import FeatureBase

import gevent # noqa


class StormControlFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'storm-control'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_map = None
        self.sc_map = None
        super(StormControlFeature, self).__init__(logger, physical_router,
                                                  configs)
    # end __init__

    def _add_to_sc_map(self, sc_name, sc_obj):
        if sc_name in self.sc_map:
            return
        else:
            self.sc_map[sc_name] = sc_obj
    # end _add_to_sc_map

    def _build_storm_control_interface_config(self, interfaces):
        gevent.idle()
        interface_map = OrderedDict()
        for interface in interfaces:
            interface_map.setdefault(interface.pi_name, []).append(interface)

        for pi_name, interface_list in list(interface_map.items()):
            _, li_map = self._add_or_lookup_pi(self.pi_map, pi_name)
            for interface in interface_list:
                if int(interface.vlan_tag) == 0:
                    vlan_tag = str(interface.port_vlan_tag)
                else:
                    vlan_tag = str(interface.vlan_tag)
                unit = self._add_or_lookup_li(li_map, interface.li_name,
                                              interface.unit)
                unit.set_vlan_tag(vlan_tag)
                # attach port profiles
                self._attach_port_profiles(unit,
                                           interface)

    def _attach_port_profiles(self, unit, interface):
        pp_list = []
        vpg_obj = interface.vpg_obj
        pp_list_temp = \
            vpg_obj.get_attached_port_profiles(unit.get_vlan_tag(),
                                               interface)
        for pp in pp_list_temp:
            if pp not in pp_list:
                pp_list.append(pp)

        for pp in pp_list or []:
            sc_uuid = pp.storm_control_profile
            scp = StormControlProfileDM.get(sc_uuid)
            if scp:
                self._build_storm_control_config(scp)
                sc_name = scp.fq_name[-1] + "-" + scp.fq_name[-2]
                unit.set_storm_control_profile(sc_name)

    def _build_storm_control_config(self, scp):
        sc_name = scp.fq_name[-1] + "-" + scp.fq_name[-2]
        sc = StormControl(name=sc_name)
        params = scp.storm_control_params
        traffic_type = []
        if params:
            sc.set_bandwidth_percent(params.get('bandwidth_percent'))
            if params.get('no_broadcast'):
                traffic_type.append('no-broadcast')
            if params.get('no_multicast'):
                traffic_type.append('no-multicast')
            if params.get('no_registered_multicast'):
                traffic_type.append('no-registered-multicast')
            if params.get('no_unregistered_multicast'):
                traffic_type.append('no-unregistered-multicast')
            if params.get('no_unknown_unicast'):
                traffic_type.append('no-unknown-unicast')
            sc.set_traffic_type(traffic_type)
            sc.set_recovery_timeout(params.get('recovery_timeout'))
            sc.set_actions(params.get('storm_control_actions'))
        self._add_to_sc_map(sc_name, sc)

    def _get_connected_vn_li_map(self):
        vns = self._get_connected_vns('l2')
        vn_li_map = self._get_vn_li_map('l2')
        for vn in vns:
            vn_li_map.setdefault(vn, [])
        return vn_li_map
    # end _get_connected_vn_li_map

    def feature_config(self, **kwargs):
        self.pi_map = OrderedDict()
        self.sc_map = OrderedDict()
        feature_config = Feature(name=self.feature_name())

        if (not self._is_enterprise_style(self._physical_router) and
           not self._physical_router.is_erb_only()):
            return feature_config

        vn_dict = self._get_connected_vn_li_map()
        for vn_uuid, interfaces in list(vn_dict.items()):
            self._build_storm_control_interface_config(interfaces)

        for pi, li_map in list(self.pi_map.values()):
            pi.set_logical_interfaces(list(li_map.values()))
            feature_config.add_physical_interfaces(pi)

        for sc_name in self.sc_map:
            feature_config.add_storm_control(self.sc_map[sc_name])

        return feature_config
    # end feature_config

# end StormControlFeature
