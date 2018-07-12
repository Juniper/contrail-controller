#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for physical router
configuration manager
"""
from device_conf import DeviceConf
from ansible_base import AnsibleBase
from dm_utils import PushConfigState
from dm_utils import DMUtils
from dm_utils import DMIndexer
from cfgm_common.vnc_db import DBBase
from cfgm_common.uve.physical_router.ttypes import *
from cfgm_common.exceptions import ResourceExistsError
from vnc_api.vnc_api import *
import socket
import gevent
import traceback
from gevent import queue
from cfgm_common.vnc_object_db import VncObjectDBClient
from netaddr import IPAddress
from cfgm_common.zkclient import IndexAllocator
from cfgm_common import vnc_greenlets
from sandesh_common.vns.constants import DEVICE_MANAGER_KEYSPACE_NAME
from time import gmtime, strftime
from cfgm_common.uve.physical_router_config.ttypes import *
from cfgm_common.uve.service_status.ttypes import *


class DBBaseDM(DBBase):
    obj_type = __name__
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


class PhysicalRouterDM(DBBaseDM):
    _dict = {}
    obj_type = 'physical_router'
    _sandesh = None

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_networks = set()
        self.logical_routers = set()
        self.bgp_router = None
        self.physical_router_role = None
        self.config_manager = None
        self.service_endpoints = set()
        self.router_mode = None
        self.e2_service_index = 0
        self.e2_service_providers = set()
        self.nc_q = queue.Queue(maxsize=1)
        self.vn_ip_map = {'irb': {}, 'lo0': {}}
        self.ae_id_map = {}
        self.allocated_asn = None
        self.config_sent = False
        self.ae_index_allocator = DMIndexer(
            DMUtils.get_max_ae_device_count(), DMIndexer.ALLOC_DECREMENT)
        self.init_cs_state()
        self.config_manager = None
        self.ansible_manager = None
        self.fabric = None
        self.node_profile = None
        self.nc_handler_gl = None
        self.update(obj_dict)
        if self.config_manager:
            self.set_conf_sent_state(False)
            self.config_repush_interval = PushConfigState.get_repush_interval()
            self.nc_handler_gl = vnc_greenlets.VncGreenlet("VNC Device Manager",
                                                           self.nc_handler)
    # end __init__

    def reinit_device_plugin(self):
        plugin_params = {
                "physical_router": self
            }
        dm_instance = self._manager
        vnc_lib = dm_instance.get_vnc()
        if not self.ansible_manager:
            self.ansible_manager = AnsibleBase.plugin(self.vendor, self.product,
                                                      plugin_params, vnc_lib,
                                                      self._logger)
        else:
            if self.ansible_manager.verify_plugin(self.physical_router_role):
                self.ansible_manager.update()
            else:
                self.ansible_manager.clear()
                self.ansible_manager = AnsibleBase.plugin(self.vendor,
                                                          self.product,
                                                          plugin_params,
                                                          vnc_lib, self._logger)
        if PushConfigState.is_push_mode_ansible() and\
                not self.no_ansible_support_for_role():
            self.config_manager = self.ansible_manager
            return
        if not self.config_manager:
            self.config_manager = DeviceConf.plugin(self.vendor, self.product,
                                                 plugin_params, self._logger)
        else:
            if self.config_manager.verify_plugin(self.vendor, self.product,
                                                 self.physical_router_role):
                self.config_manager.update()
            else:
                self.config_manager.clear()
                self.config_manager = DeviceConf.plugin(self.vendor,
                                                        self.product,
                                                        plugin_params,
                                                        self._logger)
    # end reinit_device_plugin

    def no_ansible_support_for_role(self):
        if not self.physical_router_role:
            return True
        if self.physical_router_role.lower() not in ["leaf", "spine"]:
            return True
        return False

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.management_ip = obj.get('physical_router_management_ip')
        self.loopback_ip = obj.get('physical_router_loopback_ip', '')
        self.dataplane_ip = obj.get(
            'physical_router_dataplane_ip') or self.loopback_ip
        self.vendor = obj.get('physical_router_vendor_name') or ''
        self.product = obj.get('physical_router_product_name') or ''
        self.device_family = obj.get('physical_router_device_family')
        self.vnc_managed = obj.get('physical_router_vnc_managed')
        self.physical_router_role = obj.get('physical_router_role')
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
        self.update_multiple_refs('service_endpoint', obj)
        self.update_multiple_refs('e2_service_provider', obj)
        self.update_single_ref('node_profile', obj)
        self.allocate_asn()
        self.reinit_device_plugin()
    # end update

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
                        self._logger.debug("physical router: allocated asn %d"
                                           % self.allocated_asn)
                        return True
                if namespace.asn_ranges is not None:
                    for asn_range in namespace.asn_ranges:
                        if asn_range[0] <= self.allocated_asn <= asn_range[1]:
                            self._logger.debug(
                                "physical router: allocated asn %d" %
                                self.allocated_asn)
                            return True
        self._logger.debug("physical router: asn not allocated")
        return False
    # end verify_allocated_asn

    def allocate_asn(self):
        if self.fabric is None:
            return
        fabric = FabricDM.get(self.fabric)
        if fabric is None:
            return
        if self.verify_allocated_asn(fabric):
            return

        # get the configured asn ranges for this fabric
        asn_ranges = []
        for namespace_uuid in fabric.fabric_namespaces:
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

    def delete_obj(self):
        if self.nc_handler_gl:
            gevent.kill(self.nc_handler_gl)
        if self.is_vnc_managed() and self.is_conf_sent():
            self.config_manager.push_conf(is_delete=True)
            self.config_manager.clear()
        if self.ansible_manager is not None and\
                self.ansible_manager != self.config_manager:
            self.ansible_manager.initialize()
            self.ansible_manager.underlay_config(is_delete=True)
        self._object_db.delete_pr(self.uuid)
        self.uve_send(True)
        self.update_single_ref('bgp_router', {})
        self.update_multiple_refs('virtual_network', {})
        self.update_multiple_refs('logical_router', {})
        self.update_multiple_refs('service_endpoint', {})
        self.update_multiple_refs('e2_service_provider', {})
        self.update_single_ref('fabric', {})
        self.update_single_ref('node_profile', {})
    # end delete_obj

    @classmethod
    def reset(cls):
        for obj in cls._dict.values():
            if obj.config_manager:
                obj.config_manager.clear()
        cls._dict = {}
    # end reset

    def is_junos_service_ports_enabled(self):
        if (self.junos_service_ports is not None
                and self.junos_service_ports.get('service_port') is not None):
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
                if self.ansible_manager is not None and\
                        self.ansible_manager != self.config_manager:
                    self.ansible_manager.initialize()
                    self.ansible_manager.underlay_config()
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
        vn_subnet_set = self._object_db.get_pr_vn_set(self.uuid)
        for vn_subnet in vn_subnet_set:
            subnet = vn_subnet[0]
            ip_used_for = vn_subnet[1]
            ip = self._object_db.get_ip(self.uuid + ':' + subnet, ip_used_for)
            if ip:
                self.vn_ip_map[ip_used_for][subnet] = ip
        ae_id_map = self._object_db.get_pr_ae_id_map(self.uuid)
        if not ae_id_map:
            return
        for esi, ae_id in ae_id_map.items():
            ae_id = self._object_db.get_ae_id(self.uuid + ':' + esi)
            if ae_id:
                self.ae_id_map[esi] = ae_id
                self.ae_index_allocator.reserve_index(ae_id)
        pr_asn_map = self._object_db.get_pr_asn_map(self.uuid)
        if not pr_asn_map:
            return
        asn = self._object_db.get_asn_for_pr(self.uuid)
        if asn:
            self.allocated_asn = asn

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
            self._logger.error("Exception: %s" % (str(e)))
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
            self._logger.error("Exception: %s" % (str(e)))
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

    def evaluate_ae_id_map(self, esi_map):
        # esi_map contains aggr list of latest esis need to be programmed
        # self.ae_id_map contains current status of allocated esis/aes
        esis = set(esi_map.keys())
        old_set = set(self.ae_id_map.keys())
        delete_set = old_set.difference(esis)
        create_set = esis.difference(old_set)

        for esi in delete_set:
            # get index allocated, make it available and remove entry from db,
            # finally remove from in-memory
            index = self.ae_id_map[esi]
            self.ae_index_allocator.free_index(index)
            self._object_db.delete_ae_id(self.uuid, esi)
            pi_set = PhysicalInterfaceDM.get_esi_interfaces(esi)
            pi_list = list(pi_set) if pi_set else []
            # set_parent_id modifies pi_set, hence copied to new list
            for pi_uuid in pi_list:
                pi = PhysicalInterfaceDM.get(pi_uuid)
                pi.set_parent_ae_id(None)
            del self.ae_id_map[esi]

        for esi in create_set:
            # get next av index, assign it to esi and persist in db,
            # finally assign to esi in in-memory ds
            index = self.ae_index_allocator.find_next_available_index()
            if index == -1:
                self._logger.error("Can't allocate(exhausted) index for pr/esi:" \
                                             " (%s, %s)"%(self.uuid, esi))
                continue
            self._object_db.add_ae_id(self.uuid, esi, index)
            self.ae_index_allocator.reserve_index(index)
            self.ae_id_map[esi] = index
            pi_list = esi_map.get(esi)
            for pi in pi_list:
                pi.set_parent_ae_id(index)
    # end evaluate_ae_id_map

    def evaluate_vn_irb_ip_map(self, vn_set, fwd_mode, ip_used_for, ignore_external=False):
        new_vn_ip_set = set()
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

        old_set = set(self.vn_ip_map[ip_used_for].keys())
        delete_set = old_set.difference(new_vn_ip_set)
        create_set = new_vn_ip_set.difference(old_set)
        for vn_subnet in delete_set:
            (vn_uuid, subnet_prefix) = vn_subnet.split(':', 1)
            ret = self.free_ip(vn_uuid, self.vn_ip_map[ip_used_for][vn_subnet])
            if ret == False:
                self._logger.error("Unable to free ip for vn/subnet/pr "
                                   "(%s/%s/%s)" % (
                    vn_uuid,
                    subnet_prefix,
                    self.uuid))

            ret = self._object_db.delete_ip(
                       self.uuid + ':' + vn_uuid + ':' + subnet_prefix, ip_used_for)
            if ret == False:
                self._logger.error("Unable to free ip from db for vn/subnet/pr "
                                   "(%s/%s/%s)" % (
                    vn_uuid,
                    subnet_prefix,
                    self.uuid))
                continue

            self._object_db.delete_from_pr_map(self.uuid, vn_subnet, ip_used_for)
            del self.vn_ip_map[ip_used_for][vn_subnet]

        for vn_subnet in create_set:
            (vn_uuid, subnet_prefix) = vn_subnet.split(':', 1)
            vn = VirtualNetworkDM.get(vn_uuid)
            subnet_uuid = vn.gateways[subnet_prefix].get('subnet_uuid')
            (sub, length) = subnet_prefix.split('/')
            ip_addr = self.reserve_ip(vn_uuid, subnet_uuid)
            if ip_addr is None:
                self._logger.error("Unable to allocate ip for vn/subnet/pr "
                                   "(%s/%s/%s)" % (
                    vn_uuid,
                    subnet_prefix,
                    self.uuid))
                continue
            ret = self._object_db.add_ip(self.uuid + ':' + vn_uuid + ':' + subnet_prefix,
                                         ip_used_for, ip_addr + '/' + length)
            if ret == False:
                self._logger.error("Unable to store ip for vn/subnet/pr "
                                   "(%s/%s/%s)" % (
                    self.uuid,
                    subnet_prefix,
                    self.uuid))
                if self.free_ip(vn_uuid, ip_addr) == False:
                    self._logger.error("Unable to free ip for vn/subnet/pr "
                                       "(%s/%s/%s)" % (
                        self.uuid,
                        subnet_prefix,
                        self.uuid))
                continue
            self._object_db.add_to_pr_map(self.uuid, vn_subnet, ip_used_for)
            self.vn_ip_map[ip_used_for][vn_subnet] = ip_addr + '/' + length
    # end evaluate_vn_irb_ip_map

    def is_vnc_managed(self):

        if not self.vnc_managed:
            self._logger.info("vnc managed property must be set for a physical router to get auto "
                "configured, ip: %s, not pushing netconf message" % (self.management_ip))
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
        if self.is_conf_sent() and (not self.is_vnc_managed() or not self.bgp_router):
            if not self.config_manager:
                self.uve_send()
                return False
            # user must have unset the vnc managed property
            self.config_manager.push_conf(is_delete=True)
            if self.config_manager.retry():
                # failed commit: set repush interval upto max value
                self.config_repush_interval = min([2 * self.config_repush_interval,
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
            return ('_contrail-' + si_obj.name + '-' + interface_type
                    + '-sc-entry-point')

    def allocate_pnf_resources(self, vmi):
        resources = self._object_db.get_pnf_resources(
            vmi, self.uuid)
        network_id = int(resources['network_id'])
        if vmi.service_interface_type == "left":
            ip = str(IPAddress(network_id*4+1))
        if vmi.service_interface_type == "right":
            ip = str(IPAddress(network_id*4+2))
        ip = ip + "/30"
        return {
            "ip_address": ip,
            "vlan_id": resources['vlan_id'],
            "unit_id": resources['unit_id']}
    # end

    def compute_pnf_static_route(self, ri_obj, pnf_dict):
        """
        Compute all the static route for the pnfs on the device
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
        prefrence = 0
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
    # end

    def push_config(self):
        if not self.config_manager:
            self._logger.info("plugin not found for vendor family(%s:%s), \
                  ip: %s, not pushing netconf message" % (str(self.vendor),
                                     str(self.product), self.management_ip))
            return
        if self.delete_config() or not self.is_vnc_managed():
            return
        self.config_manager.initialize()
        if not self.config_manager.validate_device():
            self._logger.error("physical router: %s, device config validation failed. "
                  "device configuration=%s" % (self.uuid, \
                           str(self.config_manager.get_device_config())))
            return
        config_size = self.config_manager.push_conf()
        if not config_size:
            return
        self.set_conf_sent_state(True)
        self.uve_send()
        if self.config_manager.retry():
            # failed commit: set repush interval upto max value
            self.config_repush_interval = min([2 * self.config_repush_interval,
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
            (last_config_size/1000) * PushConfigState.get_push_delay_per_kb())
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
            pr_trace.last_commit_time = \
                   commit_stats.get('last_commit_time', '')
            pr_trace.last_commit_duration = \
                   commit_stats.get('last_commit_duration', 0)
            pr_trace.commit_status_message = \
                   commit_stats.get('commit_status_message', '')
            pr_trace.total_commits_sent_since_up = \
                   commit_stats.get('total_commits_sent_since_up', 0)
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
        encapsulation_priorities = obj.get('encapsulation_priorities')
        if encapsulation_priorities:
            new_global_encapsulation_priorities = encapsulation_priorities.get("encapsulation")
            if new_global_encapsulation_priorities:
                new_global_encapsulation_priority = new_global_encapsulation_priorities[0]
        new_global_forwarding_mode = obj.get('forwarding_mode')
        if (GlobalVRouterConfigDM.global_vxlan_id_mode !=
                new_global_vxlan_id_mode or
            GlobalVRouterConfigDM.global_forwarding_mode !=
                new_global_forwarding_mode or
             GlobalVRouterConfigDM.global_encapsulation_priority !=
                new_global_encapsulation_priority):
            GlobalVRouterConfigDM.global_vxlan_id_mode = \
                new_global_vxlan_id_mode
            GlobalVRouterConfigDM.global_forwarding_mode = \
                new_global_forwarding_mode
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

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
    # end delete
# end GlobalVRouterConfigDM

class GlobalSystemConfigDM(DBBaseDM):
    _dict = {}
    obj_type = 'global_system_config'
    global_asn = None
    ip_fabric_subnets = None

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_routers = set()
        self.node_profiles = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        GlobalSystemConfigDM.global_asn = obj.get('autonomous_system')
        GlobalSystemConfigDM.ip_fabric_subnets = obj.get('ip_fabric_subnets')
        self.set_children('physical_router', obj)
        self.set_children('node_profile', obj)
    # end update

    @classmethod
    def get_global_asn(cls):
        return cls.global_asn

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
    # end delete
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
        self.parent_ae_id = None
        obj = self.update(obj_dict)
        self.add_to_parent(obj)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.physical_router = self.get_parent_uuid(obj)
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
        self.name = obj.get('fq_name')[-1]
        self.esi = obj.get('ethernet_segment_identifier')
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('physical_interface', obj)
        return obj
    # end update

    @classmethod
    def get_esi_interfaces(cls, esi):
        return cls._esi_map.get(esi)

    def set_parent_ae_id(self, ae_id):
        self.parent_ae_id = ae_id
        if ae_id:
            if not self.esi:
                self._logger.error("Invalid pi state, trying to set ae for non "
                              "esi interface (%s:%s)" % (self.uuid, self.name))
                return
            PhysicalInterfaceDM._esi_map.setdefault(self.esi, set()).add(self.uuid)
        else:
            if not self.esi:
                return
            pi_set = PhysicalInterfaceDM._esi_map.get(self.esi)
            if pi_set:
                pi_set.discard(self.uuid)
                if not pi_set:
                    del PhysicalInterfaceDM._esi_map[self.esi]
    # end set_parent_ae_id

    def get_parent_ae_id(self):
        return self.parent_ae_id
    # end get_parent_ae_id

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.set_parent_ae_id(None)
        obj.remove_from_parent()
        del cls._dict[uuid]
    # end delete
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
        if obj['parent_type'] == 'physical-router':
            self.physical_router = self.get_parent_uuid(obj)
            self.physical_interface = None
        else:
            self.physical_interface = self.get_parent_uuid(obj)
            self.physical_router = None

        self.name = obj['display_name']
        self.vlan_tag = obj.get('logical_interface_vlan_tag', 0)
        self.li_type = obj.get('logical_interface_type', 0)
        self.update_single_ref('virtual_machine_interface', obj)
        self.update_single_ref('instance_ip', obj)
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

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        if obj.physical_interface:
            parent = PhysicalInterfaceDM.get(obj.physical_interface)
        elif obj.physical_router:
            parent = PhysicalInterfaceDM.get(obj.physical_router)
        if parent:
            parent.logical_interfaces.discard(obj.uuid)
        obj.update_single_ref('virtual_machine_interface', {})
        obj.remove_from_parent()
        del cls._dict[uuid]
    # end delete
# end LogicalInterfaceDM


class FloatingIpDM(DBBaseDM):
    _dict = {}
    obj_type = 'floating_ip'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interface = None
        self.floating_ip_address = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]
        self.floating_ip_address = obj.get("floating_ip_address")
        self.public_network = self.get_pool_public_network(
            self.get_parent_uuid(obj))
        self.update_single_ref('virtual_machine_interface', obj)
    # end update

    def get_pool_public_network(self, pool_uuid):
        pool_obj = self.read_obj(pool_uuid, "floating_ip_pool")
        if pool_obj is None:
            return None
        return self.get_parent_uuid(pool_obj)
    # end get_pool_public_network

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('virtual_machine_interface', {})
        del cls._dict[uuid]
    # end delete

# end FloatingIpDM


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
        self.update_single_ref('logical_interface', obj)
    # end update

    def delete_obj(self):
        self.update_single_ref('virtual_machine_interface', {})
        self.update_single_ref('logical_interface', {})
    # end delete

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
        self.service_interface_type = None
        self.port_tuple = None
        self.routing_instances = set()
        self.security_groups = set()
        self.service_instance = None
        self.service_endpoint = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]
        if obj.get('virtual_machine_interface_properties', None):
            self.params = obj['virtual_machine_interface_properties']
            self.service_interface_type = self.params.get(
                'service_interface_type', None)
        self.bindings = obj.get('virtual_machine_interface_bindings') or {}
        self.device_owner = obj.get("virtual_machine_interface_device_owner") or ''
        self.update_multiple_refs('logical_interface', obj)
        self.update_single_ref('virtual_network', obj)
        self.update_single_ref('floating_ip', obj)
        self.update_single_ref('instance_ip', obj)
        self.update_single_ref('physical_interface', obj)
        self.update_multiple_refs('routing_instance', obj)
        self.update_multiple_refs('security_group', obj)
        self.update_single_ref('port_tuple', obj)
        self.service_instance = None
        if self.port_tuple:
            pt = PortTupleDM.get(self.port_tuple)
            if pt:
                self.service_instance = pt.parent_uuid
        self.update_single_ref('service_endpoint', obj)
    # end update

    def is_device_owner_bms(self):
        if self.logical_interfaces and len(self.logical_interfaces) >= 1 and \
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
        obj.update_single_ref('port_tuple', {})
        obj.update_single_ref('service_endpoint', {})
        del cls._dict[uuid]
    # end delete

# end VirtualMachineInterfaceDM

class LogicalRouterDM(DBBaseDM):
    _dict = {}
    obj_type = 'logical_router'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_routers = set()
        self.virtual_machine_interfaces = set()
        # internal virtual-network
        self.virtual_network = None
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
        self.update_multiple_refs('physical_router', obj)
        self.update_multiple_refs('virtual_machine_interface', obj)
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
            if vmi:
                vn_list.append(vmi.virtual_network)
        return vn_list
    # end get_connected_networks

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('physical_router', {})
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.update_single_ref('virtual_network', None)
        del cls._dict[uuid]
    # end delete

# end LogicalRouterDM


class VirtualNetworkDM(DBBaseDM):
    _dict = {}
    obj_type = 'virtual_network'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.physical_routers = set()
        self.logical_router = None
        self.router_external = False
        self.forwarding_mode = None
        self.gateways = None
        self.instance_ip_map = {}
        self.update(obj_dict)
    # end __init__

    def set_logical_router(self, name):
        if DMUtils.get_lr_internal_vn_prefix() in name:
            lr_uuid = DMUtils.extract_lr_uuid_from_internal_vn_name(name)
            lr_obj = LogicalRouterDM.get(lr_uuid)
            if lr_obj:
                self.logical_router = lr_obj.uuid
                lr_obj.virtual_network = self.uuid
    # end set_logical_router

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
            self.set_logical_router(obj.get("fq_name")[-1])
        self.update_multiple_refs('physical_router', obj)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]
        try:
            self.router_external = obj['router_external']
        except KeyError:
            self.router_external = False
        self.vn_network_id = obj.get('virtual_network_network_id')
        self.virtual_network_properties = obj.get('virtual_network_properties')
        self.set_forwarding_mode(obj)
        self.routing_instances = set([ri['uuid'] for ri in
                                      obj.get('routing_instances', [])])
        self.virtual_machine_interfaces = set(
            [vmi['uuid'] for vmi in
             obj.get('virtual_machine_interface_back_refs', [])])
        self.gateways = DMUtils.get_network_gateways(obj.get('network_ipam_refs', []))
    # end update

    def get_prefixes(self):
        return set(self.gateways.keys())
    # end get_prefixes

    def get_vxlan_vni(self):
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
                if fip is None or inst_ip is None:
                    continue
                instance_ip = inst_ip.instance_ip_address
                floating_ip = fip.floating_ip_address
                public_vn = VirtualNetworkDM.get(fip.public_network)
                if public_vn is None or public_vn.vn_network_id is None:
                    continue
                public_vrf_name = DMUtils.make_vrf_name(public_vn.fq_name[-1],
                                                   public_vn.vn_network_id, 'l3')
                self.instance_ip_map[instance_ip] = {
                    'floating_ip': floating_ip,
                    'vrf_name': public_vrf_name
                }
    # end update_instance_ip_map

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('physical_router', {})
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


class ServiceInstanceDM(DBBaseDM):
    _dict = {}
    obj_type = 'service_instance'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.fq_name = None
        self.name = None
        self.port_tuples = set()
        self.update(obj_dict)
    # end

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = "-".join(self.fq_name)
    # end

    @classmethod
    def delete(cls, uuid):
        obj = cls._dict[uuid]
        obj._object_db.delete_pnf_resources(uuid)
        del cls._dict[uuid]
    # end


class PortTupleDM(DBBaseDM):
    _dict = {}
    obj_type = 'port_tuple'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        obj = self.update(obj_dict)
        self.add_to_parent(obj)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.fq_name = obj['fq_name']
        self.name = self.fq_name[-1]
        self.parent_uuid = self.get_parent_uuid(obj)
        self.update_multiple_refs('virtual_machine_interface', obj)
        for vmi in self.virtual_machine_interfaces:
            vmi_obj = VirtualMachineInterfaceDM.get(vmi)
            if vmi_obj and not vmi_obj.service_instance:
                vmi_obj.service_instance = self.parent_uuid
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_multiple_refs('virtual_machine_interface', {})
        obj.remove_from_parent()
        del cls._dict[uuid]
    # end delete
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
                        self.user_creds    = pr.user_credentials
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
                    self.service_status = \
                    pr.config_manager.get_service_status(service_params)
                    self.uve_send()

    def uve_send(self):
        mydata=self.service_status
        if self.service_status is not None:
            last_timestamp = strftime("%Y-%m-%d %H:%M:%S", gmtime())
            pr_trace = UveServiceStatus(
                    name=self.name,
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


class FabricDM(DBBaseDM):
    _dict = {}
    obj_type = 'fabric'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
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
        self.name = obj['fq_name'][-1]

        # Get the 'loopback' type virtual network
        self.lo0_ipam_subnet =\
            self._get_ipam_subnets_for_virtual_network(obj, 'loopback')

        # Get the 'ip_fabric' type virtual network
        self.ip_fabric_ipam_subnet = \
            self._get_ipam_subnets_for_virtual_network(obj, 'ip_fabric')
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
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
    # end update
# end class NodeProfileDM


class RoleConfigDM(DBBaseDM):
    _dict = {}
    obj_type = 'role_config'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.name = None
        self.node_profile = None
        self.config = None
        self.job_template = None
        self.job_template_fq_name = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.node_profile = self.get_parent_uuid(obj)
        self.add_to_parent(obj)
        self.config = obj.get('role_config_config')
        self.update_single_ref('job_template', obj)
        if self.job_template is not None:
            self.job_template_fq_name =\
                self._object_db.uuid_to_fq_name(self.job_template)
        else:
            self.job_template_fq_name = None
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
    _PR_AE_ID_CF = 'dm_pr_ae_id_table'
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
    def get_instance(cls, manager=None, zkclient=None):
        if cls.dm_object_db_instance is None:
            cls.dm_object_db_instance = DMCassandraDB(manager, zkclient)
        return cls.dm_object_db_instance
    # end

    @classmethod
    def clear_instance(cls):
        cls.dm_object_db_instance = None
    # end

    def __init__(self, manager, zkclient):
        self._zkclient = zkclient
        self._manager = manager
        self._args = manager._args

        keyspaces = {
            self._KEYSPACE: {self._PR_VN_IP_CF: {},
                             self._PR_AE_ID_CF: {},
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
            manager.logger.log, credential=cred,
            ssl_enabled=self._args.cassandra_use_ssl,
            ca_certs=self._args.cassandra_ca_certs)

        self.pr_vn_ip_map = {}
        self.pr_ae_id_map = {}
        self.pr_asn_map = {}
        self.asn_pr_map = {}
        self.init_pr_map()
        self.init_pr_ae_map()
        self.init_pr_asn_map()

        self.pnf_vlan_allocator_map = {}
        self.pnf_unit_allocator_map = {}
        self.pnf_network_allocator = IndexAllocator(
            zkclient, self._zk_path_pfx+self._PNF_NETWORK_ALLOC_PATH,
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
                self._zk_path_pfx+self._PNF_VLAN_ALLOC_PATH+pr_id+'/',
                self._PNF_MAX_VLAN)
        )

    def get_pnf_unit_allocator(self, pi_id):
        return self.pnf_unit_allocator_map.setdefault(
            pi_id,
            IndexAllocator(
                self._zkclient,
                self._zk_path_pfx+self._PNF_UNIT_ALLOC_PATH+pi_id+'/',
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
                    self.get_pnf_unit_allocator(vmi_obj.physical_interface).delete(
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
        pr_entries = dict(cf.get_range(column_count=0, filter_empty=False))
        for key in pr_entries.keys():
            key_data = key.split(':', 1)
            cols = pr_entries[key] or {}
            for col in cols.keys():
                ip_used_for = DMUtils.get_ip_used_for_str(col)
                (pr_uuid, vn_subnet_uuid) = (key_data[0], key_data[1])
                self.add_to_pr_map(pr_uuid, vn_subnet_uuid, ip_used_for)
    # end

    def init_pr_ae_map(self):
        cf = self.get_cf(self._PR_AE_ID_CF)
        pr_entries = dict(cf.get_range(column_count=0, filter_empty=False))
        for key in pr_entries.keys():
            key_data = key.split(':', 1)
            (pr_uuid, esi) = (key_data[0], key_data[1])
            ae_index = pr_entries[key] or {}
            ae_id = ae_index.get("index")
            if ae_id:
                if pr_uuid not in self.pr_ae_id_map:
                    self.pr_ae_id_map[pr_uuid] = {}
                self.pr_ae_id_map[pr_uuid][esi] = ae_id
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

    def get_ae_id(self, key):
        return self.get_one_col(self._PR_AE_ID_CF, key, "index")
    # end

    def get_asn_for_pr(self, pr_uuid):
        return self.pr_asn_map.get(pr_uuid)
    # end get_asn_for_pr

    def get_pr_for_asn(self, asn):
        return self.asn_pr_map.get(asn)
    # end get_pr_for_asn

    def add_ip(self, key, ip_used_for, ip):
        self.add(self._PR_VN_IP_CF, key, {DMUtils.get_ip_cs_column_name(ip_used_for): ip})
    # end

    def add_ae_id(self, pr_uuid, esi, ae_id):
        key = pr_uuid + ':' + esi
        self.add(self._PR_AE_ID_CF, key, {"index": ae_id})
        if pr_uuid in self.pr_ae_id_map:
            self.pr_ae_id_map[pr_uuid][esi] = ae_id
        else:
            self.pr_ae_id_map[pr_uuid] = {}
            self.pr_ae_id_map[pr_uuid][esi] = ae_id
    # end

    def add_asn(self, pr_uuid, asn):
        self.add(self._PR_ASN_CF, pr_uuid, {'asn': asn})
        self.pr_asn_map[pr_uuid] = asn
        self.asn_pr_map[asn] = pr_uuid
    # end add_asn

    def delete_ip(self, key, ip_used_for):
        self.delete(self._PR_VN_IP_CF, key, [DMUtils.get_ip_cs_column_name(ip_used_for)])
    # end

    def delete_ae_id(self, pr_uuid, esi):
        key = pr_uuid + ':' + esi
        self.delete(self._PR_AE_ID_CF, key)
        del self.pr_ae_id_map[pr_uuid][esi]
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
                self._logger.error("Unable to free ip from db for vn/pr/subnet/ip_used_for "
                                   "(%s/%s/%s)" % (pr_uuid, vn_subnet, ip_used_for))
        esi_map = self.pr_ae_id_map.get(pr_uuid, {})
        for esi, ae_id in esi_map.values():
            ret = self.delete(self._PR_AE_ID_CF, pr_uuid + ':' + esi)
            if ret == False:
                self._logger.error("Unable to free ae id from db for pr/esi"
                                   "(%s/%s)" % (pr_uuid, esi))
        asn = self.pr_asn_map.pop(pr_uuid, None)
        if asn is not None:
            self.asn_pr_map.pop(asn, None)
            ret = self.delete(self._PR_ASN_CF, pr_uuid)
            if not ret:
                self._logger.error("Unable to free asn from db for pr %s" %
                                   pr_uuid)
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

    def get_pr_ae_id_map(self, pr_uuid):
        return self.pr_ae_id_map.get(pr_uuid, {})
    # end

    @classmethod
    def get_db_info(cls):
        db_info = [(cls._KEYSPACE, [cls._PR_VN_IP_CF])]
        return db_info
    # end get_db_info

# end
