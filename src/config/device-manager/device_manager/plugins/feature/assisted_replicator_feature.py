#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""Assisted Replicator Feature Implementation."""

from abstract_device_api.abstract_device_xsd import *

from .db import IntentMapDM, VirtualNetworkDM
from .dm_utils import DMUtils
from .feature_base import FeatureBase


class AssistedReplicatorFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'assisted-replicator'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        super(AssistedReplicatorFeature, self).__init__(
            logger, physical_router, configs)
    # end __init__

    def _build_vn_config(self, feature_config):
        if self._physical_router.intent_maps:
            for intent_uuid in self._physical_router.intent_maps or ():
                imp = IntentMapDM.get(intent_uuid)
                if imp.intent_type == 'assisted-replicator':
                    for vn_uuid in imp.virtual_networks or []:
                        vn_obj = VirtualNetworkDM.get(vn_uuid)
                        ri_obj = self._get_primary_ri(vn_obj)
                        if ri_obj is None:
                            continue
                        # build route targets
                        ri_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                        vn_obj.vn_network_id,
                                                        'l2')
                        export_targets, import_targets = \
                            self._get_export_import_targets(vn_obj, ri_obj)
                        network_id = vn_obj.vn_network_id
                        vxlan_id = vn_obj.get_vxlan_vni()
                        ri = RoutingInstance(
                            name=ri_name, virtual_network_mode='l2',
                            export_targets=export_targets,
                            import_targets=import_targets,
                            virtual_network_id=str(network_id),
                            vxlan_id=str(vxlan_id))
                        feature_config.add_routing_instances(ri)
                        # build vlan config
                        vlan = Vlan(
                            name=DMUtils.make_bridge_name(
                                vn_obj.get_vxlan_vni()),
                            vxlan_id=vn_obj.get_vxlan_vni())
                        vlan.set_comment(DMUtils.vn_bd_comment(vn_obj,
                                                               'VXLAN'))
                        desc = "Virtual Network - %s" % vn_obj.name
                        vlan.set_description(desc)
                        feature_config.add_vlans(vlan)

    def _build_ar_config(self, feature_config):
        rep_act_delay = None
        for c in self._configs:
            if c.additional_params.replicator_activation_delay:
                rep_act_delay = c.additional_params.replicator_activation_delay
                break

        ar = AssistedReplicator(
            ar_loopback_ip=self._physical_router.replicator_ip,
            replicator_activation_delay=rep_act_delay)

        feature_config.set_assisted_replicator(ar)

        self._build_vn_config(feature_config)

    def feature_config(self, **kwargs):
        feature_config = Feature(name=self.feature_name())
        self._build_ar_config(feature_config)
        return feature_config
    # end push_conf

# end AssistedReplicatorFeature
