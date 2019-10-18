#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
""" Virtual Port Group Feature Implementation."""
from collections import OrderedDict

from abstract_device_api.abstract_device_xsd import *
import db
from dm_utils import DMUtils
from feature_base import FeatureBase
from netaddr import IPAddress, IPNetwork


class VPGFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'virtual-port-group'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_map = None
        self.physical_router = physical_router
        super(VPGFeature, self).__init__(
            logger, physical_router, configs)
    # end __init__

    def _build_vpg_config(self):
        multi_homed = False
        pr = self._physical_router
        if not pr:
            return
        for vpg_uuid in pr.virtual_port_groups or []:
            ae_link_members = {}
            vpg_obj = db.VirtualPortGroupDM.get(vpg_uuid)
            if not vpg_obj:
                continue
            esi = vpg_obj.esi
            for pi_uuid in vpg_obj.physical_interfaces or []:
                if pi_uuid not in pr.physical_interfaces:
                    # This Physical Interface UUID is related to different
                    # Physical Router (PR)/Leaf.
                    multi_homed = True
                    continue
                ae_id = vpg_obj.pi_ae_map.get(pi_uuid)
                if ae_id is not None:
                    # For VPG (LAG & Multi Home) - we are interested only for
                    #  the "ae" interfaces.
                    ae_intf_name = 'ae' + str(ae_id)
                    pi = db.PhysicalInterfaceDM.get(pi_uuid)
                    if not pi:
                        continue
                    if ae_intf_name in ae_link_members:
                        ae_link_members[ae_intf_name].append(pi.name)
                    else:
                        ae_link_members[ae_intf_name] = []
                        ae_link_members[ae_intf_name].append(pi.name)

            for ae_intf_name, link_members in ae_link_members.iteritems():
                self._logger.info("LAG obj_uuid: %s, link_members: %s, "
                                  "name: %s" %
                                  (vpg_uuid, link_members, ae_intf_name))
                lag = LinkAggrGroup(lacp_enabled=True,
                                    link_members=link_members,
                                    description="Virtual Port Group : %s" %
                                                vpg_obj.name)
                intf, _ = self._add_or_lookup_pi(self.pi_map,
                                                 ae_intf_name, 'lag')
                intf.set_link_aggregation_group(lag)
                intf.set_comment("ae interface")

                if multi_homed:
                    intf.set_ethernet_segment_identifier(esi)
                    multi_homed = False

    # end _build_vpg_config

    def feature_config(self, **kwargs):
        self.pi_map = OrderedDict()
        feature_config = Feature(name=self.feature_name())
        self._build_vpg_config()

        for pi, li_map in self.pi_map.values():
            pi.set_logical_interfaces(li_map.values())
            feature_config.add_physical_interfaces(pi)

        return feature_config

# end VPGFeature
