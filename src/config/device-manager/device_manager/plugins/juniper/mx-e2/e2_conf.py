#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of netconf interface for E2 services
on physical router
"""
from __future__ import division

from builtins import str
from past.utils import old_div
from ncclient import manager
from ncclient.operations.errors import TimeoutExpiredError
import datetime
import time
from lxml import etree

from netaddr import IPNetwork

from .db import BgpRouterDM, E2ServiceProviderDM, LogicalInterfaceDM, \
    PhysicalRouterDM, ServiceConnectionModuleDM, ServiceEndpointDM, \
    VirtualMachineInterfaceDM, VirtualNetworkDM
from .dm_utils import PushConfigState
from .juniper_conf import JuniperConf
from .e2_services_info import L2cktErrors, L2vpnErrors

class MxE2Conf(JuniperConf):
    _products = ['mx', 'vmx', 'vrr']
    def __init__(self, logger, params={}):
        self._l2ckt_errors = L2cktErrors()
        self._l2vpn_errors = L2vpnErrors()
        self._logger = logger
        self.physical_router = params.get("physical_router")
        super(MxE2Conf, self).__init__()
    # end __init__

    def initialize(self):
        self.e2_phy_intf_config = None
        self.e2_fab_intf_config = None
        self.e2_routing_config = None
        self.e2_services_prot_config = None
        self.e2_services_ri_config = None
        self.e2_chassis_config = None
        self.e2_router_config = None
        self.e2_telemetry_config = None

        # VRS config
        self.e2_vrs_system_config = None
        self.e2_vrs_routing_options_config = None
        self.e2_vrs_policy_options_config = None
        self.e2_vrs_provider_ri_config = None
    # end initialize

    @classmethod
    def register(cls):
        mconf = {
              "vendor": cls._vendor,
              "products": cls._products,
              "class": cls
            }
        return super(MxE2Conf, cls).register(mconf)
    # end register

    @classmethod
    def is_product_supported(cls, name, role):
        if not role:
            return False
        if not role.lower().startswith('e2-'):
            return False
        for product in cls._products:
            if name.lower().startswith(product.lower()):
                return True
        return False
    # end is_product_supported

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        if is_delete:
            self.cleanup_e2_services()
            return self.send_conf(is_delete=True)
        self.config_e2_services()
        self.build_e2_router_config()
        self.build_e2_telemetry_config()
        return self.send_conf()
    # end push_conf

    def get_service_status(self, service_params={}):
        self._service_type = service_params.get("service_type")
        self._circuit_id = service_params.get("circuit_id")
        self._neigbor_id = service_params.get("neigbor_id")

        service_status_obj = self.get_e2_service_status(
                                    self._service_type,
                                    self._circuit_id,
                                    self._neigbor_id)
        return service_status_obj
    #end get_service_status

    def cleanup_e2_services(self):
        self.e2_vrs_system_config = None
        self.e2_vrs_routing_options_config = None
        self.e2_vrs_policy_options_config = None
        self.e2_vrs_provider_ri_config = None
    # end cleanup_e2_services

    def config_e2_services(self):
        status = False
        node_role = None
        service_exists = False
        service_index = 0
        node_role = self.physical_router.physical_router_role

        # Configure VRS services
        if node_role == 'e2-vrr':
            return self.config_e2_vrs_services()

        self._logger.info("Total services on %s is =%d" %
                (self.physical_router.name, \
                len(self.physical_router.service_endpoints)))
        ps_config_dict={}
        ps_intf_list=[]
        ps_circuit_id_dict = {}
        for sindex, sep_uuid in \
            enumerate(self.physical_router.service_endpoints):
            service_exists = True
            prot_configured = False
            service_type = None
            peer_pr_entry = None
            peer_sep_entry = None
            service_fabric = False
            peer_router_id =  None
            peer_router_lpbk_id = None
            local_site_id = 0
            remote_site_id = 0
            peer_phy_circuit_id = 0
            sep = ServiceEndpointDM.get(sep_uuid)
            if sep is None:
                self._logger.info("SEP is NULL for node=%s" %
                                  (self.physical_router.name))
                continue
            local_site_id = sep.site_id
            pr_uuid = sep.physical_router
            pr_entry = PhysicalRouterDM.get(pr_uuid)
            if pr_entry is None:
                self._logger.info("PR is NULL for node=%s" %
                                  (self.physical_router.name))
                continue
            bgp_router_uuid = pr_entry.bgp_router
            bgp_entry = BgpRouterDM.get(bgp_router_uuid)
            if bgp_entry is None:
                self._logger.info("BGP router is NULL for node=%s" %
                                  (self.physical_router.name))
                continue
            as_number = bgp_entry.params['autonomous_system']
            router_id = bgp_entry.params['address']
            router_lpbk_id = pr_entry.loopback_ip
            phy_circuit_id = pr_entry.e2_service_index
            for scm_id in sep.service_connection_modules:
                phy_mtu = 0
                li_mtu  = 0
                no_control_word = False
                scm    = ServiceConnectionModuleDM.get(scm_id)
                if scm is None:
                    self._logger.info("SCM is NULL for node=%s, sep=%s" %
                                      (self.physical_router.name, sep.name))
                    continue
                if scm.id_perms['enable'] == False:
                    self._logger.info("Skipping sep=%s on node=%s, set_config is disabled" %
                                      (sep.name, self.physical_router.name))
                    continue
                service_type = scm.service_type
                if scm.annotations:
                    for skey, sval in scm.annotations['key_value_pair']:
                        if 'resources' in skey:
                            mtu_present = False
                            cw_present = False
                            for res_entry in sval:
                                if 'mtu' in list(res_entry.values()):
                                    mtu_present = True
                                if 'control-word' in list(res_entry.values()):
                                    cw_present = True
                                for res_key, res_value in list(res_entry.items()):
                                    if 'rvalue' in res_key and mtu_present == True:
                                        scm.mtu = res_value
                                        phy_mtu = scm.mtu
                                        mtu_present = False
                                    if 'rvalue' in res_key and cw_present == True:
                                        scm.no_control_word = True
                                        no_control_word = True
                                        cw_present = False
                peer_seps = scm.service_endpoints
                peer_sep_circuit_id = None
                if peer_seps is not None:
                    peer_seps = list(peer_seps)
                    for peer_sep_uuid  in peer_seps:
                        if peer_sep_uuid == sep_uuid:
                            continue;
                        peer_sep_entry = ServiceEndpointDM.get(peer_sep_uuid)
                        remote_site_id = peer_sep_entry.site_id
                        peer_pr_uuid = peer_sep_entry.physical_router
                        peer_pr_entry = PhysicalRouterDM.get(peer_pr_uuid)
                        if peer_pr_entry is not None:
                            peer_phy_circuit_id = peer_pr_entry.e2_service_index
                            peer_bgp_router_uuid = peer_pr_entry.bgp_router
                            peer_bgp_entry = BgpRouterDM.get(peer_bgp_router_uuid)
                            if peer_bgp_entry is None:
                                self._logger.info("BGP router is NULL on node=%s" %
                                     (self.physical_router.name))
                            else:
                                peer_router_id = peer_bgp_entry.params['address']
                            peer_router_lpbk_id = \
                            peer_pr_entry.loopback_ip

                if phy_circuit_id > peer_phy_circuit_id:
                    scm_circuit_id = phy_circuit_id
                else:
                    scm_circuit_id = peer_phy_circuit_id
                vmi_id = sep.virtual_machine_interface
                vmi    = VirtualMachineInterfaceDM.get(vmi_id)
                if vmi is None:
                    continue
                li_list = list(vmi.logical_interfaces)
                if not li_list:
                    continue
                li_id = li_list[0]
                if not li_id:
                    continue
                li    = LogicalInterfaceDM.get(li_id)
                if li is None:
                    continue
                self._logger.info("Service config,pr=%s, role=%s, sep=%s, service-type=%s" %
                                  (self.physical_router.name, node_role,
                                  sep.name, service_type))
                if service_type == 'fabric-interface' and \
                   service_fabric == False:
                    ps_config = None
                    self.add_e2_phy_logical_interface(li.name,
                                   li.vlan_tag, service_type, node_role,
                                   phy_mtu, li_mtu, ps_config)
                    self.add_e2_lo0_config(node_role, router_lpbk_id)
                    self.add_e2_routing_options(node_role, router_id)
                    service_fabric == True
                elif service_type == 'vpws-l2ckt':
                    ps_config = None
                    self.add_e2_phy_logical_interface(li.name,
                            li.vlan_tag, service_type, node_role, phy_mtu,
                            li_mtu, ps_config)
                    if scm.circuit_id == 0:
                        service_index = scm_circuit_id + 1
                        scm.circuit_id = service_index
                        pr_entry.e2_service_index = service_index
                        if peer_pr_entry is not None:
                            peer_pr_entry.e2_service_index = service_index
                    else :
                        service_index = scm.circuit_id
                    self.add_e2_services_l2ckt_config_xml(node_role,
                            li.name, li.vlan_tag, service_index,
                            peer_router_id, no_control_word)
                elif service_type == 'vpws-l2vpn':
                    ps_config = None
                    self.add_e2_phy_logical_interface(li.name,
                            li.vlan_tag, service_type, node_role, phy_mtu,
                            li_mtu, ps_config)
                    if scm.circuit_id == 0:
                        service_index = scm_circuit_id + 1
                        scm.circuit_id = service_index
                        pr_entry.e2_service_index = service_index
                        if peer_pr_entry is not None:
                            peer_pr_entry.e2_service_index = service_index
                    else:
                        service_index = scm.circuit_id
                    if local_site_id == 0 and remote_site_id == 0:
                        local_site_id = 1
                        remote_site_id = 2
                        sep.site_id = local_site_id
                        if peer_sep_entry is not None:
                            peer_sep_entry.site_id = remote_site_id
                    # local service endpoint is created, while remote is not yet
                    elif local_site_id != 0:
                        if peer_sep_entry is not None and remote_site_id == 0:
                            remote_site_id = local_site_id + 1
                            peer_sep_entry.site_id = remote_site_id
                    elif remote_site_id != 0:
                        if  local_site_id == 0:
                            local_site_id = remote_site_id + 1
                            sep_entry.site_id = local_site_id
                    self.add_e2_services_l2vpn_config_xml(self,
                            node_role, li.name, li.vlan_tag, as_number,
                            service_index, local_site_id, remote_site_id)
                elif service_type == 'vpws-evpn':
                    self.add_e2_chassis_config()
                    vn_obj = VirtualNetworkDM.get(vmi.virtual_network)
                    ifname = li.name.split('.')
                    if 'ps' in ifname[0] and ifname[0] in ps_config_dict:
                        ps_config = ps_config_dict[ifname[0]]
                    elif 'ps' in ifname[0] and not ifname[0] in ps_intf_list:
                        ps_config = None
                        li_name = ifname[0] + '.0'
                        ps_config = self.add_e2_phy_logical_interface(
                                li_name, 0, service_type, node_role, phy_mtu,
                                li_mtu, ps_config)
                    else:
                        ps_config = None
                    ps_config = self.add_e2_phy_logical_interface(
                            li.name, li.vlan_tag, service_type, node_role,
                            phy_mtu, li_mtu, ps_config)
                    if 'ps' in ifname[0] and not ifname[0] in ps_config_dict:
                        ps_config_dict[ifname[0]] = ps_config
                    if vn_obj.circuit_id != 0:
                        scm.circuit_id = vn_obj.circuit_id
                    if scm.circuit_id == 0:
                        service_index = scm_circuit_id + 1
                        scm.circuit_id = service_index
                        pr_entry.e2_service_index = service_index
                        if peer_pr_entry is not None:
                            peer_pr_entry.e2_service_index = service_index
                    else :
                        service_index = scm.circuit_id
                    if vn_obj.circuit_id == 0:
                        vn_obj.circuit_id = service_index
                    if not ifname[0] in ps_intf_list:
                        li_name = ifname[0] + '.0'
                        self.add_e2_services_pwht_config_mx_xml(node_role,
                          li_name, li.vlan_tag, service_index, peer_router_id,
                          no_control_word)
                        if not ps_intf_list:
                            ps_intf_list = [ifname[0]]
                        else:
                            ps_intf_list.append(ifname[0])
    # end config_e2_services

    def _get_interface_unit_config_xml(self, ifl_unit, li_vlan_tag):
        unit_config = etree.Element("unit")
        etree.SubElement(unit_config, "name").text = ifl_unit
        if li_vlan_tag == 0:
            etree.SubElement(unit_config, "encapsulation").text = "ethernet-ccc"
        else:
            etree.SubElement(unit_config, "vlan-id").text = str(li_vlan_tag)
            unit_family = etree.SubElement(unit_config, "family")
            etree.SubElement(unit_family, "inet")
        return unit_config
    # end _get_interface_unit_config_xml

    def _get_interface_config_xml(self, ifd_name, ifl_unit, li_vlan_tag,
                                  service_type, phy_mtu, li_mtu):
        interface_config = etree.Element("interface")
        etree.SubElement(interface_config, "name").text = ifd_name
        if li_vlan_tag == 0:
            etree.SubElement(interface_config, "encapsulation").text = \
                                               "ethernet-ccc"
            if phy_mtu != 0:
                etree.SubElement(interface_config, "mtu").text = str(phy_mtu)
            unit = etree.SubElement(interface_config, "unit")
            etree.SubElement(unit, "name").text = ifl_unit
            family = etree.SubElement(unit, "family")
            etree.SubElement(family, "ccc")
        else:
            etree.SubElement(interface_config, "flexible-vlan-tagging")
            if phy_mtu != 0:
                etree.SubElement(interface_config, "mtu").text = str(phy_mtu)
            etree.SubElement(interface_config, "encapsulation").text = \
                                               "flexible-ethernet-services"
            unit = etree.SubElement(interface_config, "unit")
            etree.SubElement(unit, "name").text = ifl_unit
            etree.SubElement(unit, "encapsulation").text = "vlan-ccc"
            etree.SubElement(unit, "vlan-id").text = str(li_vlan_tag)
        return interface_config
    # end _get_interface_config_xml

    def _get_interface_fabric0_config_xml(self, ifl_unit, node_role):
        fabric_config = etree.Element("interface")
        etree.SubElement(fabric_config, "name").text = "ae0"
        unit = etree.SubElement(fabric_config, "unit")
        etree.SubElement(unit, "name").text = ifl_unit
        family = etree.SubElement(unit, "family")
        inet_family = etree.SubElement(family, "inet")
        address = etree.SubElement(inet_family, "address")
        if node_role == 'e2-access':
            etree.SubElement(address, "name").text = "2.2.2.1/24"
        else:
            etree.SubElement(address, "name").text = "2.2.2.2/24"
        family = etree.SubElement(unit, "family")
        etree.SubElement(family, "mpls")
        return fabric_config
    # end _get_interface_fabric0_config_xml

    def _get_interface_fabric_child_config_xml(self, phy_name):
        fabric_child = etree.Element("interface")
        etree.SubElement(fabric_child, "name").text = phy_name
        if_config = etree.SubElement(fabric_child, "gigether-options")
        ad = etree.SubElement(if_config, "ieee-802.3ad")
        etree.SubElement(ad, "bundle").text = "ae0"
        return fabric_child
    # end _get_interface_fabric_child_config_xml

    def _get_interface_lo0_config_xml(self, node_role, router_lpbk_id):
        lo0_config = etree.Element("interface")
        etree.SubElement(lo0_config, "name").text = "lo0"
        unit = etree.SubElement(lo0_config, "unit")
        etree.SubElement(unit, "name").text = "0"
        family = etree.SubElement(unit, "family")
        inet_family = etree.SubElement(family, "inet")
        address = etree.SubElement(inet_family, "address")
        if node_role == 'e2-access':
            if router_lpbk_id is None:
                etree.SubElement(address, "name").text = "100.100.100.100/32"
            else:
                etree.SubElement(address, "name").text =  router_lpbk_id + "/32"
        else:
            if router_lpbk_id is None:
                etree.SubElement(address, "name").text = "200.200.200.200/32"
            else:
                etree.SubElement(address, "name").text =  router_lpbk_id + "/32"
        etree.SubElement(address, "primary")
        return lo0_config
    # end _get_interface_lo0_config_xml

    def _get_routing_options_config_xml(self, node_role, router_id):
        routing_options = etree.Element("routing-options")
        if node_role == 'e2-access':
            if router_id is None:
                lo0_ip = "100.100.100.100/32"
                lo0_ipaddr = lo0_ip.split('/', 1)
                router_id = lo0_ipaddr[0]
        else:
            if router_id is None:
                lo0_ip = "200.200.200.200/32"
                lo0_ipaddr = lo0_ip.split('/', 1)
                router_id = lo0_ipaddr[0]
        etree.SubElement(routing_options, "router-id").text = router_id
        return routing_options
    # end _get_routing_options_config_xml

    def _get_chassis_config_xml(self):
        chassis_config = etree.Element("chassis")
        ps = etree.SubElement(chassis_config, "pseudowire-service")
        etree.SubElement(ps, "device-count").text = "2"
        fp = etree.SubElement(chassis_config, "fpc")
        etree.SubElement(fp, "name").text = "0"
        pic = etree.SubElement(fp, "pic")
        etree.SubElement(pic, "name").text = "0"
        ts = etree.SubElement(pic, "tunnel-services")
        etree.SubElement(ts, "bandwidth").text = "10g"
        return chassis_config
    # end _get_chassis_config_xml

    def add_l2ckt_protocol_config_xml(self, circuit_id, neighbor,
                                      li_name, li_vlan_tag,
                                      no_control_word):
        l2ckt_cfg = etree.Element("l2circuit")
        l2ckt = etree.SubElement(l2ckt_cfg, "neighbor")
        etree.SubElement(l2ckt, "name").text = neighbor
        l2ckt_intf = etree.SubElement(l2ckt, "interface")
        etree.SubElement(l2ckt_intf, "name").text = li_name
        etree.SubElement(l2ckt_intf, "virtual-circuit-id").text = \
                                          str(circuit_id)
        if no_control_word == True:
            etree.SubElement(l2ckt_intf, "no-control-word")
        if self.e2_services_prot_config is not None:
            self.e2_services_prot_config.append(l2ckt_cfg)
        else:
            routing_cfg = etree.Element("protocols")
            routing_cfg.append(l2ckt_cfg)
            self.e2_services_prot_config = routing_cfg
    # end add_l2ckt_protocol_config_xml

    def add_l2vpn_protocol_config_xml(self, li_name, rd, vrf_target,
                                      li_vlan_tag, site_name, site_id,
                                      remote_site_id):
        l2vpn = etree.Element("instance")
        etree.SubElement(l2vpn, "name").text = site_name
        etree.SubElement(l2vpn, "instance-type").text = "l2vpn"
        l2vpn_intf = etree.SubElement(l2vpn, "interface")
        etree.SubElement(l2vpn_intf, "name").text = li_name
        l2vpn_rd = etree.SubElement(l2vpn, "route-distinguisher")
        etree.SubElement(l2vpn_rd, "rd-type").text = rd
        l2vpn_vt = etree.SubElement(l2vpn, "vrf-target")
        etree.SubElement(l2vpn_vt, "community").text = 'target:' + vrf_target
        l2vpn_pr = etree.SubElement(l2vpn, "protocols")
        l2vpn_info = etree.SubElement(l2vpn_pr, "l2vpn")
        if li_vlan_tag != 0:
            etree.SubElement(l2vpn_info, "encapsulation-type").text = \
                    "ethernet-vlan"
        else:
            etree.SubElement(l2vpn_info, "encapsulation-type").text = \
                    "ethernet"
        site_info = etree.SubElement(l2vpn_info, "site")
        etree.SubElement(site_info, "name").text = site_name
        etree.SubElement(site_info, "site-identifier").text = str(site_id)
        site_intf = etree.SubElement(site_info, "interface")
        etree.SubElement(site_intf, "name").text = li_name
        etree.SubElement(site_intf, "remote-site-id").text = str(remote_site_id)

        if self.e2_services_ri_config is not None:
            self.e2_services_ri_config.append(l2vpn)
        else:
            routing_inst = etree.Element("routing-instances")
            routing_inst.append(l2vpn)
            self.e2_services_ri_config = routing_inst
    # end add_l2vpn_protocol_config_xml

    def _get_interface_ps_config_xml(self, ifname):
        ps_config = etree.Element("interface")
        etree.SubElement(ps_config, "name").text = ifname
        anp = etree.SubElement(ps_config, "anchor-point")
        etree.SubElement(anp, "interface-name").text = "lt-0/0/0"
        etree.SubElement(ps_config, "flexible-vlan-tagging")
        return ps_config
    # end _get_interface_ps_config_xml

    def add_pwht_config_xml(self, circuit_id, neighbor, li_name):
        #Transport and l2ckt
        if 'ps' in li_name and '.0' in li_name:
            l2ckt_cfg = etree.Element("l2circuit")
            l2ckt = etree.SubElement(l2ckt_cfg, "neighbor")
            etree.SubElement(l2ckt, "name").text = neighbor
            l2ckt_intf = etree.SubElement(l2ckt, "interface")
            etree.SubElement(l2ckt_intf, "name").text = li_name
            etree.SubElement(l2ckt_intf, "virtual-circuit-id").text = \
                                          str(circuit_id)
            if self.e2_services_prot_config is not None:
                self.e2_services_prot_config.append(l2ckt_cfg)
            else:
                routing_cfg = etree.Element("protocols")
                routing_cfg.append(l2ckt_cfg)
                self.e2_services_prot_config = routing_cfg
            return
        #Services
    # end add_pwht_config_xml

    def add_ldp_protocol_config_xml(self, interfaces):
        ldp_cfg = etree.Element("ldp")
        etree.SubElement(ldp_cfg, "track-igp-metric")
        etree.SubElement(ldp_cfg, "deaggregate")
        for interface in interfaces:
            ldp_intf = etree.SubElement(ldp_cfg, "interface")
            etree.SubElement(ldp_intf, "name").text = interface
        if self.e2_services_prot_config is not None:
            self.e2_services_prot_config.append(ldp_cfg)
        else:
            routing_cfg = etree.Element("protocols")
            routing_cfg.append(ldp_cfg)
            self.e2_services_prot_config = routing_cfg
    # end add_ldp_protocol_config_xml

    def add_mpls_protocol_config_xml(self, interfaces, lsp_name, neighbor):
        mpls_cfg = etree.Element("mpls")
        if lsp_name is not None:
            mpls_lsp = etree.SubElement(mpls_cfg, "label-switched-path")
            etree.SubElement(mpls_lsp, "name").text = lsp_name
            etree.SubElement(mpls_lsp, "to").text = neighbor
        for interface in interfaces:
            mpls_intf = etree.SubElement(mpls_cfg, "interface")
            etree.SubElement(mpls_intf, "name").text = interface
        if self.e2_services_prot_config is not None:
            self.e2_services_prot_config.append(mpls_cfg)
        else:
            routing_cfg = etree.Element("protocols")
            routing_cfg.append(mpls_cfg)
            self.e2_services_prot_config = routing_cfg
    # end add_mpls_protocol_config_xml

    def add_ospf_protocol_config_xml(self, interfaces, area):
        ospf_cfg = etree.Element("ospf")
        etree.SubElement(ospf_cfg, "traffic-engineering")
        ospf_area = etree.SubElement(ospf_cfg, "area")
        etree.SubElement(ospf_area, "name").text = "0.0.0.0"
        for interface in interfaces:
            ospf_intf = etree.SubElement(ospf_area, "interface")
            etree.SubElement(ospf_intf, "name").text = interface
            if interface.startswith("lo0"):
               etree.SubElement(ospf_intf, "passive")
        if self.e2_services_prot_config is not None:
            self.e2_services_prot_config.append(ospf_cfg)
        else:
            routing_cfg = etree.Element("protocols")
            routing_cfg.append(ospf_cfg)
            self.e2_services_prot_config = routing_cfg
    # end add_ospf_protocol_config_xml

    def add_e2_routing_options(self, node_role, router_id):
        if self.e2_routing_config is None:
            self.e2_routing_config = self._get_routing_options_config_xml(
                node_role, router_id)
    # end add_e2_routing_options

    def add_e2_chassis_config(self):
        if self.e2_chassis_config is None:
            self.e2_chassis_config = self._get_chassis_config_xml()
    # end add_e2_chassis_config

    def add_e2_phy_logical_interface(self, li_name, li_vlan_tag,
                                     service_type, node_role, phy_mtu,
                                     li_mtu, ps_config):
        if service_type == 'vpws-evpn' and node_role != 'e2-access':
            ps_config = self.add_e2_phy_logical_interface_pwht_mx(li_name,
                    li_vlan_tag, service_type, node_role, phy_mtu, li_mtu,
                    ps_config)
        else:
            self.add_e2_phy_logical_interface_mx(li_name, li_vlan_tag,
                    service_type, node_role, phy_mtu, li_mtu)
        return ps_config
    # end add_e2_phy_logical_interface

    def add_e2_phy_logical_interface_pwht_mx(self, li_name, li_vlan_tag,
            service_type, node_role, phy_mtu, li_mtu, ps_config):
        ifname = li_name.split('.')
        ps_unit_config = self._get_interface_unit_config_xml(ifname[1],
                                                             li_vlan_tag)
        if ps_config is not None:
            ps_config.append(ps_unit_config)
        else:
            ps_config = self._get_interface_ps_config_xml(ifname[0])
            ps_config.append(ps_unit_config)
            if self.e2_phy_intf_config is not None:
                self.e2_phy_intf_config.insert(0, ps_config)
            else:
                intf_cfg = etree.Element("interfaces")
                intf_cfg.append(ps_config)
                self.e2_phy_intf_config = intf_cfg
        return ps_config

    # end add_e2_phy_logical_interface_pwht_mx

    def add_e2_phy_logical_interface_mx(self, li_name, li_vlan_tag,
                                        service_type, node_role, phy_mtu,
                                        li_mtu):
        ifparts = li_name.split('.')
        ifd_name = ifparts[0]
        ifl_unit = ifparts[1]
        li1_config = li2_config = li_config = None
        if service_type == 'fabric':
            li1_config = self._get_interface_fabric_child_config_xml(ifd_name)
            li2_config = self._get_interface_fabric0_config_xml(ifl_unit,
                                                                node_role)
        else:
            li_config = self._get_interface_config_xml(ifd_name, ifl_unit,
                    li_vlan_tag, service_type, phy_mtu, li_mtu)
        if self.e2_phy_intf_config is not None:
            if li_config is not None:
                self.e2_phy_intf_config.insert(0, li_config)
            else:
                self.e2_phy_intf_config.insert(0, li1_config)
                self.e2_phy_intf_config.insert(0, li2_config)
        else:
            if li_config is not None:
                intf_cfg = etree.Element("interfaces")
                intf_cfg.append(li_config)
                self.e2_phy_intf_config = intf_cfg
            else:
                intf_cfg = etree.Element("interfaces")
                intf_cfg.append(li1_config)
                intf_cfg.append(li2_config)
                self.e2_phy_intf_config = intf_cfg
    # end add_e2_phy_logical_interface_mx

    def add_e2_lo0_config(self, node_role, router_lpbk_id):
        li_config = self._get_interface_lo0_config_xml(node_role,
                                                       router_lpbk_id)
        if self.e2_phy_intf_config is not None:
            self.e2_phy_intf_config.insert(0, li_config)
        else:
            self.e2_phy_intf_config.append(li_config)
    # end add_e2_lo0_config

    def add_e2_fabric_adjacency(self, prot_conf):
        interfaces = ['ae0.0','lo0.0']
        area = 0
        if prot_conf != True:
            self.add_ospf_protocol_config_xml(interfaces, area)
            self.add_ldp_protocol_config_xml(interfaces)
    # end add_e2_fabric_adjacency

    def add_e2_services_l2ckt_config_xml(self, node_role, li_name,
            li_vlan_tag, sindex, peer_router_id, no_control_word):
        self.add_e2_services_l2ckt_config_mx_xml(node_role, li_name,
            li_vlan_tag, sindex, peer_router_id, no_control_word)
    # end add_e2_services_l2ckt_config_xml

    def add_e2_services_l2ckt_config_mx_xml(self, node_role, li_name,
                                            li_vlan_tag, sindex,
                                            peer_router_id,
                                            no_control_word):
        circuit_id = sindex
        neighbor = peer_router_id
        if node_role == 'e2-access':
            if peer_router_id is None:
                neighbor = "200.200.200.200"
            self.add_l2ckt_protocol_config_xml(circuit_id, neighbor,
                    li_name, li_vlan_tag, no_control_word)
        elif node_role == 'e2-provider':
            if peer_router_id is None:
                neighbor = "100.100.100.100"
            self.add_l2ckt_protocol_config_xml(circuit_id, neighbor,
                    li_name, li_vlan_tag, no_control_word)
    # end add_e2_services_l2ckt_config_mx_xml

    def add_e2_services_pwht_config_mx_xml(self, node_role, li_name,
                                           li_vlan_tag, sindex,
                                           peer_router_id, no_control_word):
        circuit_id = sindex
        neighbor = peer_router_id
        if node_role == 'e2-access':
            if peer_router_id is None:
                neighbor = "200.200.200.200"
            self.add_l2ckt_protocol_config_xml(circuit_id, neighbor,
                    li_name, li_vlan_tag, no_control_word)
        else:
            if peer_router_id is None:
                neighbor = "100.100.100.100"
            self.add_pwht_config_xml(circuit_id, neighbor, li_name)
    # end add_e2_services_pwht_config_mx_xml

    def add_e2_services_l2ckt_config_mx_xml(self, node_role, li_name,
                                            li_vlan_tag, sindex,
                                            peer_router_id, no_control_word):
        circuit_id = sindex
        neighbor = peer_router_id
        if node_role == 'e2-access':
            if peer_router_id is None:
                neighbor = "200.200.200.200"
            self.add_l2ckt_protocol_config_xml(circuit_id, neighbor,
                    li_name, li_vlan_tag, no_control_word)
        elif node_role == 'e2-provider':
            if peer_router_id is None:
                neighbor = "100.100.100.100"
            self.add_l2ckt_protocol_config_xml(circuit_id, neighbor,
                    li_name, li_vlan_tag, no_control_word)
    # end add_e2_services_l2ckt_config_mx_xml

    def add_e2_services_l2vpn_config_xml(self, node_role, li_name,
                                         li_vlan_tag, as_number, circuit_id,
                                         local_site_id, remote_site_id):
        #rd is lo0.0 ipaddress:circuit_id or AS number:circuit_id
        rd = str(as_number) + ":" + str(circuit_id)
        # vrf_target is AS number:circuit_id
        vrf_target = str(as_number) + ":" + str(circuit_id)
        # site name is unique per endpoint
        site_name = "l2vpn" + str(circuit_id)
        self.add_l2vpn_protocol_config_xml(li_name, rd, vrf_target,
                                           li_vlan_tag, site_name,
                                           local_site_id, remote_site_id)
    # end add_e2_services_l2vpn_config_xml

    def get_e2_service_status(self, service_type, circuit_id, neigbor_id):
        if service_type == 'vpws-l2ckt' or service_type == 'vpws-evpn':
            #Fetch the l2circuit summary
            service_status_info = self.get_l2ckt_status(circuit_id, neigbor_id)
        elif service_type == 'vpws-l2vpn':
            #Fetch the l2vpn summary
            site_name = "l2vpn" + str(circuit_id)
            service_status_info = self.get_l2vpn_status(1, site_name)
        elif service_type == 'fabric-interface':
            #Fetch the fabric status
            service_status_info = {}
            service_status_info['service-status'] = 'Up'
        else:
            self._logger.error("could not fetch service status for type %s" % (
                                  service_type))
            service_status_info = {}

        return service_status_info
    # end get_e2_service_status

    def get_l2ckt_status(self, circuit_id, neigbor_id):
        service_status_info = {}
        service_status_info['service-status-reason'] = \
                "Operational failure, no connections found"
        try:
            rpc_command = """
            <get-l2ckt-connection-information>
                <neighbor>dummy</neighbor>
                <summary/>
            </get-l2ckt-connection-information>"""
            rpc_command = rpc_command.replace('dummy', neigbor_id)
            l2ckt_sum = self.service_request_rpc(rpc_command)
            up_count = l2ckt_sum.xpath('//l2circuit-connection-information/l2circuit-neighbor/connections-summary/vc-up-count')[0].text
            down_count = l2ckt_sum.xpath('//l2circuit-connection-information/l2circuit-neighbor/connections-summary/vc-down-count')[0].text
            total_count = int(up_count) + int(down_count)

            rpc_command = """
            <get-l2ckt-connection-information>
                <neighbor>dummy</neighbor>
            </get-l2ckt-connection-information>"""
            rpc_command = rpc_command.replace('dummy', neigbor_id)
            res = self.service_request_rpc(rpc_command)

            neigh_id = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/neighbor-address')[0].text
            if total_count == 1 and int(up_count) > 0 :
                rem_neigh_id = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/connection/remote-pe')[0].text
            else:
                rem_neigh_id = "None"
            count = 0
            up_count = 0
            while count < total_count:
                service_circuit_id_block = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/connection/connection-id')[count].text
                circuit_id_str = service_circuit_id_block[service_circuit_id_block.find("(")+1:service_circuit_id_block.find(")")]
                service_circuit_id = re.search(r'\d+', circuit_id_str).group()
                service_status = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/connection/connection-status')[count].text
                if int(service_circuit_id) == circuit_id:
                    service_status_info = {}
                    service_status_reason = None
                    if service_status != 'Up':
                        service_status_reason = \
                        self._l2ckt_errors.geterrorstr(service_status)
                        service_intf_name = \
                        service_circuit_id_block.split('(')[0]
                    else:
                        service_intf_name = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/connection/local-interface/interface-name')[up_count].text
                        service_intf_status = res.xpath('//l2circuit-connection-information/l2circuit-neighbor/connection/local-interface/interface-status')[up_count].text
                        service_status_info['service-intf-status']   = service_intf_status
                    service_status_info['service-status']      = service_status
                    service_status_info['neighbor-id']         = neigh_id
                    service_status_info['remote-neighbor-id']  = neigh_id
                    service_status_info['service-intf-name']   = \
                            service_intf_name
                    if service_status_reason is not None:
                        service_status_info['service-status-reason'] = \
                                service_status_reason
                    return service_status_info
                if service_status == 'Up':
                    up_count += 1
                count += 1
            return service_status_info
        except:
            self._logger.error("error: could not fetch service status for %s: circuit-id %s" % (
                self.physical_router.name, str(circuit_id)))
            return service_status_info

    def get_l2vpn_status(self, site_id, site_name):
        service_status_info = {}
        service_status_info['service-status-reason'] = "Operational failure, no connections found"
        try:
            rpc_command = """
            <get-l2vpn-connection-information>
                <instance>dummy</instance>
                <summary/>
            </get-l2vpn-connection-information>"""
            rpc_command = rpc_command.replace('dummy', site_name)
            l2vpn_sum = self.service_request_rpc(rpc_command)
            up_count = l2vpn_sum.xpath('//l2vpn-connection-information/instance/reference-site/connections-summary/vc-up-count')[0].text
            down_count = l2vpn_sum.xpath('//l2vpn-connection-information/instance/reference-site/connections-summary/vc-down-count')[0].text
            total_count = int(up_count) + int(down_count)
            #Now fetch the l2vpn information.
            service_data = new_ele('get-l2vpn-connection-information')
            rpc_command = """
            <get-l2vpn-connection-information>
                <instance>dummy</instance>
            </get-l2vpn-connection-information>"""
            rpc_command = rpc_command.replace('dummy', site_name)
            res = self.service_request_rpc(rpc_command)
            if total_count == 1 and int(up_count) > 0 :
                rem_neigh_id = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/remote-pe')[0].text
            else:
                rem_neigh_id = "None"
            count = 0
            while count < total_count:
                service_site_id_blk = res.xpath('//l2vpn-connection-information/instance/reference-site/local-site-id')[count].text
                site_id_str = service_site_id_blk.split()[0]
                if site_id_str == site_name:
                    service_status_info = {}
                    #service_site_id = int(service_site_id) - 1
                    service_status = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/connection-status')[count].text
                    service_status_reason = None
                    if service_status != 'Up':
                        service_status_reason = self._l2vpn_errors.geterrorstr(service_status)
                        #service_intf_name = service_circuit_id_block.split('(')[0]
                        service_intf_name = "None"
                    else:
                        last_changed = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/last-change')[count].text
                        up_transitions = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/up-transitions')[count].text
                        service_intf_name = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/local-interface/interface-name')[count].text
                        service_intf_status = res.xpath('//l2vpn-connection-information/instance/reference-site/connection/local-interface/interface-status')[count].text
                        service_status_info['service-intf-status']   = service_intf_status
                        service_status_info['last-change']           = last_changed
                        service_status_info['up-transitions']        = up_transitions
                        service_status_info['service-intf-name']     = service_intf_name
                    service_status_info['service-status']            = service_status
                    service_status_info['neighbor-id']               = rem_neigh_id
                    service_status_info['site-name']                 = site_name
                    service_status_info['site-id']                   = site_id
                    if service_status_reason is not None:
                        service_status_info['service-status-reason'] = service_status_reason
                        return service_status_info
                count += 1
            return service_status_info
        except:
            self._logger.error("error: could not fetch service status for %s : site-name %s" % (
                self.physical_router.name, site_name))
            return service_status_info
    def build_e2_router_config(self):
        self.config_e2_router_config()
    # end build_e2_router_config

    def build_e2_telemetry_config(self):
        self.config_e2_telemetry_config()
    # end build_e2_telemetry_config

    def service_request_rpc(self, rpc_command):
        res = self._nc_manager.rpc(rpc_command)
        return res
    # end service_request_rpc

    def has_conf(self):
        #if not self.proto_config or not self.proto_config.get_bgp():
            #return False
        return True
    # end has_conf

    def send_conf(self, is_delete=False):
        if not self.has_conf() and not is_delete:
            return 0
        default_operation = "none" if is_delete else "merge"
        operation = "delete" if is_delete else "replace"
        conf = self.build_e2_conf(default_operation, operation)
        return conf
    # end send_conf

    def config_e2_router_config(self):
        # Skip for VRR
        if self.physical_router.physical_router_role == 'e2-vrr':
            return
        snmp = self.physical_router.physical_router_snmp
        lldp = self.physical_router.physical_router_lldp
        self.add_e2_router_config_xml(snmp, lldp)
    # end config_e2_router_config

    def config_e2_telemetry_config(self):
        server_ip = None
        server_port = 0
        if not self.physical_router.telemetry_info:
            return
        telemetry_info = self.physical_router.telemetry_info
        for tkey, tdk in list(telemetry_info.items()):
            if 'server_ip' in tkey:
                server_ip = tdk
            if 'server_port' in tkey:
                server_port = tdk
            if 'resource' in tkey:
                for res_entry in tdk:
                    rname = rpath = rrate = None
                    for res_key, res_value in list(res_entry.items()):
                        if 'name' in res_key:
                            rname = res_value
                        if 'path' in res_key:
                            rpath = res_value
                        if 'rate' in res_key:
                            rrate = res_value
                    self.add_e2_telemetry_per_resource_config_xml(
                                rname, rpath, rrate)
        self.add_e2_telemetry_config_xml(server_ip, server_port)
    # end config_e2_telemetry_config

    def build_e2_conf(self, default_operation="merge",
                    operation="replace"):

        config_list = []
        if self.e2_router_config is not None:
            config_list.append(self.e2_router_config)
        if self.e2_chassis_config is not None:
            config_list.append(self.e2_chassis_config)
        if self.e2_phy_intf_config is not None:
            config_list.append(self.e2_phy_intf_config)
        if self.e2_fab_intf_config is not None:
            config_list.append(self.e2_fab_intf_config)
        if self.e2_routing_config is not None:
            config_list.append(self.e2_routing_config)
        if self.e2_services_prot_config is not None:
            config_list.append(self.e2_services_prot_config)
        if self.e2_services_ri_config is not None:
            config_list.append(self.e2_services_ri_config)
        if self.e2_telemetry_config is not None:
            telemetry_cfg = etree.Element("services")
            telemetry_cfg.append(self.e2_telemetry_config)
            config_list.append(telemetry_cfg)

        # VRS config
        if self.e2_vrs_system_config is not None:
            config_list.append(self.e2_vrs_system_config)
        if self.e2_vrs_routing_options_config is not None:
            config_list.append(self.e2_vrs_routing_options_config)
        if self.e2_vrs_policy_options_config is not None:
            config_list.append(self.e2_vrs_policy_options_config)
        if self.e2_vrs_provider_ri_config is not None:
            config_list.append(self.e2_vrs_provider_ri_config)

        # Delete if needed
        if operation == "delete":
           return self.device_send([], default_operation="none",
                                       operation="delete")
        return self.device_send(config_list)
    # end build_e2_conf

    def device_send(self, new_config, default_operation="merge",
                        operation="replace"):
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        start_time = None
        config_size = 0
        try:
            with manager.connect(host=self.management_ip, port=22,
                                 username=self.user_creds['username'],
                                 password=self.user_creds['password'],
                                 timeout=self.timeout,
                                 unknown_host_cb=lambda x, y: True) as m:
                add_config = etree.Element(
                    "config",
                    nsmap={"xc": "urn:ietf:params:xml:ns:netconf:base:1.0"})
                config = etree.SubElement(add_config, "configuration")
                config_group = etree.SubElement(
                    config, "groups", operation=operation)
                contrail_group = etree.SubElement(config_group, "name")
                contrail_group.text = "__contrail-e2__"
                if isinstance(new_config, list):
                    for nc in new_config:
                        config_group.append(nc)
                else:
                    config_group.append(new_config)
                if operation == "delete":
                    apply_groups = etree.SubElement(
                        config, "apply-groups", operation=operation)
                    apply_groups.text = "__contrail-e2__"
                else:
                    apply_groups = etree.SubElement(config, "apply-groups",
                                                    insert="first")
                    apply_groups.text = "__contrail-e2__"
                self._logger.info("\nsend netconf message: Router %s: %s\n" % (
                    self.management_ip,
                    etree.tostring(add_config, pretty_print=True)))
                config_str = etree.tostring(add_config)
                config_size = len(config_str)
                m.edit_config(
                    target='candidate', config=config_str,
                    test_option='test-then-set',
                    default_operation=default_operation)
                self.commit_stats['total_commits_sent_since_up'] += 1
                start_time = time.time()
                m.commit()
                end_time = time.time()
                self.commit_stats['commit_status_message'] = 'success'
                self.commit_stats['last_commit_time'] = \
                    datetime.datetime.fromtimestamp(
                    end_time).strftime('%Y-%m-%d %H:%M:%S')
                self.commit_stats['last_commit_duration'] = str(
                    end_time - start_time)
                self.push_config_state = PushConfigState.PUSH_STATE_SUCCESS
                self.timeout = 120
        except TimeoutExpiredError as e:
            self._logger.error("Could not commit(timeout error): "
                          "(%s, %ss)" % (self.management_ip, self.timeout))
            self.timeout += 120
            self.push_config_state = PushConfigState.PUSH_STATE_RETRY
        except Exception as e:
            if self._logger:
                self._logger.error("Router %s: %s" % (self.management_ip,
                                                      e.message))
                self.commit_stats[
                    'commit_status_message'] = 'failed to apply config,\
                                                router response: ' + e.message
                if start_time is not None:
                    self.commit_stats['last_commit_time'] = \
                        datetime.datetime.fromtimestamp(
                            start_time).strftime('%Y-%m-%d %H:%M:%S')
                    self.commit_stats['last_commit_duration'] = str(
                        time.time() - start_time)
                self.push_config_state = PushConfigState.PUSH_STATE_RETRY
        return config_size
    # end device_send

    def add_e2_router_config_xml(self, snmp, lldp):
        #SNMP protocol config
        snmp_cfg = self._get_snmp_config_xml()
        self.e2_router_config = snmp_cfg

        #LLDP protocol config
        lldp_cfg, lldp_med_cfg = self._get_lldp_config_xml()
        if self.e2_services_prot_config is not None:
            self.e2_services_prot_config.append(lldp_cfg)
            self.e2_services_prot_config.append(lldp_med_cfg)
        else:
            protocol_cfg = etree.Element("protocols")
            protocol_cfg.append(lldp_cfg)
            protocol_cfg.append(lldp_med_cfg)
            self.e2_services_prot_config = protocol_cfg
    # end add_e2_router_config_xml

    def _get_lldp_config_xml(self):
        lldp_config = etree.Element("lldp")
        lldp_info = etree.SubElement(lldp_config, "interface")
        etree.SubElement(lldp_info, "name").text = "all"
        lldp_med_config = etree.Element("lldp-med")
        lldp_info = etree.SubElement(lldp_med_config, "interface")
        etree.SubElement(lldp_info, "name").text = "all"
        return lldp_config, lldp_med_config
    # end _get_lldp_config_xml

    def _get_snmp_config_xml(self):
        snmp_config = etree.Element("snmp")
        etree.SubElement(snmp_config, "interface").text = "fxp0.0"
        comm_pub = etree.SubElement(snmp_config, "community")
        etree.SubElement(comm_pub, "name").text = "public"
        etree.SubElement(comm_pub, "authorization").text = "read-only"
        comm_pub = etree.SubElement(snmp_config, "community")
        etree.SubElement(comm_pub, "name").text = "private"
        etree.SubElement(comm_pub, "authorization").text = "read-write"
        return snmp_config
    # end _get_snmp_config_xml

    def _get_telemetry_global_config_xml(self, server_ip, server_port):
        analytics_cfg = etree.Element("streaming-server")
        etree.SubElement(analytics_cfg, "name").text = "AnalyticsNode"
        etree.SubElement(analytics_cfg, "remote-address").text = server_ip
        etree.SubElement(analytics_cfg, "remote-port").text = str(server_port)
        return analytics_cfg
    # end _get_telemetry_global_config_xml

    def add_e2_telemetry_config_xml(self, server_ip, server_port):
        #Global config
        tele_gcfg = self._get_telemetry_global_config_xml(server_ip,
                                                          server_port)
        if self.e2_telemetry_config is not None:
            self.e2_telemetry_config.insert(0, tele_gcfg)
        else:
            analytics_cfg = etree.Element("analytics")
            analytics_cfg.append(tele_gcfg)
            self.e2_telemetry_config = analytics_cfg
    # end add_e2_telemetry_config_xml

    def _get_telemetry_pre_resource_config_xml(self, management_ip, rname,
                                               rpath, rrate):
        rprofile = rname + '-profile'
        eprof_cfg = etree.Element("export-profile")
        etree.SubElement(eprof_cfg, "name").text = rprofile
        etree.SubElement(eprof_cfg, "local-address").text = management_ip
        etree.SubElement(eprof_cfg, "local-port").text = str(50000)
        etree.SubElement(eprof_cfg, "reporting-rate").text = str(rrate)
        etree.SubElement(eprof_cfg, "format").text = 'gpb'
        etree.SubElement(eprof_cfg, "transport").text = 'udp'
        asensor_cfg = etree.Element("sensor")
        etree.SubElement(asensor_cfg, "name").text = rname
        etree.SubElement(asensor_cfg, "server-name").text = 'AnalyticsNode'
        etree.SubElement(asensor_cfg, "export-name").text = rprofile
        etree.SubElement(asensor_cfg, "resource").text = rpath
        return eprof_cfg, asensor_cfg
    # end _get_telemetry_pre_resource_config_xml

    def add_e2_telemetry_per_resource_config_xml(self,
                                                 rname, rpath, rrate):
        management_ip = self.physical_router.management_ip
        #Per resource config
        tele_rcfg, tele_scfg = self._get_telemetry_pre_resource_config_xml(
                                         management_ip, rname, rpath, rrate)
        if self.e2_telemetry_config is not None:
            self.e2_telemetry_config.insert(0, tele_rcfg)
            self.e2_telemetry_config.insert(0, tele_scfg)
        else:
            analytics_cfg = etree.Element("analytics")
            analytics_cfg.append(tele_rcfg)
            analytics_cfg.append(tele_scfg)
            self.e2_telemetry_config = analytics_cfg
    # end add_e2_telemetry_per_resource_config_xml

    def config_e2_vrs_services(self):
        # Push vrr global config
        vrr_conf = {'vrr_name': self.physical_router.name}
        vrr_conf['vrr_ip'] = self.physical_router.management_ip
        bgp_router = BgpRouterDM.get(self.physical_router.bgp_router)
        if bgp_router:
            vrr_conf['vrr_as'] = bgp_router.params.get('autonomous_system')
        self.add_e2_vrs_vrr_global_config(vrr_conf)

        self._logger.info("Total E2 VRS clients on %s is =%d" %
                         (self.physical_router.name, \
                         len(self.physical_router.e2_service_providers)))

        for spindex, sp_uuid in enumerate(self.physical_router.e2_service_providers):
            sp = E2ServiceProviderDM.get(sp_uuid)
            if sp is None:
                self._logger.info("Service provider is NULL for node=%s" %
                                  (self.physical_router.name))
                continue

            # Push provider config
            as_set = set()
            peer_list = []
            provider_conf = {'provider_name': sp.name}
            provider_conf['vrr_ip'] = vrr_conf['vrr_ip']
            provider_conf['vrr_as'] = vrr_conf['vrr_as']
            provider_conf['promiscuous'] = sp.promiscuous

            # Get unique AS list
            prs = sp.physical_routers
            if prs is not None:
                pr_list = list(prs)
                for pr_uuid in pr_list:
                    pr_entry = PhysicalRouterDM.get(pr_uuid)
                    if pr_entry is not None:
                        # Skip VRR
                        if pr_entry.physical_router_role == 'e2-vrr':
                            continue
                        bgp_uuid = pr_entry.bgp_router
                        bgp_entry = BgpRouterDM.get(bgp_uuid)
                        if bgp_entry is not None:
                            as_set.add(bgp_entry.params['autonomous_system'])
                            peer = {}
                            peer['as_number'] = bgp_entry.params['autonomous_system']
                            peer['ip_address'] = bgp_entry.params['address']
                            peer['auth_key'] = None

                            if bgp_entry.params['auth_data'] is not None:
                                keys = bgp_entry.params['auth_data'].get('key_items', [])
                                if len(keys) > 0:
                                    peer['auth_key'] = keys[0].get('key')
                            peer_list.append(peer)


            for as_num in as_set:
                provider_conf['provider_as'] = as_num
                provider_conf['v4_present'] = False
                provider_conf['v6_present'] = False

                for peer in peer_list:
                    if peer['as_number'] == as_num:
                        if IPNetwork(peer['ip_address']).version == 4:
                            provider_conf['v4_present'] = True
                        if IPNetwork(peer['ip_address']).version == 6:
                            provider_conf['v6_present'] = True

                self.add_e2_vrs_provider_as_config(provider_conf)

            for peer in peer_list:
                self._logger.info("Peer ip %s as %s key %s." %
                    (peer['ip_address'], peer['as_number'], peer['auth_key']))
                provider_conf['peer_ip'] = peer['ip_address']
                provider_conf['provider_as'] = peer['as_number']
                provider_conf['peer_key'] = peer['auth_key']
                self.add_e2_vrs_provider_peer_config(provider_conf)

            #for pid in sp.peering_policys:
                #policy = PeeringPolicyDM.get(pid)
    # end config_e2_vrs_services

    def add_e2_vrs_vrr_global_config(self, vrr_conf):
        self.add_e2_vrs_vrr_global_config_xml(vrr_conf['vrr_name'], \
                                              vrr_conf['vrr_ip'], \
                                              vrr_conf['vrr_as'])
        self.add_e2_vrs_global_policy_config_xml(vrr_conf['vrr_as'])
    # end add_e2_vrs_vrr_global_config

    def add_e2_vrs_provider_as_config(self, provider_conf):
        self.add_e2_vrs_provider_policy_config_xml(provider_conf['provider_name'], \
                                                   provider_conf['provider_as'], \
                                                   provider_conf['vrr_as'])
        self.add_e2_vrs_provider_instance_config_xml(provider_conf['provider_name'], \
                                                     provider_conf['provider_as'], \
                                                     provider_conf['vrr_ip'], \
                                                     provider_conf['v4_present'], \
                                                     provider_conf['v6_present'], \
                                                     provider_conf['promiscuous'])
    # end add_e2_vrs_provider_as_config

    def add_e2_vrs_provider_peer_config(self, provider_conf):
        self.add_e2_vrs_provider_instance_peer_config_xml(provider_conf['provider_name'], \
                                                          provider_conf['provider_as'], \
                                                          provider_conf['peer_ip'], \
                                                          provider_conf['peer_key'])
    # end add_e2_vrs_provider_peer_config

    def add_e2_vrs_vrr_global_config_xml(self, name, ip, as_num):
        # System config
        system = etree.Element("system")
        etree.SubElement(system, "host-name").text = name

        # Telemetry config
        services = etree.SubElement(system, "services")
        ext_services = etree.SubElement(services, "extension-service")
        req_resp = etree.SubElement(ext_services, "request-response")
        grpc = etree.SubElement(req_resp, "grpc")
        etree.SubElement(grpc, "clear-text")
        etree.SubElement(grpc, "skip-authentication")
        self.e2_vrs_system_config = system

        # Routing options config
        routing_options = etree.Element("routing-options")
        etree.SubElement(routing_options, "router-id").text = ip
        as_cfg = etree.SubElement(routing_options, "autonomous-system")
        etree.SubElement(as_cfg, "as-number").text = str(as_num)
        self.e2_vrs_routing_options_config = routing_options

        # Routing instance globals
        routing_instances = etree.Element("routing-instances")
        provider_instance = etree.SubElement(routing_instances, "instance")
        etree.SubElement(provider_instance, "name").text = '<*>'
        protocols = etree.SubElement(provider_instance, "protocols")
        bgp = etree.SubElement(protocols, "bgp")
        etree.SubElement(bgp, "hold-time").text = str(300)
        self.e2_vrs_provider_ri_config = routing_instances
    # end add_e2_vrs_vrr_global_config_xml

    def add_e2_vrs_global_policy_config_xml(self, as_num):
        if self.e2_vrs_policy_options_config is None:
            self.e2_vrs_policy_options_config = etree.Element("policy-options")

        # Statement: filter-global-comms
        filter_global_stmt = etree.Element("policy-statement")
        etree.SubElement(filter_global_stmt, "name").text = "filter-global-comms"
        block_all_term = etree.SubElement(filter_global_stmt, "term")
        etree.SubElement(block_all_term, "name").text = "block-all"
        block_all_comm = etree.SubElement(block_all_term, "from")
        etree.SubElement(block_all_comm, "community").text = "block-all-comm"
        block_all_action = etree.SubElement(block_all_term, "then")
        etree.SubElement(block_all_action, "reject")
        filter_global_next = etree.SubElement(filter_global_stmt, "then")
        etree.SubElement(filter_global_next, "next").text = "policy"
        self.e2_vrs_policy_options_config.append(filter_global_stmt)

        # Statement: import-from-all-inst
        import_all_stmt = etree.Element("policy-statement")
        etree.SubElement(import_all_stmt, "name").text = "import-from-all-inst"
        import_all_term = etree.SubElement(import_all_stmt, "term")
        etree.SubElement(import_all_term, "name").text = "from-any"
        import_all_inst = etree.SubElement(import_all_term, "from")
        etree.SubElement(import_all_inst, "instance-any")
        import_all_action = etree.SubElement(import_all_term, "then")
        etree.SubElement(import_all_action, "accept")
        import_all_next = etree.SubElement(import_all_stmt, "then")
        etree.SubElement(import_all_next, "reject")
        self.e2_vrs_policy_options_config.append(import_all_stmt)

        # Statement: export-none
        export_none_stmt = etree.Element("policy-statement")
        etree.SubElement(export_none_stmt, "name").text = "export-none"
        export_none_term = etree.SubElement(export_none_stmt, "term")
        etree.SubElement(export_none_term, "name").text = "from-this"
        export_none_action = etree.SubElement(export_none_term, "then")
        etree.SubElement(export_none_action, "reject")
        self.e2_vrs_policy_options_config.append(export_none_stmt)

        # Community: block-all-comm
        block_all_comm = etree.Element("community")
        etree.SubElement(block_all_comm, "name").text = "block-all-comm"
        etree.SubElement(block_all_comm, "members").text = "0:" + str(as_num)
        self.e2_vrs_policy_options_config.append(block_all_comm)

        # Community: to-all-comm
        to_all_comm = etree.Element("community")
        etree.SubElement(to_all_comm, "name").text = "to-all-comm"
        etree.SubElement(to_all_comm, "members").text = str(as_num) + ":" + str(as_num)
        self.e2_vrs_policy_options_config.append(to_all_comm)

        # Community: to-wildcard-comm
        to_wildcard_comm = etree.Element("community")
        etree.SubElement(to_wildcard_comm, "name").text = "to-wildcard-comm"
        etree.SubElement(to_wildcard_comm, "members").text = "^1:[0-9]*$"
        self.e2_vrs_policy_options_config.append(to_wildcard_comm)

        # Statement: ipv4-only
        ipv4_only_stmt = etree.Element("policy-statement")
        etree.SubElement(ipv4_only_stmt, "name").text = "ipv4-only"
        ipv4_only_term = etree.SubElement(ipv4_only_stmt, "term")
        etree.SubElement(ipv4_only_term, "name").text = "is-ipv4"
        ipv4_only_fam = etree.SubElement(ipv4_only_term, "from")
        etree.SubElement(ipv4_only_fam, "family").text = "inet"
        ipv4_only_action = etree.SubElement(ipv4_only_term, "then")
        etree.SubElement(ipv4_only_action, "next").text = "policy"
        ipv4_only_next = etree.SubElement(ipv4_only_stmt, "then")
        etree.SubElement(ipv4_only_next, "reject")
        self.e2_vrs_policy_options_config.append(ipv4_only_stmt)

        # Statement: ipv6-only
        ipv6_only_stmt = etree.Element("policy-statement")
        etree.SubElement(ipv6_only_stmt, "name").text = "ipv6-only"
        ipv6_only_term = etree.SubElement(ipv6_only_stmt, "term")
        etree.SubElement(ipv6_only_term, "name").text = "is-ipv6"
        ipv6_only_fam = etree.SubElement(ipv6_only_term, "from")
        etree.SubElement(ipv6_only_fam, "family").text = "inet6"
        ipv6_only_action = etree.SubElement(ipv6_only_term, "then")
        etree.SubElement(ipv6_only_action, "next").text = "policy"
        ipv6_only_next = etree.SubElement(ipv6_only_stmt, "then")
        etree.SubElement(ipv6_only_next, "reject")
        self.e2_vrs_policy_options_config.append(ipv6_only_stmt)

    # end add_e2_vrs_global_policy_config_xml

    def add_e2_vrs_provider_policy_config_xml(self, name, provider_as, vrr_as):
        if self.e2_vrs_policy_options_config is None:
            self.e2_vrs_policy_options_config = etree.Element("policy-options")

        # Statement: filter-provider-as-comms
        filter_name = "filter-" + name + "-as-" + str(provider_as) + "-comms"
        filter_provider_stmt = etree.Element("policy-statement")
        etree.SubElement(filter_provider_stmt, "name").text = filter_name

        # Term: block-this-rib
        block_rib_term = etree.SubElement(filter_provider_stmt, "term")
        etree.SubElement(block_rib_term, "name").text = "block-this-rib"
        block_rib_name = "block-" + name + "-as-" + str(provider_as) + "-comm"
        block_rib_comm = etree.SubElement(block_rib_term, "from")
        etree.SubElement(block_rib_comm, "community").text = block_rib_name
        block_rib_action = etree.SubElement(block_rib_term, "then")
        etree.SubElement(block_rib_action, "reject")

        # Term: to-this-rib
        to_rib_term = etree.SubElement(filter_provider_stmt, "term")
        etree.SubElement(to_rib_term, "name").text = "to-this-rib"
        to_rib_name = "to-" + name + "-as-" + str(provider_as) + "-comm"
        to_rib_comm = etree.SubElement(to_rib_term, "from")
        etree.SubElement(to_rib_comm, "community").text = to_rib_name
        to_rib_action = etree.SubElement(to_rib_term, "then")
        etree.SubElement(to_rib_action, "next").text = "policy"

        # Term: to-all
        to_all_term = etree.SubElement(filter_provider_stmt, "term")
        etree.SubElement(to_all_term, "name").text = "to-all"
        to_all_comm = etree.SubElement(to_all_term, "from")
        etree.SubElement(to_all_comm, "community").text = "to-all-comm"
        to_all_action = etree.SubElement(to_all_term, "then")
        etree.SubElement(to_all_action, "next").text = "policy"

        # Term: to-any-specific-rib
        to_any_rib_term = etree.SubElement(filter_provider_stmt, "term")
        etree.SubElement(to_any_rib_term, "name").text = "to-any-specific-rib"
        to_any_rib_comm = etree.SubElement(to_any_rib_term, "from")
        etree.SubElement(to_any_rib_comm, "community").text = "to-wildcard-comm"
        to_any_rib_action = etree.SubElement(to_any_rib_term, "then")
        etree.SubElement(to_any_rib_action, "reject")

        # Default action
        filter_provider_next = etree.SubElement(filter_provider_stmt, "then")
        etree.SubElement(filter_provider_next, "next").text = "policy"
        self.e2_vrs_policy_options_config.append(filter_provider_stmt)

        # Community: block-provider-as-comm
        block_provider_comm = etree.Element("community")
        etree.SubElement(block_provider_comm, "name").text = block_rib_name
        if provider_as > 65535:
            provider_comm_str = "large:0:" + str(old_div(provider_as,65536)) + ":" + \
                                 str(provider_as%65536)
        else:
            provider_comm_str = "0:" + str(provider_as)
        etree.SubElement(block_provider_comm, "members").text = provider_comm_str
        self.e2_vrs_policy_options_config.append(block_provider_comm)

        # Community: to-all-comm
        to_provider_comm = etree.Element("community")
        etree.SubElement(to_provider_comm, "name").text = to_rib_name
        if provider_as > 65535:
            provider_comm_str = "large:" + str(vrr_as) + ":" + str(old_div(provider_as,65536)) + \
                                ":" + str(provider_as%65536)
        else:
            provider_comm_str = str(vrr_as) + ":" + str(provider_as)
        etree.SubElement(to_provider_comm, "members").text = provider_comm_str
        self.e2_vrs_policy_options_config.append(to_provider_comm)
    # end add_e2_vrs_provider_policy_config_xml

    def add_e2_vrs_provider_instance_config_xml(self, name, provider_as, vrr_ip, \
                                                v4_yes, v6_yes, promiscuous):
        if self.e2_vrs_provider_ri_config is None:
            self.e2_vrs_provider_ri_config = etree.Element("routing-instances")

        # Instance
        provider_instance = etree.Element("instance")
        ri_name = name + "-as-" + str(provider_as)
        filter_name = "filter-" + name + "-as-" + str(provider_as) + "-comms"
        etree.SubElement(provider_instance, "name").text = ri_name
        etree.SubElement(provider_instance, "instance-type").text = "no-forwarding"

        # Instance routing-options
        routing_options = etree.SubElement(provider_instance, "routing-options")
        etree.SubElement(routing_options, "router-id").text = vrr_ip

        if v4_yes is True:
            if v6_yes is False:
                etree.SubElement(routing_options, "instance-import").text = "ipv4-only"
        else:
            if v6_yes is True:
                etree.SubElement(routing_options, "instance-import").text = "ipv6-only"
        etree.SubElement(routing_options, "instance-import").text = "filter-global-comms"
        etree.SubElement(routing_options, "instance-import").text = filter_name
        if promiscuous is True:
            etree.SubElement(routing_options, "instance-import").text = "import-from-all-inst"
        else:
            etree.SubElement(routing_options, "instance-export").text = "export-none"

        # Instance protocols
        protocols = etree.SubElement(provider_instance, "protocols")
        bgp = etree.SubElement(protocols, "bgp")
        etree.SubElement(bgp, "peer-as").text = str(provider_as)

        # IPv4 BGP group
        if v4_yes is True:
            bgp_group = etree.SubElement(bgp, "group")
            etree.SubElement(bgp_group, "name").text = "bgp-v4-as-" + str(provider_as)
            etree.SubElement(bgp_group, "type").text = "external"
            etree.SubElement(bgp_group, "route-server-client")
            etree.SubElement(bgp_group, "mtu-discovery")
            bgp_group_family = etree.SubElement(bgp_group, "family")
            bgp_group_family_inet = etree.SubElement(bgp_group_family, "inet")
            bgp_group_family_inet_ucast = etree.SubElement(bgp_group_family_inet, "unicast")

        # IPv6 BGP group
        if v6_yes is True:
            bgp_group = etree.SubElement(bgp, "group")
            etree.SubElement(bgp_group, "name").text = "bgp-v6-as-" + str(provider_as)
            etree.SubElement(bgp_group, "type").text = "external"
            etree.SubElement(bgp_group, "route-server-client")
            etree.SubElement(bgp_group, "mtu-discovery")
            bgp_group_family = etree.SubElement(bgp_group, "family")
            bgp_group_family_inet6 = etree.SubElement(bgp_group_family, "inet6")
            bgp_group_family_inet6_ucast = etree.SubElement(bgp_group_family_inet6, "unicast")

        self.e2_vrs_provider_ri_config.append(provider_instance)
    # end add_e2_vrs_provider_instance_config_xml

    def add_e2_vrs_provider_instance_peer_config_xml(self, name, provider_as, peer_ip, key):
        if self.e2_vrs_provider_ri_config is None:
            self.e2_vrs_provider_ri_config = etree.Element("routing-instances")

        # Instance
        provider_instance = etree.Element("instance")
        ri_name = name + "-as-" + str(provider_as)
        etree.SubElement(provider_instance, "name").text = ri_name

        # Instance protocols
        protocols = etree.SubElement(provider_instance, "protocols")
        bgp = etree.SubElement(protocols, "bgp")
        bgp_group = etree.SubElement(bgp, "group")

        if IPNetwork(peer_ip).version == 4:
            etree.SubElement(bgp_group, "name").text = "bgp-v4-as-" + str(provider_as)
        if IPNetwork(peer_ip).version == 6:
            etree.SubElement(bgp_group, "name").text = "bgp-v6-as-" + str(provider_as)
        bgp_group_nbr = etree.SubElement(bgp_group, "neighbor")
        etree.SubElement(bgp_group_nbr, "name").text = peer_ip
        etree.SubElement(bgp_group_nbr, "forwarding-context").text = "master"
        if key is not None:
            etree.SubElement(bgp_group_nbr, "authentication-key").text = key

        self.e2_vrs_provider_ri_config.append(provider_instance)
    # end add_e2_vrs_provider_instance_peer_config_xml

# end MxE2Conf
