#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
# This file contains utility method for importing all plugin
# imeplementations for self registration. All plugins must be
# imported before DeviceConf invokes plugin registrations.
# Please add an entry here if there is a new plugin
#


def import_plugins():
    from .juniper_conf import JuniperConf
    from .mx_conf import MxConf
    from .qfx_conf import QfxConf
    from .qfx_5k import Qfx5kConf
    from .qfx_10k import Qfx10kConf
    from .e2_conf import MxE2Conf
# end import_plugins


def import_ansible_plugins():
    from .ansible_conf import AnsibleConf
    from .ansible_role_common import AnsibleRoleCommon
    from .overlay_conf import OverlayConf
    from .pnf_conf import PnfConf
# end import_ansible_plugins


def import_feature_plugins():
    from .underlay_ip_clos_feature import UnderlayIpClosFeature
    from .overlay_bgp_feature import OverlayBgpFeature
    from .l2_gateway_feature import L2GatewayFeature
    from .l3_gateway_feature import L3GatewayFeature
    from .vn_interconnect_feature import VnInterconnectFeature
    from .assisted_replicator_feature import AssistedReplicatorFeature
    from .storm_control_feature import StormControlFeature
    from .telemetry_feature import TelemetryFeature
# end import_feature_plugins
