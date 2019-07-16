#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""Assisted Replicator Feature Implementation."""

from abstract_device_api.abstract_device_xsd import *
from feature_base import FeatureBase


class AssistedReplicatorFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'assisted-replicator'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        super(AssistedReplicatorFeature, self).__init__(
            logger, physical_router, configs)
    # end __init__

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

    def feature_config(self, **kwargs):
        feature_config = Feature(name=self.feature_name())
        self._build_ar_config(feature_config)
        return feature_config
    # end push_conf

# end AssistedReplicatorFeature
