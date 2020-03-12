#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
Telemetry feature implementation.

This file contains implementation of abstract config generation for
telemetry feature
"""

from collections import OrderedDict

from abstract_device_api.abstract_device_xsd import *

from .db import FlowNodeDM, PhysicalInterfaceDM, SflowProfileDM, \
    TelemetryProfileDM
from .feature_base import FeatureBase


class TelemetryFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'telemetry'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_list = None
        self.telemetry_map = None
        super(TelemetryFeature, self).__init__(logger, physical_router,
                                               configs)
    # end __init__

    def _add_to_telemetry_map(self, telemetry_name, telemetry_obj):
        if telemetry_name in self.telemetry_map:
            return
        else:
            self.telemetry_map[telemetry_name] = telemetry_obj
    # end _add_to_telemetry_map

    def _build_telemetry_interface_config(self, interface,
                                          telemetry_profile_name,
                                          sflow_profile_name=None,
                                          sflow_profile_params=None):
        if sflow_profile_params:
            self._build_sflow_interface_config(
                interface,
                telemetry_profile_name,
                sflow_profile_name,
                sflow_profile_params)
    # end _build_telemetry_interface_config

    def _build_sflow_interface_config(self, interface,
                                      telemetry_profile_name,
                                      sflow_profile_name,
                                      sflow_profile_params):

        interface_name = interface.name
        interface_fqname_str = ':'.join(interface.fq_name)
        interface_type = interface.interface_type
        sflow_interface_type = sflow_profile_params.get(
            'enabled_interface_type')

        if TelemetryFeature._check_interface_for_sflow(
                interface_fqname_str,
                interface_type,
                sflow_interface_type,
                sflow_profile_params.get('enabled_interface_params')):
            self._build_telemetry_config(telemetry_profile_name,
                                         sflow_profile_name,
                                         sflow_profile_params)

            pi = PhysicalInterface(name=interface_name)
            self.pi_list.add(pi)
            pi.set_telemetry_profile(telemetry_profile_name)
    # end _build_sflow_interface_config

    @staticmethod
    def _check_interface_for_sflow(interface_fqname_str,
                                   interface_type,
                                   sflow_interface_type,
                                   enabled_custom_interface_list):

        if sflow_interface_type == "all":
            return True
        elif sflow_interface_type == "custom":
            for custom_intf in enabled_custom_interface_list:
                # Assumption: custom_intf['name'] will in fact be
                # a fqname str as sent by the UI
                if interface_fqname_str == custom_intf.get('name'):
                    return True
        elif sflow_interface_type == interface_type:
            return True

        return False
    # end _check_interface_for_sflow

    def _build_telemetry_config(self, tp_name, sflow_name, sflow_params):

        tp = Telemetry(name=tp_name)
        collector_ip_addr = None

        sflow_profile_obj = SflowProfile(name=sflow_name)
        scf = sflow_params.get('stats_collection_frequency')
        if scf:
            if scf.get('sample_rate'):
                sflow_profile_obj.set_sample_rate(scf.get('sample_rate'))
            if scf.get('polling_interval'):
                sflow_profile_obj.set_polling_interval(
                    scf.get('polling_interval'))
            if scf.get('direction'):
                sflow_profile_obj.set_sample_direction(
                    scf.get('direction'))

        agent_id = sflow_params.get('agent_id')
        if agent_id:
            sflow_profile_obj.set_agent_id(agent_id)
        adap_sampl_rt = sflow_params.get('adaptive_sample_rate')
        if adap_sampl_rt:
            sflow_profile_obj.set_adaptive_sample_rate(adap_sampl_rt)
        enbld_intf_type = sflow_params.get('enabled_interface_type')
        if enbld_intf_type:
            sflow_profile_obj.set_enabled_interface_type(enbld_intf_type)
        enbld_intf_params = sflow_params.get('enabled_interface_params')
        for param in enbld_intf_params or []:
            enbld_intf_name = param.get('name')
            stats_sampl_rt = None
            stats_poll_intvl = None
            stats_coll_freq = \
                param.get('stats_collection_frequency')
            if stats_coll_freq:
                stats_sampl_rt = stats_coll_freq.get('sample_rate')
                stats_poll_intvl = stats_coll_freq.get('polling_interval')

            enbld_intf_params_obj = EnabledInterfaceParams(
                name=enbld_intf_name
            )
            if stats_sampl_rt:
                enbld_intf_params_obj.set_sample_rate(stats_sampl_rt)
            if stats_poll_intvl:
                enbld_intf_params_obj.set_polling_interval(stats_poll_intvl)
            sflow_profile_obj.add_enabled_interface_params(
                enbld_intf_params_obj)

        # all flow nodes will have same same load balancer IP
        for node in list(FlowNodeDM.values()):
            collector_ip_addr = node.virtual_ip_addr

        if collector_ip_addr:
            collector_params = CollectorParams(
                ip_address=collector_ip_addr,
                udp_port=6343
            )
            sflow_profile_obj.set_collector_params(
                collector_params)

        tp.set_sflow_profile(sflow_profile_obj)
        self._add_to_telemetry_map(tp_name, tp)
    # end _build_telemetry_config

    def feature_config(self, **kwargs):
        self.pi_list = set()
        self.telemetry_map = OrderedDict()
        feature_config = Feature(name=self.feature_name())

        pr = self._physical_router
        tp_uuid = pr.telemetry_profile
        tp = TelemetryProfileDM.get(tp_uuid)
        sflow_profile_params = None
        sflow_profile_name = ''
        tp_name = ''

        if tp:
            tp_name = tp.fq_name[-1] + "-" + tp.fq_name[-2]
            sflow_uuid = tp.sflow_profile
            sflow_profile = SflowProfileDM.get(sflow_uuid)
            if sflow_profile:
                sflow_profile_params = \
                    sflow_profile.sflow_params
                sflow_profile_name = sflow_profile.fq_name[-1] + \
                    "-" + sflow_profile.fq_name[-2]

        for interface_uuid in pr.physical_interfaces:
            interface = PhysicalInterfaceDM.get(interface_uuid)
            self._build_telemetry_interface_config(interface, tp_name,
                                                   sflow_profile_name,
                                                   sflow_profile_params)

        for pi in self.pi_list:
            feature_config.add_physical_interfaces(pi)

        for telemetry_name in self.telemetry_map:
            feature_config.add_telemetry(self.telemetry_map[telemetry_name])

        return feature_config
    # end feature_config

# end TelemetryFeature
