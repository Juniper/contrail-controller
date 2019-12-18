#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""Underlay IP CLOS Feature Implementation."""

from collections import OrderedDict

from abstract_device_api.abstract_device_xsd import *

from .db import LogicalInterfaceDM, PhysicalInterfaceDM, PhysicalRouterDM
from .dm_utils import DMUtils
from .feature_base import FeatureBase


class UnderlayIpClosFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'underlay-ip-clos'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        super(UnderlayIpClosFeature, self).__init__(
            logger, physical_router, configs)
    # end __init__

    def _fetch_pi_li_iip(self, physical_interfaces):
        for pi_uuid in physical_interfaces:
            pi_obj = PhysicalInterfaceDM.get(pi_uuid)
            if pi_obj is None:
                self._logger.error("unable to read physical interface %s" %
                                   pi_uuid)
            else:
                for li_uuid in pi_obj.logical_interfaces:
                    li_obj = LogicalInterfaceDM.get(li_uuid)
                    if li_obj is None:
                        self._logger.error(
                            "unable to read logical interface %s" % li_uuid)
                    elif li_obj.instance_ip is not None:
                        iip_obj = InstanceIpDM.get(li_obj.instance_ip)
                        if iip_obj is None:
                            self._logger.error(
                                "unable to read instance ip %s" %
                                li_obj.instance_ip)
                        else:
                            yield pi_obj, li_obj, iip_obj
    # end _fetch_pi_li_iip

    def _build_underlay_bgp(self, feature_config):
        if self._physical_router.allocated_asn is None:
            self._logger.error("physical router %s(%s) does not have asn"
                               " allocated" % (self._physical_router.name,
                                               self._physical_router.uuid))
            return

        pi_map = OrderedDict()
        for pi_obj, li_obj, iip_obj in self.\
                _fetch_pi_li_iip(self._physical_router.physical_interfaces):
            if pi_obj and li_obj and iip_obj and iip_obj.instance_ip_address:
                pi, li_map = self._add_or_lookup_pi(
                    pi_map, pi_obj.name, 'regular')

                li = self._add_or_lookup_li(li_map, li_obj.name,
                                            int(li_obj.name.split('.')[-1]))

                self._add_ip_address(li, iip_obj.instance_ip_address)

                self._logger.debug("looking for peers for physical"
                                   " interface %s(%s)" % (pi_obj.name,
                                                          pi_obj.uuid))
                # Add this bgp object only if it has a peer
                underlay_asn = self._physical_router.allocated_asn
                bgp_name = DMUtils.make_underlay_bgp_group_name(
                    underlay_asn, li_obj.name, is_external=True)
                bgp = Bgp(name=bgp_name,
                          ip_address=iip_obj.instance_ip_address,
                          autonomous_system=underlay_asn,
                          type_='external')
                peers = OrderedDict()
                # Assumption: PIs are connected for IP-CLOS peering only
                for peer_pi_obj, peer_li_obj, peer_iip_obj in\
                        self._fetch_pi_li_iip(pi_obj.physical_interfaces):
                    if peer_pi_obj and peer_li_obj and peer_iip_obj and\
                            peer_iip_obj.instance_ip_address:

                        peer_pr = PhysicalRouterDM.get(
                            peer_pi_obj.physical_router)
                        if peer_pr is None:
                            self._logger.error(
                                "unable to read peer physical router %s"
                                % peer_pi_obj.physical_router)
                        elif peer_pr.allocated_asn is None:
                            self._logger.error(
                                "peer physical router %s does not have"
                                " asn allocated" % peer_pi_obj.physical_router)
                        elif peer_pr != self._physical_router:
                            peer = Bgp(
                                name=peer_pr.name,
                                ip_address=peer_iip_obj.instance_ip_address,
                                autonomous_system=peer_pr.allocated_asn)
                            peers[peer_pr.name] = peer

                if peers:
                    bgp.set_peers(list(peers.values()))
                    feature_config.add_bgp(bgp)
    # end _build_underlay_bgp

    def feature_config(self, **kwargs):
        if not self._physical_router.underlay_managed:
            return None
        feature_config = Feature(name=self.feature_name())
        self._build_underlay_bgp(feature_config)
        return feature_config
    # end push_conf

# end UnderlayIpClosFeature
