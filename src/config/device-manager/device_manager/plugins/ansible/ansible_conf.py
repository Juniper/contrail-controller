#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains generic plugin implementation device configuration using ansible
"""

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
    @classmethod
    def register(cls, plugin_info):
        # plugin_info.update(common_params)
        return super(AnsibleConf, cls).register(plugin_info)
    # end register

    def __init__(self, logger, params={}):
        self.last_config_hash = None
        self.physical_router = params.get("physical_router")
        super(AnsibleConf, self).__init__(logger)
    # end __init__

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

    def push_in_progress(self):
        return self.push_config_state == PushConfigState.PUSH_STATE_IN_PROGRESS
    # end push_in_progress

    def is_connected(self):
        return True
    # end is_connected

    def initialize(self):
        super(AnsibleConf, self).initialize()
        self.evpn = None
        self.bgp_map = {}
        self.ri_map = {}
        self.pi_map = {}
        self.firewall_config = None
        self.inet4_forwarding_filter = None
        self.inet6_forwarding_filter = None
        self.vlan_map = {}
        self.bgp_peers = {}
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
        self.system_config.set_encapsulation_priorities(
            GlobalVRouterConfigDM.global_encapsulation_priorities)
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
                if loopback_ip == self.physical_router.loopback_ip:
                    already_added = True
                    break
            if not already_added:
                self.system_config.add_loopback_ip_list(
                    self.physical_router.loopback_ip)
    # end update_system_config

    def set_default_pi(self, name, interface_type=None):
        if name in self.pi_map:
            pi, li_map = self.pi_map[name]
            if interface_type:
                pi.set_interface_type(interface_type)
        else:
            pi = PhysicalInterface(name=name, interface_type=interface_type)
            li_map = {}
            self.pi_map[name] = (pi, li_map)
        return pi, li_map
    # end set_default_pi

    def set_default_li(self, li_map, name, unit):
        if name in li_map:
            li = li_map[name]
        else:
            li = LogicalInterface(name=name, unit=unit)
            li_map[name] = li
        return li
    # end set_default_li

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
        if self.physical_router.allocated_asn is None:
            self._logger.error("physical router %s(%s) does not have asn"
                               " allocated" % (self.physical_router.name,
                                               self.physical_router.uuid))
            return

        for pi_obj, li_obj, iip_obj in self.\
                fetch_pi_li_iip(self.physical_router.physical_interfaces):
            if pi_obj and li_obj and iip_obj and iip_obj.instance_ip_address:
                pi, li_map = self.set_default_pi(pi_obj.name, 'regular')
                pi.set_comment(DMUtils.ip_clos_comment())

                li = self.set_default_li(li_map, li_obj.name,
                                         int(li_obj.name.split('.')[-1]))
                li.set_comment(DMUtils.ip_clos_comment())

                self.add_ip_address(li, iip_obj.instance_ip_address)

                self._logger.debug("looking for peers for physical"
                                   " interface %s(%s)" % (pi_obj.name,
                                                          pi_obj.uuid))
                # Add this bgp object only if it has a peer
                underlay_asn = self.physical_router.allocated_asn
                bgp_name = DMUtils.make_underlay_bgp_group_name(underlay_asn,
                    li_obj.name, is_external=True)
                bgp = Bgp(name=bgp_name,
                          ip_address=iip_obj.instance_ip_address,
                          autonomous_system=underlay_asn,
                          type_='external',
                          comment=DMUtils.ip_clos_comment())
                peers = {}
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
                            peer = Bgp(name=peer_pr.name,
                                       ip_address=peer_iip_obj.instance_ip_address,
                                       autonomous_system=peer_pr.allocated_asn,
                                       comment=peer_pr.name)
                            peers[peer_pr.name] = peer

                if peers:
                    bgp.set_peers(self.get_values_sorted_by_key(peers))
                    self.bgp_map[bgp_name] = bgp
    # end build_underlay_bgp

    def build_dci_bgp_config(self):
        if not self.physical_router.has_rb_role("DCI-Gateway"):
            return
        self.physical_router.evaluate_dci_ip_map()
        dci_list = self.physical_router.get_dci_list()
        for dci_uuid in dci_list or []:
            params = self.physical_router.get_dci_bgp_params(dci_uuid)
            if not params:
                continue
            asn = params.get("asn")
            ip = params.get("ip")
            name = params.get("name")
            typ = params.get("type")
            bgp = Bgp(name=name,
                          ip_address=ip,
                          autonomous_system=asn,
                          type_=typ)
            bgp.set_comment(DMUtils.dci_bgp_group_comment(bgp))
            bgp.set_hold_time(params.get('hold_time'))
            for family in params.get("families") or []:
                if family in ['e-vpn', 'e_vpn']:
                    family = 'evpn'
                bgp.add_families(family)

            bgp_name = name
            neigh_list = self.physical_router.get_dci_bgp_neighbours(dci_uuid)
            peers = {}
            for neigh in neigh_list or []:
                asn = neigh.get("asn")
                ip = neigh.get("ip")
                name = neigh.get("name")
                typ = neigh.get("type")
                peer = Bgp(name=name,
                           ip_address=ip,
                           autonomous_system=asn, type_=typ)
                peers[name] = peer
                peer.set_hold_time(neigh.get('hold_time'))
                for family in neigh.get("families") or []:
                    if family in ['e-vpn', 'e_vpn']:
                        family = 'evpn'
                    peer.add_families(family)

            if peers:
                bgp.set_peers(self.get_values_sorted_by_key(peers))
                self.bgp_map[bgp_name] = bgp
    # end build_dci_bgp_config

    def device_send(self, job_template, job_input, is_delete, retry):
        config_str = json.dumps(job_input, sort_keys=True)
        self.push_config_state = PushConfigState.PUSH_STATE_IN_PROGRESS
        start_time = None
        config_size = 0
        try:
            config_size = len(config_str)
            current_config_hash = md5(config_str).hexdigest()
            if self.last_config_hash is None or\
                    current_config_hash != self.last_config_hash:
                self._logger.info("Config push for %s(%s) using job template %s" %
                          (self.physical_router.name, self.physical_router.uuid,
                           str(job_template)))
                self._logger.debug("Abstract config: %s" %
                                   json.dumps(job_input, indent=4,
                                              sort_keys=True))
                device_manager = DeviceManager.get_instance()
                job_handler = JobHandler(job_template, job_input,
                                         None if is_delete else
                                         [self.physical_router.uuid],
                                         device_manager.get_api_server_config(),
                                         self._logger,
                                         device_manager._amqp_client,
                                         device_manager._args)
                self.commit_stats['total_commits_sent_since_up'] += 1
                start_time = time.time()
                job_handler.push(**device_manager.get_job_status_config())
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
                               (self.physical_router.management_ip, repr(e)))
            self._logger.error("Abstract config: %s" %
                               json.dumps(job_input, indent=4,
                                          sort_keys=True))
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

    def read_node_profile_info(self):
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

    @staticmethod
    def add_to_list(lst, value):
        if value not in lst: lst.append(value)
    # end add_to_list

    @staticmethod
    def add_ref_to_list(lst, value):
        if not any(v.get_name() == value for v in lst):
            lst.append(Reference(name=value))
    # end add_ref_to_list

    @classmethod
    def add_ip_address(cls, unit, address, gateway=None):
        cls.add_to_list(unit.get_ip_list(), address)
        if ':' in address:
            family = 'inet6'
        else:
            family = 'inet'
        ip_address = IpAddress(address=address, family=family, gateway=gateway)
        cls.add_to_list(unit.get_ip_addresses(), ip_address)
    # end add_ref_to_list

    @staticmethod
    def get_values_sorted_by_key(dict_obj):
        return [dict_obj[key] for key in sorted(dict_obj.keys())]
    # end get_values_sorted_by_key

    @staticmethod
    def get_sorted_key_value_pairs(dict_obj):
        return [(key, dict_obj[key]) for key in sorted(dict_obj.keys())]
    # end get_values_sorted_by_key

    def prepare_conf(self, is_delete=False):
        device = Device()
        device.set_comment(DMUtils.groups_comment())
        device.set_system(self.system_config)
        if is_delete:
            return device

        device.set_evpn(self.evpn)
        device.set_bgp(self.get_values_sorted_by_key(self.bgp_map))

        pis = []
        for pi, li_map in self.get_values_sorted_by_key(self.pi_map):
            pi.set_logical_interfaces(self.get_values_sorted_by_key(li_map))
            pis.append(pi)

        device.set_physical_interfaces(pis)
        device.set_routing_instances(self.get_values_sorted_by_key(self.ri_map))
        device.set_vlans(self.get_values_sorted_by_key(self.vlan_map))
        device.set_firewall(self.firewall_config)
        return device
    # end prepare_conf

    def has_conf(self):
        return self.system_config or self.bgp_map or self.pi_map
    # end has_conf

    def send_conf(self, is_delete=False, retry=True):
        if not self.has_conf() and not is_delete:
            return 0
        config = self.prepare_conf(is_delete=is_delete)
        feature_configs, job_template = self.read_node_profile_info()
        job_input = {
            'fabric_uuid': self.physical_router.fabric,
            'device_management_ip': self.physical_router.management_ip,
            'additional_feature_params': feature_configs,
            'device_abstract_config': self.export_dict(config),
            'is_delete': is_delete,
            # TODO
            # Need to support brownfield underlay config
            #'is_ztp': self.physical_router.is_ztp()
            'is_ztp': True
        }
        return self.device_send(job_template, job_input, is_delete, retry)
    # end send_conf

    def add_dynamic_tunnels(self, tunnel_source_ip, ip_fabric_nets):
        if not self.system_config:
            self.system_config = System()
        self.system_config.set_tunnel_ip(tunnel_source_ip)
        if ip_fabric_nets is not None:
            for subnet in ip_fabric_nets.get("subnet", []):
                dest_net = Subnet(prefix=subnet['ip_prefix'],
                                  prefix_len=subnet['ip_prefix_len'])
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

    def get_bgp_config(self, external=False):
        if self.bgp_params is None or not self.bgp_params.get('address'):
            return None
        bgp = Bgp()
        cluster_id = self.bgp_params.get('cluster_id')
        if cluster_id:
            bgp.set_cluster_id(cluster_id)
        bgp.set_comment(DMUtils.bgp_group_comment(self.bgp_obj))
        if external:
            bgp.set_name(DMUtils.make_bgp_group_name(self.get_asn(), True))
            bgp.set_type('external')
        else:
            bgp.set_name(DMUtils.make_bgp_group_name(self.get_asn(), False))
            bgp.set_type('internal')
        bgp.set_ip_address(self.bgp_params['address'])
        bgp.set_autonomous_system(self.get_asn())
        self.add_families(bgp, self.bgp_params)
        self.add_bgp_auth_config(bgp, self.bgp_params)
        self.add_bgp_hold_time_config(bgp, self.bgp_params)
        return bgp
    # end get_bgp_config

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

    def add_peer_bgp_config(self, bgp_config, peers):
        peer_map = {}
        for peer, peer_data in self.get_sorted_key_value_pairs(peers):
            obj = peer_data.get('obj')
            params = peer_data.get('params', {})
            attr = peer_data.get('attr', {})
            nbr = Bgp(name=peer, ip_address=peer)
            nbr.set_comment(DMUtils.bgp_group_comment(obj))
            peer_map[peer] = nbr
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
        if peer_map:
            bgp_config.set_peers(self.get_values_sorted_by_key(peer_map))
    # end get_peer_bgp_config

    def get_asn(self):
        return self.bgp_params.get('local_autonomous_system') or self.bgp_params.get('autonomous_system')
    # end get_asn

    def set_bgp_group_config(self):
        bgp_config = self.get_bgp_config()
        if not bgp_config:
            return
        self.bgp_map[bgp_config.get_name()] = bgp_config
        self.add_peer_bgp_config(bgp_config, self.bgp_peers)
        if self.external_peers:
            ext_grp_config = self.get_bgp_config(True)
            self.bgp_map[ext_grp_config.get_name()] = ext_grp_config
            self.add_peer_bgp_config(ext_grp_config, self.external_peers)
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
            tunnel_ip = self.physical_router.dataplane_ip
            if not tunnel_ip and bgp_router.params:
                tunnel_ip = bgp_router.params.get('address')
            if tunnel_ip and self.physical_router.is_valid_ip(tunnel_ip):
                self.add_dynamic_tunnels(tunnel_ip,
                                         GlobalSystemConfigDM.ip_fabric_subnets)

        self.set_bgp_group_config()
    # end build_bgp_config

    def ensure_bgp_config(self):
        if not self.physical_router.bgp_router:
            self._logger.info("bgp router not configured for pr: " + \
                                                 self.physical_router.name)
            return False
        bgp_router = BgpRouterDM.get(self.physical_router.bgp_router)
        if not bgp_router or not bgp_router.params or not bgp_router.params.get("address"):
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
