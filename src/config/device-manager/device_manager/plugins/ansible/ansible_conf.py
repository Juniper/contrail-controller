#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains generic plugin implementation device configuration using ansible
"""

import abc
import time
import datetime
import json
from hashlib import md5
from dm_utils import DMUtils
from ansible_base import AnsibleBase
from dm_utils import PushConfigState
from db import *
from abstract_device_api.abstract_device_xsd import *
from device_manager import DeviceManager
from job_handler import JobHandler


class AnsibleConf(AnsibleBase):
    _roles = ['leaf', 'spine']

    @classmethod
    def register(cls):
        qconf = {
              "roles": cls._roles,
              "class": cls
            }
        return super(AnsibleConf, cls).register(qconf)
    # end register

    @classmethod
    def is_role_supported(cls, role):
        if role and role.lower().startswith('e2-'):
            return False
        return True
    # end is_role_supported

    def __init__(self, logger, params={}):
        self.last_config_hash = None
        self.physical_router = params.get("physical_router")
        super(AnsibleConf, self).__init__(logger)
    # end __init__

    def underlay_config(self, is_delete=False):
        self._logger.info("underlay config start: %s(%s)\n" %
                          (self.physical_router.name,
                           self.physical_router.uuid))
        if not is_delete:
            self.build_underlay_bgp()
        self.send_conf(is_delete=is_delete, retry=False)
        self._logger.info("underlay config end: %s(%s)\n" %
                          (self.physical_router.name,
                           self.physical_router.uuid))
    # end underlay_config

    def update(self):
        self.update_system_config()
    # end update

    def get_commit_stats(self):
        return self.commit_stats
    # end get_commit_stats

    def retry(self):
        if self.push_config_state == PushConfigState.PUSH_STATE_RETRY:
            return True
        return False
    # end retry

    def device_connect(self):
        pass
    # end device_connect

    def device_disconnect(self):
        pass
    # end device_disconnect

    def is_connected(self):
        return True
    # end is_connected

    def initialize(self):
        super(AnsibleConf, self).initialize()
        self.system_config = None
        self.evpn = None
        self.bgp_configs = None
        self.ri_config = None
        self.routing_instances = {}
        self.interfaces_config = None
        self.services_config = None
        self.policy_config = None
        self.firewall_config = None
        self.inet4_forwarding_filter = None
        self.inet6_forwarding_filter = None
        self.forwarding_options_config = None
        self.global_routing_options_config = None
        self.global_switch_options_config = None
        self.vlans_config = None
        self.route_targets = set()
        self.bgp_peers = {}
        self.chassis_config = None
        self.external_peers = {}
        self.timeout = 120
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        self.system_config = System()
        self.update_system_config()
        self.global_switch_options_config = None
        self.chassis_config = None
        self.vlans_config = None
        self.irb_interfaces = []
        self.internal_vn_ris = []
    # end initialize

    def update_system_config(self):
        self.system_config.set_name(self.physical_router.name)
        self.system_config.set_uuid(self.physical_router.uuid)
        self.system_config.set_vendor_name(self.physical_router.vendor)
        self.system_config.set_product_name(self.physical_router.product)
        self.system_config.set_device_family(self.physical_router.device_family)
        self.system_config.set_management_ip(self.physical_router.management_ip)
        self.system_config.set_physical_role(
            self.physical_router.physical_router_role)
        self.system_config.set_routing_bridging_roles(
            self.physical_router.routing_bridging_roles)
        if self.physical_router.user_credentials:
            self.system_config.set_credentials(Credentials(
                authentication_method="PasswordBasedAuthentication",
                user_name=self.physical_router.user_credentials.get('username'),
                password=self.physical_router.user_credentials.get('password')))
        if self.physical_router.loopback_ip is not None:
            already_added = False
            for loopback_ip in self.system_config.get_loopback_ip_list():
                if loopback_ip.get_address() == self.physical_router.loopback_ip:
                    already_added = True
                    break
            if not already_added:
                self.system_config.add_loopback_ip_list(IpType(
                    address=self.physical_router.loopback_ip))
    # end update_system_config

    def attach_irb(self, ri_conf, ri):
        is_l2 = ri_conf.get("is_l2", False)
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        vni = ri_conf.get("vni", None)
        network_id = ri_conf.get("network_id", None)
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            if is_l2_l3:
                self.irb_interfaces.append("irb." + str(network_id))
    # end attach_irb

    def fetch_pi_li_iip(self, physical_interfaces):
        for pi_uuid in physical_interfaces:
            pi_obj = PhysicalInterfaceDM.get(pi_uuid)
            if pi_obj is None:
                self._logger.error("unable to read physical interface %s" %
                                   pi_uuid)
            else:
                for li_uuid in pi_obj.logical_interfaces:
                    li_obj = LogicalInterfaceDM.get(li_uuid)
                    if li_obj is None:
                        self._logger.error("unable to read logical interface %s"
                                           % li_uuid)
                    elif li_obj.instance_ip is not None:
                        iip_obj = InstanceIpDM.get(li_obj.instance_ip)
                        if iip_obj is None:
                            self._logger.error("unable to read instance ip %s" %
                                               li_obj.instance_ip)
                        else:
                            yield pi_obj, li_obj, iip_obj
    # end fetch_pi_li_iip

    def build_underlay_bgp(self):
        self.bgp_configs = self.bgp_configs or []
        self.interfaces_config = self.interfaces_config or []

        if self.physical_router.allocated_asn is None:
            self._logger.error("physical router %s(%s) does not have asn"
                               " allocated" % (self.physical_router.name,
                                               self.physical_router.uuid))
            return

        for pi_obj, li_obj, iip_obj in self.\
                fetch_pi_li_iip(self.physical_router.physical_interfaces):
            if pi_obj and li_obj and iip_obj and iip_obj.instance_ip_address:
                pi = PhysicalInterface(uuid=pi_obj.uuid, name=pi_obj.name,
                                       interface_type='regular',
                                       comment=DMUtils.ip_clos_comment())
                li = LogicalInterface(uuid=li_obj.uuid, name=li_obj.name,
                                      unit=int(li_obj.name.split('.')[-1]),
                                      comment=DMUtils.ip_clos_comment())
                li.add_ip_list(IpType(address=iip_obj.instance_ip_address))
                pi.add_logical_interfaces(li)
                self.interfaces_config.append(pi)

                self._logger.debug("looking for peers for physical"
                                   " interface %s(%s)" % (pi_obj.name,
                                                          pi_obj.uuid))
                # Add this bgp object only if it has a peer
                bgp = Bgp(ip_address=iip_obj.instance_ip_address,
                          autonomous_system=self.physical_router.allocated_asn,
                          comment=DMUtils.ip_clos_comment())
                # Assumption: PIs are connected for IP-CLOS peering only
                for peer_pi_obj, peer_li_obj, peer_iip_obj in\
                        self.fetch_pi_li_iip(pi_obj.physical_interfaces):
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
                        elif peer_pr != self.physical_router:
                            if bgp not in self.bgp_configs:
                                self.bgp_configs.append(bgp)

                            peer = Bgp(
                                ip_address=peer_iip_obj.instance_ip_address,
                                autonomous_system=peer_pr.allocated_asn,
                                comment=peer_pr.name)
                            bgp.add_peers(peer)
    # end build_bgp_config

    def device_send(self, job_template, job_input, is_delete, retry):
        config_str = json.dumps(job_input, sort_keys=True)
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        start_time = None
        config_size = 0
        try:
            config_size = len(config_str)
            current_config_hash = md5(config_str).hexdigest()
            if self.last_config_hash is None or\
                    current_config_hash != self.last_config_hash:
                self._logger.debug("playbook send message: %s" %
                                   json.dumps(job_input, indent=4,
                                              sort_keys=True))
                device_manager = DeviceManager.get_instance()
                job_handler = JobHandler(job_template, job_input,
                                         None if is_delete else
                                         [self.physical_router.uuid],
                                         device_manager.get_analytics_config(),
                                         device_manager.get_vnc(), self._logger)
                self.commit_stats['total_commits_sent_since_up'] += 1
                start_time = time.time()
                job_handler.push()
                end_time = time.time()
                self.commit_stats['commit_status_message'] = 'success'
                self.commit_stats['last_commit_time'] = \
                        datetime.datetime.fromtimestamp(
                        end_time).strftime('%Y-%m-%d %H:%M:%S')
                self.commit_stats['last_commit_duration'] = str(
                        end_time - start_time)
                self.last_config_hash = current_config_hash
            else:
                self._logger.debug("not pushing since no config change"
                                   " detected")
            self.push_config_state = PushConfigState.PUSH_STATE_SUCCESS
        except Exception as e:
            self._logger.error("Router %s: %s" %
                               (self.physical_router.management_ip, e.message))
            self.commit_stats[
                    'commit_status_message'] = 'failed to apply config,\
                                                router response: ' + e.message
            if start_time is not None:
                self.commit_stats['last_commit_time'] = \
                        datetime.datetime.fromtimestamp(
                            start_time).strftime('%Y-%m-%d %H:%M:%S')
                self.commit_stats['last_commit_duration'] = str(
                        time.time() - start_time)
            self.push_config_state = PushConfigState.PUSH_STATE_RETRY if retry\
                else PushConfigState.PUSH_STATE_FAILED
        return config_size
    # end device_send

    # Product Specific configuration, called from parent class
    def add_product_specific_config(self, groups):
        groups.set_switch_options(self.global_switch_options_config)
        if self.vlans_config:
            groups.set_vlans(self.vlans_config)
        if self.chassis_config:
            groups.set_chassis(self.chassis_config)
    # end add_product_specific_config

    def read_feature_configs(self):
        feature_configs = {}
        job_template = None
        if self.physical_router.node_profile is not None:
            node_profile = NodeProfileDM.get(self.physical_router.node_profile)
            if node_profile is not None:
                for role_config_uuid in node_profile.role_configs:
                    role_config = RoleConfigDM.get(role_config_uuid)
                    if role_config is None:
                        continue
                    if job_template is None:
                        job_template = role_config.job_template_fq_name
                    feature_configs[role_config.name] = role_config.config
        return feature_configs, job_template
    # end read_feature_configs

    def prepare_conf(self, is_delete=False):
        device = Device()
        device.set_comment(DMUtils.groups_comment())
        device.set_system(self.system_config)
        if is_delete:
            return device
        device.set_evpn(self.evpn)
        device.set_bgp(self.bgp_configs)
        device.set_routing_instances(self.ri_config)
        device.set_physical_interfaces(self.interfaces_config)
        device.set_vlans(self.vlans_config)
        device.set_policies(self.policy_config)
        device.set_firewall(self.firewall_config)
        return device
    # end prepare_conf

    def has_conf(self):
        return self.system_config or self.bgp_configs or self.interfaces_config
    # end has_conf

    def send_conf(self, is_delete=False, retry=True):
        if not self.has_conf() and not is_delete:
            return 0
        config = self.prepare_conf(is_delete=is_delete)
        feature_params, job_template = self.read_feature_configs()
        job_input = {
            'device_abstract_config': self.export_dict(config),
            'additional_feature_params': feature_params,
            'fabric_uuid': self.physical_router.fabric,
            'push_mode': PushConfigState.is_push_mode_ansible(),
            'is_delete': is_delete
        }
        return self.device_send(job_template, job_input, is_delete, retry)
    # end send_conf

    def add_dynamic_tunnels(self, tunnel_source_ip,
                            ip_fabric_nets, bgp_router_ips):
        if not self.system_config:
            self.system_config = System()
        self.system_config.set_tunnel_ip(tunnel_source_ip)
        if ip_fabric_nets is not None:
            for subnet in ip_fabric_nets.get("subnet", []):
                dest_net = Subnet(prefix=subnet['ip_prefix'],
                                  prefix_len=subnet['ip_prefix_len'])
                self.system_config.add_tunnel_destination_networks(dest_net)

        for r_name, bgp_router_ip in bgp_router_ips.items():
            dest_net = Subnet(prefix=bgp_router_ip, prefix_len=32)
            self.system_config.add_tunnel_destination_networks(dest_net)
    # end add_dynamic_tunnels

    def is_family_configured(self, params, family_name):
        if params is None or params.get('address_families') is None:
            return False
        families = params['address_families'].get('family', [])
        if family_name in families:
            return True
        return False
    # end is_family_configured

    def add_families(self, parent, params):
        if params.get('address_families') is None:
            return
        families = params['address_families'].get('family', [])
        if not families:
            return
        for family in families:
            if family in ['e-vpn', 'e_vpn']:
                family = 'evpn'
            parent.add_families(family)
    # end add_families

    def add_ibgp_export_policy(self, params, bgp_group):
        if params.get('address_families') is None:
            return
        families = params['address_families'].get('family', [])
        if not families:
            return
        if self.policy_config is None:
            self.policy_config = Policy(
                comment=DMUtils.policy_options_comment())
        ps = PolicyRule(name=DMUtils.make_ibgp_export_policy_name())
        self.policy_config.add_policy_rule(ps)
        ps.set_comment(DMUtils.ibgp_export_policy_comment())
        vpn_types = []
        for family in ['inet-vpn', 'inet6-vpn']:
            if family in families:
                vpn_types.append(family)
        for vpn_type in vpn_types:
            is_v6 = True if vpn_type == 'inet6-vpn' else False
            term = Term(name=DMUtils.make_ibgp_export_policy_term_name(is_v6))
            ps.add_term(term)
            then = Then()
            from_ = From()
            term.set_from(from_)
            term.set_then(then)
            from_.set_family(DMUtils.get_inet_family_name(is_v6))
        bgp_group.set_export_policy(DMUtils.make_ibgp_export_policy_name())
    # end add_ibgp_export_policy

    def add_bgp_auth_config(self, bgp_config, bgp_params):
        if bgp_params.get('auth_data') is None:
            return
        keys = bgp_params['auth_data'].get('key_items', [])
        if len(keys) > 0:
            bgp_config.set_authentication_key(keys[0].get('key'))
    # end add_bgp_auth_config

    def add_bgp_hold_time_config(self, bgp_config, bgp_params):
        if bgp_params.get('hold_time') is None:
            return
        bgp_config.set_hold_time(bgp_params.get('hold_time'))
    # end add_bgp_hold_time_config

    def set_bgp_config(self, params, bgp_obj):
        self.bgp_params = params
        self.bgp_obj = bgp_obj
    # end set_bgp_config

    def _get_bgp_config_xml(self, external=False):
        if self.bgp_params is None or not self.bgp_params.get('address'):
            return None
        bgp = Bgp()
        bgp.set_comment(DMUtils.bgp_group_comment(self.bgp_obj))
        if external:
            bgp.set_name(DMUtils.make_bgp_group_name(self.get_asn(), True))
            bgp.set_type('external')
        else:
            bgp.set_name(DMUtils.make_bgp_group_name(self.get_asn(), False))
            bgp.set_type('internal')
            self.add_ibgp_export_policy(self.bgp_params, bgp)
        bgp.set_ip_address(self.bgp_params['address'])
        bgp.set_autonomous_system(self.get_asn())
        self.add_families(bgp, self.bgp_params)
        self.add_bgp_auth_config(bgp, self.bgp_params)
        self.add_bgp_hold_time_config(bgp, self.bgp_params)
        return bgp
    # end _get_bgp_config_xml

    def add_bgp_peer(self, router, params, attr, external, peer):
        peer_data = {}
        peer_data['params'] = params
        peer_data['attr'] = attr
        peer_data['obj'] = peer
        if external:
            self.external_peers[router] = peer_data
        else:
            self.bgp_peers[router] = peer_data
    # end add_peer

    def _get_neighbor_config_xml(self, bgp_config, peers):
        for peer, peer_data in peers.items():
            obj = peer_data.get('obj')
            params = peer_data.get('params', {})
            attr = peer_data.get('attr', {})
            nbr = Bgp(name=peer, ip_address=peer)
            nbr.set_comment(DMUtils.bgp_group_comment(obj))
            bgp_config.add_peers(nbr)
            bgp_sessions = attr.get('session')
            if bgp_sessions:
                # for now assume only one session
                session_attrs = bgp_sessions[0].get('attributes', [])
                for session_attr in session_attrs:
                    # For not, only consider the attribute if bgp-router is
                    # not specified
                    if session_attr.get('bgp_router') is None:
                        self.add_families(nbr, session_attr)
                        self.add_bgp_auth_config(nbr, session_attr)
                        break
            peer_as = params.get('local_autonomous_system') or params.get('autonomous_system')
            nbr.set_autonomous_system(peer_as)
    # end _get_neighbor_config_xml

    def get_asn(self):
        return self.bgp_params.get('local_autonomous_system') or self.bgp_params.get('autonomous_system')
    # end get_asn

    def set_bgp_group_config(self):
        bgp_config = self._get_bgp_config_xml()
        if not bgp_config:
            return
        self.bgp_configs = self.bgp_configs or []
        self.bgp_configs.append(bgp_config)
        self._get_neighbor_config_xml(bgp_config, self.bgp_peers)
        if self.external_peers:
            ext_grp_config = self._get_bgp_config_xml(True)
            self.bgp_configs.append(ext_grp_config)
            self._get_neighbor_config_xml(ext_grp_config, self.external_peers)
        return
    # end set_bgp_group_config

    def build_bgp_config(self):
        bgp_router = BgpRouterDM.get(self.physical_router.bgp_router)
        if not bgp_router:
            return
        if bgp_router:
            for peer_uuid, attr in bgp_router.bgp_routers.items():
                peer = BgpRouterDM.get(peer_uuid)
                if not peer or not peer.params or not peer.params.get(
                        'address'):
                    continue
                local_as = (bgp_router.params.get('local_autonomous_system') or
                            bgp_router.params.get('autonomous_system'))
                peer_as = (peer.params.get('local_autonomous_system') or
                           peer.params.get('autonomous_system'))
                external = (local_as != peer_as)
                self.add_bgp_peer(peer.params['address'],
                                  peer.params, attr, external, peer)
            self.set_bgp_config(bgp_router.params, bgp_router)
            bgp_router_ips = bgp_router.get_all_bgp_router_ips()
            tunnel_ip = self.physical_router.dataplane_ip
            if not tunnel_ip and bgp_router.params:
                tunnel_ip = bgp_router.params.get('address')
            if tunnel_ip and self.physical_router.is_valid_ip(tunnel_ip):
                self.add_dynamic_tunnels(tunnel_ip,
                                         GlobalSystemConfigDM.ip_fabric_subnets,
                                         bgp_router_ips)

        self.set_bgp_group_config()
    # end build_bgp_config

    def ensure_bgp_config(self):
        if not self.physical_router.bgp_router:
            self._logger.info("bgp router not configured for pr: " + \
                                                 self.physical_router.name)
            return False
        bgp_router = BgpRouterDM.get(self.physical_router.bgp_router)
        if not bgp_router.params or not bgp_router.params.get("address"):
            self._logger.info("bgp router parameters not configured for pr: " + \
                                                 bgp_router.name)
            return False
        return True
    # end ensure_bgp_config

    def set_internal_vn_irb_config(self):
        if self.internal_vn_ris and self.irb_interfaces:
            for int_ri in self.internal_vn_ris:
                lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(int_ri.name)
                lr = LogicalRouterDM.get(lr_uuid)
                if not lr:
                    continue
                vn_list = lr.get_connected_networks(include_internal=False)
                for vn in vn_list:
                    vn_obj = VirtualNetworkDM.get(vn)
                    irb_name = "irb." + str(vn_obj.vn_network_id)
                    if irb_name in self.irb_interfaces:
                        int_ri.add_routing_interfaces(LogicalInterface(name=irb_name))
    # end set_internal_vn_irb_config

    def add_irb_config(self, ri_conf):
        vn = ri_conf.get("vn")
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        gateways = ri_conf.get("gateways", [])
        network_id = ri_conf.get("network_id", None)
        self.interfaces_config = self.interfaces_config or []
        irb_intf = PhysicalInterface(name='irb', interface_type='irb')
        self.interfaces_config.append(irb_intf)
        self._logger.info("Vn=" + vn.name + ", IRB: " + str(gateways) + ", pr="
                          + self.physical_router.name)
        if gateways is not None:
            intf_unit = LogicalInterface(
                name='irb.' + str(network_id), unit=network_id,
                comment=DMUtils.vn_irb_comment(vn, False, is_l2_l3))
            irb_intf.add_logical_interfaces(intf_unit)
            for (irb_ip, gateway) in gateways:
                ip = IpType(address=irb_ip,
                            comment=DMUtils.irb_ip_comment(irb_ip))
                intf_unit.add_ip_list(ip)
                if len(gateway) and gateway != '0.0.0.0':
                    intf_unit.set_gateway(gateway)
    # end add_irb_config

    # lo0 interface in RI for route lookup to happen for Inter VN traffic
    # qfx10k pfe limitation
    def add_bogus_lo0(self, ri, network_id, vn):
        self.interfaces_config = self.interfaces_config or []
        ifl_num = 1000 + int(network_id)
        lo_intf = PhysicalInterface(name="lo0", interface_type='loopback')
        self.interfaces_config.append(lo_intf)
        intf_unit = LogicalInterface(
            name="lo0." + str(ifl_num), unit=ifl_num,
            comment=DMUtils.l3_bogus_lo_intf_comment(vn))
        intf_unit.add_ip_list(IpType(address="127.0.0.1"))
        lo_intf.add_logical_interfaces(intf_unit)
        ri.add_loopback_interfaces(LogicalInterface(name="lo0." + str(ifl_num)))
    # end add_bogus_lo0

    def add_inet_public_vrf_filter(self, firewall_config, inet_type):
        firewall_config.set_family(inet_type)
        f = FirewallFilter(name=DMUtils.make_public_vrf_filter_name(inet_type))
        f.set_comment(DMUtils.public_vrf_filter_comment())
        firewall_config.add_firewall_filters(f)
        term = Term(name="default-term", then=Then(accept_or_reject=True))
        f.add_terms(term)
        return f
    # end add_inet_public_vrf_filter

    def add_inet_filter_term(self, ri_name, prefixes, inet_type):
        if inet_type == 'inet6':
            prefixes = DMUtils.get_ipv6_prefixes(prefixes)
        else:
            prefixes = DMUtils.get_ipv4_prefixes(prefixes)

        from_ = From()
        for prefix in prefixes:
            from_.add_destination_address(self.get_subnet_for_cidr(prefix))
        then_ = Then()
        then_.add_routing_instance(ri_name)
        return Term(name=DMUtils.make_vrf_term_name(ri_name),
                    fromxx=from_, then=then_)
    # end add_inet_filter_term

    def add_routing_instance(self, ri_conf):
        ri_name = ri_conf.get("ri_name")
        vn = ri_conf.get("vn")
        is_l2 = ri_conf.get("is_l2", False)
        is_l2_l3 = ri_conf.get("is_l2_l3", False)
        import_targets = ri_conf.get("import_targets", set())
        export_targets = ri_conf.get("export_targets", set())
        prefixes = ri_conf.get("prefixes", [])
        gateways = ri_conf.get("gateways", [])
        router_external = ri_conf.get("router_external", False)
        interfaces = ri_conf.get("interfaces", [])
        vni = ri_conf.get("vni", None)
        fip_map = ri_conf.get("fip_map", None)
        network_id = ri_conf.get("network_id", None)
        is_internal_vn = True if '_contrail_lr_internal_vn_' in vn.name else False
        highest_encapsulation_priority = \
            ri_conf.get("highest_encapsulation_priority") or "MPLSoGRE"

        self.routing_instances[ri_name] = ri_conf
        self.ri_config = self.ri_config or []
        self.policy_config = self.policy_config or\
                             Policy(comment=DMUtils.policy_options_comment())
        ri = RoutingInstance(name=ri_name)
        if vn:
            is_nat = True if fip_map else False
            ri.set_comment(DMUtils.vn_ri_comment(vn, is_l2, is_l2_l3, is_nat,
                                                 router_external))
        self.ri_config.append(ri)

        ri.set_virtual_network_id(str(network_id))
        ri.set_vxlan_id(str(vni))
        ri.set_virtual_network_is_internal(is_internal_vn)
        ri.set_is_public_network(router_external)
        if is_l2_l3:
            ri.set_virtual_network_mode('l2-l3')
        elif is_l2:
            ri.set_virtual_network_mode('l2')
        else:
            ri.set_virtual_network_mode('l3')

        has_ipv6_prefixes = DMUtils.has_ipv6_prefixes(prefixes)
        has_ipv4_prefixes = DMUtils.has_ipv4_prefixes(prefixes)

        if not is_l2:
            ri.set_routing_instance_type("vrf")
            if fip_map is None:
                for interface in interfaces:
                    ri.add_interfaces(LogicalInterface(name=interface.name))
                if prefixes:
                    for prefix in prefixes:
                        ri.add_static_routes(self.get_route_for_cidr(prefix))
                        ri.add_prefixes(self.get_subnet_for_cidr(prefix))
        else:
            if highest_encapsulation_priority == "VXLAN":
                ri.set_routing_instance_type("virtual-switch")
            elif highest_encapsulation_priority in ["MPLSoGRE", "MPLSoUDP"]:
                ri.set_routing_instance_type("evpn")

        if is_internal_vn:
            self.internal_vn_ris.append(ri)
            self.add_bogus_lo0(ri, network_id, vn)

        if is_l2_l3:
            self.add_irb_config(ri_conf)
            self.attach_irb(ri_conf, ri)

        if fip_map is not None:
            ri.add_interfaces(LogicalInterface(name=interfaces[0].name))

            public_vrf_ips = {}
            for pip in fip_map.values():
                if pip["vrf_name"] not in public_vrf_ips:
                    public_vrf_ips[pip["vrf_name"]] = set()
                public_vrf_ips[pip["vrf_name"]].add(pip["floating_ip"])

            for public_vrf, fips in public_vrf_ips.items():
                ri.add_interfaces(LogicalInterface(name=interfaces[1].name))
                floating_ips = []
                for fip in fips:
                    ri.add_static_routes(
                        Route(prefix=fip,
                              prefix_len=32,
                              next_hop=interfaces[1].name,
                              comment=DMUtils.fip_egress_comment()))
                    floating_ips.append(FloatingIpMap(floating_ip=fip + "/32"))
                ri.add_floating_ip_list(FloatingIpList(
                    public_routing_instance=public_vrf,
                    floating_ips=floating_ips))

        # add policies for export route targets
        p = PolicyRule(name=DMUtils.make_export_name(ri_name))
        p.set_comment(DMUtils.vn_ps_comment(vn, "Export"))
        then = Then()
        p.add_term(Term(name="t1", then=then))
        for route_target in export_targets:
            then.add_community(DMUtils.make_community_name(route_target))
        then.set_accept_or_reject(True)
        self.policy_config.add_policy_rule(p)

        # add policies for import route targets
        p = PolicyRule(name=DMUtils.make_import_name(ri_name))
        p.set_comment(DMUtils.vn_ps_comment(vn, "Import"))

        # add term switch policy
        from_ = From()
        term = Term(name=DMUtils.get_switch_policy_name(), fromxx=from_)
        p.add_term(term)
        from_.add_community(DMUtils.get_switch_policy_name())
        term.set_then(Then(accept_or_reject=True))

        from_ = From()
        term = Term(name="t1", fromxx=from_)
        p.add_term(term)
        for route_target in import_targets:
            from_.add_community(DMUtils.make_community_name(route_target))
            if not is_internal_vn:
                self.add_vni_option(vni or network_id, route_target)
        term.set_then(Then(accept_or_reject=True))
        self.policy_config.add_policy_rule(p)

        # add firewall config for public VRF
        if router_external and is_l2 is False:
            self.firewall_config = self.firewall_config or Firewall(
                comment=DMUtils.firewall_comment())
            if has_ipv4_prefixes and not self.inet4_forwarding_filter:
                # create single instance inet4 filter
                self.inet4_forwarding_filter = self.add_inet_public_vrf_filter(
                    self.firewall_config, "inet")
            if has_ipv6_prefixes and not self.inet6_forwarding_filter:
                # create single instance inet6 filter
                self.inet6_forwarding_filter = self.add_inet_public_vrf_filter(
                    self.firewall_config, "inet6")
            if has_ipv4_prefixes:
                # add terms to inet4 filter
                term = self.add_inet_filter_term(ri_name, prefixes, "inet4")
                # insert before the last term
                terms = self.inet4_forwarding_filter.get_terms()
                terms = [term] + (terms or [])
                self.inet4_forwarding_filter.set_terms(terms)
            if has_ipv6_prefixes:
                # add terms to inet6 filter
                term = self.add_inet_filter_term(ri_name, prefixes, "inet6")
                # insert before the last term
                terms = self.inet6_forwarding_filter.get_terms()
                terms = [term] + (terms or [])
                self.inet6_forwarding_filter.set_terms(terms)

        if fip_map is not None:
            self.firewall_config = self.firewall_config or Firewall(
                comment=DMUtils.firewall_comment())
            f = FirewallFilter(
                name=DMUtils.make_private_vrf_filter_name(ri_name))
            f.set_comment(DMUtils.vn_firewall_comment(vn, "private"))
            self.firewall_config.add_firewall_filters(f)

            term = Term(name=DMUtils.make_vrf_term_name(ri_name))
            from_ = From()
            for fip_user_ip in fip_map.keys():
                from_.add_source_address(fip_user_ip)
            term.set_from(from_)
            term.set_then(Then(routing_instance=[ri_name]))
            f.add_terms(term)

            irb_intf = PhysicalInterface(name='irb', interface_type='irb')
            self.interfaces_config.append(irb_intf)
            intf_unit = LogicalInterface(
                name="irb." + str(network_id), unit=network_id,
                comment=DMUtils.vn_irb_fip_inet_comment(vn))
            irb_intf.add_logical_interfaces(intf_unit)
            intf_unit.set_family("inet")
            intf_unit.add_firewall_filters(
                DMUtils.make_private_vrf_filter_name(ri_name))
            ri.add_routing_interfaces(intf_unit)

        if gateways is not None:
            for (ip, gateway) in gateways:
                ri.add_gateways(GatewayRoute(
                    ip_address=self.get_subnet_for_cidr(ip),
                    gateway=self.get_subnet_for_cidr(gateway)))

        # add L2 EVPN and BD config
        if (is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            vlan = None
            if highest_encapsulation_priority == "VXLAN":
                self.vlans_config = self.vlans_config or []
                vlan = Vlan(name=DMUtils.make_bridge_name(vni),
                            vxlan_id=vni, vlan_or_bridge_domain=False)
                vlan.set_comment(DMUtils.vn_bd_comment(vn, "VXLAN"))
                self.vlans_config.append(vlan)
                for interface in interfaces:
                    vlan.add_interfaces(LogicalInterface(name=interface.name))
                if is_l2_l3:
                    # network_id is unique, hence irb
                    irb_intf = "irb." + str(network_id)
                    vlan.set_vlan_id(vni)
                    vlan.add_interfaces(LogicalInterface(name=irb_intf))
            elif highest_encapsulation_priority in ["MPLSoGRE", "MPLSoUDP"]:
                self.evpn = Evpn(encapsulation=highest_encapsulation_priority)
                self.evpn.set_comment(
                    DMUtils.vn_evpn_comment(vn, highest_encapsulation_priority))
                for interface in interfaces:
                    self.evpn.add_interfaces(LogicalInterface(name=interface.name))

            self.interfaces_config = self.interfaces_config or []
            self.build_l2_evpn_interface_config(self.interfaces_config,
                                                interfaces, vn, vlan)

        if (not is_l2 and vni is not None and
                self.is_family_configured(self.bgp_params, "e-vpn")):
            self.evpn = self.build_evpn_config()
            # add vlans
            self.add_ri_vlan_config(ri, vni)

        if (not is_l2 and not is_l2_l3 and gateways):
            self.interfaces_config = self.interfaces_config or []
            ifl_num = 1000 + int(network_id)
            lo_intf = PhysicalInterface(name="lo0", interface_type='loopback')
            self.interfaces_config.append(lo_intf)
            intf_unit = LogicalInterface(name="lo0." + str(ifl_num),
                                         unit=ifl_num,
                                         comment=DMUtils.l3_lo_intf_comment(vn))
            lo_intf.add_logical_interfaces(intf_unit)
            for (lo_ip, _) in gateways:
                subnet = lo_ip
                (ip, _) = lo_ip.split('/')
                if ':' in lo_ip:
                    lo_ip = ip + '/' + '128'
                else:
                    lo_ip = ip + '/' + '32'
                intf_unit.add_ip_list(IpType(
                    address=lo_ip, comment=DMUtils.lo0_ip_comment(subnet)))
            ri.add_loopback_interfaces(LogicalInterface(
                name="lo0." + str(ifl_num),
                comment=DMUtils.lo0_ri_intf_comment(vn)))

        # fip services config
        if fip_map is not None:
            nat_rules = NatRules(allow_overlapping_nat_pools=True,
                                 name=DMUtils.make_services_set_name(ri_name),
                                 comment=DMUtils.service_set_comment(vn))
            ri.set_nat_rules(nat_rules)
            snat_rule = NatRule(
                name=DMUtils.make_snat_rule_name(ri_name),
                comment=DMUtils.service_set_nat_rule_comment(vn, "SNAT"),
                direction="input", translation_type="basic-nat44")
            snat_rule.set_comment(DMUtils.snat_rule_comment())
            nat_rules.add_rules(snat_rule)
            dnat_rule = NatRule(
                name=DMUtils.make_dnat_rule_name(ri_name),
                comment=DMUtils.service_set_nat_rule_comment(vn, "DNAT"),
                direction="output", translation_type="dnat-44")
            dnat_rule.set_comment(DMUtils.dnat_rule_comment())
            nat_rules.add_rules(dnat_rule)
            nat_rules.set_inside_interface(interfaces[0].name)
            nat_rules.set_outside_interface(interfaces[1].name)

            for pip, fip_vn in fip_map.items():
                fip = fip_vn["floating_ip"]
                # private ip
                snat_rule.add_source_addresses(self.get_subnet_for_cidr(pip))
                # public ip
                snat_rule.add_source_prefixes(self.get_subnet_for_cidr(fip))

                # public ip
                dnat_rule.add_destination_addresses(
                    self.get_subnet_for_cidr(fip))
                # private ip
                dnat_rule.add_destination_prefixes(
                    self.get_subnet_for_cidr(pip))

            intf_unit = LogicalInterface(
                name=interfaces[0].name,
                unit=interfaces[0].unit,
                comment=DMUtils.service_intf_comment("Ingress"))
            intf_unit.set_family("inet")
            ri.add_service_interfaces(intf_unit)

            intf_unit = LogicalInterface(
                name=interfaces[1].name,
                unit=interfaces[1].unit,
                comment=DMUtils.service_intf_comment("Egress"))
            intf_unit.set_family("inet")
            ri.add_service_interfaces(intf_unit)

        for target in import_targets:
            ri.add_import_targets(target)

        for target in export_targets:
            ri.add_export_targets(target)
    # end add_routing_instance

    def attach_acls(self, interface, unit):
        if not interface.li_uuid:
            return
        interface = LogicalInterfaceDM.find_by_name_or_uuid(interface.li_uuid)
        if not interface:
            return
        sg_list = interface.get_attached_sgs()
        filter_list = []
        for sg in sg_list:
            flist = self.get_configured_filters(sg)
            filter_list += flist
        if filter_list:
            for fname in filter_list:
                unit.add_firewall_filters(fname)
    # end attach_acls

    def build_l2_evpn_interface_config(self, interfaces_config, interfaces, vn,
                                       vlan_conf):
        ifd_map = {}
        for interface in interfaces:
            ifd_map.setdefault(interface.ifd_name, []).append(interface)

        for ifd_name, interface_list in ifd_map.items():
            intf = PhysicalInterface(name=ifd_name)
            interfaces_config.append(intf)
            if interface_list[0].is_untagged():
                if len(interface_list) > 1:
                    self._logger.error(
                        "invalid logical interfaces config for ifd %s" % (
                            ifd_name))
                    continue
                unit_name = ifd_name + "." + str(interface_list[0].unit)
                unit = LogicalInterface(
                    name=unit_name,
                    unit=interface_list[0].unit,
                    comment=DMUtils.l2_evpn_intf_unit_comment(vn, False),
                    is_tagged=False,
                    vlan_tag="4094")
                # attach acls
                self.attach_acls(interface_list[0], unit)
                intf.add_logical_interfaces(unit)
                if vlan_conf:
                    vlan_conf.add_interfaces(LogicalInterface(name=unit_name))
            else:
                for interface in interface_list:
                    unit_name = ifd_name + "." + str(interface.unit)
                    unit = LogicalInterface(
                        name=unit_name,
                        unit=interface.unit,
                        comment=DMUtils.l2_evpn_intf_unit_comment(
                            vn, True, interface.vlan_tag),
                        is_tagged=True,
                        vlan_tag=str(interface.vlan_tag))
                    # attach acls
                    self.attach_acls(interface, unit)
                    intf.add_logical_interfaces(unit)
                    if vlan_conf:
                        vlan_conf.add_interfaces(LogicalInterface(
                            name=unit_name))
    # end build_l2_evpn_interface_config

    def build_evpn_config(self):
        return Evpn(encapsulation='vxlan')
    # end build_evpn_config

    def init_evpn_config(self):
        if not self.routing_instances:
            # no vn config then no need to configure evpn
            return
        if self.evpn:
            # evpn init done
            return
        self.evpn = self.build_evpn_config()
    # end init_evpn_config

    def add_vni_option(self, vni, vrf_target):
        if not self.evpn:
            self.init_evpn_config()

    def init_global_switch_opts(self):
        if self.global_switch_options_config is None:
            self.global_switch_options_config = SwitchOptions(comment=DMUtils.switch_options_comment())
        self.global_switch_options_config.set_vtep_source_interface("lo0.0")
        if not self.routing_instances:
            # no vn config then no need to configure vrf target
            return
        self.global_switch_options_config.add_vrf_target(VniTarget(auto=''))
        switch_options_community = DMUtils.get_switch_vrf_import(self.get_asn())
        self.global_switch_options_config.add_vrf_target(VniTarget(community=switch_options_community))
        self.set_global_export_policy()
    # end init_global_switch_opts

    def set_global_export_policy(self):
        export_policy = DMUtils.get_switch_export_policy_name()
        ps = PolicyStatement(name=export_policy)
        ps.set_comment(DMUtils.switch_export_policy_comment())

        export_community = DMUtils.get_switch_export_community_name()
        then = Then()
        comm = Community(add='', community_name=export_community)
        then.add_community(comm)
        ps.add_term(Term(name="t1", then=then))

        if not self.policy_config:
            self.policy_config = PolicyOptions(comment=DMUtils.policy_options_comment())
        self.policy_config.add_policy_statement(ps)
        if not self.global_switch_options_config:
            self.global_switch_options_config = SwitchOptions(comment=DMUtils.switch_options_comment())
        self.global_switch_options_config.add_vrf_export(export_policy)
    # end set_global_export_policy

    def add_to_global_switch_opts(self, policy, is_import):
        if not self.global_switch_options_config:
            self.init_global_switch_opts()
        if is_import:
            self.global_switch_options_config.add_vrf_import(policy)
        else:
            self.global_switch_options_config.add_vrf_export(policy)
    # end add_to_global_switch_opts

    def set_route_targets_config(self):
        self.policy_config = self.policy_config or\
                             Policy(comment=DMUtils.policy_options_comment())
        # add export community
        export_comm = CommunityType(name=DMUtils.get_switch_export_community_name())
        for route_target in self.route_targets:
            comm = CommunityType(name=DMUtils.make_community_name(route_target))
            comm.add_members(route_target)
            self.policy_config.add_community(comm)
            # add route-targets to export community
            export_comm.add_members(route_target)
        # if no members, no need to add community
        if export_comm.get_members():
            self.policy_config.add_community(export_comm)
        # add community for switch options
        comm = CommunityType(name=DMUtils.get_switch_policy_name())
        comm.add_members(DMUtils.get_switch_vrf_import(self.get_asn()))
        self.policy_config.add_community(comm)
    # end set_route_targets_config

    def add_vlan_config(self, vrf_name, vni, is_l2_l3=False, irb_intf=None):
        self.vlans_config = self.vlans_config or []
        vlan = Vlan(name=vrf_name[1:], vxlan_id=vni)
        if is_l2_l3:
            if not irb_intf:
                self._logger.error("Missing irb interface config l3 vlan: %s" % vrf_name)
            else:
                vlan.set_vlan_id(vni)
                vlan.add_interfaces(LogicalInterface(name=irb_intf))
        self.vlans_config.append(vlan)
        return vlan
    # end add_vlan_config

    def add_ri_vlan_config(self, ri, vni):
        self.vlans_config = self.vlans_config or []
        self.vlans_config.append(Vlan(name=vrf_name[-1], vlan_id=vni, vxlan_id=vni))
    # end add_ri_vlan_config

    def set_route_distinguisher_config(self):
        if not self.routing_instances or not self.bgp_params.get('identifier'):
            # no vn config then no need to configure route distinguisher
            return
        if self.global_switch_options_config is None:
            self.global_switch_options_config = SwitchOptions(comment=DMUtils.switch_options_comment())
        self.global_switch_options_config.set_route_distinguisher(
                                 RouteDistinguisher(rd_type=self.bgp_params['identifier'] + ":1"))
    # end set_route_distinguisher_config

    def build_esi_config(self):
        pr = self.physical_router
        if not pr:
            return
        self.interfaces_config = self.interfaces_config or []
        for pi_uuid in pr.physical_interfaces:
            pi = PhysicalInterfaceDM.get(pi_uuid)
            if not pi or not pi.esi or pi.esi == "0" or pi.get_parent_ae_id():
                continue
            intf = PhysicalInterface(name=pi.name,
                                     ethernet_segment_identifier=pi.esi)
            self.interfaces_config.append(intf)
    # end build_esi_config

    def build_lag_config(self):
        pr = self.physical_router
        if not pr:
            return
        self.interfaces_config = self.interfaces_config or []
        for pi_uuid in pr.physical_interfaces:
            pi = PhysicalInterfaceDM.get(pi_uuid)
            if not pi or not pi.link_aggregation_group:
                continue
            lag = LinkAggrGroup(lacp_enabled=pi.lacp_enabled,
                                link_members=pi.link_members)
            intf = PhysicalInterface(name=pi.name,
                                     interface_type=pi.interface_type,
                                     link_aggregation_group=lag)
            self.interfaces_config.append(intf)
    # end build_lag_config

    def get_vn_li_map(self):
        pr = self.physical_router
        vn_list = []
        # get all logical router connected networks
        for lr_id in pr.logical_routers or []:
            lr = LogicalRouterDM.get(lr_id)
            if not lr:
                continue
            vn_list += lr.get_connected_networks(include_internal=True)

        vn_dict = {}
        for vn_id in vn_list:
            vn_dict[vn_id] = []

        for vn_id in pr.virtual_networks:
            vn_dict[vn_id] = []

        li_set = pr.logical_interfaces
        for pi_uuid in pr.physical_interfaces:
            pi = PhysicalInterfaceDM.get(pi_uuid)
            if pi is None:
                continue
            li_set |= pi.logical_interfaces
        for li_uuid in li_set:
            li = LogicalInterfaceDM.get(li_uuid)
            if li is None:
                continue
            vmi_id = li.virtual_machine_interface
            vmi = VirtualMachineInterfaceDM.get(vmi_id)
            if vmi is None:
                continue
            vn_id = vmi.virtual_network
            if li.physical_interface:
                pi = PhysicalInterfaceDM.get(li.physical_interface)
                ae_id = pi.get_parent_ae_id()
                if ae_id and li.physical_interface:
                    _, unit= li.name.split('.')
                    ae_name = "ae" + str(ae_id) + "." + unit
                    vn_dict.setdefault(vn_id, []).append(
                           JunosInterface(ae_name, li.li_type, li.vlan_tag))
                    continue
            vn_dict.setdefault(vn_id, []).append(
                JunosInterface(li.name, li.li_type, li.vlan_tag, li_uuid=li.uuid))
        return vn_dict
    # end

    def get_vn_associated_physical_interfaces(self):
        pr = self.physical_router
        li_set = set()
        pi_list = []
        for pi_uuid in pr.physical_interfaces:
            pi = PhysicalInterfaceDM.get(pi_uuid)
            if pi is None or not pi.esi or pi.esi == "0":
                continue
            if self.has_vmi(pi.logical_interfaces):
                pi_list.append(pi)
        return pi_list
    # end get_vn_associated_physical_interfaces

    def has_vmi(self, li_set):
        if not li_set:
            return False
        for li_uuid in li_set:
            li = LogicalInterfaceDM.get(li_uuid)
            if not li or not li.virtual_machine_interface \
                or not VirtualMachineInterfaceDM.get(li.virtual_machine_interface):
                continue
            return True
        return False
    # end has_vmi

    def get_ae_alloc_esi_map(self):
        pi_list = self.get_vn_associated_physical_interfaces()
        esi_map = {}
        for pi in pi_list:
            if not pi.name.startswith("ae") and pi.esi:
                esi_map.setdefault(pi.esi, []).append(pi)
        return esi_map
    # end get_ae_alloc_esi_map

    def is_l2_supported(self, vn):
        """ Check l2 capability """
        return True
    # end is_l2_supported
    def is_l3_supported(self, vn):
        """ Check l3 capability """
        return '_lr_internal_vn_' in vn.name
    # end is_l3_supported

    def set_resolve_bgp_route_target_family_config(self):
        """ configure resolution config in global routing options if needed """
        if not self.global_routing_options_config:
            self.global_routing_options_config = RoutingOptions(
                                       comment=DMUtils.routing_options_comment())
        resolve = Resolution(rib=RIB(name="bgp.rtarget.0",
                                       resolution_ribs="inet.0"))
        self.global_routing_options_config.set_resolution(resolve)
    # end set_resolve_bgp_route_target_family_config

    def set_chassis_config(self):
        device_count =  DMUtils.get_max_ae_device_count()
        aggr_devices = AggregatedDevices(Ethernet(device_count=device_count))
        if not self.chassis_config:
            self.chassis_config = Chassis()
        self.chassis_config.set_aggregated_devices(aggr_devices)
    # end set_chassis_config

    def build_ae_config(self, esi_map):
        self.interfaces_config = self.interfaces_config or []
        # self.ae_id_map should have all esi => ae_id mapping
        # esi_map should have esi => interface memberships
        for esi, ae_id in self.physical_router.ae_id_map.items():
            # config ae interface
            ae_name = "ae" + str(ae_id)
            # associate 'ae' membership
            pi_list = esi_map.get(esi)
            link_members = []
            for pi in pi_list or []:
                 link_members.append(pi.name)

            lag = LinkAggrGroup(link_members=link_members)
            intf = PhysicalInterface(name=ae_name,
                                     ethernet_segment_identifier=esi,
                                     link_aggregation_group=lag)
            self.interfaces_config.append(intf)
    # end build_ae_config

    def add_addr_term(self, ff, addr_match, is_src):
        if not addr_match:
            return None
        subnet = addr_match.get_subnet()
        if not subnet:
            return None
        subnet_ip = subnet.get_ip_prefix()
        subnet_len = subnet.get_ip_prefix_len()
        if not subnet_ip or not subnet_len:
            return None
        term = Term()
        from_ = From()
        term.set_from(from_)
        if is_src:
            term.set_name("src-addr-prefix")
            src_prefix = Subnet(prefix=str(subnet_ip),
                                prefix_len=int(subnet_len))
            from_.add_source_prefix_list(src_prefix)
        else:
            term.set_name("dst-addr-prefix")
            dst_prefix = Subnet(prefix=str(subnet_ip),
                                prefix_len=int(subnet_len))
            from_.add_destination_prefix_list(dst_prefix)
        term.set_then(Then(accept=''))
        ff.add_term(term)
    # end add_addr_term

    def add_port_term(self, ff, port_match, is_src):
        if not port_match:
            return None
        start_port = port_match.get_start_port()
        end_port = port_match.get_end_port()
        if not start_port or not end_port:
            return None
        port_str = str(start_port) + "-" + str(end_port)
        term = Term()
        from_ = From()
        term.set_from(from_)
        if is_src:
            term.set_name("src-port")
            from_.add_source_ports(port_str)
        else:
            term.set_name("dst-port")
            from_.add_destination_ports(port_str)
        term.set_then(Then(accept=''))
        ff.add_term(term)
    # end add_port_term

    def add_protocol_term(self, ff, protocol_match):
        if not protocol_match or protocol_match == 'any':
            return None
        term = Term()
        from_ = From()
        term.set_from(from_)
        term.set_name("protocol")
        from_.set_ip_protocol(protocol_match)
        term.set_then(Then(accept=''))
        ff.add_term(term)
    # end add_protocol_term

    def add_dns_dhcp_terms(self, ff):
        port_list = [67, 68, 53]
        term = Term()
        term.set_name("allow-dns-dhcp")
        from_ = From()
        from_.set_ip_protocol("udp")
        term.set_from(from_)
        for port in port_list:
            from_.add_source_ports(str(port))
        term.set_then(Then(accept=''))
        ff.add_term(term)
    # end add_dns_dhcp_terms

    def add_ether_type_term(self, ff, ether_type_match):
        if not ether_type_match:
            return None
        term = Term()
        from_ = From()
        term.set_from(from_)
        term.set_name("ether-type")
        from_.set_ether_type(ether_type_match.lower())
        term.set_then(Then(accept=''))
        ff.add_term(term)
    # end add_ether_type_term

    def build_firewall_filters(self, sg, acl, is_egress=False):
        if not sg or not acl or not acl.vnc_obj:
            return
        acl = acl.vnc_obj
        entries = acl.get_access_control_list_entries()
        if not entries:
            return
        rules = entries.get_acl_rule() or []
        if not rules:
            return
        self.firewall_config = self.firewall_config or\
                               Firewall(DMUtils.firewall_comment())
        for rule in rules:
            if not self.has_terms(rule):
                continue
            match = rule.get_match_condition()
            if not match:
                continue
            rule_uuid = rule.get_rule_uuid()
            dst_addr_match = match.get_dst_address()
            dst_port_match = match.get_dst_port()
            ether_type_match = match.get_ethertype()
            protocol_match = match.get_protocol()
            src_addr_match = match.get_src_address()
            src_port_match = match.get_src_port()
            filter_name = DMUtils.make_sg_filter_name(sg.name, ether_type_match, rule_uuid)
            f = FirewallFilter(name=filter_name)
            f.set_comment(DMUtils.sg_firewall_comment(sg.name, ether_type_match, rule_uuid))
            self.add_addr_term(f, dst_addr_match, False)
            self.add_addr_term(f, src_addr_match, True)
            self.add_port_term(f, dst_port_match, False)
            self.add_port_term(f, src_port_match, True)
            # allow arp ether type always
            self.add_ether_type_term(f, 'arp')
            self.add_protocol_term(f, protocol_match)
            # allow dhcp/dns always
            self.add_dns_dhcp_terms(f)
            self.firewall_config.add_firewall_filters(f)
    # end build_firewall_filters

    def build_firewall_config(self):
        sg_list = LogicalInterfaceDM.get_sg_list()
        for sg in sg_list or []:
            acls = sg.access_control_lists
            for acl in acls or []:
                acl = AccessControlListDM.get(acl)
                if acl and acl.is_ingress:
                    self.build_firewall_filters(sg, acl)
    # end build_firewall_config

    def has_terms(self, rule):
        match = rule.get_match_condition()
        if not match or match.get_protocol() == 'any':
            return False
        return match.get_dst_address() or match.get_dst_port() or \
              match.get_ethertype() or match.get_src_address() or match.get_src_port()

    def get_firewall_filters(self, sg, acl, is_egress=False):
        if not sg or not acl or not acl.vnc_obj:
            return []
        acl = acl.vnc_obj
        entries = acl.get_access_control_list_entries()
        if not entries:
            return []
        rules = entries.get_acl_rule() or []
        if not rules:
            return []
        filter_names = []
        for rule in rules:
            if not self.has_terms(rule):
                continue
            match = rule.get_match_condition()
            if not match:
                continue
            rule_uuid = rule.get_rule_uuid()
            ether_type_match = match.get_ethertype()
            if not ether_type_match:
                continue
            if 'ipv6' in ether_type_match.lower():
                continue
            filter_name = DMUtils.make_sg_filter_name(sg.name, ether_type_match, rule_uuid)
            filter_names.append(filter_name)
        return filter_names
    # end get_firewall_filters

    def get_configured_filters(self, sg):
        if not sg:
            return []
        filter_names = []
        acls = sg.access_control_lists
        for acl in acls or []:
            acl = AccessControlListDM.get(acl)
            if acl and acl.is_ingress:
                fnames = self.get_firewall_filters(sg, acl)
                filter_names += fnames
        return filter_names
    # end get_configured_filters

    def build_ri_config(self):
        esi_map = self.get_ae_alloc_esi_map()
        self.physical_router.evaluate_ae_id_map(esi_map)
        self.build_ae_config(esi_map)
        vn_dict = self.get_vn_li_map()
        vn_irb_ip_map = None
        self.physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l2_l3', 'irb', False)
        self.physical_router.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l3', 'lo0', True)
        vn_irb_ip_map = self.physical_router.get_vn_irb_ip_map()

        for vn_id, interfaces in vn_dict.items():
            vn_obj = VirtualNetworkDM.get(vn_id)
            if (vn_obj is None or
                    vn_obj.get_vxlan_vni() is None or
                    vn_obj.vn_network_id is None):
                continue

            export_set = None
            import_set = None
            for ri_id in vn_obj.routing_instances:
                # Find the primary RI by matching the name
                ri_obj = RoutingInstanceDM.get(ri_id)
                if ri_obj is None:
                    continue
                if ri_obj.fq_name[-1] == vn_obj.fq_name[-1]:
                    vrf_name_l2 = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                   vn_obj.vn_network_id, 'l2')
                    vrf_name_l3 = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                   vn_obj.vn_network_id, 'l3')
                    export_set = copy.copy(ri_obj.export_targets)
                    import_set = copy.copy(ri_obj.import_targets)
                    for ri2_id in ri_obj.routing_instances:
                        ri2 = RoutingInstanceDM.get(ri2_id)
                        if ri2 is None:
                            continue
                        import_set |= ri2.export_targets

                    if vn_obj.get_forwarding_mode() in ['l2', 'l2_l3']:
                        irb_ips = None
                        if vn_obj.get_forwarding_mode() == 'l2_l3':
                            irb_ips = vn_irb_ip_map['irb'].get(vn_id, [])

                        ri_conf = {'ri_name': vrf_name_l2, 'vn': vn_obj,
                                   'is_l2': True, 'is_l2_l3': (
                                        vn_obj.get_forwarding_mode() == 'l2_l3'),
                                   'import_targets': import_set,
                                   'export_targets': export_set,
                                   'prefixes': vn_obj.get_prefixes(),
                                   'gateways': irb_ips,
                                   'router_external': vn_obj.router_external,
                                   'interfaces': interfaces,
                                   'vni': vn_obj.get_vxlan_vni(),
                                   'network_id': vn_obj.vn_network_id,
                                   'highest_encapsulation_priority':
                                       GlobalVRouterConfigDM.
                                           global_encapsulation_priority}
                        self.add_routing_instance(ri_conf)

                    if vn_obj.get_forwarding_mode() in ['l3', 'l2_l3']:
                        interfaces = []
                        lo0_ips = None
                        if vn_obj.get_forwarding_mode() == 'l2_l3':
                            interfaces = [
                                 JunosInterface(
                                'irb.' + str(vn_obj.vn_network_id),
                                'l3', 0)]
                        lo0_ips = vn_irb_ip_map['lo0'].get(vn_id, [])
                        ri_conf = {'ri_name': vrf_name_l3, 'vn': vn_obj,
                                   'is_l2': False,
                                   'is_l2_l3': vn_obj.get_forwarding_mode() ==
                                               'l2_l3',
                                   'import_targets': import_set,
                                   'export_targets': export_set,
                                   'prefixes': vn_obj.get_prefixes(),
                                   'router_external': vn_obj.router_external,
                                   'interfaces': interfaces,
                                   'gateways': lo0_ips,
                                   'network_id': vn_obj.vn_network_id}
                        self.add_routing_instance(ri_conf)
                    break

            if export_set and\
                    self.physical_router.is_junos_service_ports_enabled() and\
                    len(vn_obj.instance_ip_map) > 0:
                service_port_ids = DMUtils.get_service_ports(
                    vn_obj.vn_network_id)
                if not self.physical_router \
                        .is_service_port_id_valid(service_port_ids[0]):
                    self._logger.error("DM can't allocate service interfaces"
                                       " for (vn, vn-id)=(%s,%s)" %
                                       (vn_obj.fq_name,
                                        vn_obj.vn_network_id))
                else:
                    vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                     vn_obj.vn_network_id,
                                                     'l3', True)
                    interfaces = []
                    service_ports = self.physical_router.junos_service_ports.\
                        get('service_port')
                    interfaces.append(
                        JunosInterface(
                            service_ports[0] + "." + str(service_port_ids[0]),
                            'l3', 0))
                    interfaces.append(
                        JunosInterface(
                            service_ports[0] + "." + str(service_port_ids[1]),
                            'l3', 0))
                    ri_conf = {'ri_name': vrf_name, 'vn': vn_obj,
                               'import_targets': import_set,
                               'interfaces': interfaces,
                               'fip_map': vn_obj.instance_ip_map,
                               'network_id': vn_obj.vn_network_id,
                               'restrict_proxy_arp': vn_obj.router_external}
                    self.add_routing_instance(ri_conf)
        return
    # end build_ri_config

    def set_common_config(self):
        self.build_underlay_bgp()
        if not self.ensure_bgp_config():
            return
        self.build_bgp_config()
        self.build_ri_config()
        self.set_internal_vn_irb_config()
        self.init_evpn_config()
        self.build_firewall_config()
        self.build_esi_config()
        self.build_lag_config()
        self.set_route_targets_config()
    # end set_common_config

    def push_conf(self, is_delete=False):
        if not self.physical_router:
            return 0
        if is_delete:
            return self.send_conf(is_delete=True)
        self.set_common_config()
        return self.send_conf()
    # end push_conf

    @staticmethod
    def get_subnet_for_cidr(cidr):
        cidr_parts = cidr.split('/', 1)
        return Subnet(prefix=cidr_parts[0],
                      prefix_len=int(cidr_parts[1]) if len(cidr_parts) > 1
                      else 32)
    # end get_subnet_for_cidr


    @staticmethod
    def get_route_for_cidr(cidr):
        cidr_parts = cidr.split('/', 1)
        return Route(prefix=cidr_parts[0],
                     prefix_len=int(cidr_parts[1]) if len(cidr_parts) > 1
                     else 32)
    # end get_route_for_cidr

    @staticmethod
    def do_export(obj):
        # ignore None and empty list
        return obj is not None and obj != []
    # end do_export

    @staticmethod
    def export_dict(obj):
        if obj is None:
            return None
        obj_json = json.dumps(obj, default=lambda o: dict(
            (k, v) for k, v in o.__dict__.iteritems()
            if AnsibleConf.do_export(v)))
        return json.loads(obj_json)
    # end export_dict

# end AnsibleConf

class JunosInterface(object):

    def __init__(self, if_name, if_type, if_vlan_tag=0, if_ip=None, li_uuid=None):
        self.li_uuid = li_uuid
        self.name = if_name
        self.if_type = if_type
        self.vlan_tag = if_vlan_tag
        ifparts = if_name.split('.')
        self.ifd_name = ifparts[0]
        self.unit = ifparts[1]
        self.ip = if_ip
    # end __init__

    def is_untagged(self):
        if not self.vlan_tag:
            return True
        return False
    # end is_untagged

# end JunosInterface
