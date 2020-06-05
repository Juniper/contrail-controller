#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
Port Profile feature implementation.

This file contains implementation of abstract config generation for
port profile feature
"""

from builtins import str
from collections import OrderedDict

from abstract_device_api.abstract_device_xsd import *

from .db import PhysicalInterfaceDM, PortProfileDM, StormControlProfileDM, \
    VirtualMachineInterfaceDM, VirtualPortGroupDM
from .feature_base import FeatureBase

import gevent # noqa


class PortProfileFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'port-profile'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_list = None
        self.pp_map = None
        super(PortProfileFeature, self).__init__(logger, physical_router,
                                                 configs)
    # end __init__

    def _add_to_pp_map(self, pp_name, pp_obj):
        if pp_name in self.pp_map:
            return
        else:
            self.pp_map[pp_name] = pp_obj
    # end _add_to_pp_map

    def _build_port_profile_interface_config(self):
        pr = self._physical_router

        for vpg_uuid in pr.virtual_port_groups or []:
            vpg_obj = VirtualPortGroupDM.get(vpg_uuid)
            if not vpg_obj:
                continue
            pp_list = vpg_obj.port_profiles
            for pp_uuid in pp_list or []:
                pp = PortProfileDM.get(pp_uuid)
                if pp:
                    pp_name = pp.fq_name[-1] + "-" + pp.fq_name[-2]
                    pp_obj = PortProfile(name=pp_name)

                    for pi_uuid in vpg_obj.physical_interfaces or []:
                        if pi_uuid not in pr.physical_interfaces:
                            continue
                        ae_id = vpg_obj.pi_ae_map.get(pi_uuid)
                        if ae_id is not None:
                            ae_intf_name = 'ae' + str(ae_id)
                            pi = PhysicalInterface(name=ae_intf_name)
                        else:
                            pi_obj = PhysicalInterfaceDM.get(pi_uuid)
                            pi = PhysicalInterface(name=pi_obj.name)
                        if pi not in self.pi_list:
                            self.pi_list.add(pi)
                            pi.set_port_profile(pp_name)

                    # storm control feature is only supported with enterprise
                    # style configs. User can on-board PR with ERB role in a SP
                    # style fabric, which supports only enterprise style config
                    if self._is_enterprise_style(pr) or pr.is_erb_only():
                        self._build_storm_control_interface_config(
                            pp, pp_obj)

                    # TODO
                    self._build_port_profile_parameters_config(pp, pp_obj)

                    self._add_to_pp_map(pp_name, pp_obj)

    def _build_port_profile_parameters_config(self, pp, pp_obj):
        pp_params = pp.port_profile_params
        if pp_params:
            port_params = pp_params.get('port_params', {})
            flow_control = pp_params.get('flow_control', False)
            lacp_params = pp_params.get('lacp_params', {})
            loop_protection = pp_params.get('bpdu_loop_protection', False)
            port_cos_untrust = pp_params.get('port_cos_untrust', False)

            pp_obj.set_flow_control(flow_control)
            pp_obj.set_bpdu_loop_protection(loop_protection)
            pp_obj.set_port_cos_untrust(port_cos_untrust)
            if port_params:
                port_params_obj = PortParameters()
                if port_params.get('port_disable'):
                    port_params_obj.set_port_disable(True)
                if port_params.get('port_mtu'):
                    port_params_obj.set_port_mtu(
                        port_params.get('port_mtu')
                    )
                if port_params.get('port_description'):
                    port_params_obj.set_port_description(
                        port_params.get('port_description')
                    )
                pp_obj.set_port_params(port_params_obj)
            if lacp_params:
                lacp_obj = LacpParams()
                if lacp_params.get('lacp_enable'):
                    lacp_obj.set_lacp_enable(True)
                    if lacp_params.get('lacp_interval'):
                        lacp_obj.set_lacp_interval(
                            lacp_params.get('lacp_interval')
                        )
                    if lacp_params.get('lacp_mode'):
                        lacp_obj.set_lacp_mode(
                            lacp_params.get('lacp_mode')
                        )
                pp_obj.set_lacp_params(lacp_obj)

    def _build_storm_control_interface_config(self, pp, pp_obj):
        sc_uuid = pp.storm_control_profile
        scp = StormControlProfileDM.get(sc_uuid)
        if scp:
            self._build_storm_control_config(scp, pp_obj)
    # end _build_storm_control_interface_config

    def _build_storm_control_config(self, scp, pp_obj):
        sc_name = (scp.fq_name[-1].replace(' ', '-')) + "-" + scp.fq_name[-2]
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
        pp_obj.set_storm_control_profile(sc)
    # end _build_storm_control_config

    def feature_config(self, **kwargs):
        self.pi_list = set()
        self.pp_map = OrderedDict()
        feature_config = Feature(name=self.feature_name())

        self._build_port_profile_interface_config()

        for pi in set(self.pi_list):
            feature_config.add_physical_interfaces(pi)

        for pp_name in self.pp_map:
            feature_config.add_port_profile(self.pp_map[pp_name])

        return feature_config
    # end feature_config

# end PortProfileFeature
