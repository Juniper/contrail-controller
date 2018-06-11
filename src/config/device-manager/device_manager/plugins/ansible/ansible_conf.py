#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains generic plugin implementation device configuration using ansible
"""

import abc
from ncclient import manager
from ncclient.xml_ import new_ele
from ncclient.operations.errors import TimeoutExpiredError
from ncclient.transport.errors import TransportError
import time
import datetime
from cStringIO import StringIO
from dm_utils import DMUtils
from ansible_base import AnsibleBase
from dm_utils import PushConfigState
from db import PhysicalInterfaceDM
from db import LogicalInterfaceDM
from db import BgpRouterDM
from db import GlobalSystemConfigDM
from db import VirtualMachineInterfaceDM
from abstract_device_api.abstract_device_xsd import *
from job_handler import JobHandler


class AnsibleConf(AnsibleBase):
    @classmethod
    def register(cls, plugin_info):
        # plugin_info.update(common_params)
        return super(AnsibleConf, cls).register(plugin_info)
    # end register

    def __init__(self):
        # initialize data structures
        self.user_creds = self.physical_router.user_credentials
        self.management_ip = self.physical_router.management_ip
        self.timeout = 120
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        super(AnsibleConf, self).__init__()
    # end __init__

    def plugin_init(self):
        # build and push underlay config, onetime job
        self.underlay_config()
        super(AnsibleConf, self).plugin_init()
    # end plugin_init

    def initialize(self):
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
        self.proto_config = None
        self.route_targets = set()
        self.bgp_peers = {}
        self.chassis_config = None
        self.external_peers = {}
        super(AnsibleConf, self).initialize()
    # end initialize

    @abc.abstractmethod
    def underlay_config(self):
        # abstract method
        pass
    # end underlay_config

    def update(self):
        if not self.are_creds_modified():
            return
        self.user_creds = self.physical_router.user_credentials
        self.management_ip = self.physical_router.management_ip
    # ene update

    def get_commit_stats(self):
        return self.commit_stats
    # end get_commit_stats

    def retry(self):
        if self.push_config_state == PushConfigState.PUSH_STATE_RETRY:
            return True
        return False
    # end retry

    def are_creds_modified(self):
        user_creds = self.physical_router.user_credentials
        management_ip = self.physical_router.management_ip
        if self.user_creds != user_creds or self.management_ip != management_ip:
            return True
        return False
    # end are_creds_modified

    def device_connect(self):
        pass
    # end device_connect

    def device_disconnect(self):
        pass
    # end device_disconnect

    def is_connected(self):
        return True
    # end is_connected

    def device_send(self, conf, default_operation="merge",
                     operation="replace"):
        config_str = self.serialize(conf)
        self.push_config_state = PushConfigState.PUSH_STATE_INIT
        start_time = None
        config_size = 0
        try:
            self._logger.info("\nplaybook send message: %s\n" % config_str)
            config_size = len(config_str)
            #do op
            #TODO
            self.commit_stats['total_commits_sent_since_up'] += 1
            start_time = time.time()
            end_time = time.time()
            self.commit_stats['commit_status_message'] = 'success'
            self.commit_stats['last_commit_time'] = \
                    datetime.datetime.fromtimestamp(
                    end_time).strftime('%Y-%m-%d %H:%M:%S')
            self.commit_stats['last_commit_duration'] = str(
                    end_time - start_time)
            self.push_config_state = PushConfigState.PUSH_STATE_SUCCESS
        except Exception as e:
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

    def get_xpath_data(self, res, path_name, is_node=False):
        data = ''
        try:
            if not is_node:
                data = res.xpath(path_name)[0].text
            else:
                data = res.xpath(path_name)[0]
        except IndexError:
            if self._logger:
                self._logger.warning("could not fetch element data: %s, ip: %s" % (
                                             path_name, self.management_ip))
        return data
    # end get_xpath_data

    def device_get(self, filters = {}):
        dev_conf = {
                     'product-name' : '',
                     'product-model': '',
                     'software-version': ''
                   }
        try:
            pass
        except Exception as e:
            if self._logger:
                self._logger.error("could not fetch config from router %s: %s" % (
                                          self.management_ip, e.message))
        return dev_conf
    # end device_get

    def device_get_config(self, filters = {}):
        try:
            #config_data = self._nc_manager.get_config(source='running').data_xml
            pass
        except Exception as e:
            if self._logger:
                self._logger.error("could not fetch config from router %s: %s" % (
                                          self.management_ip, e.message))
        return config_data
    # end device_get_config

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

    def prepare_groups(self, is_delete=False):
        groups = Groups()
        if is_delete:
            return groups
        groups.set_comment(DMUtils.groups_comment())
        groups.set_routing_instances(self.ri_config)
        groups.set_interfaces(self.interfaces_config)
        groups.set_services(self.services_config)
        groups.set_policy_options(self.policy_config)
        groups.set_firewall(self.firewall_config)
        groups.set_forwarding_options(self.forwarding_options_config)
        groups.set_routing_options(self.global_routing_options_config)
        groups.set_protocols(self.proto_config)
        self.add_product_specific_config(groups)
        return groups
    # end prepare_groups

    def build_conf(self, groups, operation='replace'):
        groups.set_name("__contrail__")
        configuraion = Configuration(groups=groups)
        groups.set_operation(operation)
        apply_groups = ApplyGroups(name="__contrail__")
        configuraion.set_apply_groups(apply_groups)
        if operation == "delete":
            apply_groups.set_operation(operation)
        conf = config(configuration=configuraion)
        return conf
    # end build_conf

    def serialize(self, config):
        xml_data = StringIO()
        config.export_xml(xml_data, 1)
        return xml_data.getvalue()
    # end serialize

    def prepare_conf(self):
        device = Device()
        device.set_system(self.system)
        device.set_bgp(self.bgp)
        return device
    # end prepare_conf

    def has_conf(self):
        if not self.system or not self.bgp:
            return False
        return True
    # end has_conf

    def send_conf(self, is_delete=False):
        if not self.has_conf() and not is_delete:
            return
        config = self.prepare_conf()
        serialized_config = self.serialize(config)
        device_manager = DeviceManager.get_instance()
        job_handler = JobHandler(['default-global-system-config', 'ztp'],
                                 serialized_config, [self.management_ip],
                                 device_manager.get_analytics_config(),
                                 device_manager.get_vnc(), self._logger)
        job_handler.push()
    # end send_conf

    def add_lo0_unit_0_interface(self, loopback_ip=''):
        if not loopback_ip:
            return
        if not self.system:
            self.system = System()
        self.system.add_loopback_ip_list(loopback_ip)
    # end add_lo0_unit_0_interface

    def add_dynamic_tunnels(self, tunnel_source_ip, ip_fabric_nets):
        if not self.system:
            self.system = System()

        self.system.set_tunnel_ip(tunnel_source_ip)
        if ip_fabric_nets is not None:
            for subnet in ip_fabric_nets.get("subnet", []):
                self.system.add_tunnel_destination_networks(
                    Subnet(prefix=subnet['ip_prefix'],
                           prefix_len=subnet['ip_prefix_len'],
                           comment=DMUtils.ip_fabric_subnet_comment()))
    # end add_dynamic_tunnels

    def set_global_routing_options(self, bgp_params):
        router_id = bgp_params.get('identifier') or bgp_params.get('address')
        if router_id:
            if not self.global_routing_options_config:
                self.global_routing_options_config = RoutingOptions(comment=DMUtils.routing_options_comment())
            self.global_routing_options_config.set_router_id(router_id)
    # end set_global_routing_options

    def is_family_configured(self, params, family_name):
        if params is None or params.get('address_families') is None:
            return False
        families = params['address_families'].get('family', [])
        if family_name in families:
            return True
        return False
    # end is_family_configured

    def add_families(self, bgp_config, params):
        if params.get('address_families') is None:
            return
        families = params['address_families'].get('family', [])
        for family in families:
            if family in ['e-vpn', 'e_vpn']:
                fam = 'evpn'
            else:
                fam = family.replace('-', '_')
            bgp_config.add_families(fam)
    # end add_families

    def add_ibgp_export_policy(self, params, bgp_group):
        if params.get('address_families') is None:
            return
        families = params['address_families'].get('family', [])
        if not families:
            return
        if self.policy_config is None:
            self.policy_config = PolicyOptions(comment=DMUtils.policy_options_comment())
        ps = PolicyStatement(name=DMUtils.make_ibgp_export_policy_name())
        self.policy_config.add_policy_statement(ps)
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
            then.set_next_hop(NextHop(selfxx=''))
        bgp_group.set_export(DMUtils.make_ibgp_export_policy_name())
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
        bgp_group = BgpGroup()
        bgp_group.set_comment(DMUtils.bgp_group_comment(self.bgp_obj))
        if external:
            bgp_group.set_name(DMUtils.make_bgp_group_name(self.get_asn(), True))
            bgp_group.set_type('external')
            bgp_group.set_multihop('')
        else:
            bgp_group.set_name(DMUtils.make_bgp_group_name(self.get_asn(), False))
            bgp_group.set_type('internal')
            self.add_ibgp_export_policy(self.bgp_params, bgp_group)
        bgp_group.set_local_address(self.bgp_params['address'])
        self.add_families(bgp_group, self.bgp_params)
        self.add_bgp_auth_config(bgp_group, self.bgp_params)
        self.add_bgp_hold_time_config(bgp_group, self.bgp_params)
        return bgp_group
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
            nbr = BgpGroup(name=peer)
            nbr.set_comment(DMUtils.bgp_group_comment(obj))
            bgp_config.add_neighbor(nbr)
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
            nbr.set_peer_as(peer_as)
    # end _get_neighbor_config_xml

    def get_asn(self):
        return self.bgp_params.get('local_autonomous_system') or self.bgp_params.get('autonomous_system')
    # end get_asn

    def set_as_config(self):
        if not self.bgp_params.get("identifier"):
            return
        if self.global_routing_options_config is None:
            self.global_routing_options_config = RoutingOptions(comment=DMUtils.routing_options_comment())
        self.global_routing_options_config.set_route_distinguisher_id(self.bgp_params['identifier'])
        self.global_routing_options_config.set_autonomous_system(str(self.get_asn()))
    # end set_as_config

    def set_bgp_group_config(self):
        bgp_config = self._get_bgp_config_xml()
        if not bgp_config:
            return
        if not self.proto_config:
            self.proto_config = Protocols(comment=DMUtils.protocols_comment())
        bgp = Bgp()
        self.proto_config.set_bgp(bgp)
        bgp.add_group(bgp_config)
        self._get_neighbor_config_xml(bgp_config, self.bgp_peers)
        if self.external_peers:
            ext_grp_config = self._get_bgp_config_xml(True)
            bgp.add_group(ext_grp_config)
            self._get_neighbor_config_xml(ext_grp_config, self.external_peers)
        return
    # end set_bgp_group_config

    def build_bgp_config(self):
        bgp_router = BgpRouterDM.get(self.physical_router.bgp_router)
        if not bgp_router:
            return

        if not self.bgp:
            self.bgp = Bgp()
        if bgp_router.params.get('address'):
            self.bgp.set_ip_address(bgp_router.params.get('address'))
        if self.physical_router.allocated_asn is not None:
            self.bgp.set_autonomous_system(self.physical_router.allocated_asn)
        for peer_uuid, attr in bgp_router.bgp_routers.items():
            peer = BgpRouterDM.get(peer_uuid)
            if not peer or not peer.params or not peer.params.get('address'):
                continue
            peer_as = peer.params.get('local_autonomous_system') or\
                peer.params.get('autonomous_system')
            self.bgp.add_peers(Bgp(ip_address=peer.params['address'],
                                   autonomous_system=peer_as))
        tunnel_ip = self.physical_router.dataplane_ip
        if not tunnel_ip and bgp_router.params:
            tunnel_ip = bgp_router.params.get('address')
        if tunnel_ip and self.physical_router.is_valid_ip(tunnel_ip):
            self.add_dynamic_tunnels(tunnel_ip,
                                     GlobalSystemConfigDM.ip_fabric_subnets)

        if self.physical_router.loopback_ip:
            self.add_lo0_unit_0_interface(self.physical_router.loopback_ip)

        self.add_bgp_auth_config(self.bgp, self.bgp_params)
        self.add_families(self.bgp, self.bgp_params)
        self.add_bgp_hold_time_config(self.bgp, self.bgp_params)
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
