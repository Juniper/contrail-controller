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
import db
from feature_base import FeatureBase

import gevent # noqa


class StormControlFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'storm-control'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_list = None
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

    def _build_storm_control_interface_config(self):
        pr = self._physical_router
        for vpg_uuid in pr.virtual_port_groups or []:
            vpg_obj = db.VirtualPortGroupDM.get(vpg_uuid)
            if not vpg_obj:
                continue
            pp_list = vpg_obj.port_profiles
            for pp_uuid in pp_list or []:
                pp = db.PortProfileDM.get(pp_uuid)
                if pp:
                    sc_uuid = pp.storm_control_profile
                    scp = db.StormControlProfileDM.get(sc_uuid)
                    if scp:
                        self._build_storm_control_config(scp)
                        sc_name = scp.fq_name[-1] + "-" + scp.fq_name[-2]
                        for pi_uuid in vpg_obj.physical_interfaces or []:
                            if pi_uuid not in pr.physical_interfaces:
                                continue
                            ae_id = vpg_obj.pi_ae_map.get(pi_uuid)
                            if ae_id is not None:
                                ae_intf_name = 'ae' + str(ae_id)
                                pi = PhysicalInterface(name=ae_intf_name)
                            else:
                                pi_obj = db.PhysicalInterfaceDM.get(pi_uuid)
                                pi = PhysicalInterface(name=pi_obj.name)
                            if pi not in self.pi_list:
                                self.pi_list.add(pi)
                                pi.set_storm_control_profile(sc_name)
    # end _build_storm_control_interface_config

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
    # end _build_storm_control_config

    def feature_config(self, **kwargs):
        self.pi_list = set()
        self.sc_map = OrderedDict()
        feature_config = Feature(name=self.feature_name())

        # storm control feature is only supported with enterprise
        # style configs. User can on-board PR with ERB role in a SP
        # style fabric, which supports only enterprise style config
        if (not self._is_enterprise_style(self._physical_router) and
           not self._physical_router.is_erb_only()):
            return feature_config

        self._build_storm_control_interface_config()

        for pi in set(self.pi_list):
            feature_config.add_physical_interfaces(pi)

        for sc_name in self.sc_map:
            feature_config.add_storm_control(self.sc_map[sc_name])

        return feature_config
    # end feature_config

# end StormControlFeature
