#
# Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of abstract config generation for PNF Service Chaining
"""

import db
from abstract_device_api.abstract_device_xsd import *
from feature_base import FeatureBase

class PNFSrvcChainingFeature(FeatureBase):

    @classmethod
    def feature_name(cls):
        return 'pnf-service_chaining'
    # end feature_name

    def __init__(self, logger, physical_router, configs):
        self.pi_map = {}
        self.ri_map = {}
        self.sc_zone_map = {}
        self.sc_policy_map = {}
        super(PNFSrvcChainingFeature, self).__init__(logger, physical_router, configs)
    # end __init__

    def get_peer_asn(self, pi_obj):
        pr_uuid = pi_obj.physical_router
        pr_obj = db.PhysicalRouterDM.get(pr_uuid)
        if pr_obj:
            bgp_uuid = pr_obj.bgp_router
            bgp_obj = db.BgpRouterDM.get(bgp_uuid)
            if bgp_obj and bgp_obj.params:
                return bgp_obj.params.get('autonomous_system')

        return None
    # end get_peer_asn

    @staticmethod
    def get_values_sorted_by_key(dict_obj):
        return [dict_obj[key] for key in sorted(dict_obj.keys())]
    # end get_values_sorted_by_key

    def build_pnf_required_params(self, svc_params):
        if svc_params.get('peer_right_li_ips') and \
           svc_params.get('right_li') and \
           svc_params.get('right_li_ip') and \
           svc_params.get('left_li') and \
           svc_params.get('peer_left_li_ips') and \
           svc_params.get('left_li_ip') and \
           svc_params.get('lo0_li') and \
           svc_params.get('left_peer_asn') and \
           svc_params.get('right_peer_asn') and \
           svc_params.get('lo0_li_ip'):
            return True

        return False
    # end build_pnf_required_params

    def build_pnf_svc_ri_config(self, svc_params):
        if svc_params.get('svc_inst_name'):
            ri_name = svc_params.get('svc_inst_name') + '_left_right'
            ri = RoutingInstance(name=ri_name)
            pim = Pim()
            protocol = db.RoutingInstanceProtocols()
            self.ri_map[ri_name] = ri
            ri.set_comment("PNF svc routing instance")
            ri.set_routing_instance_type('virtual-router')
            if svc_params.get('lo0_li') and svc_params.get('lo0_li_ip'):
                ri.add_loopback_interfaces(
                    db.LogicalInterface(
                        name=svc_params.get('lo0_li')))
                pimrp = PimRp(ip_address=svc_params.get('lo0_li_ip'))
                pim.set_rp(pimrp)
            if svc_params.get('left_li'):
                ri.add_routing_interfaces(
                    db.LogicalInterface(
                        name=svc_params.get('left_li')))
                pim_intf = PimInterface(
                    LogicalInterface(
                        name=svc_params.get('left_li')))
                pim.add_pim_interfaces(pim_intf)
            if svc_params.get('right_li'):
                ri.add_routing_interfaces(
                    LogicalInterface(
                        name=svc_params.get('right_li')))
                pim_intf = PimInterface(
                    LogicalInterface(
                        name=svc_params.get('right_li')))
                pim.add_pim_interfaces(pim_intf)

            protocol.add_pim(pim)
            # Build BGP config associated to this RI
            left_bgp_name = svc_params.get('svc_inst_name') + '_left'
            if svc_params.get('left_li_ip') and svc_params.get('left_asns'):
                bgp = Bgp(name=left_bgp_name,
                          ip_address=svc_params.get('left_li_ip'),
                          autonomous_system=svc_params.get('left_asns')[0],
                          type_='external')
                bgp.set_comment("PNF left BGP group")
                peers = {}
                for peer_ip in svc_params.get('peer_left_li_ips') or []:
                    name = left_bgp_name + '-' + peer_ip
                    peer = Bgp(
                        name=name,
                        ip_address=peer_ip,
                        autonomous_system=svc_params.get('left_peer_asn'))
                    peers[name] = peer
                if peers:
                    bgp.set_peers(self.get_values_sorted_by_key(peers))
                protocol.add_bgp(bgp)

            right_bgp_name = svc_params.get('svc_inst_name') + '_right'
            if svc_params.get('right_li_ip') and svc_params.get('right_asns'):
                bgp = Bgp(name=right_bgp_name,
                          ip_address=svc_params.get('right_li_ip'),
                          autonomous_system=svc_params.get('right_asns')[0],
                          type_='external')
                bgp.set_comment("PNF right BGP group")
                peers = {}
                for peer_ip in svc_params.get('peer_right_li_ips') or []:
                    name = right_bgp_name + '-' + peer_ip
                    peer = Bgp(
                        name=name,
                        ip_address=peer_ip,
                        autonomous_system=svc_params.get('right_peer_asn'))
                    peers[name] = peer
                if peers:
                    bgp.set_peers(self.get_values_sorted_by_key(peers))
                protocol.add_bgp(bgp)

            ri.set_protocols(protocol)
    # end build_pnf_svc_ri_config

    def build_pnf_svc_intfs_config(self, svc_params):
        # Add loopback interface
        if svc_params.get('lo0_li') and svc_params.get('lo0_li_ip'):
            lo0_intf = svc_params.get('lo0_li')
            ip = svc_params.get('lo0_li_ip')
            intf, li_map = self._add_or_lookup_pi(self.pi_map, 'lo0', 'loopback')
            intf_unit = self._add_or_lookup_li(
                li_map, lo0_intf, lo0_intf.split('.')[1])
            intf_unit.set_comment("PNF loopback interface")
            intf_unit.set_vlan_tag(lo0_intf.split('.')[1])
            self._add_ip_address(intf_unit, ip)

        # Add left svc interface
        if svc_params.get('left_li') and svc_params.get('left_li_ip'):
            left_intf = svc_params.get('left_li')
            ip = svc_params.get('left_li_ip')
            intf, li_map = self._add_or_lookup_pi(self.pi_map, 
                left_intf.split('.')[0], 'service')
            intf_unit = self._add_or_lookup_li(
                li_map, left_intf, left_intf.split('.')[1])
            intf_unit.set_comment("PNF left svc interface")
            intf_unit.set_vlan_tag(left_intf.split('.')[1])
            self._add_ip_address(intf_unit, ip)

        # Add right svc interface
        if svc_params.get('right_li') and svc_params.get('right_li_ip'):
            right_intf = svc_params.get('right_li')
            ip = svc_params.get('right_li_ip')
            intf, li_map = self._add_or_lookup_pi(self.pi_map, 
                right_intf.split('.')[0], 'service')
            intf_unit = self._add_or_lookup_li(
                li_map, right_intf, right_intf.split('.')[1])
            intf_unit.set_comment("PNF right svc interface")
            intf_unit.set_vlan_tag(right_intf.split('.')[1])
            self._add_ip_address(intf_unit, ip)
    # end build_pnf_svc_intfs_config

    def build_pnf_svc_sc_zone_policy_config(self, svc_params):
        if svc_params.get('svc_inst_name'):
            left_sec_zone = svc_params.get('svc_inst_name') + '_left'
            right_sec_zone = svc_params.get('svc_inst_name') + '_right'
            left_sc = SecurityZone(name=left_sec_zone)
            self.sc_zone_map[left_sec_zone] = left_sc
            if svc_params.get('left_li'):
                left_sc.add_interfaces(
                    LogicalInterface(
                        name=svc_params.get('left_li')))
            right_sc = SecurityZone(name=right_sec_zone)
            self.sc_zone_map[right_sec_zone] = right_sc
            if svc_params.get('right_li'):
                right_sc.add_interfaces(
                    LogicalInterface(
                        name=svc_params.get('right_li')))
            sc_policy = SecurityPolicy(
                from_zone=left_sec_zone,
                to_zone=right_sec_zone)
            self.sc_policy_map[left_sec_zone] = sc_policy
            sc_policy = SecurityPolicy(
                from_zone=right_sec_zone,
                to_zone=left_sec_zone)
            self.sc_policy_map[right_sec_zone] = sc_policy
    # end build_pnf_svc_sc_zone_policy_config

    def build_pnf_svc_config(self):
        pr = self._physical_router
        pt_list = pr.port_tuples
        for pt in pt_list or []:
            pt_obj = db.PortTupleDM.get(pt)
            if pt_obj:
                si = pt_obj.svc_isntance
                si_obj = db.ServiceInstanceDM.get(si)
                if si_obj:
                    svc_params = {}
                    svc_params['svc_inst_name'] = si_obj.name
                    svc_params['left_vlan'] = si_obj.left_svc_vlan
                    svc_params['right_vlan'] = si_obj.right_svc_vlan
                    svc_params['left_asns'] = si_obj.left_svc_asns
                    svc_params['right_asns'] = si_obj.right_svc_asns
                    svc_params['left_peer_unit'] = si_obj.left_svc_unit
                    svc_params['right_peer_unit'] = si_obj.right_svc_unit
                    sa_obj = pt_obj.get_sa_obj()
                    if sa_obj:
                        right_li_name = left_li_name = lo0_li_name = None
                        right_li_ip = left_li_ip = lo0_li_ip = None
                        for pi, intf_type in (
                            sa_obj.physical_interfaces).iteritems():
                            pi_obj = db.PhysicalInterfaceDM.get(pi)
                            attr = intf_type.get('interface_type')
                            if attr == 'left':
                                left_li_name = pi_obj.name + '.' + \
                                    str(svc_params['left_vlan'])
                                left_li_fq_name = pi_obj.fq_name + \
                                    [left_li_name.replace(":", "_")]
                                left_li_obj = \
                                    db.LogicalInterfaceDM.find_by_fq_name(
                                        left_li_fq_name)
                                if left_li_obj:
                                    instance_ip = db.InstanceIpDM.get(
                                        left_li_obj.instance_ip)
                                    if instance_ip:
                                        left_li_ip = \
                                            instance_ip.instance_ip_address
                                        svc_params['left_li'] = left_li_name
                                        svc_params['left_li_ip'] = left_li_ip
                                lo0_li_name = 'lo0' + '.' + \
                                    str(svc_params['left_vlan'])
                                lo0_fq_name = pr.fq_name + ['lo0', lo0_li_name]
                                lo0_li_obj = \
                                    db.LogicalInterfaceDM.find_by_fq_name(
                                        lo0_fq_name)
                                if lo0_li_obj:
                                    instance_ip = db.InstanceIpDM.get(
                                        lo0_li_obj.instance_ip)
                                    if instance_ip:
                                        lo0_li_ip = \
                                            instance_ip.instance_ip_address
                                        svc_params['lo0_li'] = lo0_li_name
                                        svc_params['lo0_li_ip'] = lo0_li_ip
                                peer_left_intfs_ip = []
                                for pi_ref in pi_obj.physical_interfaces or []:
                                    pi_ref_obj = db.PhysicalInterfaceDM.get(
                                        pi_ref)
                                    svc_params['left_peer_asn'] = \
                                        self.get_peer_asn(pi_ref_obj)
                                    peer_li_name = pi_ref_obj.name + \
                                        '.' + str(svc_params['left_vlan'])
                                    peer_li_fq_name = pi_ref_obj.fq_name + \
                                        [peer_li_name.replace(":", "_")]
                                    peer_li_obj = \
                                        db.LogicalInterfaceDM.find_by_fq_name(
                                            peer_li_fq_name)
                                    if peer_li_obj:
                                        instance_ip = db.InstanceIpDM.get(
                                            peer_li_obj.instance_ip)
                                        if instance_ip:
                                            peer_li_ip = \
                                                instance_ip.instance_ip_address
                                            peer_left_intfs_ip.append(
                                                peer_li_ip)
                                svc_params['peer_left_li_ips'] = \
                                    peer_left_intfs_ip
                            elif attr == 'right':
                                right_li_name = pi_obj.name + '.' + \
                                    str(svc_params['right_vlan'])
                                right_li_fq_name = pi_obj.fq_name + \
                                    [right_li_name.replace(":", "_")]
                                right_li_obj = \
                                    db.LogicalInterfaceDM.find_by_fq_name(
                                        right_li_fq_name)
                                if right_li_obj:
                                    instance_ip = db.InstanceIpDM.get(
                                        right_li_obj.instance_ip)
                                    if instance_ip:
                                        right_li_ip = \
                                            instance_ip.instance_ip_address
                                        svc_params['right_li'] = right_li_name
                                        svc_params['right_li_ip'] = right_li_ip
                                peer_right_intfs_ip = []
                                for pi_ref in pi_obj.physical_interfaces or []:
                                    pi_ref_obj = db.PhysicalInterfaceDM.get(
                                        pi_ref)
                                    svc_params['right_peer_asn'] = \
                                        self.get_peer_asn(pi_ref_obj)
                                    peer_li_name = pi_ref_obj.name + \
                                        '.' + str(svc_params['right_vlan'])
                                    peer_li_fq_name = pi_ref_obj.fq_name + \
                                        [peer_li_name.replace(":", "_")]
                                    peer_li_obj = \
                                        db.LogicalInterfaceDM.find_by_fq_name(
                                            peer_li_fq_name)
                                    if peer_li_obj:
                                        instance_ip = db.InstanceIpDM.get(
                                            peer_li_obj.instance_ip)
                                        if instance_ip:
                                            peer_li_ip = \
                                                instance_ip.instance_ip_address
                                            peer_right_intfs_ip.append(
                                                peer_li_ip)
                                svc_params['peer_right_li_ips'] = \
                                    peer_right_intfs_ip
                        self._logger.debug(
                            "PR: %s svc params: %s" %
                            (pr.name, svc_params))
                        # Make sure all required parameters are present,
                        # before creating the abstract config
                        if self.build_pnf_required_params(svc_params):
                            self.build_pnf_svc_ri_config(svc_params)
                            self.build_pnf_svc_intfs_config(svc_params)
                            self.build_pnf_svc_sc_zone_policy_config(
                                svc_params)
    # end build_pnf_config

    def feature_config(self, **kwargs):
        pr = self._physical_router
        if not pr:
            return
        if pr.physical_router_role != 'pnf':
            return
        
        feature_config = Feature(name=self.feature_name)

        self.build_pnf_svc_config()

        for pi, li_map in self.pi_map.values():
            pi.set_logical_interfaces(li_map.values())
            feature_config.add_physical_interfaces(pi)
        
        feature_config.set_routing_instances(
            self.get_values_sorted_by_key(
                self.ri_map))

        feature_config.set_security_zones(
            self.get_values_sorted_by_key(
                self.sc_zone_map))
        
        feature_config.set_security_policies(
            self.get_values_sorted_by_key(
                self.sc_policy_map))
        
    # end feature_config

# end PNFSrvcChainingFeature