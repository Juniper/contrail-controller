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
from dm_utils import DMUtils
from ansible_base import AnsibleBase
from dm_utils import PushConfigState
from db import *
from abstract_device_api.abstract_device_xsd import *
from device_manager import DeviceManager
from job_handler import JobHandler


class AnsibleConf(AnsibleBase):
    @classmethod
    def register(cls, plugin_info):
        # plugin_info.update(common_params)
        return super(AnsibleConf, cls).register(plugin_info)
    # end register

    def __init__(self, logger, params={}):
        self.physical_router = params.get("physical_router")
        super(AnsibleConf, self).__init__(logger)
    # end __init__

    def plugin_init(self):
        # build and push underlay config, onetime job
        self.underlay_config()
        self.plugin_init_done =\
            self.push_config_state == PushConfigState.PUSH_STATE_SUCCESS
    # end plugin_init

    @abc.abstractmethod
    def underlay_config(self):
        # abstract method
        pass
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
    # end initialize

    def update_system_config(self):
        self.system_config.set_name(self.physical_router.name)
        self.system_config.set_uuid(self.physical_router.uuid)
        self.system_config.set_vendor_name(self.physical_router.vendor)
        self.system_config.set_product_name(self.physical_router.product)
        self.system_config.set_device_family(self.physical_router.device_family)
        self.system_config.set_management_ip(self.physical_router.management_ip)
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

    def fetch_pi_li_iip(self, pi_uuid):
        li_obj = None
        iip_obj = None
        pi_obj = PhysicalInterfaceDM.get(pi_uuid)
        if pi_obj is None:
            self._logger.error("unable to read physical interface %s" %
                               pi_uuid)
        elif len(pi_obj.logical_interfaces) > 0:
            # Read unit 0 only
            li_uuid = next(iter(pi_obj.logical_interfaces))
            li_obj = LogicalInterfaceDM.get(li_uuid)
            if li_obj is None:
                self._logger.error("unable to read logical interface %s" %
                                   li_uuid)
            elif li_obj.instance_ip is not None:
                iip_obj = InstanceIpDM.get(li_obj.instance_ip)
                if iip_obj is None:
                    self._logger.error("unable to read instance ip %s" %
                                       li_obj.instance_ip)
        return pi_obj, li_obj, iip_obj
    # end fetch_pi_li_iip

    def build_underlay_bgp(self):
        self.bgp_configs = self.bgp_configs or []
        self.interfaces_config = self.interfaces_config or []

        if self.physical_router.allocated_asn is None:
            self._logger.error("physical router %s(%s) does not have asn"
                               " allocated" % (self.physical_router.name,
                                               self.physical_router.uuid))
            return

        for pi_uuid in self.physical_router.physical_interfaces:
            pi_obj, li_obj, iip_obj = self.fetch_pi_li_iip(pi_uuid)
            if pi_obj and li_obj and iip_obj and iip_obj.instance_ip_address:
                self._logger.debug("looking for peers for physical"
                                   " interface %s(%s)" % (pi_obj.name, pi_uuid))
                # Add this bgp object only if it has a peer
                bgp = Bgp(ip_address=iip_obj.instance_ip_address,
                          autonomous_system=self.physical_router.allocated_asn,
                          comment=DMUtils.ip_clos_comment())
                # Assumption: PIs are connected for IP-CLOS peering only
                for peer_pi_uuid in pi_obj.physical_interfaces:
                    peer_pi_obj, peer_li_obj, peer_iip_obj =\
                        self.fetch_pi_li_iip(peer_pi_uuid)
                    if peer_pi_obj and peer_li_obj and peer_iip_obj and\
                            peer_iip_obj.instance_ip_address:

                        peer_pr = PhysicalRouterDM.get(
                            peer_pi_obj.physical_router)
                        if peer_pr is None:
                            self._logger.error(
                                "unable to read peer physical router %s"
                                % pi_uuid)
                        elif peer_pr.allocated_asn is None:
                            self._logger.error(
                                "peer physical router %s does not have"
                                " asn allocated" % pi_uuid)
                        elif peer_pr != self.physical_router:
                            if bgp not in self.bgp_configs:
                                self.bgp_configs.append(bgp)

                            pi = PhysicalInterface(
                                uuid=pi_uuid, name=pi_obj.name,
                                comment=DMUtils.ip_clos_comment())
                            li = LogicalInterface(
                                uuid=li_obj.uuid, name=li_obj.name, unit=0,
                                comment=DMUtils.ip_clos_comment())
                            li.add_ip_list(
                                IpType(address=iip_obj.instance_ip_address))
                            pi.add_logical_interfaces(li)
                            self.interfaces_config.append(pi)

                            peer = Bgp(
                                ip_address=peer_iip_obj.instance_ip_address,
                                autonomous_system=peer_pr.allocated_asn,
                                comment=peer_pr.name)
                            bgp.add_peers(peer)
    # end build_bgp_config

    def device_send(self, job_template, job_input, retry):
        config_str = json.dumps(job_input)
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        start_time = None
        config_size = 0
        try:
            self._logger.debug("playbook send message: %s" % config_str)
            config_size = len(config_str)
            device_manager = DeviceManager.get_instance()
            job_handler = JobHandler(job_template, job_input,
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

    def get_vn_li_map(self):
        pr = self.physical_router
        vn_dict = {}
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
            vn_dict.setdefault(vn_id, []).append(
                JunosInterface(li.name, li.li_type, li.vlan_tag, li_uuid=li.uuid))
        return vn_dict
    # end

    def add_product_specific_config(self, groups):
        # override this method to add any product specific configurations
        pass
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
        if is_delete:
            return device
        device.set_comment(DMUtils.groups_comment())
        device.set_system(self.system_config)
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
            return
        config = self.prepare_conf()
        feature_params, job_template = self.read_feature_configs()
        job_input = {
            'device_abstract_config': self.export_dict(config),
            'additional_feature_params': feature_params
        }
        self.device_send(job_template, job_input, retry)
    # end send_conf

    def add_lo0_unit_0_interface(self, loopback_ip=''):
        if not loopback_ip:
            return
        if not self.interfaces_config:
            self.interfaces_config = []
        lo_intf = PhysicalInterface(name="lo0", interface_type="loopback")
        self.interfaces_config.append(lo_intf)
        li = LogicalInterface(name="lo0", unit=0,
                              comment=DMUtils.lo0_unit_0_comment(),
                              family="inet")
        li.add_ip_list(IpType(address=loopback_ip))
        lo_intf.add_logical_interfaces(li)
    # end add_lo0_unit_0_interface

    def add_dynamic_tunnels(self, tunnel_source_ip,
                            ip_fabric_nets, bgp_router_ips):
        if not self.system_config:
            self.system_config = System()
        self.system_config.set_tunnel_ip(tunnel_source_ip)
        dynamic_tunnel = DynamicTunnel(
            name=DMUtils.dynamic_tunnel_name(self.get_asn()),
            source_address=tunnel_source_ip, gre='')
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
            nbr = Bgp(name=peer)
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
        if not self.bgp_configs:
            self.bgp_configs = []
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
            if (tunnel_ip and self.physical_router.is_valid_ip(tunnel_ip)):
                self.add_dynamic_tunnels(
                    tunnel_ip,
                    GlobalSystemConfigDM.ip_fabric_subnets,
                    bgp_router_ips)

        if self.physical_router.loopback_ip:
            self.add_lo0_unit_0_interface(self.physical_router.loopback_ip)
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

    @staticmethod
    def do_export(obj):
        # ignore None and empty list
        return obj is not None and obj != []
    # end do_export

    @staticmethod
    def export_dict(obj):
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
