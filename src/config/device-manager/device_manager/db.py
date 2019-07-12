#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for physical router.

for device specific configuration manager
"""

import json
import re
import socket
import struct
from time import gmtime, strftime
import traceback

from ansible_base import AnsibleBase
from attrdict import AttrDict
from cfgm_common import vnc_greenlets
from cfgm_common.exceptions import ResourceExistsError
from cfgm_common.uve.physical_router.ttypes import *
from cfgm_common.uve.physical_router_config.ttypes import *
from cfgm_common.uve.service_status.ttypes import *
from cfgm_common.vnc_db import DBBase
from cfgm_common.vnc_object_db import VncObjectDBClient
from cfgm_common.zkclient import IndexAllocator
from device_conf import DeviceConf
from dm_utils import DMIndexer
from dm_utils import DMUtils
from dm_utils import PushConfigState
from feature_base import FeatureBase
import gevent
from gevent import queue
from netaddr import IPAddress
import pyhash
from sandesh_common.vns.constants import DEVICE_MANAGER_KEYSPACE_NAME
from vnc_api.vnc_api import *


class DBBaseDM(DBBase):
    obj_type = __name__

    @classmethod
    def locate_all(cls):
        for obj in cls.list_obj():
            cls.locate(obj['uuid'], obj)
    # end locate

    @staticmethod
    def _read_key_value_pair(obj, prop):
        kvps = None
        if obj.get(prop):
            kvps = obj.get(prop).get('key_value_pair', [])
            kvps = AttrDict(dict((kvp['key'], kvp['value']) for kvp in kvps))
        return kvps
    # end _read_key_value_pair

    def _get_single_ref(self, ref_type, obj):
        if isinstance(obj, dict):
            refs = (obj.get(ref_type + '_refs') or
                    obj.get(ref_type + '_back_refs'))
        else:
            refs = (getattr(obj, ref_type + '_refs', None) or
                    getattr(obj, ref_type + '_back_refs', None))
        if refs:
            return self._get_ref_key(refs[0], ref_type)
        else:
            return None
    # end _get_single_ref
# end DBBaseDM


class BgpRouterDM(DBBaseDM):
    _dict = {}
    obj_type = 'bgp_router'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.bgp_routers = {}
        self.physical_router = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.params = obj.get('bgp_router_parameters') or {}
        if self.params and self.params.get('autonomous_system') is None:
            self.params[
                'autonomous_system'] = GlobalSystemConfigDM.get_global_asn()
        self.update_single_ref('physical_router', obj)
        new_peers = {}
        for ref in obj.get('bgp_router_refs', []):
            new_peers[ref['uuid']] = ref['attr']
        for peer_id in set(self.bgp_routers.keys()) - set(new_peers.keys()):
            peer = BgpRouterDM.get(peer_id)
            if not peer:
                continue
            if self.uuid in peer.bgp_routers:
                del peer.bgp_routers[self.uuid]
        for peer_id, attrs in new_peers.items():
            peer = BgpRouterDM.get(peer_id)
            if peer:
                peer.bgp_routers[self.uuid] = attrs
        self.bgp_routers = new_peers

    def get_all_bgp_router_ips(self):
        bgp_router_ips = {}
        if self.params.get('address'):
            bgp_router_ips[self.name] = self.params['address']

        for peer_uuid in self.bgp_routers:
            peer = BgpRouterDM.get(peer_uuid)
            if peer is None or not peer.params.get('address'):
                continue
            bgp_router_ips[peer.name] = peer.params['address']
        return bgp_router_ips
    # end get_all_bgp_router_ips

# end class BgpRouterDM


class FeatureDM(DBBaseDM):
    _dict = {}
    obj_type = 'feature'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.features = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]

        self.features = set()
        self.update_multiple_refs('feature', obj)

        if self.features:
            self.features = [FeatureDM.get(f) for f in self.features]
    # end update
# end class FeatureDM


class PhysicalRoleDM(DBBaseDM):
    _dict = {}
    obj_type = 'physical_role'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
    # end update
# end class PhysicalRoleDM


class OverlayRoleDM(DBBaseDM):
    _dict = {}
    obj_type = 'overlay_role'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
    # end update
# end class OverlayRoleDM


class FeatureConfigDM(DBBaseDM):
    _dict = {}
    obj_type = 'feature_config'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.additional_params = None
        self.vendor_config = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.additional_params = self._read_key_value_pair(
            obj, 'feature_config_additional_params')
        self.vendor_config = self._read_key_value_pair(
            obj, 'feature_config_vendor_config')
        self.add_to_parent(obj)
    # end update
# end class FeatureDM


class RoleDefinitionDM(DBBaseDM):
    _dict = {}
    obj_type = 'role_definition'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.features = set()
        self.feature_configs = set()
        self.physical_role = None
        self.overlay_role = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]

        self.features = set()
        self.feature_configs = set()
        self.physical_role = None
        self.overlay_role = None

        self.update_multiple_refs('feature', obj)
        self.update_single_ref('physical_role', obj)
        self.update_single_ref('overlay_role', obj)

        if self.features:
            self.features = [FeatureDM.get(f) for f in self.features]
        if self.physical_role:
            self.physical_role = PhysicalRoleDM.get(self.physical_role)
        if self.overlay_role:
            self.overlay_role = OverlayRoleDM.get(self.overlay_role)
    # end update
# end class RoleDefinitionDM


class PhysicalRouterDM(DBBaseDM):
    _dict = {}
    obj_type = 'physical_router'
    _sandesh = None

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_role = None
        self.overlay_roles = set()
        self.virtual_networks = set()
        self.logical_routers = set()
        self.bgp_router = None
        self.physical_router_role = None
        self.routing_bridging_roles = None
        self.replicator_ip = None
        self.config_manager = None
        self.service_endpoints = set()
        self.router_mode = None
        self.e2_service_index = 0
        self.e2_service_providers = set()
        self.nc_q = queue.Queue(maxsize=1)
        self.vn_ip_map = {'irb': {}, 'lo0': {}}
        self.allocated_asn = None
        self.config_sent = False
        self.ae_index_allocator = DMIndexer(
            DMUtils.get_max_ae_device_count(), DMIndexer.ALLOC_DECREMENT)
        self.init_cs_state()
        self.fabric = None
        self.fabric_obj = None
        self.virtual_port_groups = []
        self.port_tuples = []
        self.node_profile = None
        self.plugins = None
        self.nc_handler_gl = None
        self.update(obj_dict)

        self.set_conf_sent_state(False)
        self.config_repush_interval = PushConfigState.get_repush_interval()
        self.nc_handler_gl = vnc_greenlets.VncGreenlet("VNC Device Manager",
                                                       self.nc_handler)
    # end __init__

    def use_ansible_plugin(self):
        return (PushConfigState.is_push_mode_ansible() and not
                self.is_ec2_role())
    # end use_ansible_plugin

    def reinit_device_plugin(self):
        plugin_params = {
            "physical_router": self
        }

        if self.use_ansible_plugin():
            self.plugins = FeatureBase.plugins(self._logger, self)
            plugin_base = AnsibleBase
        else:
            plugin_base = DeviceConf

        if not self.config_manager:
            self.config_manager = plugin_base.plugin(
                self.vendor, self.product, plugin_params, self._logger)
        elif self.config_manager.verify_plugin(
                self.vendor, self.product, self.physical_router_role):
            self.config_manager.update()
        else:
            self.config_manager.clear()
            self.config_manager = plugin_base.plugin(
                self.vendor, self.product, plugin_params, self._logger)
    # end reinit_device_plugin

    def is_ec2_role(self):
        return self.physical_router_role and\
            self.physical_router_role.lower().startswith('e2-')
    # end is_ec2_role

    def has_rb_role(self, role):
        if self.routing_bridging_roles and role in self.routing_bridging_roles:
            return True
        return False
    # end has_rb_role

    def _any_rb_role_matches(self, sub_str):
        if self.routing_bridging_roles and sub_str:
            return any(sub_str.lower() in r.lower()
                       for r in self.routing_bridging_roles)
        return False
    # end _any_rb_role_matches

    def is_gateway(self):
        return self._any_rb_role_matches('gateway')
    # end is_gateway

    def is_erb_only(self):
        if self.routing_bridging_roles:
            erb_gateway_role = any('erb' in r.lower() and
                                   'gateway' in r.lower()
                                   for r in self.routing_bridging_roles)
            non_erb_gateway_role = any('erb' not in r.lower() and
                                       'gateway' in r.lower()
                                       for r in self.routing_bridging_roles)
            return erb_gateway_role and not non_erb_gateway_role
        return False
    # end is_erb_only

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = obj['fq_name'][-1]
        self.management_ip = obj.get('physical_router_management_ip')
        self.loopback_ip = obj.get('physical_router_loopback_ip')
        self.replicator_ip = obj.get(
            'physical_router_replicator_loopback_ip') or None
        self.dummy_ip = self.get_dummy_ip(obj)
        self.dataplane_ip = obj.get(
            'physical_router_dataplane_ip') or self.loopback_ip
        self.vendor = obj.get('physical_router_vendor_name') or ''
        self.managed_state = obj.get(
            'physical_router_managed_state') or 'active'
        if self.managed_state == 'activating':
            self.forced_cfg_push = True
        else:
            self.forced_cfg_push = False
        self.product = obj.get('physical_router_product_name') or ''
        self.device_family = obj.get('physical_router_device_family')
        self.vnc_managed = obj.get('physical_router_vnc_managed')
        self.underlay_managed = obj.get('physical_router_underlay_managed')
        self.physical_router_role = obj.get('physical_router_role')
        routing_bridging_roles = obj.get('routing_bridging_roles')
        if routing_bridging_roles is not None:
            self.routing_bridging_roles = routing_bridging_roles.get(
                'rb_roles')
        self.user_credentials = obj.get('physical_router_user_credentials')
        self.physical_router_snmp = obj.get('physical_router_snmp')
        self.physical_router_lldp = obj.get('physical_router_lldp')
        self.telemetry_info = obj.get('telemetry_info')
        self.junos_service_ports = obj.get(
            'physical_router_junos_service_ports')
        self.update_single_ref('bgp_router', obj)
        self.update_multiple_refs('virtual_network', obj)
        self.update_multiple_refs('logical_router', obj)
        self.physical_interfaces = set([pi['uuid'] for pi in
                                        obj.get('physical_interfaces', [])])
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
        self.update_single_ref('fabric', obj)
        if self.fabric is not None:
            self.fabric_obj = FabricDM.get(self.fabric)
        self.update_multiple_refs('service_endpoint', obj)
        self.update_multiple_refs('e2_service_provider', obj)
        self.update_single_ref('node_profile', obj)

        self.physical_role = None
        self.overlay_roles = set()

        self.update_single_ref('physical_role', obj)
        self.update_multiple_refs('overlay_role', obj)

        if self.physical_role:
            self.physical_role = PhysicalRoleDM.get(self.physical_role)
        if self.overlay_roles:
            self.overlay_roles = [OverlayRoleDM.get(o)
                                  for o in self.overlay_roles]

        self.reinit_device_plugin()
        self.allocate_asn()
    # end update

    def get_dummy_ip(self, obj):
        annotations = obj.get('annotations')
        if annotations:
            kvps = annotations.get('key_value_pair') or []
            kvp_dict = dict((kvp['key'], kvp['value']) for kvp in kvps)
            return kvp_dict.get('dummy_ip')
        return None
    # end get_dummy_ip

    def get_features(self):
        features = {}
        if not self.physical_role or not self.overlay_roles:
            return features.values()
        for rd in RoleDefinitionDM.values():
            if rd.physical_role != self.physical_role or \
                    rd.overlay_role not in self.overlay_roles:
                continue
            for feature in rd.features:
                if feature.name not in features:
                    features[feature.name] = AttrDict(feature=feature,
                                                      configs=[])
            for feature_config in rd.feature_configs:
                feature_config = FeatureConfigDM.get(feature_config)
                if feature_config.name in features:
                    features[feature_config.name]['configs'].append(
                        feature_config)
        return features.values()
    # end get_features

    def set_associated_lags(self, lag_uuid):
        self.virtual_port_groups.append(lag_uuid)

    def remove_associated_lags(self, lag_uuid):
        self.virtual_port_groups.remove(lag_uuid)

    def set_associated_port_tuples(self, pt_uuid):
        self.port_tuples.append(pt_uuid)

    def remove_associated_port_tuples(self, pt_uuid):
        self.port_tuples.remove(pt_uuid)

    def get_lr_dci_map(self):
        lrs = self.logical_routers
        dcis = []
        for lr_uuid in lrs or []:
            lr = LogicalRouterDM.get(lr_uuid)
            if lr and lr.data_center_interconnect:
                dci = DataCenterInterconnectDM.get(lr.data_center_interconnect)
                if dci:
                    dcis.append({"from": lr_uuid,
                                 "dci": lr.data_center_interconnect})
        return dcis
    # end get_lr_dci_map

    def get_dci_list(self):
        lrs = self.logical_routers
        dcis = []
        for lr_uuid in lrs or []:
            lr = LogicalRouterDM.get(lr_uuid)
            if lr and lr.data_center_interconnect:
                dci = DataCenterInterconnectDM.get(lr.data_center_interconnect)
                if dci:
                    dcis.append(lr.data_center_interconnect)
        return dcis
    # end get_dci_list

    def get_dci_bgp_neighbours(self):
        if not self.has_rb_role('DCI-Gateway'):
            return set()
        pr_set = DataCenterInterconnectDM.get_dci_peers(self.uuid)
        neigh = set()
        for pr in pr_set:
            if self.uuid == pr.uuid:
                continue
            if pr.bgp_router:
                neigh.add(pr.bgp_router)
        return neigh
    # end get_dci_bgp_neighbours

    def verify_allocated_asn(self, fabric):
        self._logger.debug("physical router: verify allocated asn for %s" %
                           self.uuid)
        if self.allocated_asn is not None and fabric is not None:
            for namespace_uuid in fabric.fabric_namespaces:
                namespace = FabricNamespaceDM.get(namespace_uuid)
                if namespace is None:
                    continue
                if namespace.as_numbers is not None:
                    if self.allocated_asn in namespace.as_numbers:
                        self._logger.debug(
                            "physical router: asn %d is allocated" %
                            self.allocated_asn)
                        return True
                if namespace.asn_ranges is not None:
                    for asn_range in namespace.asn_ranges:
                        if asn_range[0] <= self.allocated_asn <= asn_range[1]:
                            self._logger.debug(
                                "physical router: asn %d is allocated" %
                                self.allocated_asn)
                            return True
        self._logger.debug("physical router: asn not allocated")
        return False
    # end verify_allocated_asn

    def allocate_asn(self):
        if not self.fabric or not self.underlay_managed \
           or not self.fabric_obj or not self.config_manager:
            return
        if self.verify_allocated_asn(self.fabric_obj):
            return

        # get the configured asn ranges for this fabric
        asn_ranges = []
        for namespace_uuid in self.fabric_obj.fabric_namespaces:
            namespace = FabricNamespaceDM.get(namespace_uuid)
            if namespace is None:
                continue
            if namespace.as_numbers is not None:
                asn_ranges.extend([(asn, asn) for asn in namespace.as_numbers])
            if namespace.asn_ranges is not None:
                asn_ranges.extend(namespace.asn_ranges)
        asn_ranges = sorted(asn_ranges)

        # find the first available asn
        # loop through all asns to account for dangling asn in a range
        # due to deleted PRs
        for asn_range in asn_ranges:
            for asn in range(asn_range[0], asn_range[1] + 1):
                if self._object_db.get_pr_for_asn(asn) is None:
                    self.allocated_asn = asn
                    self._object_db.add_asn(self.uuid, asn)
                    self._logger.debug(
                        "physical router: allocated asn %d for %s" %
                        (self.allocated_asn, self.uuid))
                    return
        self._logger.error(
            "physical router: could not find an unused asn to allocate for %s"
            % self.uuid)
    # end allocate_asn

    def wait_for_config_push(self, timeout=1):
        if self.use_ansible_plugin() and self.config_manager:
            while self.config_manager.push_in_progress():
                try:
                    self.nc_q.get(True, timeout)
                except queue.Empty:
                    pass
    # end wait_for_config_push

    def delete_handler(self):
        self.wait_for_config_push()
        if self.nc_handler_gl:
            gevent.kill(self.nc_handler_gl)

        self.update_single_ref('bgp_router', {})
        self.update_multiple_refs('virtual_network', {})
        self.update_multiple_refs('logical_router', {})
        self.update_multiple_refs('service_endpoint', {})
        self.update_multiple_refs('e2_service_provider', {})

        if self.config_manager:
            if self.use_ansible_plugin():
                self.config_manager.push_conf(is_delete=True)
                max_retries = 3
                for _ in range(max_retries):
                    if self.config_manager.retry():
                        self.config_manager.push_conf(is_delete=True)
                    else:
                        break
                self.set_conf_sent_state(False)
            elif self.is_vnc_managed():
                self.config_manager.push_conf(is_delete=True)
                self.config_manager.clear()

        self._object_db.delete_pr(self.uuid)
        self.uve_send(True)
        self.update_single_ref('node_profile', {})
        self.update_single_ref('fabric', {})
        self.fabric_obj = None
    # end delete_handler

    def delete_obj(self):
        vnc_greenlets.VncGreenlet("VNC Device Manager", self.delete_handler)
    # end delete_obj

    @classmethod
    def reset(cls):
        for obj in cls._dict.values():
            if obj.config_manager:
                obj.config_manager.clear()
        cls._dict = {}
    # end reset

    def is_junos_service_ports_enabled(self):
        if (self.junos_service_ports is not None and
                self.junos_service_ports.get('service_port') is not None):
            return True
        return False
    # end is_junos_service_ports_enabled

    def block_and_set_config_state(self, timeout):
        try:
            if self.nc_q.get(True, timeout) is not None:
                self.set_config_state()
        except queue.Empty:
            self.set_config_state()
    # end block_and_set_config_state

    def set_config_state(self):
        try:
            self.nc_q.put_nowait(1)
        except queue.Full:
            pass
    # end

    def nc_handler(self):
        while self.nc_q.get() is not None:
            try:
                self.push_config()
            except Exception as e:
                tb = traceback.format_exc()
                self._logger.error("Exception: " + str(e) + tb)
    # end

    def is_valid_ip(self, ip_str):
        try:
            socket.inet_aton(ip_str)
            return True
        except socket.error:
            return False
    # end

    def init_cs_state(self):
        asn = self._object_db.get_asn_for_pr(self.uuid)
        if asn:
            self.allocated_asn = asn
        vn_subnet_set = self._object_db.get_pr_vn_set(self.uuid)
        for vn_subnet in vn_subnet_set:
            subnet = vn_subnet[0]
            ip_used_for = vn_subnet[1]
            ip = self._object_db.get_ip(self.uuid + ':' + subnet, ip_used_for)
            if ip:
                self.vn_ip_map[ip_used_for][subnet] = ip
    # end init_cs_state

    def reserve_ip(self, vn_uuid, subnet_uuid):
        try:
            vn = VirtualNetwork()
            vn.set_uuid(vn_uuid)
            ip_addr = self._manager._vnc_lib.virtual_network_ip_alloc(
                vn,
                subnet=subnet_uuid)
            if ip_addr:
                return ip_addr[0]  # ip_alloc default ip count is 1
        except Exception as e:
            tb = traceback.format_exc()
            self._logger.error("Exception: " + str(e) + tb)
            return None
    # end

    def free_ip(self, vn_uuid, ip_addr):
        try:
            vn = VirtualNetwork()
            vn.set_uuid(vn_uuid)
            ip_addr = ip_addr.split('/')[0]
            self._manager._vnc_lib.virtual_network_ip_free(
                vn, [ip_addr])
            return True
        except Exception as e:
            tb = traceback.format_exc()
            self._logger.error("Exception: " + str(e) + tb)
            return False
    # end

    def get_vn_irb_ip_map(self):
        ips = {'irb': {}, 'lo0': {}}
        for ip_used_for in ['irb', 'lo0']:
            for vn_subnet, ip_addr in self.vn_ip_map[ip_used_for].items():
                (vn_uuid, subnet_prefix) = vn_subnet.split(':', 1)
                vn = VirtualNetworkDM.get(vn_uuid)
                if vn_uuid not in ips[ip_used_for]:
                    ips[ip_used_for][vn_uuid] = set()
                ips[ip_used_for][vn_uuid].add(
                    (ip_addr,
                     vn.gateways[subnet_prefix].get('default_gateway')))
        return ips
    # end get_vn_irb_ip_map

    def evaluate_vn_irb_ip_map(self, vn_set, fwd_mode, ip_used_for,
                               ignore_external=False):
        new_vn_ip_set = set()
        if not self.use_ansible_plugin() or self.is_gateway():
            for vn_uuid in vn_set:
                vn = VirtualNetworkDM.get(vn_uuid)
                if not vn:
                    continue
                # dont need irb ip, gateway ip
                if vn.get_forwarding_mode() != fwd_mode:
                    continue
                if vn.router_external and ignore_external:
                    continue
                for subnet_prefix in vn.gateways.keys():
                    new_vn_ip_set.add(vn_uuid + ':' + subnet_prefix)

        self.evaluate_vn_ip_map(new_vn_ip_set, self.vn_ip_map[ip_used_for],
                                ip_used_for,
                                use_gateway_ip=self.is_erb_only())
    # end evaluate_vn_irb_ip_map

    def evaluate_vn_ip_map(self, vn_set, ip_map, ip_used_for,
                           use_gateway_ip=False):
        old_set = set(ip_map.keys())
        delete_set = old_set.difference(vn_set)
        create_set = vn_set.difference(old_set)

        if not use_gateway_ip:
            for vn_subnet in vn_set.intersection(old_set):
                (vn_uuid, subnet_prefix) = vn_subnet.split(':', 1)
                vn = VirtualNetworkDM.get(vn_uuid)
                ip = ip_map[vn_subnet].split('/')[0]
                if ip == vn.gateways[subnet_prefix].get('default_gateway'):
                    create_set.add(vn_subnet)
                    delete_set.add(vn_subnet)

        for vn_subnet in delete_set:
            (vn_uuid, subnet_prefix) = vn_subnet.split(':', 1)
            vn = VirtualNetworkDM.get(vn_uuid)
            ip = ip_map[vn_subnet]
            if vn and ip != vn.gateways[subnet_prefix].get('default_gateway'):
                ret = self.free_ip(vn_uuid, ip_map[vn_subnet])
                if ret == False:
                    self._logger.error("Unable to free ip for vn/subnet/pr "
                                       "(%s/%s/%s)" % (vn_uuid, subnet_prefix,
                                                       self.uuid))

            ret = self._object_db.delete_ip(
                self.uuid + ':' + vn_uuid + ':' + subnet_prefix, ip_used_for)
            if ret == False:
                self._logger.error("Unable to free ip from db for vn/subnet/pr"
                                   " (%s/%s/%s)" % (vn_uuid, subnet_prefix,
                                                    self.uuid))
                continue

            self._object_db.delete_from_pr_map(self.uuid, vn_subnet,
                                               ip_used_for)
            del ip_map[vn_subnet]

        for vn_subnet in create_set:
            (vn_uuid, subnet_prefix) = vn_subnet.split(':', 1)
            vn = VirtualNetworkDM.get(vn_uuid)
            subnet_uuid = vn.gateways[subnet_prefix].get('subnet_uuid')
            (sub, length) = subnet_prefix.split('/')
            if use_gateway_ip:
                ip_addr = vn.gateways[subnet_prefix].get('default_gateway')
            else:
                ip_addr = self.reserve_ip(vn_uuid, subnet_uuid)
            if ip_addr is None:
                self._logger.error("Unable to allocate ip for vn/subnet/pr "
                                   "(%s/%s/%s)" % (vn_uuid, subnet_prefix,
                                                   self.uuid))
                continue
            ret = self._object_db.add_ip(
                self.uuid + ':' + vn_uuid + ':' + subnet_prefix, ip_used_for,
                ip_addr + '/' + length)
            if ret == False:
                self._logger.error("Unable to store ip for vn/subnet/pr "
                                   "(%s/%s/%s)" % (vn_uuid, subnet_prefix,
                                                   self.uuid))
                if self.free_ip(vn_uuid, ip_addr) == False:
                    self._logger.error("Unable to free ip for vn/subnet/pr "
                                       "(%s/%s/%s)" % (vn_uuid, subnet_prefix,
                                                       self.uuid))
                continue
            self._object_db.add_to_pr_map(self.uuid, vn_subnet, ip_used_for)
            ip_map[vn_subnet] = ip_addr + '/' + length
    # end evaluate_vn_ip_map

    def allocate_irb_ips_for(self, vns, use_gateway_ip):
        new_vns = set()
        for vn_uuid in vns:
            vn = VirtualNetworkDM.get(vn_uuid)
            if not vn:
                continue
            if vn.get_forwarding_mode() != 'l2_l3':
                continue
            for subnet_prefix in vn.gateways.keys():
                new_vns.add(vn_uuid + ':' + subnet_prefix)

        irb_ip_map = self.vn_ip_map['irb']
        self.evaluate_vn_ip_map(new_vns, irb_ip_map, 'irb',
                                use_gateway_ip=use_gateway_ip)
        return self.get_ip_map_for(irb_ip_map)
    # end allocate_irb_ips_for

    def get_ip_map_for(self, vn_ip_map):
        ips = {}
        for vn_subnet, ip_addr in vn_ip_map.items():
            (vn_uuid, subnet_prefix) = vn_subnet.split(':', 1)
            vn = VirtualNetworkDM.get(vn_uuid)
            if vn_uuid not in ips:
                ips[vn_uuid] = set()
            ips[vn_uuid].add(
                (ip_addr,
                 vn.gateways[subnet_prefix].get('default_gateway')))
        return ips
    # end get_ip_map_for

    def is_vnc_managed(self):
        if not self.vnc_managed:
            self._logger.info(
                "vnc managed property must be set for a physical router to get"
                " auto configured, ip: %s, not pushing config"
                % (self.management_ip))
            return False
        return True
    # end is_vnc_managed

    def set_conf_sent_state(self, state):
        self.config_sent = state
    # end set_conf_sent_state

    def is_conf_sent(self):
        return self.config_sent
    # end is_conf_sent

    def delete_config(self):
        if (self.is_conf_sent() and
            (not self.is_vnc_managed() or
             (not self.bgp_router and self.physical_router_role != 'pnf'))):
            if not self.config_manager:
                self.uve_send()
                return False
            # user must have unset the vnc managed property
            self.config_manager.push_conf(is_delete=True)
            if self.config_manager.retry():
                # failed commit: set repush interval upto max value
                self.config_repush_interval = min(
                    [2 * self.config_repush_interval,
                     PushConfigState.get_repush_max_interval()])
                self.block_and_set_config_state(self.config_repush_interval)
                return True
            # succesful commit: reset repush interval
            self.config_repush_interval = PushConfigState.get_repush_interval()
            self.set_conf_sent_state(False)
            self.uve_send()
            self.config_manager.clear()
            return True
        return False
    # end delete_config

    def get_pnf_vrf_name(self, si_obj, interface_type, first_tag):
        if not first_tag:
            return '_contrail-' + si_obj.name + '-' + interface_type
        else:
            return ('_contrail-' + si_obj.name + '-' + interface_type +
                    '-sc-entry-point')
    # end get_pnf_vrf_name

    def allocate_pnf_resources(self, vmi):
        resources = self._object_db.get_pnf_resources(
            vmi, self.uuid)
        network_id = int(resources['network_id'])
        if vmi.service_interface_type == "left":
            ip = str(IPAddress(network_id * 4 + 1))
        if vmi.service_interface_type == "right":
            ip = str(IPAddress(network_id * 4 + 2))
        ip = ip + "/30"
        return {
            "ip_address": ip,
            "vlan_id": resources['vlan_id'],
            "unit_id": resources['unit_id']}
    # end allocate_pnf_resources

    def compute_pnf_static_route(self, ri_obj, pnf_dict):
        """
        Compute all the static route for the pnfs on the device.

        Args:
            ri_obj: The routing instance need to added the static routes
            pnf_dict: The pnf mapping dict
        Returns:
            static_routes: a static route list
                [
                    "service_chain_address":{
                        "next-hop":"ip_address",
                        "preference": int #use for the load balance

                    }

                ]
        """
        preference = 0
        static_routes = {}

        for vmi_uuid in ri_obj.virtual_machine_interfaces:
            # found the service chain address
            # Check if this vmi is a PNF vmi
            vmi = VirtualMachineInterfaceDM.get(vmi_uuid)

            preference = 0
            if vmi is not None:
                if vmi.service_instance is not None:
                    li_list = []
                    if vmi.service_interface_type == 'left':
                        li_list = pnf_dict[vmi.service_instance]['right']
                    elif vmi.service_interface_type == 'right':
                        li_list = pnf_dict[vmi.service_instance]['left']

                    for li in li_list:
                        static_entry = {
                            "next-hop": li.ip.split('/')[0]
                        }
                        if preference > 0:
                            static_entry[
                                "preference"] = preference
                        preference += 1
                        srs = static_routes.setdefault(
                            ri_obj.service_chain_address, [])
                        srs.append(static_entry)

        return static_routes
    # end compute_pnf_static_route

    def push_config(self):
        if not self.config_manager:
            self._logger.info(
                "Plugin not found for vendor family(%s:%s), "
                "ip: %s, not pushing config" % (str(self.vendor),
                                                str(self.product),
                                                self.management_ip))
            return

        if self.managed_state in ('rma', 'error'):
            # do not push any config to this device
            self._logger.debug(
                "No config push for PR(%s) in %s state" % (self.name,
                                                           self.managed_state))
            return

        if self.delete_config() or not self.is_vnc_managed():
            return

        self.config_manager.initialize()
        if not self.config_manager.validate_device():
            self._logger.error(
                "physical router: %s, device config validation failed. "
                "device configuration=%s"
                % (self.uuid, str(self.config_manager.get_device_config())))
            return

        if self.use_ansible_plugin():
            feature_configs = {}
            for plugin in self.plugins:
                feature_config = plugin.feature_config()
                if feature_config:
                    feature_configs[feature_config.name] = feature_config
            config_size = self.config_manager.push_conf(
                feature_configs=feature_configs)
        else:
            config_size = self.config_manager.push_conf()

        if not config_size:
            return
        self.set_conf_sent_state(True)
        self.uve_send()
        if self.config_manager.retry():
            # failed commit: set repush interval upto max value
            self.config_repush_interval = min(
                [2 * self.config_repush_interval,
                 PushConfigState.get_repush_max_interval()])
            self.block_and_set_config_state(self.config_repush_interval)
        else:
            # successful commit: reset repush interval to base
            self.config_repush_interval = PushConfigState.get_repush_interval()
            if PushConfigState.get_push_delay_enable():
                # sleep, delay=compute max delay between two successive commits
                gevent.sleep(self.get_push_config_interval(config_size))
    # end push_config

    def get_push_config_interval(self, last_config_size):
        config_delay = int(
            (last_config_size / 1000) *
            PushConfigState.get_push_delay_per_kb())
        delay = min([PushConfigState.get_push_delay_max(), config_delay])
        return delay

    def is_service_port_id_valid(self, service_port_id):
        # mx allowed ifl unit number range is (1, 16385) for service ports
        if service_port_id < 1 or service_port_id > 16384:
            return False
        return True
    # end is_service_port_id_valid

    def uve_send(self, deleted=False):
        pr_trace = UvePhysicalRouterConfig(
            name=self.name,
            ip_address=self.management_ip,
            connected_bgp_router=self.bgp_router,
            auto_conf_enabled=self.vnc_managed,
            product_info=str(self.vendor) + ':' + str(self.product))
        if deleted:
            pr_trace.deleted = True
            pr_msg = UvePhysicalRouterConfigTrace(
                data=pr_trace,
                sandesh=DBBaseDM._sandesh)
            pr_msg.send(sandesh=DBBaseDM._sandesh)
            return

        commit_stats = {}
        if self.config_manager:
            commit_stats = self.config_manager.get_commit_stats()

        if self.is_vnc_managed():
            pr_trace.netconf_enabled_status = True
            pr_trace.last_commit_time = commit_stats.get(
                'last_commit_time', '')
            pr_trace.last_commit_duration = commit_stats.get(
                'last_commit_duration', '0')
            pr_trace.commit_status_message = commit_stats.get(
                'commit_status_message', '')
            pr_trace.total_commits_sent_since_up = commit_stats.get(
                'total_commits_sent_since_up', 0)
        else:
            pr_trace.netconf_enabled_status = False

        pr_msg = UvePhysicalRouterConfigTrace(
            data=pr_trace, sandesh=DBBaseDM._sandesh)
        pr_msg.send(sandesh=DBBaseDM._sandesh)
    # end uve_send

# end PhysicalRouterDM


class GlobalVRouterConfigDM(DBBaseDM):
    _dict = {}
    obj_type = 'global_vrouter_config'
    global_vxlan_id_mode = None
    global_forwarding_mode = None
    global_encapsulation_priorities = []
    global_encapsulation_priority = None

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        new_global_vxlan_id_mode = obj.get('vxlan_network_identifier_mode')
        new_global_encapsulation_priority = None
        new_global_encapsulation_priorities = []
        encapsulation_priorities = obj.get('encapsulation_priorities')
        if encapsulation_priorities:
            new_global_encapsulation_priorities = encapsulation_priorities.get(
                "encapsulation")
            if new_global_encapsulation_priorities:
                new_global_encapsulation_priority = \
                    new_global_encapsulation_priorities[0]
        new_global_forwarding_mode = obj.get('forwarding_mode')
        if (GlobalVRouterConfigDM.global_vxlan_id_mode !=
                new_global_vxlan_id_mode or
            GlobalVRouterConfigDM.global_forwarding_mode !=
                new_global_forwarding_mode or
            GlobalVRouterConfigDM.global_encapsulation_priorities !=
                new_global_encapsulation_priorities or
            GlobalVRouterConfigDM.global_encapsulation_priority !=
                new_global_encapsulation_priority):
            GlobalVRouterConfigDM.global_vxlan_id_mode = \
                new_global_vxlan_id_mode
            GlobalVRouterConfigDM.global_forwarding_mode = \
                new_global_forwarding_mode
            GlobalVRouterConfigDM.global_encapsulation_priorities = \
                new_global_encapsulation_priorities
            GlobalVRouterConfigDM.global_encapsulation_priority = \
                new_global_encapsulation_priority
            self.update_physical_routers()
    # end update

    def update_physical_routers(self):
        for pr in PhysicalRouterDM.values():
            pr.set_config_state()
    # end update_physical_routers

    @classmethod
    def is_global_vxlan_id_mode_auto(cls):
        if (cls.global_vxlan_id_mode is not None and
                cls.global_vxlan_id_mode == 'automatic'):
            return True
        return False
# end GlobalVRouterConfigDM


class GlobalSystemConfigDM(DBBaseDM):
    _dict = {}
    obj_type = 'global_system_config'
    global_asn = None
    ip_fabric_subnets = None

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_routers = set()
        self.data_center_interconnects = set()
        self.node_profiles = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        GlobalSystemConfigDM.global_asn = obj.get('autonomous_system')
        GlobalSystemConfigDM.ip_fabric_subnets = obj.get('ip_fabric_subnets')
        self.set_children('physical_router', obj)
        self.set_children('data_center_interconnect', obj)
        self.set_children('node_profile', obj)
    # end update

    @classmethod
    def get_global_asn(cls):
        return cls.global_asn
# end GlobalSystemConfigDM


class PhysicalInterfaceDM(DBBaseDM):
    _dict = {}
    obj_type = 'physical_interface'
    _esi_map = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.physical_router = None
        self.logical_interfaces = set()
        self.virtual_machine_interfaces = set()
        self.physical_interfaces = set()
        self.mtu = 0
        self.esi = None
        self.interface_type = None
        self.port = None
        obj = self.update(obj_dict)
        self.add_to_parent(obj)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.physical_router = self.get_parent_uuid(obj)
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
        self.name = obj.get('display_name')
        if self.name and re.search(r"[0-9]+_[0-9]+$", self.name):
            # For channelized ports
            self.name = self.name.replace("_", ":")
        self.esi = obj.get('ethernet_segment_identifier')
        self.interface_type = obj.get('physical_interface_type')
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('physical_interface', obj)
        self.update_single_ref('port', obj)
        return obj
    # end update

    def get_pr_uuid(self):
        return self.physical_router

    def delete_obj(self):
        self.update_multiple_refs('virtual_machine_interface', {})
        self.update_multiple_refs('physical_interface', {})
        self.update_single_ref('port', None)
        self.remove_from_parent()
    # end delete_obj
# end PhysicalInterfaceDM


class LogicalInterfaceDM(DBBaseDM):
    _dict = {}
    obj_type = 'logical_interface'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interface = None
        self.vlan_tag = 0
        self.li_type = None
        self.instance_ip = None
        obj = self.update(obj_dict)
        self.add_to_parent(obj)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        if obj['parent_type'] == 'physical-router':
            self.physical_router = self.get_parent_uuid(obj)
            self.physical_interface = None
        else:
            self.physical_interface = self.get_parent_uuid(obj)
            self.physical_router = None

        self.name = obj.get('display_name')
        if self.name and re.search(r"[0-9]+(_[0-9]+){2}$", self.name):
            # For channelized ports
            self.name = self.name.replace("_", ":")
        self.vlan_tag = obj.get('logical_interface_vlan_tag', 0)
        self.li_type = obj.get('logical_interface_type', 0)
        self.update_single_ref('virtual_machine_interface', obj)
        return obj
    # end update

    @classmethod
    def get_sg_list(cls):
        sg_list = []
        li_dict = cls._dict
        for li_obj in li_dict.values() or []:
            sg_list += li_obj.get_attached_sgs()
        return sg_list
    # end get_sg_list

    def get_attached_sgs(self):
        sg_list = []
        if self.virtual_machine_interface:
            vmi = VirtualMachineInterfaceDM.get(self.virtual_machine_interface)
            if not vmi:
                return sg_list
            for sg in vmi.security_groups or []:
                sg = SecurityGroupDM.get(sg)
                if sg:
                    sg_list.append(sg)
        return sg_list
    # end get_attached_sgs

    def get_attached_acls(self):
        acl_list = []
        sg_list = li_obj.get_attached_sgs()
        for sg in sg_list or []:
            for acl in sg.access_control_lists or []:
                acl = AccessControlListDM.get(acl)
                if acl:
                    acl_list.append(acl)
        return acl_list
    # end get_attached_acls

    def delete_obj(self):
        if self.physical_interface:
            parent = PhysicalInterfaceDM.get(self.physical_interface)
        elif self.physical_router:
            parent = PhysicalRouterDM.get(self.physical_router)
        if parent:
            parent.logical_interfaces.discard(self.uuid)
        self.update_single_ref('virtual_machine_interface', {})
        self.remove_from_parent()
    # end delete_obj
# end LogicalInterfaceDM


class FloatingIpDM(DBBaseDM):
    _dict = {}
    obj_type = 'floating_ip'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interface = None
        self.floating_ip_address = None
        self.floating_ip_pool = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]
        self.floating_ip_address = obj.get("floating_ip_address")
        self.update_single_ref('virtual_machine_interface', obj)
        self.add_to_parent(obj)
    # end update

    def get_public_network(self):
        if self.floating_ip_pool is None:
            return None
        pool_obj = FloatingIpPoolDM.get(self.floating_ip_pool)
        return pool_obj.virtual_network if pool_obj else None
    # end get_public_network

    def get_private_network(self):
        if self.virtual_machine_interface:
            vmi_obj = VirtualMachineInterfaceDM.get(
                self.virtual_machine_interface)
            return vmi_obj.virtual_network if vmi_obj else None
        return None
    # end get_private_network

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('virtual_machine_interface', {})
        obj.remove_from_parent()
        del cls._dict[uuid]
    # end delete

# end FloatingIpDM


class FloatingIpPoolDM(DBBaseDM):
    _dict = {}
    obj_type = 'floating_ip_pool'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_network = None
        self.floating_ips = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]
        self.add_to_parent(obj)
        self.update_multiple_refs('floating_ip', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('floating_ip', {})
        obj.remove_from_parent()
        del cls._dict[uuid]
    # end delete
# end FloatingIpPoolDM


class InstanceIpDM(DBBaseDM):
    _dict = {}
    obj_type = 'instance_ip'

    def __init__(self, uuid, obj_dict=None):
        self.name = None
        self.fq_name = None
        self.uuid = uuid
        self.instance_ip_address = None
        self.virtual_machine_interface = None
        self.logical_interface = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]
        self.instance_ip_address = obj.get("instance_ip_address")
        self.update_single_ref('virtual_machine_interface', obj)

        # Not using update_single_ref to update logical interface uuid,
        # because update_single_ref internally updates the
        # LogicalInterfaceDM.instance_ip property as well. We want to do a
        # conditional set only which this code takes care of.
        self.logical_interface = self._get_single_ref('logical_interface', obj)

        if self.logical_interface:
            li_obj = LogicalInterfaceDM.get(self.logical_interface)
            if li_obj:
                if li_obj.fq_name[-1] == 'lo0.0':
                    if self.fq_name[-1] == (li_obj.fq_name[1] +
                                            '/' + li_obj.fq_name[-1]):
                        li_obj.instance_ip = self.uuid
                else:
                    li_obj.instance_ip = self.uuid
    # end update

    def delete_obj(self):
        self.update_single_ref('virtual_machine_interface', {})
        self.logical_interface = None
    # end delete_obj
# end InstanceIpDM


class AccessControlListDM(DBBaseDM):
    _dict = {}
    obj_type = 'access_control_list'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.vnc_obj = None
        self.security_group = None
        self.update(obj_dict)
        self.is_ingress = self.name.startswith('ingress-')
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj.get('fq_name')[-1]
        self.vnc_obj = self.vnc_obj_from_dict(self.obj_type, obj)
        if obj.get('parent_type') == "security-group":
            self.add_to_parent(obj)
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        if obj.security_group:
            obj.remove_from_parent()
        del cls._dict[uuid]
    # end delete
# end AccessControlListDM


class SecurityGroupDM(DBBaseDM):
    _dict = {}
    obj_type = 'security_group'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.virtual_machine_interfaces = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.set_children('access_control_list', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        del cls._dict[uuid]
    # end delete
# end SecurityGroupDM


class VirtualMachineInterfaceDM(DBBaseDM):
    _dict = {}
    obj_type = 'virtual_machine_interface'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.virtual_network = None
        self.floating_ip = None
        self.instance_ip = None
        self.logical_interfaces = set()
        self.physical_interface = None
        self.vlan_tag = None
        self.service_interface_type = None
        self.port_tuple = None
        self.routing_instances = set()
        self.security_groups = set()
        self.port_profiles = set()
        self.service_instance = None
        self.service_endpoint = None
        self.virtual_port_group = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]

        if obj.get('virtual_machine_interface_properties'):
            self.params = obj['virtual_machine_interface_properties']
            self.vlan_tag = self.params.get('sub_interface_vlan_tag', None)
            self.service_interface_type = self.params.get(
                'service_interface_type', None)
        else:
            self.vlan_tag = 0
        self.bindings = obj.get('virtual_machine_interface_bindings') or {}
        kvps = self.bindings.get('key_value_pair') or []
        kvp_dict = dict((kvp['key'], kvp['value']) for kvp in kvps)
        self.port_vlan_tag = kvp_dict.get('tor_port_vlan_id') or 4094
        self.device_owner = obj.get(
            "virtual_machine_interface_device_owner") or ''
        self.update_multiple_refs('logical_interface', obj)
        self.update_single_ref('virtual_network', obj)
        self.update_single_ref('floating_ip', obj)
        self.update_single_ref('instance_ip', obj)
        self.update_single_ref('physical_interface', obj)
        self.update_multiple_refs('routing_instance', obj)
        self.update_multiple_refs('security_group', obj)
        self.update_multiple_refs('port_profile', obj)
        self.update_single_ref('port_tuple', obj)
        self.service_instance = None
        if self.port_tuple:
            pt = PortTupleDM.get(self.port_tuple)
            if pt:
                self.service_instance = pt.svc_instance
        self.update_single_ref('service_endpoint', obj)
        self.update_single_ref('virtual_port_group', obj)
    # end update

    def is_device_owner_bms(self):
        if self.logical_interfaces and \
           len(self.logical_interfaces) >= 1 and \
           self.device_owner.lower() in ['physicalrouter', 'physical-router']:
            return True
        kvps = self.bindings.get('key_value_pair') or []
        kvp_dict = dict((kvp['key'], kvp['value']) for kvp in kvps)
        vnic_type = kvp_dict.get('vnic_type') or ''
        if vnic_type == 'baremetal':
            return True
        return False
    # end

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('logical_interface', {})
        obj.update_single_ref('virtual_network', {})
        obj.update_single_ref('floating_ip', {})
        obj.update_single_ref('instance_ip', {})
        obj.update_single_ref('physical_interface', {})
        obj.update_multiple_refs('routing_instance', {})
        obj.update_multiple_refs('security_group', {})
        obj.update_multiple_refs('port_profile', {})
        obj.update_single_ref('port_tuple', {})
        obj.update_single_ref('service_endpoint', {})
        obj.update_single_ref('virtual_port_group', {})
        del cls._dict[uuid]
    # end delete
# end VirtualMachineInterfaceDM


class LogicalRouterDM(DBBaseDM):
    _dict = {}
    obj_type = 'logical_router'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_routers = set()
        self.data_center_interconnect = None
        self.virtual_machine_interfaces = set()
        self.dhcp_relay_servers = None
        # internal virtual-network
        self.virtual_network = None
        self.port_tuples = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        if not self.virtual_network:
            vn_name = DMUtils.get_lr_internal_vn_name(self.uuid)
            vn_obj = VirtualNetworkDM.find_by_name_or_uuid(vn_name)
            if vn_obj:
                self.virtual_network = vn_obj.uuid
                vn_obj.logical_router = self.uuid
        self.logical_router_gateway_external = obj.get(
            "logical_router_gateway_external")
        self.dhcp_relay_servers = obj.get(
            'logical_router_dhcp_relay_server').get('ip_address')
        self.update_multiple_refs('physical_router', obj)
        self.update_single_ref('data_center_interconnect', obj)
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('port_tuple', obj)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]
    # end update

    def get_internal_vn_name(self):
        return '__contrail_' + self.uuid + '_lr_internal_vn__'
    # end get_internal_vn_name

    def get_connected_networks(self, include_internal=True):
        vn_list = []
        if include_internal and self.virtual_network:
            vn_list.append(self.virtual_network)
        for vmi_uuid in self.virtual_machine_interfaces or []:
            vmi = VirtualMachineInterfaceDM.get(vmi_uuid)
            if vmi and vmi.virtual_network:
                vn_list.append(vmi.virtual_network)
        return vn_list
    # end get_connected_networks

    def get_dci_network(self):
        if self.data_center_interconnect:
            dci = DataCenterInterconnectDM.get(self.data_center_interconnect)
            if dci:
                return dci.virtual_network
        return None
    # end get_dci_network

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('physical_router', {})
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.update_multiple_refs('port_tuple', {})
        obj.update_single_ref('virtual_network', None)
        obj.update_single_ref('data_center_interconnect', None)
        del cls._dict[uuid]
    # end delete
# end LogicalRouterDM


class NetworkIpamDM(DBBaseDM):
    _dict = {}
    obj_type = 'network_ipam'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.ipam_subnets = set()
        self.ipam_method = None
        self.server_discovery_params = None
        self.virtual_networks = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]

        self.ipam_method = obj.get('ipam_subnet_method')
        self.ipam_subnets = obj.get('ipam_subnets')
        if self.ipam_subnets:
            self.server_discovery_params = \
                DMUtils.get_server_discovery_parameters(self.ipam_subnets.get(
                                                        'subnets', []))
        self.update_multiple_refs('virtual_network', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_network', {})
        del cls._dict[uuid]
    # end delete
# end NetworkIpamDM


class VirtualNetworkDM(DBBaseDM):
    _dict = {}
    obj_type = 'virtual_network'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.physical_routers = set()
        self.tags = set()
        self.network_ipams = set()
        self.logical_router = None
        self.router_external = False
        self.forwarding_mode = None
        self.gateways = None
        self.floating_ip_pools = set()
        self.instance_ip_map = {}
        self.route_targets = None
        self.update(obj_dict)
    # end __init__

    def get_route_targets(self):
        export_set, import_set = None, None
        vn_obj = self
        if vn_obj.routing_instances:
            for ri_id in vn_obj.routing_instances:
                ri_obj = RoutingInstanceDM.get(ri_id)
                if ri_obj is None:
                    continue
                if ri_obj.fq_name[-1] == vn_obj.fq_name[-1]:
                    if vn_obj.route_targets:
                        export_set = (vn_obj.route_targets &
                                      ri_obj.export_targets)
                        import_set = (vn_obj.route_targets &
                                      ri_obj.import_targets)
                    else:
                        export_set = copy.copy(ri_obj.export_targets)
                        import_set = copy.copy(ri_obj.import_targets)
                    break
        return export_set, import_set
    # end get_route_targets

    def set_logical_router(self, name):
        if DMUtils.get_lr_internal_vn_prefix() in name:
            lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(name)
            lr_obj = LogicalRouterDM.get(lr_uuid)
            if lr_obj:
                self.logical_router = lr_obj.uuid
                self.router_external = lr_obj.logical_router_gateway_external
                lr_obj.virtual_network = self.uuid
    # end set_logical_router

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
            self.set_logical_router(obj.get("fq_name")[-1])
        self.update_multiple_refs('physical_router', obj)
        self.update_multiple_refs('tag', obj)
        self.update_multiple_refs('network_ipam', obj)
        self.set_children('floating_ip_pool', obj)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]
        if not self.logical_router:
            self.router_external = obj.get('router_external', False)
        self.vn_network_id = obj.get('virtual_network_network_id')
        self.virtual_network_properties = obj.get('virtual_network_properties')
        self.set_forwarding_mode(obj)
        self.routing_instances = set([ri['uuid'] for ri in
                                      obj.get('routing_instances', [])])
        self.virtual_machine_interfaces = set(
            [vmi['uuid'] for vmi in
             obj.get('virtual_machine_interface_back_refs', [])])
        self.gateways = DMUtils.get_network_gateways(obj.get(
            'network_ipam_refs', []))
        self.route_targets = None
        route_target_list = obj.get('route_target_list')
        if route_target_list:
            route_targets = route_target_list.get('route_target')
            if route_targets:
                self.route_targets = set(route_targets)
    # end update

    def get_prefixes(self, pr_uuid=None):
        lr = None
        if self.logical_router:
            lr = LogicalRouterDM.get(self.logical_router)
        if not lr or not lr.logical_router_gateway_external:
            return set(self.gateways.keys())
        vn_list = lr.get_connected_networks(include_internal=False)
        prefix_set = set()
        if self.gateways and self.gateways.keys():
            prefix_set = set(self.gateways.keys())
        for vn in vn_list:
            vn_obj = VirtualNetworkDM.get(vn)
            if vn_obj:
                prefixes = vn_obj.get_prefixes()
                if prefixes:
                    prefix_set = prefix_set.union(prefixes)
        return prefix_set
    # end get_prefixes

    def get_vxlan_vni(self, is_internal_vn=False, is_dci_vn=False):
        if is_internal_vn or is_dci_vn:
            props = self.virtual_network_properties or {}
            return props.get("vxlan_network_identifier") or self.vn_network_id
        if GlobalVRouterConfigDM.is_global_vxlan_id_mode_auto():
            return self.vn_network_id
        props = self.virtual_network_properties or {}
        return props.get("vxlan_network_identifier") or self.vn_network_id
    # end get_vxlan_vni

    def set_forwarding_mode(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.forwarding_mode = None
        try:
            prop = obj['virtual_network_properties']
            if prop['forwarding_mode'] is not None:
                self.forwarding_mode = prop['forwarding_mode']
        except KeyError:
            pass
    # end set_forwarding_mode

    def get_forwarding_mode(self):
        if not self.forwarding_mode:
            return GlobalVRouterConfigDM.global_forwarding_mode or 'l2_l3'
        return self.forwarding_mode
    # end get_forwarding_mode

    def update_instance_ip_map(self):
        self.instance_ip_map = {}
        for vmi_uuid in self.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceDM.get(vmi_uuid)
            if vmi is None or vmi.is_device_owner_bms() == False:
                continue
            if vmi.floating_ip is not None and vmi.instance_ip is not None:
                fip = FloatingIpDM.get(vmi.floating_ip)
                inst_ip = InstanceIpDM.get(vmi.instance_ip)
                if (fip is None or inst_ip is None or
                        fip.get_public_network() is None):
                    continue
                instance_ip = inst_ip.instance_ip_address
                floating_ip = fip.floating_ip_address
                public_vn = VirtualNetworkDM.get(fip.get_public_network())
                if public_vn is None or public_vn.vn_network_id is None:
                    continue
                public_vrf_name = DMUtils.make_vrf_name(
                    public_vn.fq_name[-1], public_vn.vn_network_id, 'l3')
                self.instance_ip_map[instance_ip] = {
                    'floating_ip': floating_ip,
                    'vrf_name': public_vrf_name
                }
    # end update_instance_ip_map

    def get_connected_private_networks(self):
        vn_list = set()
        for pool_uuid in self.floating_ip_pools or []:
            pool = FloatingIpPoolDM.get(pool_uuid)
            if not pool or not pool.floating_ips:
                continue
            floating_ips = pool.floating_ips
            for fip in floating_ips:
                fip_obj = FloatingIpDM.get(fip)
                if not fip_obj or not fip_obj.virtual_machine_interface:
                    continue
                vmi = VirtualMachineInterfaceDM.get(
                    fip_obj.virtual_machine_interface)
                if vmi is None or vmi.is_device_owner_bms() == False:
                    continue
                if vmi.floating_ip is not None and vmi.instance_ip is not None:
                    fip = FloatingIpDM.get(vmi.floating_ip)
                    inst_ip = InstanceIpDM.get(vmi.instance_ip)
                    if (fip is None or inst_ip is None or
                            fip.get_private_network() is None):
                        continue
                    private_vn = VirtualNetworkDM.get(
                        fip.get_private_network())
                    if private_vn is None or private_vn.vn_network_id is None:
                        continue
                    vn_list.add(private_vn.uuid)
        return list(vn_list)
    # end get_connected_private_networks:w

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('physical_router', {})
        obj.update_multiple_refs('tag', {})
        obj.update_multiple_refs('network_ipam', {})
        del cls._dict[uuid]
    # end delete
# end VirtualNetworkDM


class RoutingInstanceDM(DBBaseDM):
    _dict = {}
    obj_type = 'routing_instance'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.virtual_network = None
        self.import_targets = set()
        self.export_targets = set()
        self.routing_instances = set()
        self.service_chain_address = None
        self.virtual_machine_interfaces = set()
        self.update(obj_dict)
        vn = VirtualNetworkDM.get(self.virtual_network)
        if vn:
            vn.routing_instances.add(self.uuid)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = obj['fq_name'][-1]
        self.virtual_network = self.get_parent_uuid(obj)
        self.import_targets = set()
        self.export_targets = set()
        for rt_ref in obj.get('route_target_refs', []):
            rt_name = rt_ref['to'][0]
            exim = rt_ref.get('attr').get('import_export')
            if exim == 'export':
                self.export_targets.add(rt_name)
            elif exim == 'import':
                self.import_targets.add(rt_name)
            else:
                self.import_targets.add(rt_name)
                self.export_targets.add(rt_name)
        self.update_multiple_refs('routing_instance', obj)
        self.update_multiple_refs('virtual_machine_interface', obj)
        service_chain_information = obj.get('service_chain_information')
        if service_chain_information is not None:
            self.service_chain_address = service_chain_information.get(
                'service_chain_address')

    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        vn = VirtualNetworkDM.get(obj.virtual_network)
        if vn:
            vn.routing_instances.discard(obj.uuid)
        del cls._dict[uuid]
    # end delete
# end RoutingInstanceDM


class ServiceTemplateDM(DBBaseDM):
    _dict = {}
    obj_type = 'service_template'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_instances = set()
        self.service_appliance_set = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.port_tuple_order = []
        self.params = obj.get('service_template_properties')
        if self.params:
            self.virtualization_type = self.params.get(
                'service_virtualization_type')
            intf_type = self.params.get('interface_type')
            if intf_type is not None:
                for svc_intf_type in intf_type:
                    if svc_intf_type.get('service_interface_type'):
                        self.port_tuple_order.append(
                            svc_intf_type.get('service_interface_type'))
        self.update_multiple_refs('service_instance', obj)
        self.update_single_ref('service_appliance_set', obj)
    # end update

    def delete_obj(self):
        self.update_multiple_refs('service_instance', {})
        self.update_single_ref('service_appliance_set', {})
    # end delete_obj
# end class ServiceTemplateDM


class ServiceApplianceDM(DBBaseDM):
    _dict = {}
    obj_type = 'service_appliance'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_appliance_set = None
        self.physical_interfaces = {}
        self.kvpairs = []
        obj = self.update(obj_dict)
        self.add_to_parent(obj)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        kvpairs = obj.get('service_appliance_properties', None)
        if kvpairs:
            self.kvpairs = kvpairs.get('key_value_pair', [])
        self.service_appliance_set = self.get_parent_uuid(obj)
        self.update_multiple_refs_with_attr('physical_interface', obj)
        return obj
    # end update

    def delete_obj(self):
        self.update_multiple_refs_with_attr('physical_interface', {})
        self.remove_from_parent()
    # end delete_obj
# end ServiceApplianceDM


class ServiceApplianceSetDM(DBBaseDM):
    _dict = {}
    obj_type = 'service_appliance_set'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_appliances = set()
        self.service_template = None
        self.kvpairs = []
        self.ha_mode = "active-active"
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_single_ref("service_template", obj)
        kvpairs = obj.get('service_appliance_set_properties', None)
        if kvpairs:
            self.kvpairs = kvpairs.get('key_value_pair', [])
        self.service_appliances = set(
            [sa['uuid'] for sa in obj.get('service_appliances', [])])
        self.ha_mode = obj.get('service_appliance_ha_mode')
    # end update

    def delete_obj(self):
        self.update_single_ref("service_template", {})
    # end delete_obj
# end ServiceApplianceSetDM


class ServiceInstanceDM(DBBaseDM):
    _dict = {}
    obj_type = 'service_instance'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.fq_name = None
        self.name = None
        self.params = None
        self.service_template = None
        self.port_tuples = set()
        self.update(obj_dict)
    # end

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = obj['fq_name'][-1]
        self.params = obj.get('service_instance_properties', None)
        bindings = obj.get('service_instance_bindings', None)
        annotations = obj.get('annotations')
        if annotations:
            kvps = annotations.get('key_value_pair') or []
            kvp_dict = dict((kvp['key'], kvp['value']) for kvp in kvps)
            self.left_svc_asns = \
                kvp_dict.get('left-svc-asns').split(',') or []
            self.right_svc_asns = \
                kvp_dict.get('right-svc-asns').split(',') or []
            self.left_svc_vlan = kvp_dict.get('left-svc-vlan') or None
            self.right_svc_vlan = kvp_dict.get('right-svc-vlan') or None
            self.rp_ip_addr = kvp_dict.get('rp-ip-addr') or None
        if bindings:
            kvps = bindings.get('key_value_pair') or []
            kvp_dict = dict((kvp['key'], kvp['value']) for kvp in kvps)
            self.left_svc_unit = kvp_dict.get('left-svc-unit') or None
            self.right_svc_unit = kvp_dict.get('right-svc-unit') or None
        self.port_tuples = set(
            [pt['uuid'] for pt in obj.get('port_tuples', [])])
        self.update_single_ref('service_template', obj)
        self.bgp_enabled = obj.get('service_instance_bgp_enabled')
    # end

    def delete_obj(self):
        self.update_single_ref('service_template', {})
        self._object_db.delete_pnf_resources(uuid)
    # end


class PortTupleDM(DBBaseDM):
    _dict = {}
    obj_type = 'port_tuple'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.logical_routers = set()
        self.virtual_networks = set()
        obj = self.update(obj_dict)
        self.add_to_parent(obj)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]
        self.svc_instance = self.get_parent_uuid(obj)
        annotations = obj.get('annotations')
        if annotations:
            kvps = annotations.get('key_value_pair') or []
            kvp_dict = dict((kvp['key'], kvp['value']) for kvp in kvps)
            self.left_lr = kvp_dict.get('left-lr') or None
            self.right_lr = kvp_dict.get('right-lr') or None
        self.build_pt_pr_map()
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('logical_router', obj)
        self.update_multiple_refs('virtual_network', obj)
        for vmi in self.virtual_machine_interfaces:
            vmi_obj = VirtualMachineInterfaceDM.get(vmi)
            if vmi_obj and not vmi_obj.service_instance:
                vmi_obj.service_instance = self.svc_instance
        return obj
    # end update

    def get_sa_obj(self):
        svc_appliance_obj = None
        si_obj = ServiceInstanceDM.get(self.svc_instance)
        if si_obj is not None:
            if si_obj.service_template is not None:
                svc_tmpl_obj = ServiceTemplateDM.get(si_obj.service_template)
                if svc_tmpl_obj.service_appliance_set is not None:
                    svc_appliance_set_obj = ServiceApplianceSetDM.get(
                        svc_tmpl_obj.service_appliance_set)
                    for sa in svc_appliance_set_obj.service_appliances or []:
                        svc_appliance_obj = ServiceApplianceDM.get(sa)

        return svc_appliance_obj

    def build_pt_pr_map(self):
        sa_obj = self.get_sa_obj()
        if sa_obj is not None:
            for pi in sa_obj.physical_interfaces or {}:
                pi_obj = PhysicalInterfaceDM.get(pi)
                pr_obj = PhysicalRouterDM.get(pi_obj.get_pr_uuid())
                if self.uuid not in pr_obj.port_tuples:
                    pr_obj.set_associated_port_tuples(self.uuid)

                for pi_ref in pi_obj.physical_interfaces or []:
                    pi_ref_obj = PhysicalInterfaceDM.get(pi_ref)
                    pr_ref_obj = PhysicalRouterDM.get(
                        pi_ref_obj.get_pr_uuid())
                    if self.uuid not in pr_ref_obj.port_tuples:
                        pr_ref_obj.set_associated_port_tuples(self.uuid)
    # end build_pr_pt_map

    def delete_obj(self):
        sa_obj = self.get_sa_obj()
        if sa_obj is not None:
            for pi in sa_obj.physical_interfaces or {}:
                pi_obj = PhysicalInterfaceDM.get(pi)
                pr_obj = PhysicalRouterDM.get(pi_obj.get_pr_uuid())
                if self.uuid in pr_obj.port_tuples:
                    pr_obj.remove_associated_port_tuples(self.uuid)

                for pi_ref in pi_obj.physical_interfaces or []:
                    pi_ref_obj = PhysicalInterfaceDM.get(pi_ref)
                    pr_ref_obj = PhysicalRouterDM.get(
                        pi_ref_obj.get_pr_uuid())
                    if self.uuid in pr_ref_obj.port_tuples:
                        pr_ref_obj.remove_associated_port_tuples(self.uuid)

        self.update_multiple_refs('virtual_machine_interface', {})
        self.update_multiple_refs('logical_router', {})
        self.update_multiple_refs('virtual_network', {})
        self.remove_from_parent()
    # end delete_obj
# end PortTupleDM


class ServiceEndpointDM(DBBaseDM):
    _dict = {}
    obj_type = 'service_endpoint'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_router = None
        self.service_connection_modules = set()
        self.virtual_machine_interface = None
        self.site_id = 0
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.service_name = obj.get('service_name')
        self.update_single_ref('physical_router', obj)
        self.update_multiple_refs('service_connection_module', obj)
        self.update_single_ref('virtual_machine_interface', obj)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('physical_router', {})
        obj.update_multiple_refs('service_connection_module', {})
        obj.update_single_ref('virtual_machine_interface', {})
        del cls._dict[uuid]
# end class ServiceEndpointDM


class ServiceConnectionModuleDM(DBBaseDM):
    _dict = {}
    obj_type = 'service_connection_module'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_endpoints = set()
        self.service_object = None
        self.circuit_id = 0
        self.mtu = 0
        self.no_control_word = False
        self.management_ip = None
        self.user_creds = None
        self.sap_info = None
        self.sdp_info = None
        self.id_perms = None
        self.service_type = None
        self.commit_stats = {
            'last_commit_time': '',
            'last_commit_duration': '',
            'commit_status_message': '',
            'total_commits_sent_since_up': 0,
        }
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.e2service = obj.get('e2service')
        self.id_perms = obj.get('id_perms')
        self.annotations = obj.get('annotations')
        self.service_type = obj.get('service_type')
        self.update_multiple_refs('service_endpoint', obj)
        self.update_single_ref('service_object', obj)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('service_endpoint', {})
        obj.update_single_ref('service_object', {})
        del cls._dict[uuid]
# end class ServiceConnectionModuleDM


class ServiceObjectDM(DBBaseDM):
    _dict = {}
    obj_type = 'service_object'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.service_connection_module = None
        self.sep_list = None
        self.physical_router = None
        self.service_status = {}
        self.management_ip = None
        self.user_creds = None
        self.service_type = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.service_object_name = obj.get('service_object_name')
        self.update_single_ref('service_connection_module', obj)
        circuit_id = 0
        if self.service_connection_module is not None:
            scm = ServiceConnectionModuleDM.get(self.service_connection_module)
            if scm is not None:
                circuit_id = scm.circuit_id
                if circuit_id == 0 and \
                   scm.service_type != 'fabric-interface':
                    return
                found = False
                neigbor_id = None
                for sindex, sep_uuid in enumerate(scm.service_endpoints):
                    sep = ServiceEndpointDM.get(sep_uuid)
                    if sep is None:
                        continue
                    pr_uuid = sep.physical_router
                    pr = PhysicalRouterDM.get(pr_uuid)
                    if pr is not None and pr.vendor.lower() == "juniper" \
                       and found != True:
                        self.management_ip = pr.management_ip
                        self.user_creds = pr.user_credentials
                        self.service_type = scm.service_type
                        found = True
                    elif pr is not None:
                        bgp_uuid = pr.bgp_router
                        bgp_entry = BgpRouterDM.get(bgp_uuid)
                        neigbor_id = bgp_entry.params.get('address')
                if found == True:
                    service_params = {
                        "service_type": self.service_type,
                        "circuit_id": circuit_id,
                        "neigbor_id": neigbor_id,
                    }
                    self.service_status = pr.config_manager.get_service_status(
                        service_params)
                    self.uve_send()

    def uve_send(self):
        mydata = self.service_status
        if self.service_status is not None:
            last_timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
            pr_trace = UveServiceStatus(name=self.name,
                                        ip_address=self.management_ip,
                                        service_name=self.name,
                                        status_data=str(mydata),
                                        operational_status="None",
                                        last_get_time=last_timestamp)

            pr_msg = UveServiceStatusTrace(
                data=pr_trace, sandesh=DBBaseDM._sandesh)
            pr_msg.send(sandesh=DBBaseDM._sandesh)
    # end uve_send
    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('service_connection_module', {})
        del cls._dict[uuid]
# end class ServiceObjectDM


class NetworkDeviceConfigDM(DBBaseDM):
    _dict = {}
    obj_type = 'network_device_config'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_router = None
        self.management_ip = None
        self.config_manager = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.config_object_name = obj.get('config_object_name')
        self.update_single_ref('physical_router', obj)
        if self.physical_router is not None:
            pr = PhysicalRouterDM.get(self.physical_router)
            if pr is not None:
                self.management_ip = pr.management_ip
                self.config_manager = pr.config_manager
                self.uve_send()
    # end update

    def uve_send(self):
        mydata = self.config_manager.device_get_config()
        if mydata:
            last_timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
            pr_trace = UvePhysicalRouterConfiguration(
                name=self.name,
                ip_address=self.management_ip,
                config_data=mydata,
                last_get_time=last_timestamp)

            pr_msg = UvePhysicalRouterConfigurationTrace(
                data=pr_trace, sandesh=DBBaseDM._sandesh)
            pr_msg.send(sandesh=DBBaseDM._sandesh)
    # end uve_send

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('physical_router', {})
        del cls._dict[uuid]
# end class NetworkDeviceConfigDM


class DataCenterInterconnectDM(DBBaseDM):
    _dict = {}
    obj_type = 'data_center_interconnect'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.logical_routers = set()
        obj = self.update(obj_dict)
        self.add_to_parent(obj)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.update_multiple_refs('logical_router', obj)
        return obj
    # end update

    @classmethod
    def get_dci_peers(cls, pr_uuid):
        dci_list = cls._dict.values()
        pr_list = []
        for dci in dci_list or []:
            prs = dci.get_connected_physical_routers()
            for pr in prs or []:
                if pr.uuid == pr_uuid:
                    pr_list += prs
                    break
        return set(pr_list)
    # end get_dci_peers

    def get_connected_lr_internal_vns(self, exclude_lr=None):
        vn_list = []
        for lr_uuid in self.logical_routers or []:
            if exclude_lr == lr_uuid:
                continue
            lr = LogicalRouterDM.get(lr_uuid)
            if lr and lr.virtual_network:
                vn = VirtualNetworkDM.get(lr.virtual_network)
                if vn:
                    vn_list.append(vn)
            client_vns = lr.get_connected_networks(False)
            for vn_id in client_vns or []:
                vn = VirtualNetworkDM.get(vn_id)
                if vn:
                    vn_list.append(vn)
        return vn_list
    # end get_connected_lr_internal_vns

    def get_connected_physical_routers(self):
        if not self.logical_routers:
            return []
        pr_list = []
        for lr_uuid in self.logical_routers:
            lr = LogicalRouterDM.get(lr_uuid)
            if lr and lr.physical_routers:
                prs = lr.physical_routers
                for pr_uuid in prs:
                    pr = PhysicalRouterDM.get(pr_uuid)
                    if pr.has_rb_role("DCI-Gateway"):
                        pr_list.append(pr)
        return pr_list
    # end get_connected_physical_routers

    def get_lr(self, pr):
        if not self.logical_routers:
            return None
        for lr_uuid in self.logical_routers:
            lr = LogicalRouterDM.get(lr_uuid)
            if lr and lr.physical_routers:
                prs = lr.physical_routers
                for pr_uuid in prs:
                    if pr == pr_uuid:
                        return lr
        return None
    # end get_lr

    def get_lr_vn(self, pr):
        if not self.logical_routers:
            return None
        for lr_uuid in self.logical_routers:
            lr = LogicalRouterDM.get(lr_uuid)
            if lr and lr.physical_routers:
                prs = lr.physical_routers
                for pr_uuid in prs:
                    if pr == pr_uuid:
                        return lr.virtual_network
        return None
    # end get_lr_vn

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj._object_db.delete_dci(obj.uuid)
        obj.update_multiple_refs('logical_router', {})
        del cls._dict[uuid]
    # end delete
# end class DataCenterInterconnectDM


class FabricDM(DBBaseDM):
    _dict = {}
    obj_type = 'fabric'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.fq_name = None
        self.fabric_namespaces = set()
        self.lo0_ipam_subnet = None
        self.ip_fabric_ipam_subnet = None
        self.update(obj_dict)
    # end __init__

    @classmethod
    def _get_ipam_subnets_for_virtual_network(cls, obj, vn_type):
        vn_uuid = None
        virtual_network_refs = obj.get('virtual_network_refs') or []
        for ref in virtual_network_refs:
            if vn_type in ref['attr']['network_type']:
                vn_uuid = ref['uuid']
                break

        # Get the IPAM attached to the virtual network
        ipam_subnets = None
        if vn_uuid is not None:
            vn = VirtualNetworkDM.get(vn_uuid)
            if vn is not None:
                ipam_refs = vn.get('network_ipam_refs')
                if ipam_refs:
                    ipam_ref = ipam_refs[0]
                    ipam_subnets = ipam_ref['attr'].get('ipam_subnets')
        return ipam_subnets
    # end _get_ipam_for_virtual_network

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]

        # Get the 'loopback' type virtual network
        self.lo0_ipam_subnet =\
            self._get_ipam_subnets_for_virtual_network(obj, 'loopback')

        # Get the 'ip_fabric' type virtual network
        self.ip_fabric_ipam_subnet = \
            self._get_ipam_subnets_for_virtual_network(obj, 'ip_fabric')

        # Get the enterprise-style flag
        self.enterprise_style = obj.get('fabric_enterprise_style', True)
    # end update
# end class FabricDM


class FabricNamespaceDM(DBBaseDM):
    _dict = {}
    obj_type = 'fabric_namespace'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.as_numbers = None
        self.asn_ranges = None
        self.update(obj_dict)
    # end __init__

    def _read_as_numbers(self, obj):
        fabric_namespace_type = obj.get('fabric_namespace_type')
        if fabric_namespace_type != "ASN":
            return
        tag_ids = list(set([tag['uuid'] for tag in obj.get('tag_refs') or []]))
        if len(tag_ids) == 0:
            return
        tag = self.read_obj(tag_ids[0], "tag")
        if tag.get('tag_type_name') != 'label' or\
                tag.get('tag_value') != 'fabric-ebgp-as-number':
            return
        value = obj.get('fabric_namespace_value')
        if value is not None and value['asn'] is not None and\
                value['asn']['asn'] is not None:
            self.as_numbers = list(map(int, value['asn']['asn']))
    # end _read_as_numbers

    def _read_asn_ranges(self, obj):
        fabric_namespace_type = obj.get('fabric_namespace_type')
        if fabric_namespace_type != "ASN_RANGE":
            return
        tag_ids = list(set([tag['uuid'] for tag in obj.get('tag_refs') or []]))
        if len(tag_ids) == 0:
            return
        tag = self.read_obj(tag_ids[0], "tag")
        if tag.get('tag_type_name') != 'label' or\
                tag.get('tag_value') != 'fabric-ebgp-as-number':
            return
        value = obj.get('fabric_namespace_value')
        if value is not None and value['asn_ranges'] is not None:
            self.asn_ranges = list(map(lambda asn_range:
                                       (int(asn_range['asn_min']),
                                        int(asn_range['asn_max'])),
                                       value['asn_ranges']))
    # end _read_asn_ranges

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self._read_as_numbers(obj)
        self._read_asn_ranges(obj)
        self.add_to_parent(obj)
    # end update

    def delete_obj(self):
        self.remove_from_parent()
    # end delete_obj
# end class FabricNamespaceDM


class NodeProfileDM(DBBaseDM):
    _dict = {}
    obj_type = 'node_profile'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.role_configs = set()
        self.job_template = None
        self.job_template_fq_name = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        for rc in obj.get('role_configs') or []:
            self.role_configs.add(rc.get('uuid', None))
        self.update_single_ref('job_template', obj)
        if self.job_template is not None:
            self.job_template_fq_name =\
                self._object_db.uuid_to_fq_name(self.job_template)
        else:
            self.job_template_fq_name = None
    # end update
# end class NodeProfileDM


class PortDM(DBBaseDM):
    _dict = {}
    obj_type = 'port'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.tags = set()
        self.physical_interfaces = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.update_multiple_refs('tag', obj)
        self.update_multiple_refs('physical_interface', obj)
    # end update

    def delete_obj(self):
        self.update_multiple_refs('tag', None)
        self.update_multiple_refs('physical_interface', {})
    # end delete_obj
# end class PortDM


class TagDM(DBBaseDM):
    _dict = {}
    obj_type = 'tag'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_networks = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1].split('=')[-1]
        self.update_multiple_refs('virtual_network', obj)
    # end update

    def delete_obj(self):
        self.update_multiple_refs('virtual_network', {})
    # end delete_obj
# end class TagDM


class PortProfileDM(DBBaseDM):
    _dict = {}
    obj_type = 'port_profile'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.storm_control_profile = None
        self.virtual_machine_interface = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.update_single_ref('virtual_machine_interface', obj)
        self.update_single_ref('storm_control_profile', obj)
    # end update

    def delete_obj(self):
        self.update_single_ref('storm_control_profile', {})
        self.update_single_ref('virtual_machine_interface', {})
    # end delete_obj
# end class PortProfileDM


class StormControlProfileDM(DBBaseDM):
    _dict = {}
    obj_type = 'storm_control_profile'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.port_profile = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.fq_name = obj['fq_name']
        self.storm_control_params = obj.get('storm_control_parameters')
        self.update_single_ref('port_profile', obj)
    # end update

    def delete_obj(self):
        self.update_single_ref('port_profile', {})
    # end delete_obj
# end class StormControlProfileDM


class LinkAggregationGroupDM(DBBaseDM):
    _dict = {}
    obj_type = 'link_aggregation_group'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.physical_interfaces = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.add_to_parent(obj)
        self.lacp_enabled = obj.get('link_aggregation_group_lacp_enabled')
        self.update_multiple_refs('physical_interface', obj)
    # end update

    def delete_obj(self):
        self.remove_from_parent()
        self.update_multiple_refs('physical_interface', {})
    # end delete_obj
# end class LinkAggregationGroupDM


class VirtualPortGroupDM(DBBaseDM):
    _dict = {}
    obj_type = 'virtual_port_group'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.physical_interfaces = set()
        self.virtual_machine_interfaces = set()
        self.esi = None
        self.pi_ae_map = {}
        self.update(obj_dict)
        self.get_esi()
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.add_to_parent(obj)
        self.update_multiple_refs('physical_interface', obj)
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.get_ae_for_pi(obj.get('physical_interface_refs'))
        self.build_lag_pr_map()

    # end update

    def get_ae_for_pi(self, pi_refs):
        self.pi_ae_map = {}
        if not pi_refs:
            return
        for pi in pi_refs:
            if pi.get('attr') is not None:
                self.pi_ae_map.update(
                    {pi.get('uuid'): pi.get('attr').get('ae_num')})

    def get_esi(self):
        hash_obj = pyhash.city_64()
        unpacked = struct.unpack('>8B', struct.pack('>Q', hash_obj(self.uuid)))
        self.esi = '00:%s:00' % (':'.join('%02x' % i for i in unpacked))

    def build_lag_pr_map(self):
        for pi in self.physical_interfaces or []:
            pi_obj = PhysicalInterfaceDM.get(pi)
            pr_obj = PhysicalRouterDM.get(pi_obj.get_pr_uuid())
            if self.uuid not in pr_obj.virtual_port_groups:
                pr_obj.set_associated_lags(self.uuid)

    def get_attached_sgs(self, vlan_tag):
        sg_list = []
        for vmi_uuid in self.virtual_machine_interfaces:
            vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
            if not vmi_obj:
                return sg_list
            if vmi_obj.vlan_tag == \
                int(vlan_tag) or (not vmi_obj.vlan_tag and
                                  int(vlan_tag) == int(vmi_obj.port_vlan_tag)):
                for sg in vmi_obj.security_groups or []:
                    sg = SecurityGroupDM.get(sg)
                    if sg and sg not in sg_list:
                        sg_list.append(sg)
        return sg_list
    # end get_attached_sgs

    def get_attached_port_profiles(self, vlan_tag):
        pp_list = []
        for vmi_uuid in self.virtual_machine_interfaces:
            vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
            if not vmi_obj:
                return pp_list
            if vmi_obj.vlan_tag == \
                int(vlan_tag) or (not vmi_obj.vlan_tag and
                                  int(vlan_tag) == int(vmi_obj.port_vlan_tag)):
                for pp in vmi_obj.port_profiles or []:
                    pp = PortProfileDM.get(pp)
                    if pp and pp not in pp_list:
                        pp_list.append(pp)
        return pp_list
    # end get_attached_port_profiles

    def delete_obj(self):
        for pi in self.physical_interfaces or []:
            pi_obj = PhysicalInterfaceDM.get(pi)
            pr_obj = PhysicalRouterDM.get(pi_obj.get_pr_uuid())
            if self.uuid in pr_obj.virtual_port_groups:
                pr_obj.remove_associated_lags(self.uuid)

        self.update_multiple_refs('physical_interface', {})
        self.update_multiple_refs('virtual_machine_interface', {})
        self.remove_from_parent()
    # end delete_obj
# end class VirtualPortGroupDM


class RoleConfigDM(DBBaseDM):
    _dict = {}
    obj_type = 'role_config'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.node_profile = None
        self.config = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.node_profile = self.get_parent_uuid(obj)
        self.add_to_parent(obj)
        self.config = obj.get('role_config_config')
        if self.config and isinstance(self.config, basestring):
            self.config = json.loads(self.config)
    # end update

    def delete_obj(self):
        self.remove_from_parent()
        self.update_single_ref('job_template', {})
    # end delete_obj
# end class RoleConfigDM


class E2ServiceProviderDM(DBBaseDM):
    _dict = {}
    obj_type = 'e2_service_provider'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.promiscuous = None
        self.physical_routers = set()
        self.peering_policys = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.promiscuous = obj.get('e2_service_provider_promiscuous')
        self.update_multiple_refs('physical_router', obj)
        self.update_multiple_refs('peering_policy', obj)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('peering_policy', {})
        obj.update_multiple_refs('physical_router', {})
        del cls._dict[uuid]
# end class E2ServiceProviderDM


class PeeringPolicyDM(DBBaseDM):
    _dict = {}
    obj_type = 'peering_policy'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.e2_service_providers = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.policy_name = obj.get('name')
        self.update_multiple_refs('e2_service_provider', obj)

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('e2_service_provider', {})
        del cls._dict[uuid]
# end class PeeringPolicyDM


class DMCassandraDB(VncObjectDBClient):
    _KEYSPACE = DEVICE_MANAGER_KEYSPACE_NAME
    _PR_VN_IP_CF = 'dm_pr_vn_ip_table'
    _PR_ASN_CF = 'dm_pr_asn_table'
    # PNF table
    _PNF_RESOURCE_CF = 'dm_pnf_resource_table'

    _zk_path_pfx = ''

    _PNF_MAX_NETWORK_ID = 4294967292
    _PNF_NETWORK_ALLOC_PATH = "/id/pnf/network_id"

    _PNF_MAX_VLAN = 4093
    _PNF_VLAN_ALLOC_PATH = "/id/pnf/vlan_id"

    _PNF_MAX_UNIT = 16385
    _PNF_UNIT_ALLOC_PATH = "/id/pnf/unit_id"

    dm_object_db_instance = None

    @classmethod
    def get_instance(cls, zkclient=None, args=None, logger=None):
        if cls.dm_object_db_instance is None:
            cls.dm_object_db_instance = DMCassandraDB(zkclient, args, logger)
        return cls.dm_object_db_instance
    # end

    @classmethod
    def clear_instance(cls):
        cls.dm_object_db_instance = None
    # end

    def __init__(self, zkclient, args, logger):
        self._zkclient = zkclient
        self._args = args

        keyspaces = {
            self._KEYSPACE: {self._PR_VN_IP_CF: {},
                             self._PR_ASN_CF: {},
                             self._PNF_RESOURCE_CF: {}}}

        cass_server_list = self._args.cassandra_server_list
        cred = None
        if (self._args.cassandra_user is not None and
                self._args.cassandra_password is not None):
            cred = {'username': self._args.cassandra_user,
                    'password': self._args.cassandra_password}

        super(DMCassandraDB, self).__init__(
            cass_server_list, self._args.cluster_id, keyspaces, None,
            logger.log, credential=cred,
            ssl_enabled=self._args.cassandra_use_ssl,
            ca_certs=self._args.cassandra_ca_certs)

        self.pr_vn_ip_map = {}
        self.pr_asn_map = {}
        self.asn_pr_map = {}
        self.init_pr_map()
        self.init_pr_asn_map()

        self.pnf_vlan_allocator_map = {}
        self.pnf_unit_allocator_map = {}
        self.pnf_network_allocator = IndexAllocator(
            zkclient, self._zk_path_pfx + self._PNF_NETWORK_ALLOC_PATH,
            self._PNF_MAX_NETWORK_ID)

        self.pnf_cf = self.get_cf(self._PNF_RESOURCE_CF)
        self.pnf_resources_map = dict(
            self.pnf_cf.get_range(column_count=0, filter_empty=True))
    # end

    def get_si_pr_set(self, si_id):
        si_obj = ServiceInstanceDM.get(si_id)
        pr_set = set()
        for pt_uuid in si_obj.port_tuples:
            pt_obj = PortTupleDM.get(pt_uuid)
            for vmi_uuid in pt_obj.virtual_machine_interfaces:
                vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
                pi_obj = PhysicalInterfaceDM.get(vmi_obj.physical_interface)
                pr_set.add(pi_obj.physical_router)
        return pr_set

    def get_pnf_vlan_allocator(self, pr_id):
        return self.pnf_vlan_allocator_map.setdefault(
            pr_id,
            IndexAllocator(
                self._zkclient,
                self._zk_path_pfx + self._PNF_VLAN_ALLOC_PATH + pr_id + '/',
                self._PNF_MAX_VLAN)
        )

    def get_pnf_unit_allocator(self, pi_id):
        return self.pnf_unit_allocator_map.setdefault(
            pi_id,
            IndexAllocator(
                self._zkclient,
                self._zk_path_pfx + self._PNF_UNIT_ALLOC_PATH + pi_id + '/',
                self._PNF_MAX_UNIT)
        )

    def get_pnf_resources(self, vmi_obj, pr_id):
        si_id = vmi_obj.service_instance
        pi_id = vmi_obj.physical_interface
        if not si_id or not pi_id:
            return None
        if si_id in self.pnf_resources_map:
            return self.pnf_resources_map[si_id]

        network_id = self.pnf_network_allocator.alloc(si_id)
        vlan_alloc = self.get_pnf_vlan_allocator(pr_id)
        try:
            vlan_alloc.reserve(0)
        except ResourceExistsError:
            # must have been reserved already, restart case
            pass
        vlan_id = vlan_alloc.alloc(si_id)
        pr_set = self.get_si_pr_set(si_id)
        for other_pr_uuid in pr_set:
            if other_pr_uuid != pr_id:
                try:
                    self.get_pnf_vlan_allocator(other_pr_uuid).reserve(vlan_id)
                except ResourceExistsError:
                    pass
        unit_alloc = self.get_pnf_unit_allocator(pi_id)
        try:
            unit_alloc.reserve(0)
        except ResourceExistsError:
            # must have been reserved already, restart case
            pass
        unit_id = unit_alloc.alloc(si_id)
        pnf_resources = {
            "network_id": str(network_id),
            "vlan_id": str(vlan_id),
            "unit_id": str(unit_id)
        }
        self.pnf_resources_map[si_id] = pnf_resources
        self.pnf_cf.insert(si_id, pnf_resources)
        return pnf_resources
    # end

    def delete_pnf_resources(self, si_id):
        pnf_resources = self.pnf_resources_map.get(si_id, None)
        if not pnf_resources:
            return
        self.pnf_network_allocator.delete(int(pnf_resources['network_id']))

        pr_set = self.get_si_pr_set(si_id)
        for pr_uuid in pr_set:
            if pr_uuid in self.pnf_vlan_allocator_map:
                self.get_pnf_vlan_allocator(pr_uuid).delete(
                    int(pnf_resources['vlan_id']))

        si_obj = ServiceInstanceDM.get(si_id)
        for pt_uuid in si_obj.port_tuples:
            pt_obj = PortTupleDM.get(pt_uuid)
            for vmi_uuid in pt_obj.virtual_machine_interfaces:
                vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
                if vmi_obj.physical_interface:
                    self.get_pnf_unit_allocator(
                        vmi_obj.physical_interface).delete(
                        int(pnf_resources['unit_id']))

        del self.pnf_resources_map[si_id]
        self.pnf_cf.remove(si_id)
    # end

    def handle_pnf_resource_deletes(self, si_id_list):
        for si_id in self.pnf_resources_map:
            if si_id not in si_id_list:
                self.delete_pnf_resources(si_id)
    # end

    def init_pr_map(self):
        cf = self.get_cf(self._PR_VN_IP_CF)
        pr_entries = dict(cf.get_range(column_count=1000000))
        for key in pr_entries.keys():
            key_data = key.split(':', 1)
            cols = pr_entries[key] or {}
            for col in cols.keys():
                ip_used_for = DMUtils.get_ip_used_for_str(col)
                (pr_uuid, vn_subnet_uuid) = (key_data[0], key_data[1])
                self.add_to_pr_map(pr_uuid, vn_subnet_uuid, ip_used_for)
    # end

    def init_pr_asn_map(self):
        cf = self.get_cf(self._PR_ASN_CF)
        pr_entries = dict(cf.get_range())
        for pr_uuid in pr_entries.keys():
            pr_entry = pr_entries[pr_uuid] or {}
            asn = pr_entry.get('asn')
            if asn:
                if pr_uuid not in self.pr_asn_map:
                    self.pr_asn_map[pr_uuid] = asn
                if asn not in self.asn_pr_map:
                    self.asn_pr_map[asn] = pr_uuid
    # end init_pr_asn_map

    def get_ip(self, key, ip_used_for):
        return self.get_one_col(self._PR_VN_IP_CF, key,
                                DMUtils.get_ip_cs_column_name(ip_used_for))
    # end

    def get_asn_for_pr(self, pr_uuid):
        return self.pr_asn_map.get(pr_uuid)
    # end get_asn_for_pr

    def get_pr_for_asn(self, asn):
        return self.asn_pr_map.get(asn)
    # end get_pr_for_asn

    def add_ip(self, key, ip_used_for, ip):
        self.add(self._PR_VN_IP_CF, key,
                 {DMUtils.get_ip_cs_column_name(ip_used_for): ip})
    # end

    def add_asn(self, pr_uuid, asn):
        self.add(self._PR_ASN_CF, pr_uuid, {'asn': asn})
        self.pr_asn_map[pr_uuid] = asn
        self.asn_pr_map[asn] = pr_uuid
    # end add_asn

    def delete_ip(self, key, ip_used_for):
        self.delete(self._PR_VN_IP_CF, key,
                    [DMUtils.get_ip_cs_column_name(ip_used_for)])
    # end

    def add_to_pr_map(self, pr_uuid, vn_subnet, ip_used_for):
        if pr_uuid in self.pr_vn_ip_map:
            self.pr_vn_ip_map[pr_uuid].add((vn_subnet, ip_used_for))
        else:
            self.pr_vn_ip_map[pr_uuid] = set()
            self.pr_vn_ip_map[pr_uuid].add((vn_subnet, ip_used_for))
    # end

    def delete_from_pr_map(self, pr_uuid, vn_subnet, ip_used_for):
        if pr_uuid in self.pr_vn_ip_map:
            self.pr_vn_ip_map[pr_uuid].remove((vn_subnet, ip_used_for))
            if not self.pr_vn_ip_map[pr_uuid]:
                del self.pr_vn_ip_map[pr_uuid]
    # end

    def delete_pr(self, pr_uuid):
        vn_subnet_set = self.pr_vn_ip_map.get(pr_uuid, set())
        for vn_subnet_ip_used_for in vn_subnet_set:
            vn_subnet = vn_subnet_ip_used_for[0]
            ip_used_for = vn_subnet_ip_used_for[1]
            ret = self.delete(self._PR_VN_IP_CF, pr_uuid + ':' + vn_subnet,
                              [DMUtils.get_ip_cs_column_name(ip_used_for)])
            if ret == False:
                self._logger.error(
                    "Unable to free ip from db for vn/pr/subnet/ip_used_for "
                    "(%s/%s/%s)" % (pr_uuid, vn_subnet, ip_used_for))
        asn = self.pr_asn_map.pop(pr_uuid, None)
        if asn is not None:
            self.asn_pr_map.pop(asn, None)
            ret = self.delete(self._PR_ASN_CF, pr_uuid)
            if not ret:
                self._logger.error("Unable to free asn from db for pr %s" %
                                   pr_uuid)
    # end

    def delete_dci(self, dci_uuid):
        pass
    # end

    def handle_dci_deletes(self, current_dci_set):
        pass
    # end

    def handle_pr_deletes(self, current_pr_set):
        cs_pr_set = set(self.pr_vn_ip_map.keys())
        delete_set = cs_pr_set.difference(current_pr_set)
        for pr_uuid in delete_set:
            self.delete_pr(pr_uuid)
    # end

    def get_pr_vn_set(self, pr_uuid):
        return self.pr_vn_ip_map.get(pr_uuid, set())
    # end

    @classmethod
    def get_db_info(cls):
        db_info = [(cls._KEYSPACE, [cls._PR_VN_IP_CF])]
        return db_info
    # end get_db_info

# end
