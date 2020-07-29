#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#
"""PNF Service Chaining Feature Implementation."""

from collections import defaultdict, OrderedDict

from abstract_device_api.abstract_device_xsd import *

from .db import BgpRouterDM, InstanceIpDM, LogicalInterfaceDM, \
    LogicalRouterDM, PhysicalInterfaceDM, PhysicalRouterDM, PortTupleDM, \
    ServiceApplianceDM, ServiceApplianceSetDM, ServiceInstanceDM, \
    ServiceTemplateDM, VirtualNetworkDM
from .dm_utils import DMUtils
from .feature_base import FeatureBase


class PNFSrvcChainingFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return "pnf-service-chaining"

    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_map = None
        self.pi_map_leafspine = None
        self.ri_map = None
        self.ri_map_leafspine = None
        self.sc_zone_map = None
        self.sc_policy_map = None
        self.vlan_map = None
        self.svc_intf_vlans = None
        self.ri_protocols_leafspine_map = None
        super(PNFSrvcChainingFeature, self).__init__(logger, physical_router,
                                                     configs)

    # end __init__

    def get_peer_asn(self, pi_obj):
        pr_uuid = pi_obj.physical_router
        pr_obj = PhysicalRouterDM.get(pr_uuid)
        if pr_obj:
            bgp_uuid = pr_obj.bgp_router
            bgp_obj = BgpRouterDM.get(bgp_uuid)
            if bgp_obj and bgp_obj.params:
                return bgp_obj.params.get("autonomous_system")

        return None

    # end get_peer_asn

    def get_lr_name_from_ri(self, ri_name):
        lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(ri_name)
        lr = LogicalRouterDM.get(lr_uuid)
        if lr:
            return lr

    # end get_lr_name_from_ri

    def check_pnf_svc_required_params(self, svc_params):
        if (svc_params.get("peer_right_li_ips") and
                svc_params.get("right_li") and
                svc_params.get("right_li_ip") and svc_params.get("left_li") and
                svc_params.get("peer_left_li_ips") and
                svc_params.get("left_li_ip") and svc_params.get("lo0_li") and
                svc_params.get("left_peer_asn") and
                svc_params.get("right_peer_asn") and
                svc_params.get("lo0_li_ip")):
            return True
        return False

    # end check_pnf_svc_required_params

    def build_pnf_svc_ri_config(self, svc_params):
        if svc_params.get("svc_inst_name"):
            ri_name = svc_params.get("svc_inst_name") + "_left_right"
            ri = RoutingInstance(name=ri_name)
            pim = Pim()
            protocol = RoutingInstanceProtocols()
            self.ri_map[ri_name] = ri
            ri.set_comment("PNF svc routing instance")
            ri.set_routing_instance_type("virtual-router")
            if svc_params.get("lo0_li") and svc_params.get("lo0_li_ip"):
                ri.add_loopback_interfaces(
                    LogicalInterface(name=svc_params.get("lo0_li")))
                pimrp = PimRp(ip_address=svc_params.get("lo0_li_ip"))
                pim.set_rp(pimrp)
            if svc_params.get("left_li"):
                ri.add_routing_interfaces(
                    LogicalInterface(name=svc_params.get("left_li")))
                pim_intf = PimInterface(
                    LogicalInterface(name=svc_params.get("left_li")))
                pim.add_pim_interfaces(pim_intf)
            if svc_params.get("right_li"):
                ri.add_routing_interfaces(
                    LogicalInterface(name=svc_params.get("right_li")))
                pim_intf = PimInterface(
                    LogicalInterface(name=svc_params.get("right_li")))
                pim.add_pim_interfaces(pim_intf)

            protocol.add_pim(pim)
            # Build BGP config associated to this RI
            left_bgp_name = svc_params.get("svc_inst_name") + "_left"
            if svc_params.get("left_li_ip") and svc_params.get("left_asns"):
                bgp = Bgp(
                    name=left_bgp_name,
                    ip_address=svc_params.get("left_li_ip"),
                    autonomous_system=svc_params.get("left_asns")[0],
                    type_="external",
                )
                bgp.set_comment("PNF left BGP group")
                peers = {}
                for peer_ip in svc_params.get("peer_left_li_ips") or []:
                    name = left_bgp_name + "-" + peer_ip
                    peer = Bgp(
                        name=name,
                        ip_address=peer_ip,
                        autonomous_system=svc_params.get("left_peer_asn"),
                    )
                    peers[name] = peer
                if peers:
                    bgp.set_peers(self._get_values_sorted_by_key(peers))
                protocol.add_bgp(bgp)

            right_bgp_name = svc_params.get("svc_inst_name") + "_right"
            if svc_params.get("right_li_ip") and svc_params.get("right_asns"):
                bgp = Bgp(
                    name=right_bgp_name,
                    ip_address=svc_params.get("right_li_ip"),
                    autonomous_system=svc_params.get("right_asns")[0],
                    type_="external",
                )
                bgp.set_comment("PNF right BGP group")
                peers = {}
                for peer_ip in svc_params.get("peer_right_li_ips") or []:
                    name = right_bgp_name + "-" + peer_ip
                    peer = Bgp(
                        name=name,
                        ip_address=peer_ip,
                        autonomous_system=svc_params.get("right_peer_asn"),
                    )
                    peers[name] = peer
                if peers:
                    bgp.set_peers(self._get_values_sorted_by_key(peers))
                protocol.add_bgp(bgp)

            ri.add_protocols(protocol)

    # end build_pnf_svc_ri_config

    def build_pnf_svc_intfs_config(self, svc_params):
        # Add loopback interface
        if svc_params.get("lo0_li") and svc_params.get("lo0_li_ip"):
            lo0_intf = svc_params.get("lo0_li")
            ip = svc_params.get("lo0_li_ip")
            intf, li_map = self._add_or_lookup_pi(self.pi_map, "lo0",
                                                  "loopback")
            intf_unit = self._add_or_lookup_li(li_map, lo0_intf,
                                               lo0_intf.split(".")[1])
            intf_unit.set_comment("PNF loopback interface")
            intf_unit.set_vlan_tag(lo0_intf.split(".")[1])
            self._add_ip_address(intf_unit, ip)

        # Add left svc interface
        if svc_params.get("left_li") and svc_params.get("left_li_ip"):
            left_intf = svc_params.get("left_li")
            ip = svc_params.get("left_li_ip")
            intf, li_map = self._add_or_lookup_pi(self.pi_map,
                                                  left_intf.split(".")[0],
                                                  "service")
            intf_unit = self._add_or_lookup_li(li_map, left_intf,
                                               left_intf.split(".")[1])
            intf_unit.set_comment("PNF left svc interface")
            intf_unit.set_vlan_tag(left_intf.split(".")[1])
            self._add_ip_address(intf_unit, ip)

        # Add right svc interface
        if svc_params.get("right_li") and svc_params.get("right_li_ip"):
            right_intf = svc_params.get("right_li")
            ip = svc_params.get("right_li_ip")
            intf, li_map = self._add_or_lookup_pi(self.pi_map,
                                                  right_intf.split(".")[0],
                                                  "service")
            intf_unit = self._add_or_lookup_li(li_map, right_intf,
                                               right_intf.split(".")[1])
            intf_unit.set_comment("PNF right svc interface")
            intf_unit.set_vlan_tag(right_intf.split(".")[1])
            self._add_ip_address(intf_unit, ip)

    # end build_pnf_svc_intfs_config

    def build_pnf_svc_sc_zone_policy_config(self, svc_params):
        if svc_params.get("svc_inst_name"):
            left_sec_zone = svc_params.get("svc_inst_name") + "_left"
            right_sec_zone = svc_params.get("svc_inst_name") + "_right"
            left_sc = SecurityZone(name=left_sec_zone)
            self.sc_zone_map[left_sec_zone] = left_sc
            if svc_params.get("left_li"):
                left_sc.add_interfaces(
                    LogicalInterface(name=svc_params.get("left_li")))
            right_sc = SecurityZone(name=right_sec_zone)
            self.sc_zone_map[right_sec_zone] = right_sc
            if svc_params.get("right_li"):
                right_sc.add_interfaces(
                    LogicalInterface(name=svc_params.get("right_li")))
            sc_policy = SecurityPolicy(from_zone=left_sec_zone,
                                       to_zone=right_sec_zone)
            self.sc_policy_map[left_sec_zone] = sc_policy
            sc_policy = SecurityPolicy(from_zone=right_sec_zone,
                                       to_zone=left_sec_zone)
            self.sc_policy_map[right_sec_zone] = sc_policy

    # end build_pnf_svc_sc_zone_policy_config

    def build_pnf_svc_config(self):
        pr = self._physical_router
        pt_list = pr.port_tuples
        for pt in pt_list or []:
            pt_obj = PortTupleDM.get(pt)
            if pt_obj:
                si = pt_obj.svc_instance
                si_obj = ServiceInstanceDM.get(si)
                if si_obj:
                    svc_params = {}
                    svc_params["svc_inst_name"] = si_obj.name
                    svc_params["left_vlan"] = si_obj.left_svc_vlan
                    svc_params["right_vlan"] = si_obj.right_svc_vlan
                    svc_params["left_asns"] = si_obj.left_svc_asns
                    svc_params["right_asns"] = si_obj.right_svc_asns
                    svc_params["left_peer_unit"] = si_obj.left_svc_unit
                    svc_params["right_peer_unit"] = si_obj.right_svc_unit
                    sa_obj = pt_obj.get_sa_obj()
                    if sa_obj:
                        right_li_name = left_li_name = lo0_li_name = None
                        right_li_ip = left_li_ip = lo0_li_ip = None
                        for pi, intf_type in list(
                                (sa_obj.physical_interfaces).items()):
                            pi_obj = PhysicalInterfaceDM.get(pi)
                            attr = intf_type.get("interface_type")
                            if attr == "left":
                                left_li_name = (pi_obj.name + "." +
                                                str(svc_params["left_vlan"]))
                                left_li_fq_name = pi_obj.fq_name + [
                                    left_li_name.replace(":", "_")
                                ]
                                left_li_obj = (
                                    LogicalInterfaceDM.find_by_fq_name(
                                        left_li_fq_name))
                                if left_li_obj:
                                    instance_ip = InstanceIpDM.get(
                                        left_li_obj.instance_ip)
                                    if instance_ip:
                                        left_li_ip = (
                                            instance_ip.instance_ip_address)
                                        svc_params["left_li"] = left_li_name
                                        svc_params["left_li_ip"] = left_li_ip
                                lo0_li_name = ("lo0" + "." +
                                               str(svc_params["left_vlan"]))
                                lo0_fq_name = pr.fq_name + ["lo0", lo0_li_name]
                                lo0_li_obj = (LogicalInterfaceDM.
                                              find_by_fq_name(lo0_fq_name))

                                if lo0_li_obj:
                                    instance_ip = InstanceIpDM.get(
                                        lo0_li_obj.instance_ip)
                                    if instance_ip:
                                        lo0_li_ip = (
                                            instance_ip.instance_ip_address)
                                        svc_params["lo0_li"] = lo0_li_name
                                        svc_params["lo0_li_ip"] = lo0_li_ip
                                peer_left_intfs_ip = []
                                for pi_ref in pi_obj.physical_interfaces or []:
                                    pi_ref_obj = PhysicalInterfaceDM.get(
                                        pi_ref)
                                    svc_params[
                                        "left_peer_asn"] = self.get_peer_asn(
                                            pi_ref_obj)

                                    peer_li_name = (
                                        pi_ref_obj.name + "." +
                                        str(svc_params["left_vlan"]))

                                    peer_li_fq_name = pi_ref_obj.fq_name + [
                                        peer_li_name.replace(":", "_")
                                    ]

                                    peer_li_obj = (
                                        LogicalInterfaceDM.find_by_fq_name(
                                            peer_li_fq_name))

                                    if peer_li_obj:
                                        instance_ip = InstanceIpDM.get(
                                            peer_li_obj.instance_ip)
                                        if instance_ip:
                                            peer_li_ip = (instance_ip.
                                                          instance_ip_address)
                                            peer_left_intfs_ip.append(
                                                peer_li_ip)
                                svc_params[
                                    "peer_left_li_ips"] = peer_left_intfs_ip
                            elif attr == "right":

                                right_li_name = (pi_obj.name + "." +
                                                 str(svc_params["right_vlan"]))

                                right_li_fq_name = pi_obj.fq_name + [
                                    right_li_name.replace(":", "_")
                                ]

                                right_li_obj = (
                                    LogicalInterfaceDM.find_by_fq_name(
                                        right_li_fq_name))

                                if right_li_obj:
                                    instance_ip = InstanceIpDM.get(
                                        right_li_obj.instance_ip)
                                    if instance_ip:
                                        right_li_ip = (
                                            instance_ip.instance_ip_address)
                                        svc_params["right_li"] = right_li_name
                                        svc_params["right_li_ip"] = right_li_ip
                                peer_right_intfs_ip = []
                                for pi_ref in pi_obj.physical_interfaces or []:
                                    pi_ref_obj = PhysicalInterfaceDM.get(
                                        pi_ref)
                                    svc_params[
                                        "right_peer_asn"] = self.get_peer_asn(
                                            pi_ref_obj)

                                    peer_li_name = (
                                        pi_ref_obj.name + "." +
                                        str(svc_params["right_vlan"]))

                                    peer_li_fq_name = pi_ref_obj.fq_name + [
                                        peer_li_name.replace(":", "_")
                                    ]

                                    peer_li_obj = (
                                        LogicalInterfaceDM.find_by_fq_name(
                                            peer_li_fq_name))

                                    if peer_li_obj:
                                        instance_ip = InstanceIpDM.get(
                                            peer_li_obj.instance_ip)
                                        if instance_ip:
                                            peer_li_ip = (instance_ip.
                                                          instance_ip_address)
                                            peer_right_intfs_ip.append(
                                                peer_li_ip)
                                svc_params[
                                    "peer_right_li_ips"] = peer_right_intfs_ip
                        self._logger.debug("PR: %s svc params: %s" %
                                           (pr.name, svc_params))
                        # Make sure all required parameters are present,
                        # before creating the abstract config
                        if self.check_pnf_svc_required_params(svc_params):
                            self.build_pnf_svc_ri_config(svc_params)
                            self.build_pnf_svc_intfs_config(svc_params)
                            self.build_pnf_svc_sc_zone_policy_config(
                                svc_params)

    # end build_pnf_svc_config

    def build_svc_chaining_ri_config(self, si_name, left_vrf_info,
                                     right_vrf_info):
        # left vrf
        vn_obj = VirtualNetworkDM.get(left_vrf_info.get("vn_id"))
        if vn_obj:
            vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                             vn_obj.vn_network_id, "l3")

            network_id = vn_obj.vn_network_id
            vxlan_id = vn_obj.get_vxlan_vni()
            primary_ri = self._get_primary_ri(vn_obj)
            et, it = self._get_export_import_targets(vn_obj, primary_ri)
            is_internal_vn = (True if "_contrail_lr_internal_vn_"
                              in vn_obj.name else False)

            left_ri = RoutingInstance(
                name=vrf_name,
                virtual_network_mode="l3",
                export_targets=et,
                import_targets=it,
                virtual_network_id=str(network_id),
                vxlan_id=str(vxlan_id),
                is_public_network=vn_obj.router_external,
            )
            left_ri.set_routing_instance_type("vrf")
            left_ri.set_virtual_network_is_internal(is_internal_vn)
            if is_internal_vn:
                lr = self.get_lr_name_from_ri(vrf_name)
                if lr:
                    left_ri.set_description("__contrail_%s_%s" %
                                            (lr.name, lr.uuid))

            self.ri_map_leafspine[vrf_name] = left_ri

            self._add_ref_to_list(
                left_ri.get_routing_interfaces(),
                "irb." + left_vrf_info.get("left_svc_unit"),
            )

            if left_vrf_info.get("srx_left_interface") and left_vrf_info.get(
                    "loopback_ip"):
                protocols = RoutingInstanceProtocols()
                bgp_name = si_name + "_left"

                peer_bgp_name = (bgp_name + "_" +
                                 left_vrf_info.get("srx_left_interface"))

                peer_bgp = Bgp(
                    name=peer_bgp_name,
                    autonomous_system=left_vrf_info.get("peer"),
                    ip_address=left_vrf_info.get("srx_left_interface"),
                )

                bgp = Bgp(
                    name=bgp_name,
                    type_="external",
                    autonomous_system=left_vrf_info.get("local"),
                )
                bgp.add_peers(peer_bgp)
                bgp.set_comment("PNF-Service-Chaining")
                protocols.add_bgp(bgp)

                pimrp = PimRp(ip_address=left_vrf_info.get("loopback_ip"))
                pim = Pim(name=si_name + "_left")
                pim.set_rp(pimrp)
                pim.set_comment("PNF-Service-Chaining")
                protocols.add_pim(pim)
                self.ri_protocols_leafspine_map[vrf_name][bgp_name] = protocols
                left_ri.set_protocols(
                    self._get_values_sorted_by_key(
                        self.ri_protocols_leafspine_map[vrf_name]))

        # create new service chain ri for vni targets
        for vn in left_vrf_info.get("tenant_vn") or []:
            vn_obj = VirtualNetworkDM.get(vn)
            if vn_obj:
                vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                 vn_obj.vn_network_id, "l3")

                network_id = vn_obj.vn_network_id
                vxlan_id = vn_obj.get_vxlan_vni()
                primary_ri = self._get_primary_ri(vn_obj)
                et, it = self._get_export_import_targets(vn_obj, primary_ri)

                ri = RoutingInstance(
                    name=vrf_name,
                    virtual_network_mode="l3",
                    export_targets=et,
                    import_targets=it,
                    virtual_network_id=str(network_id),
                    vxlan_id=str(vxlan_id),
                    is_public_network=vn_obj.router_external,
                )
                vni_ri_left = RoutingInstance(name=si_name +
                                              "_service_chain_left")
                self.ri_map_leafspine[si_name +
                                      "_service_chain_left"] = vni_ri_left

                vni_ri_left.set_comment("PNF-Service-Chaining")
                vni_ri_left.set_routing_instance_type("virtual-switch")
                vni_ri_left.set_vxlan_id(left_vrf_info.get("left_svc_unit"))

                for target in ri.get_export_targets():
                    self._add_to_list(vni_ri_left.get_export_targets(), target)

        # right vrf
        vn_obj = VirtualNetworkDM.get(right_vrf_info.get("vn_id"))
        if vn_obj:
            vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                             vn_obj.vn_network_id, "l3")

            network_id = vn_obj.vn_network_id
            vxlan_id = vn_obj.get_vxlan_vni()
            primary_ri = self._get_primary_ri(vn_obj)
            et, it = self._get_export_import_targets(vn_obj, primary_ri)
            is_internal_vn = (True if "_contrail_lr_internal_vn_"
                              in vn_obj.name else False)
            right_ri = RoutingInstance(
                name=vrf_name,
                virtual_network_mode="l3",
                export_targets=et,
                import_targets=it,
                virtual_network_id=str(network_id),
                vxlan_id=str(vxlan_id),
                is_public_network=vn_obj.router_external,
            )
            right_ri.set_routing_instance_type("vrf")
            right_ri.set_virtual_network_is_internal(is_internal_vn)
            if is_internal_vn:
                lr = self.get_lr_name_from_ri(vrf_name)
                if lr:
                    right_ri.set_description("__contrail_%s_%s" %
                                             (lr.name, lr.uuid))

            self.ri_map_leafspine[vrf_name] = right_ri

            self._add_ref_to_list(
                right_ri.get_routing_interfaces(),
                "irb." + right_vrf_info.get("right_svc_unit"),
            )

            if right_vrf_info.get("srx_right_interface") and left_vrf_info.get(
                    "loopback_ip"):
                protocols = RoutingInstanceProtocols()
                bgp_name = si_name + "_right"

                peer_bgp_name = (bgp_name + "_" +
                                 right_vrf_info.get("srx_right_interface"))

                peer_bgp = Bgp(
                    name=peer_bgp_name,
                    autonomous_system=right_vrf_info.get("peer"),
                    ip_address=right_vrf_info.get("srx_right_interface"),
                )

                bgp = Bgp(
                    name=bgp_name,
                    type_="external",
                    autonomous_system=right_vrf_info.get("local"),
                )
                bgp.add_peers(peer_bgp)
                bgp.set_comment("PNF-Service-Chaining")
                protocols.add_bgp(bgp)

                pimrp = PimRp(ip_address=left_vrf_info.get("loopback_ip"))
                pim = Pim(name=si_name + "_right")
                pim.set_rp(pimrp)
                pim.set_comment("PNF-Service-Chaining")
                protocols.add_pim(pim)
                self.ri_protocols_leafspine_map[vrf_name][bgp_name] = protocols
                right_ri.set_protocols(
                    self._get_values_sorted_by_key(
                        self.ri_protocols_leafspine_map[vrf_name]))

        # create new service chain ri for vni targets
        for vn in right_vrf_info.get("tenant_vn") or []:
            vn_obj = VirtualNetworkDM.get(vn)
            if vn_obj:
                vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                 vn_obj.vn_network_id, "l3")

                network_id = vn_obj.vn_network_id
                vxlan_id = vn_obj.get_vxlan_vni()
                primary_ri = self._get_primary_ri(vn_obj)
                et, it = self._get_export_import_targets(vn_obj, primary_ri)
                ri = RoutingInstance(
                    name=vrf_name,
                    virtual_network_mode="l3",
                    export_targets=et,
                    import_targets=it,
                    virtual_network_id=str(network_id),
                    vxlan_id=str(vxlan_id),
                    is_public_network=vn_obj.router_external,
                )

                vni_ri_right = RoutingInstance(name=si_name +
                                               "_service_chain_right")
                self.ri_map_leafspine[si_name +
                                      "_service_chain_right"] = vni_ri_right

                vni_ri_right.set_comment("PNF-Service-Chaining")
                vni_ri_right.set_routing_instance_type("virtual-switch")
                vni_ri_right.set_vxlan_id(right_vrf_info.get("right_svc_unit"))

                for target in ri.get_export_targets():
                    self._add_to_list(vni_ri_right.get_export_targets(),
                                      target)

    # end build_service_chain_ri_config

    def build_svc_chaining_irb_bd_config(self, svc_app_obj, left_right_params):
        left_fq_name = left_right_params["left_qfx_fq_name"]
        right_fq_name = left_right_params["right_qfx_fq_name"]
        left_svc_vlan = left_right_params["left_svc_vlan"]
        right_svc_vlan = left_right_params["right_svc_vlan"]
        left_svc_unit = left_right_params["left_svc_unit"]
        right_svc_unit = left_right_params["right_svc_unit"]

        for pnf_pi, intf_type in list(svc_app_obj.physical_interfaces.items()):
            pnf_pi_obj = PhysicalInterfaceDM.get(pnf_pi)
            for spine_pi in pnf_pi_obj.physical_interfaces:
                if spine_pi not in self._physical_router.physical_interfaces:
                    continue
                spine_pi_obj = PhysicalInterfaceDM.get(spine_pi)
                if spine_pi_obj.fq_name == left_fq_name:
                    li_name = spine_pi_obj.name + "." + left_svc_vlan
                    li_fq_name = spine_pi_obj.fq_name + [
                        li_name.replace(":", "_")
                    ]
                    li_obj = LogicalInterfaceDM.find_by_fq_name(li_fq_name)
                    if li_obj:
                        # irb creation
                        iip_obj = InstanceIpDM.get(li_obj.instance_ip)
                        if iip_obj:
                            irb_addr = iip_obj.instance_ip_address
                            irb_unit = left_svc_unit
                            left_irb_intf, li_map = self._add_or_lookup_pi(
                                self.pi_map_leafspine, "irb", "irb")
                            intf_unit = self._add_or_lookup_li(
                                li_map, "irb." + str(irb_unit), irb_unit)
                            intf_unit.set_comment("PNF-Service-Chaining")
                            self._add_ip_address(intf_unit, irb_addr + "/29")
                            # build BD config
                            left_bd_vlan = Vlan(
                                name=DMUtils.make_bridge_name(left_svc_unit),
                                vxlan_id=left_svc_unit,
                            )
                            left_bd_vlan.set_vlan_id(left_svc_vlan)
                            left_bd_vlan.set_description(
                                "PNF-Service-Chaining")
                            left_bd_vlan.set_comment("PNF-Service-Chaining")
                            self._add_ref_to_list(
                                left_bd_vlan.get_interfaces(),
                                "irb." + str(irb_unit),
                            )
                            self.vlan_map[
                                left_bd_vlan.get_name()] = left_bd_vlan
                            # create logical interfaces for the aggregated
                            # interfaces
                            left_svc_intf, li_map = self._add_or_lookup_pi(
                                self.pi_map_leafspine,
                                spine_pi_obj.name,
                                "service",
                            )
                            left_svc_intf_unit = self._add_or_lookup_li(
                                li_map, left_fq_name[-1] + ".0", "0")
                            left_svc_intf_unit.set_comment(
                                "PNF-Service-Chaining")
                            left_svc_intf_unit.set_family("ethernet-switching")
                            vlan_left = Vlan(name="bd-" + left_svc_unit)
                            if (vlan_left.get_name()
                                    not in self.svc_intf_vlans["vlan_names"]):
                                left_svc_intf_unit.add_vlans(vlan_left)
                                self.svc_intf_vlans["vlan_names"].append(
                                    vlan_left.get_name())

                if spine_pi_obj.fq_name == right_fq_name:
                    li_name = spine_pi_obj.name + "." + right_svc_vlan
                    li_fq_name = spine_pi_obj.fq_name + [
                        li_name.replace(":", "_")
                    ]
                    li_obj = LogicalInterfaceDM.find_by_fq_name(li_fq_name)
                    if li_obj:
                        # irb creation
                        iip_obj = InstanceIpDM.get(li_obj.instance_ip)
                        if iip_obj:
                            irb_addr = iip_obj.instance_ip_address
                            irb_unit = right_svc_unit
                            right_irb_intf, li_map = self._add_or_lookup_pi(
                                self.pi_map_leafspine, "irb", "irb")
                            intf_unit = self._add_or_lookup_li(
                                li_map, "irb." + str(irb_unit), irb_unit)
                            intf_unit.set_comment("PNF-Service-Chaining")
                            self._add_ip_address(intf_unit, irb_addr + "/29")
                            # build BD config
                            right_bd_vlan = Vlan(
                                name=DMUtils.make_bridge_name(right_svc_unit),
                                vxlan_id=right_svc_unit,
                            )
                            right_bd_vlan.set_vlan_id(right_svc_vlan)
                            right_bd_vlan.set_description(
                                "PNF-Service-Chaining")
                            right_bd_vlan.set_comment("PNF-Service-Chaining")
                            self._add_ref_to_list(
                                right_bd_vlan.get_interfaces(),
                                "irb." + str(irb_unit),
                            )
                            self.vlan_map[
                                right_bd_vlan.get_name()] = right_bd_vlan
                            # create logical interfaces for the aggregated
                            # interfaces
                            right_svc_intf, li_map = self._add_or_lookup_pi(
                                self.pi_map_leafspine,
                                spine_pi_obj.name,
                                "service",
                            )
                            right_svc_intf_unit = self._add_or_lookup_li(
                                li_map, right_fq_name[-1] + ".0", "0")
                            right_svc_intf_unit.set_comment(
                                "PNF-Service-Chaining")
                            right_svc_intf_unit.set_family(
                                "ethernet-switching")
                            vlan_right = Vlan(name="bd-" + right_svc_unit)
                            if (vlan_right.get_name()
                                    not in self.svc_intf_vlans["vlan_names"]):
                                right_svc_intf_unit.add_vlans(vlan_right)
                                self.svc_intf_vlans["vlan_names"].append(
                                    vlan_right.get_name())

    # end build_service_chain_irb_bd_config

    def check_svc_chaining_required_params(self, left_vrf_info,
                                           right_vrf_info):
        if left_vrf_info.get("left_svc_unit") and right_vrf_info.get(
                "right_svc_unit"):
            return True

        return False

    # end build_service_chain_required_params

    def build_svc_chaining_config(self):
        pr = self._physical_router
        if not pr:
            return

        if self._is_service_chained():
            if not pr.port_tuples:
                return

            for pt in pr.port_tuples:
                left_vrf_info = {}
                right_vrf_info = {}
                left_interface = ""
                right_interface = ""
                left_right_params = {}

                pt_obj = PortTupleDM.get(pt)

                if not pt_obj:
                    continue

                if not pt_obj.logical_routers:
                    continue

                si_obj = ServiceInstanceDM.get(pt_obj.svc_instance)

                # get unit for irb interface
                left_svc_unit = si_obj.left_svc_unit
                right_svc_unit = si_obj.right_svc_unit

                left_svc_vlan = si_obj.left_svc_vlan
                right_svc_vlan = si_obj.right_svc_vlan

                left_vrf_info["peer"] = si_obj.left_svc_asns[0]
                left_vrf_info["local"] = si_obj.left_svc_asns[1]
                right_vrf_info["peer"] = si_obj.right_svc_asns[0]
                right_vrf_info["local"] = si_obj.right_svc_asns[1]

                # get left LR and right LR
                for lr_uuid in pt_obj.logical_routers:
                    lr_obj = LogicalRouterDM.get(lr_uuid)
                    if pr.uuid not in lr_obj.physical_routers:
                        continue
                    if pt_obj.left_lr == lr_uuid:
                        left_vrf_info["vn_id"] = lr_obj.virtual_network
                        left_vrf_info[
                            "tenant_vn"] = lr_obj.get_connected_networks(
                                include_internal=False, pr_uuid=pr.uuid)
                    if pt_obj.right_lr == lr_uuid:
                        right_vrf_info["vn_id"] = lr_obj.virtual_network
                        right_vrf_info[
                            "tenant_vn"] = lr_obj.get_connected_networks(
                                include_internal=False, pr_uuid=pr.uuid)

                st_obj = ServiceTemplateDM.get(si_obj.service_template)
                sas_obj = ServiceApplianceSetDM.get(
                    st_obj.service_appliance_set)
                svc_app_obj = ServiceApplianceDM.get(
                    list(sas_obj.service_appliances)[0])

                # get the left and right interfaces
                for kvp in svc_app_obj.kvpairs or []:
                    if kvp.get("key") == "left-attachment-point":
                        left_interface = kvp.get("value")
                    if kvp.get("key") == "right-attachment-point":
                        right_interface = kvp.get("value")

                left_fq_name = left_interface.split(":")
                right_fq_name = right_interface.split(":")

                left_right_params["left_qfx_fq_name"] = left_fq_name
                left_right_params["right_qfx_fq_name"] = right_fq_name
                left_right_params["left_svc_vlan"] = left_svc_vlan
                left_right_params["right_svc_vlan"] = right_svc_vlan
                left_right_params["left_svc_unit"] = left_svc_unit
                left_right_params["right_svc_unit"] = right_svc_unit

                left_vrf_info["left_svc_unit"] = left_svc_unit
                right_vrf_info["right_svc_unit"] = right_svc_unit

                # get left srx and right srx interface IPs
                for pnf_pi, intf_type in list(
                        svc_app_obj.physical_interfaces.items()):
                    if intf_type.get("interface_type") == "left":
                        pnf_pi_obj = PhysicalInterfaceDM.get(pnf_pi)
                        pnf_pr = pnf_pi_obj.physical_router
                        pnf_pr_obj = PhysicalRouterDM.get(pnf_pr)
                        for li in pnf_pi_obj.logical_interfaces:
                            li_obj = LogicalInterfaceDM.get(li)
                            if li_obj:
                                vlan = li_obj.fq_name[-1].split(".")[-1]
                                if left_svc_vlan == vlan:
                                    instance_ip = InstanceIpDM.get(
                                        li_obj.instance_ip)
                                    if instance_ip:
                                        left_vrf_info[(
                                            "srx_left_interface"
                                        )] = instance_ip.instance_ip_address
                    if intf_type.get("interface_type") == "right":
                        pnf_pi_obj = PhysicalInterfaceDM(pnf_pi)
                        for li in pnf_pi_obj.logical_interfaces:
                            li_obj = LogicalInterfaceDM.get(li)
                            if li_obj:
                                vlan = li_obj.fq_name[-1].split(".")[-1]
                                if right_svc_vlan == vlan:
                                    instance_ip = InstanceIpDM.get(
                                        li_obj.instance_ip)
                                    if instance_ip:
                                        right_vrf_info[(
                                            "srx_right_interface"
                                        )] = instance_ip.instance_ip_address

                if si_obj.rp_ip_addr:
                    # get rendezvous point IP addr from SI object, passed as
                    # user input
                    left_vrf_info["loopback_ip"] = si_obj.rp_ip_addr
                else:
                    # get rendezvous point IP address as srx loopback IP
                    li_name = "lo0." + left_svc_vlan
                    if pnf_pr_obj:
                        li_fq_name = pnf_pr_obj.fq_name + ['lo0', li_name]
                        li_obj = LogicalInterfaceDM.find_by_fq_name(li_fq_name)
                        if li_obj:
                            instance_ip = InstanceIpDM.get(li_obj.instance_ip)
                            if instance_ip:
                                left_vrf_info['loopback_ip'] = \
                                    instance_ip.instance_ip_address
                self._logger.debug(
                    "PR: %s left_vrf_info: %s right_vrf_info: %s " %
                    (pr.name, left_vrf_info, right_vrf_info))
                # Make sure all required parameters are present before
                # creating the abstract config
                if self.check_svc_chaining_required_params(
                        left_vrf_info, right_vrf_info):
                    self.build_svc_chaining_irb_bd_config(
                        svc_app_obj, left_right_params)
                    self.build_svc_chaining_ri_config(si_obj.name,
                                                      left_vrf_info,
                                                      right_vrf_info)

    def feature_config(self, **kwargs):
        pr = self._physical_router
        if not pr:
            return

        self.pi_map = OrderedDict()
        self.pi_map_leafspine = OrderedDict()
        self.ri_map = OrderedDict()
        self.ri_map_leafspine = OrderedDict()
        self.sc_zone_map = OrderedDict()
        self.sc_policy_map = OrderedDict()
        self.vlan_map = OrderedDict()
        self.svc_intf_vlans = OrderedDict({"vlan_names": []})
        self.ri_protocols_leafspine_map = defaultdict(dict)

        feature_config = Feature(name=self.feature_name())

        if pr.physical_router_role == "pnf":
            self.build_pnf_svc_config()

            for pi, li_map in list(self.pi_map.values()):
                pi.set_logical_interfaces(list(li_map.values()))
                feature_config.add_physical_interfaces(pi)

            feature_config.set_routing_instances(
                self._get_values_sorted_by_key(self.ri_map))

            feature_config.set_security_zones(
                self._get_values_sorted_by_key(self.sc_zone_map))

            feature_config.set_security_policies(
                self._get_values_sorted_by_key(self.sc_policy_map))

            return feature_config

        elif pr.physical_router_role in ["leaf", "spine"]:
            self.build_svc_chaining_config()

            for pi, li_map in list(self.pi_map_leafspine.values()):
                pi.set_logical_interfaces(list(li_map.values()))
                feature_config.add_physical_interfaces(pi)

            feature_config.set_vlans(
                self._get_values_sorted_by_key(self.vlan_map))

            feature_config.set_routing_instances(
                self._get_values_sorted_by_key(self.ri_map_leafspine))

            return feature_config

        else:
            return

    # end feature_config


# end PNFSrvcChainingFeature
