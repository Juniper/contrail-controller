#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""L3 Gateway Feature Implementation."""

from builtins import str
from collections import OrderedDict

from abstract_device_api.abstract_device_xsd import *

from .db import VirtualNetworkDM
from .dm_utils import DMUtils
from .feature_base import FeatureBase

import gevent # noqa


class L3GatewayFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'l3-gateway'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_map = None
        super(L3GatewayFeature, self).__init__(
            logger, physical_router, configs)
    # end __init__

    def _get_connected_vn_ids(self):
        return self._get_connected_vns('l3')
    # end _get_connected_vn_ids

    def _build_ri_config(self, vn, ri_name, ri_obj, export_targets,
                         import_targets, feature_config, irb_ips):
        gevent.idle()
        network_id = vn.vn_network_id
        vxlan_id = vn.get_vxlan_vni()
        is_master_vn = False
        if vn.logical_router is None:
            # try updating logical router incase of DM restart
            vn.set_logical_router(vn.fq_name[-1])
        lr_uuid = vn.logical_router
        if lr_uuid:
            lr = db.LogicalRouterDM.get(lr_uuid)
            if lr:
                if lr.is_master == True:
                    is_master_vn = True
        ri = RoutingInstance(
            name=ri_name, virtual_network_mode='l3',
            export_targets=export_targets, import_targets=import_targets,
            virtual_network_id=str(network_id), vxlan_id=str(vxlan_id),
            is_public_network=vn.router_external, routing_instance_type='vrf',
            is_master=is_master_vn)

        for prefix in vn.get_prefixes(self._physical_router.uuid):
            ri.add_prefixes(self._get_subnet_for_cidr(prefix))

        _, li_map = self._add_or_lookup_pi(self.pi_map, 'irb', 'irb')
        irb = None
        if irb_ips:
            irb = self._add_or_lookup_li(li_map, 'irb.' + str(network_id),
                                         network_id)
            if vn.has_ipv6_subnet is True:
                irb.set_is_virtual_router(True)
            for (irb_ip, gateway) in irb_ips:
                self._add_ip_address(irb, irb_ip, gateway=gateway)

        vlan = Vlan(name=DMUtils.make_bridge_name(vxlan_id), vxlan_id=vxlan_id)
        desc = "Virtual Network - %s" % vn.name
        vlan.set_description(desc)
        feature_config.add_vlans(vlan)
        if irb:
            self._add_ref_to_list(vlan.get_interfaces(), irb.get_name())

        return ri
    # end _build_ri_config

    def feature_config(self, **kwargs):
        self.pi_map = OrderedDict()
        feature_config = Feature(name=self.feature_name())
        vns = self._get_connected_vn_ids()
        use_gateway_ip = all(
            [c.additional_params.use_gateway_ip == 'True'
                for c in self._configs])
        if vns:
            irb_ip_map = self._physical_router.allocate_irb_ips_for(
                vns, use_gateway_ip)

        for vn_uuid in vns:
            vn_obj = VirtualNetworkDM.get(vn_uuid)
            ri_obj = self._get_primary_ri(vn_obj)
            if ri_obj is None:
                continue
            ri_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                            vn_obj.vn_network_id, 'l3')
            export_targets, import_targets = self._get_export_import_targets(
                vn_obj, ri_obj)
            ri = self._build_ri_config(
                vn_obj, ri_name, ri_obj, export_targets,
                import_targets, feature_config, irb_ip_map.get(vn_uuid, []))
            feature_config.add_routing_instances(ri)

        for pi, li_map in list(self.pi_map.values()):
            pi.set_logical_interfaces(list(li_map.values()))
            feature_config.add_physical_interfaces(pi)

        return feature_config
    # end push_conf

# end L3GatewayFeature
