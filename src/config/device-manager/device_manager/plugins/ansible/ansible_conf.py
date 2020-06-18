#
# Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
#
# This file contains generic plugin implementation device configuration
# using ansible
#

from builtins import str
from builtins import object
import datetime
from hashlib import md5
import json
import time

from abstract_device_api.abstract_device_xsd import *

from .ansible_base import AnsibleBase
from .db import BgpRouterDM, GlobalVRouterConfigDM, GlobalSystemConfigDM, \
    InstanceIpDM, LogicalInterfaceDM, NodeProfileDM, PhysicalInterfaceDM, \
    PhysicalRouterDM, RoleConfigDM
from .device_manager import DeviceManager
from .dm_utils import DMUtils
from .dm_utils import PushConfigState
from .job_handler import JobHandler


class AnsibleConf(AnsibleBase):
    @classmethod
    def register(cls, plugin_info):
        # plugin_info.update(common_params)
        return super(AnsibleConf, cls).register(plugin_info)
    # end register

    def __init__(self, logger, params={}):
        """Initialize AnsibleConf init params."""
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
        self.forwarding_options_config = None
        self.routing_policies = []
        self.rib_groups = []
        self.routing_protocols = []
        self.firewall_config = None
        self.inet4_forwarding_filter = None
        self.inet6_forwarding_filter = None
        self.vlan_map = {}
        self.sc_zone_map = {}
        self.sc_policy_map = {}
        self.bgp_peers = {}
        self.external_peers = {}
        self.rr_peers = {}
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
        self.system_config.set_device_family(
            self.physical_router.device_family)
        self.system_config.set_management_ip(
            self.physical_router.management_ip)
        self.system_config.set_dummy_ip(self.physical_router.dummy_ip)
        self.system_config.set_encapsulation_priorities(
            GlobalVRouterConfigDM.global_encapsulation_priorities)
        self.system_config.set_physical_role(
            self.physical_router.physical_router_role)
        self.system_config.set_routing_bridging_roles(
            self.physical_router.routing_bridging_roles)
        is_erb_only = self.physical_router.is_erb_only()
        self.system_config.set_is_ucast_gateway_only(is_erb_only)
        self.system_config.set_brownfield_global_asn(
            self.physical_router.brownfield_global_asn)
        if self.physical_router.user_credentials:
            self.system_config.set_credentials(Credentials(
                authentication_method="PasswordBasedAuthentication",
                user_name=self.physical_router.user_credentials.get(
                    'username'),
                password=self.physical_router.user_credentials.get(
                    'password')))
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

    def device_send(self, job_template, job_input, is_delete, retry):
        config_str = json.dumps(job_input, sort_keys=True)
        self.push_config_state = PushConfigState.PUSH_STATE_IN_PROGRESS
        start_time = None
        config_size = 0
        forced_cfg_push = self.physical_router.forced_cfg_push
        try:
            config_size = len(config_str)
            current_config_hash = md5(config_str).hexdigest()
            if self.last_config_hash is None or (
                    current_config_hash != self.last_config_hash or
                    forced_cfg_push):
                self._logger.info(
                    "Config push for %s(%s) using job template %s, "
                    "forced_push %s" %
                    (self.physical_router.name,
                     self.physical_router.uuid,
                     str(job_template),
                        forced_cfg_push))
                if self._logger.ok_to_log("SYS_DEBUG"):
                    self._logger.debug("Abstract config: %s" %
                                       json.dumps(job_input, indent=4,
                                                  sort_keys=True))
                device_manager = DeviceManager.get_instance()
                job_handler = JobHandler(
                    job_template,
                    job_input,
                    [self.physical_router.uuid],
                    device_manager.get_api_server_config(),
                    self._logger,
                    device_manager._amqp_client,
                    self.physical_router.transaction_id,
                    self.physical_router.transaction_descr,
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
            exp_job_input = job_input.copy()
            exp_job_input['device_abstract_config'] = \
                json.loads(exp_job_input.get('device_abstract_config', {}))
            self._logger.error("Abstract config: %s" %
                               json.dumps(exp_job_input, indent=4,
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
        feature_params = {}
        job_template = None
        if self.physical_router.node_profile is not None:
            node_profile = NodeProfileDM.get(self.physical_router.node_profile)
            if node_profile is not None:
                job_template = node_profile.job_template_fq_name
                for role_config_uuid in node_profile.role_configs:
                    role_config = RoleConfigDM.get(role_config_uuid)
                    if role_config is None:
                        continue
                    feature_params[role_config.name] = role_config.config
        return feature_params, job_template
    # end read_node_profile_info

    @staticmethod
    def add_to_list(lst, value):
        if value not in lst:
            lst.append(value)
    # end add_to_list

    @staticmethod
    def add_ref_to_list(lst, value):
        if not any(v.get_name() == value for v in lst):
            lst.append(Reference(name=value))
    # end add_ref_to_list

    @classmethod
    def add_ip_address(cls, unit, address, gateway=None):
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

    def prepare_conf(self, feature_configs=None, is_delete=False):
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
        device.set_routing_instances(
            self.get_values_sorted_by_key(
                self.ri_map))
        device.set_vlans(self.get_values_sorted_by_key(self.vlan_map))
        device.set_forwarding_options(self.forwarding_options_config)
        device.set_firewall(self.firewall_config)
        device.set_security_zones(
            self.get_values_sorted_by_key(
                self.sc_zone_map))
        device.set_security_policies(
            self.get_values_sorted_by_key(
                self.sc_policy_map))
        if len(self.routing_policies) > 0:
            device.set_routing_policies(self.routing_policies)
        if len(self.rib_groups) > 0:
            device.set_rib_groups(self.rib_groups)
        if len(self.routing_protocols) > 0:
            device.set_routing_protocols(self.routing_protocols)
        if feature_configs:
            device.features = feature_configs
        return device
    # end prepare_conf

    def has_conf(self):
        return self.system_config or self.bgp_map or self.pi_map
    # end has_conf

    def send_conf(self, feature_configs=None, is_delete=False, retry=True):
        if not self.has_conf() and not is_delete:
            return 0
        config = self.prepare_conf(feature_configs=feature_configs,
                                   is_delete=is_delete)
        feature_params, job_template = self.read_node_profile_info()
        if not self.physical_router.fabric_obj:
            self._logger.warning("Could not push "
                                 "config to the device: %s, %s; "
                                 "Fabric Object not yet available for "
                                 "this Physical Router" %
                                 (self.physical_router.uuid,
                                  self.physical_router.name))
            return 0
        if not job_template:
            self._logger.warning("Could not push "
                                 "config to the device: %s, %s; "
                                 "Job Tempalte not available for "
                                 "this Physical Router" %
                                 (self.physical_router.uuid,
                                  self.physical_router.name))
            return 0
        fabric_fq_name = self.physical_router.fabric_obj.fq_name
        job_input = {
            'fabric_uuid': self.physical_router.fabric,
            'fabric_fq_name': fabric_fq_name,
            'device_management_ip': self.physical_router.management_ip,
            'additional_feature_params': feature_params,
            'device_abstract_config': self.export_dict(config),
            'is_delete': is_delete,
            'manage_underlay': self.physical_router.underlay_managed,
            'enterprise_style':
                self.physical_router.fabric_obj.enterprise_style
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

    def set_bgp_config(self, params, bgp_obj):
        self.bgp_params = params
        self.bgp_obj = bgp_obj
    # end set_bgp_config

    def ensure_bgp_config(self):
        if not self.physical_router.bgp_router:
            self._logger.info("bgp router not configured for pr: " +
                              self.physical_router.name)
            return False
        bgp_router = BgpRouterDM.get(self.physical_router.bgp_router)
        if not bgp_router or not bgp_router.params or \
                not bgp_router.params.get("address"):
            self._logger.info("bgp router parameters not configured for pr: " +
                              bgp_router.name)
            return False
        # storing bgp router related configuration as we are using for
        # validations to check whether e-vpn family configured or not.
        self.set_bgp_config(bgp_router.params, bgp_router)
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
        obj_json = json.dumps(obj, default=AnsibleConf.export_default)
        return obj_json
    # end export_dict

    @staticmethod
    def export_default(obj):
        if isinstance(obj, set):
            return list(obj)
        if hasattr(obj, '__dict__'):
            return dict((k, v) for k, v in list(obj.__dict__.items())
                        if AnsibleConf.do_export(v))
        raise TypeError(
            'Object of type %s is not JSON serializable' %
            obj.__class__.__name__)
    # end export_default

# end AnsibleConf


class JunosInterface(object):

    def __init__(self, if_name, if_type, if_vlan_tag=0, if_ip=None,
                 li_uuid=None, port_vlan_tag=4094, vpg_obj=None):
        """Initialize JunosInterface init params."""
        self.li_uuid = li_uuid
        self.name = if_name
        self.if_type = if_type
        self.vlan_tag = if_vlan_tag
        self.port_vlan_tag = port_vlan_tag
        ifparts = if_name.split('.')
        self.ifd_name = ifparts[0]
        self.unit = ifparts[1]
        self.ip = if_ip
        self.vpg_obj = vpg_obj
    # end __init__

    def is_untagged(self):
        return not self.vlan_tag
    # end is_untagged

# end JunosInterface
