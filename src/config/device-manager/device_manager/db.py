#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for physical router
configuration manager
"""
from physical_router_config import PhysicalRouterConfig
from physical_router_config import JunosInterface
from physical_router_config import PushConfigState
from dm_utils import DMUtils
from sandesh.dm_introspect import ttypes as sandesh
from cfgm_common.vnc_db import DBBase
from cfgm_common.uve.physical_router.ttypes import *
from vnc_api.vnc_api import *
import copy
import socket
import gevent
import traceback
from gevent import queue
from cfgm_common.vnc_object_db import VncObjectDBClient
from netaddr import IPAddress
from cfgm_common.zkclient import IndexAllocator
from cfgm_common import vnc_greenlets
from sandesh_common.vns.constants import DEVICE_MANAGER_KEYSPACE_NAME


class DBBaseDM(DBBase):
    obj_type = __name__


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
        self.params = obj['bgp_router_parameters']
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
        if self.params['address'] is not None:
            bgp_router_ips[self.name] = self.params['address']

        for peer_uuid in self.bgp_routers:
            peer = BgpRouterDM.get(peer_uuid)
            if peer is None or peer.params['address'] is None:
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
        self.bgp_router = None
        self.config_manager = None
        self.nc_q = queue.Queue(maxsize=1)
        self.vn_ip_map = {'irb': {}, 'lo0': {}}
        self.device_config = {}
        self.init_cs_state()
        self.update(obj_dict)
        self.config_manager = PhysicalRouterConfig(
            self.management_ip, self.user_credentials, self.vendor,
            self.product, self._logger)
        self.set_conf_sent_state(False)
        self.config_repush_interval = PushConfigState.get_repush_interval()
        self.nc_handler_gl = vnc_greenlets.VncGreenlet("VNC Device Manager",
                                                       self.nc_handler)
        self.uve_send()
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.management_ip = obj.get('physical_router_management_ip')
        self.loopback_ip = obj.get('physical_router_loopback_ip', '')
        self.dataplane_ip = obj.get('physical_router_dataplane_ip') or self.loopback_ip
        self.vendor = obj.get('physical_router_vendor_name', '')
        self.product = obj.get('physical_router_product_name', '')
        self.vnc_managed = obj.get('physical_router_vnc_managed')
        self.user_credentials = obj.get('physical_router_user_credentials')
        self.junos_service_ports = obj.get(
            'physical_router_junos_service_ports')
        self.update_single_ref('bgp_router', obj)
        self.update_multiple_refs('virtual_network', obj)
        self.physical_interfaces = set([pi['uuid'] for pi in
                                        obj.get('physical_interfaces', [])])
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
        if self.config_manager is not None:
            self.config_manager.update(
                self.management_ip, self.user_credentials, self.vendor,
                self.product)
            self.init_device_config()
    # end update

    def init_device_config(self):
        if not self.device_config:
            self.device_config = self.config_manager.get_device_config()
    # end init_device_config

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        if obj.is_vnc_managed() and obj.is_conf_sent():
            obj.config_manager.delete_bgp_config()
        obj._object_db.delete_pr(uuid)
        obj.uve_send(True)
        obj.update_single_ref('bgp_router', {})
        obj.update_multiple_refs('virtual_network', {})
        del cls._dict[uuid]
    # end delete

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

    def evaluate_vn_irb_ip_map(self, vn_set, fwd_mode, ip_used_for, ignore_external=False):
        new_vn_ip_set = set()
        for vn_uuid in vn_set:
            vn = VirtualNetworkDM.get(vn_uuid)
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

    def get_vn_li_map(self):
        vn_dict = {}
        for vn_id in self.virtual_networks:
            vn_dict[vn_id] = []

        li_set = self.logical_interfaces
        for pi_uuid in self.physical_interfaces:
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
                JunosInterface(li.name, li.li_type, li.vlan_tag))
        return vn_dict
    # end

    def is_vnc_managed(self):

        if (self.vendor is None or self.product is None or
                self.vendor.lower() != "juniper" or self.product.lower()[:2] != "mx"):
            self._logger.info("auto configuraion of physical router is not supported "
               "vendor family(%s:%s), ip: %s, not pushing netconf message" %
                              (str(self.vendor),  str(self.product), self.management_ip))
            return False

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
            # user must have unset the vnc managed property
            self.config_manager.delete_bgp_config()
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

    def config_pnf_logical_interface(self):
        pnf_dict = {}
        pnf_ris = set()
        # make it fake for now
        # sholud save to the database, the allocation
        self.vlan_alloc = {"max": 1}
        self.ip_alloc = {"max": -1}
        self.li_alloc = {}

        for pi_uuid in self.physical_interfaces:
            pi = PhysicalInterfaceDM.get(pi_uuid)
            if pi is None:
                continue
            for pi_pi_uuid in pi.physical_interfaces:
                pi_pi = PhysicalInterfaceDM.get(pi_pi_uuid)
                for pi_vmi_uuid in pi_pi.virtual_machine_interfaces:
                    allocate_li = False
                    pi_vmi = VirtualMachineInterfaceDM.get(pi_vmi_uuid)
                    if (pi_vmi is None or
                            pi_vmi.service_instance is None or
                            pi_vmi.service_interface_type is None):
                        continue
                    if pi_vmi.routing_instances:
                        for ri_id in pi_vmi.routing_instances:
                            ri_obj = RoutingInstanceDM.get(ri_id)
                            if ri_obj and ri_obj.routing_instances and ri_obj.service_chain_address:
                                pnf_ris.add(ri_obj)
                                # If this service is on a service chain, we need allocate
                                # a logic interface for its VMI
                                allocate_li = True

                    if allocate_li:
                        resources = self.allocate_pnf_resources(pi_vmi)
                        if (not resources or
                                not resources["ip_address"] or
                                not resources["vlan_id"] or
                                not resources["unit_id"]):
                            self._logger.error(
                                "Cannot allocate PNF resources for "
                                "Virtual Machine Interface" + pi_vmi_uuid)
                            return
                        logical_interface = JunosInterface(
                            pi.name + '.' + resources["unit_id"],
                            "l3", resources["vlan_id"], resources["ip_address"])
                        self.config_manager.add_pnf_logical_interface(
                            logical_interface)
                        lis = pnf_dict.setdefault(
                            pi_vmi.service_instance,
                            {"left": [], "right": [],
                                "mgmt": [], "other": []}
                        )[pi_vmi.service_interface_type]
                        lis.append(logical_interface)

        return (pnf_dict, pnf_ris)
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

    def add_pnf_vrfs(self, first_vrf, pnf_dict, pnf_ris):
        is_first_vrf = False
        is_left_first_vrf = False
        for ri_obj in pnf_ris:
            if ri_obj in first_vrf:
                is_first_vrf = True
            else:
                is_first_vrf = False
            export_set = copy.copy(ri_obj.export_targets)
            import_set = copy.copy(ri_obj.import_targets)
            for ri2_id in ri_obj.routing_instances:
                ri2 = RoutingInstanceDM.get(ri2_id)
                if ri2 is None:
                    continue
                import_set |= ri2.export_targets

            pnf_inters = set()
            static_routes = self.compute_pnf_static_route(ri_obj, pnf_dict)
            if_type = ""
            for vmi_uuid in ri_obj.virtual_machine_interfaces:
                vmi_obj = VirtualMachineInterfaceDM.get(vmi_uuid)
                if vmi_obj.service_instance is not None:
                    si_obj = ServiceInstanceDM.get(vmi_obj.service_instance)
                    if_type = vmi_obj.service_interface_type
                    pnf_li_inters = pnf_dict[
                        vmi_obj.service_instance][if_type]
                    if if_type == 'left' and is_first_vrf:
                        is_left_first_vrf = True
                    else:
                        is_left_first_vrf = False

                    for pnf_li in pnf_li_inters:
                        pnf_inters.add(pnf_li)

            if pnf_inters:
                vrf_name = self.get_pnf_vrf_name(
                    si_obj, if_type, is_left_first_vrf)
                vrf_interfaces = pnf_inters
                ri_conf = { 'ri_name': vrf_name }
                ri_conf['import_targets'] = import_set
                ri_conf['export_targets'] = export_set
                ri_conf['interfaces'] = vrf_interfaces
                ri_conf['static_routes'] = static_routes
                ri_conf['no_vrf_table_label'] = True
                self.config_manager.add_routing_instance(ri_conf)

    def push_config(self):
        if self.delete_config() or not self.is_vnc_managed():
            return
        self.config_manager.reset_bgp_config()

        self.init_device_config()
        model = self.device_config.get('product-model')
        if 'mx' not in model.lower():
            self._logger.error("physical router: %s, product model is not supported. "
                      "device configuration=%s" % (self.uuid, str(self.device_config)))
            self.device_config = {}
            return

        bgp_router = BgpRouterDM.get(self.bgp_router)
        if bgp_router:
            for peer_uuid, attr in bgp_router.bgp_routers.items():
                peer = BgpRouterDM.get(peer_uuid)
                if peer is None:
                    continue
                local_as = (bgp_router.params.get('local_autonomous_system') or
                               bgp_router.params.get('autonomous_system'))
                peer_as = (peer.params.get('local_autonomous_system') or
                               peer.params.get('autonomous_system'))
                external = (local_as != peer_as)
                self.config_manager.add_bgp_peer(peer.params['address'],
                                                 peer.params, attr, external, peer)
            self.config_manager.set_bgp_config(bgp_router.params, bgp_router)
            self.config_manager.set_global_routing_options(bgp_router.params)
            bgp_router_ips = bgp_router.get_all_bgp_router_ips()
            tunnel_ip = self.dataplane_ip
            if not tunnel_ip and bgp_router.params:
                tunnel_ip = bgp_router.params.get('address')
            if (tunnel_ip and self.is_valid_ip(tunnel_ip)):
                self.config_manager.add_dynamic_tunnels(
                    tunnel_ip,
                    GlobalSystemConfigDM.ip_fabric_subnets,
                    bgp_router_ips)

        if self.loopback_ip:
            self.config_manager.add_lo0_unit_0_interface(self.loopback_ip)

        vn_dict = self.get_vn_li_map()
        self.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l2_l3', 'irb', False)
        self.evaluate_vn_irb_ip_map(set(vn_dict.keys()), 'l3', 'lo0', True)
        vn_irb_ip_map = self.get_vn_irb_ip_map()

        first_vrf = []
        pnfs = self.config_pnf_logical_interface()
        pnf_dict = pnfs[0]
        pnf_ris = pnfs[1]

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
                        if ri2 in pnf_ris:
                            first_vrf.append(ri2)
                        if ri2 is None:
                            continue
                        import_set |= ri2.export_targets

                    if vn_obj.get_forwarding_mode() in ['l2', 'l2_l3']:
                        irb_ips = None
                        if vn_obj.get_forwarding_mode() == 'l2_l3':
                            irb_ips = vn_irb_ip_map['irb'].get(vn_id, [])

                        ri_conf = { 'ri_name': vrf_name_l2, 'vn': vn_obj }
                        ri_conf['is_l2'] = True
                        ri_conf['is_l2_l3'] = (vn_obj.get_forwarding_mode() == 'l2_l3')
                        ri_conf['import_targets'] = import_set
                        ri_conf['export_targets'] = export_set
                        ri_conf['prefixes'] = vn_obj.get_prefixes()
                        ri_conf['gateways'] = irb_ips
                        ri_conf['router_external'] = vn_obj.router_external
                        ri_conf['interfaces'] = interfaces
                        ri_conf['vni'] = vn_obj.get_vxlan_vni()
                        ri_conf['network_id'] = vn_obj.vn_network_id
                        ri_conf['highest_enapsulation_priority'] = \
                                  GlobalVRouterConfigDM.global_encapsulation_priority
                        self.config_manager.add_routing_instance(ri_conf)

                    if vn_obj.get_forwarding_mode() in ['l3', 'l2_l3']:
                        interfaces = []
                        lo0_ips = None
                        if vn_obj.get_forwarding_mode() == 'l2_l3':
                            interfaces = [
                                 JunosInterface(
                                'irb.' + str(vn_obj.vn_network_id),
                                'l3', 0)]
                        else:
                            lo0_ips = vn_irb_ip_map['lo0'].get(vn_id, [])
                        ri_conf = { 'ri_name': vrf_name_l3, 'vn': vn_obj }
                        ri_conf['is_l2_l3'] = (vn_obj.get_forwarding_mode() == 'l2_l3')
                        ri_conf['import_targets'] = import_set
                        ri_conf['export_targets'] = export_set
                        ri_conf['prefixes'] = vn_obj.get_prefixes()
                        ri_conf['router_external'] = vn_obj.router_external
                        ri_conf['interfaces'] = interfaces
                        ri_conf['gateways'] = lo0_ips
                        ri_conf['network_id'] = vn_obj.vn_network_id
                        self.config_manager.add_routing_instance(ri_conf)
                    break

            if (export_set is not None and
                    self.is_junos_service_ports_enabled() and
                    len(vn_obj.instance_ip_map) > 0):
                service_port_ids = DMUtils.get_service_ports(vn_obj.vn_network_id)
                if self.is_service_port_id_valid(service_port_ids[0]) == False:
                    self._logger.error("DM can't allocate service interfaces for "
                                       "(vn, vn-id)=(%s,%s)" % (
                        vn_obj.fq_name,
                        vn_obj.vn_network_id))
                else:
                    vrf_name = DMUtils.make_vrf_name(vn_obj.fq_name[-1],
                                                 vn_obj.vn_network_id, 'l3', True)
                    interfaces = []
                    service_ports = self.junos_service_ports.get(
                        'service_port')
                    interfaces.append(
                        JunosInterface(
                            service_ports[0] + "." + str(service_port_ids[0]),
                            'l3', 0))
                    interfaces.append(
                        JunosInterface(
                            service_ports[0] + "." + str(service_port_ids[1]),
                            'l3', 0))
                    ri_conf = { 'ri_name': vrf_name, 'vn': vn_obj }
                    ri_conf['import_targets'] = import_set
                    ri_conf['interfaces'] = interfaces
                    ri_conf['fip_map'] = vn_obj.instance_ip_map
                    ri_conf['network_id'] = vn_obj.vn_network_id
                    ri_conf['restrict_proxy_arp'] = vn_obj.router_external
                    self.config_manager.add_routing_instance(ri_conf)
        # Add PNF ri configuration
        self.add_pnf_vrfs(first_vrf, pnf_dict, pnf_ris)

        config_size = self.config_manager.send_bgp_config()
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

        commit_stats = self.config_manager.get_commit_stats()

        if self.is_vnc_managed():
            pr_trace.netconf_enabled_status = True
            pr_trace.last_commit_time = commit_stats['last_commit_time']
            pr_trace.last_commit_duration = commit_stats[
                'last_commit_duration']
            pr_trace.commit_status_message = commit_stats[
                'commit_status_message']
            pr_trace.total_commits_sent_since_up = commit_stats[
                'total_commits_sent_since_up']
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
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        GlobalSystemConfigDM.global_asn = obj.get('autonomous_system')
        GlobalSystemConfigDM.ip_fabric_subnets = obj.get('ip_fabric_subnets')
        self.set_children('physical_router', obj)
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

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interfaces = set()
        self.physical_interfaces = set()
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
        self.update_multiple_refs('virtual_machine_interface', obj)
        self.update_multiple_refs('physical_interface', obj)
        return obj
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
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

        self.vlan_tag = obj.get("logical_interface_vlan_tag", 0)
        self.li_type = obj.get("logical_interface_type", 0)
        self.update_single_ref('virtual_machine_interface', obj)
        self.name = obj['fq_name'][-1]
        return obj
    # end update

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
        self.uuid = uuid
        self.instance_ip_address = None
        self.virtual_machine_interface = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.instance_ip_address = obj.get("instance_ip_address")
        self.update_single_ref('virtual_machine_interface', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('virtual_machine_interface', {})
        del cls._dict[uuid]
    # end delete

# end InstanceIpDM


class VirtualMachineInterfaceDM(DBBaseDM):
    _dict = {}
    obj_type = 'virtual_machine_interface'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_network = None
        self.floating_ip = None
        self.instance_ip = None
        self.logical_interface = None
        self.physical_interface = None
        self.service_interface_type = None
        self.port_tuple = None
        self.routing_instances = set()
        self.service_instance = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        if obj.get('virtual_machine_interface_properties', None):
            self.params = obj['virtual_machine_interface_properties']
            self.service_interface_type = self.params.get(
                'service_interface_type', None)
        self.device_owner = obj.get("virtual_machine_interface_device_owner") or ''
        self.update_single_ref('logical_interface', obj)
        self.update_single_ref('virtual_network', obj)
        self.update_single_ref('floating_ip', obj)
        self.update_single_ref('instance_ip', obj)
        self.update_single_ref('physical_interface', obj)
        self.update_multiple_refs('routing_instance', obj)
        self.update_single_ref('port_tuple', obj)
        self.service_instance = None
        if self.port_tuple:
            pt = PortTupleDM.get(self.port_tuple)
            if pt:
                self.service_instance = pt.parent_uuid
    # end update

    def is_device_owner_bms(self):
        if self.logical_interface and self.device_owner.lower() in ['physicalrouter', 'physical-router']:
            return True
        return False
    # end

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('logical_interface', {})
        obj.update_single_ref('virtual_network', {})
        obj.update_single_ref('floating_ip', {})
        obj.update_single_ref('instance_ip', {})
        obj.update_single_ref('physical_interface', {})
        obj.update_multiple_refs('routing_instance', {})
        obj.update_single_ref('port_tuple', {})
        del cls._dict[uuid]
    # end delete

# end VirtualMachineInterfaceDM


class VirtualNetworkDM(DBBaseDM):
    _dict = {}
    obj_type = 'virtual_network'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_routers = set()
        self.router_external = False
        self.forwarding_mode = None
        self.gateways = None
        self.instance_ip_map = {}
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.update_multiple_refs('physical_router', obj)
        self.fq_name = obj['fq_name']
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
        self.add_to_parent(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.parent_uuid = self.get_parent_uuid(obj)
        self.update_multiple_refs('virtual_machine_interface', obj)
        for vmi in self.virtual_machine_interfaces:
            vmi_obj = VirtualMachineInterfaceDM.get(vmi)
            if vmi_obj and not vmi_obj.service_instance:
                vmi_obj.service_instance = self.parent_uuid
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


class DMCassandraDB(VncObjectDBClient):
    _KEYSPACE = DEVICE_MANAGER_KEYSPACE_NAME
    _PR_VN_IP_CF = 'dm_pr_vn_ip_table'
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
        if cls.dm_object_db_instance == None:
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
                             self._PNF_RESOURCE_CF: {}}}

        cass_server_list = self._args.cassandra_server_list
        cred = None
        if (self._args.cassandra_user is not None and
            self._args.cassandra_password is not None):
            cred = {'username': self._args.cassandra_user,
                    'password': self._args.cassandra_password}

        super(DMCassandraDB, self).__init__(
            cass_server_list, self._args.cluster_id, keyspaces, None,
            manager.logger.log, credential=cred)

        self.pr_vn_ip_map = {}
        self.init_pr_map()

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
        vlan_alloc.reserve(0)
        vlan_id = vlan_alloc.alloc(si_id)
        pr_set = self.get_si_pr_set(si_id)
        for other_pr_uuid in pr_set:
            if other_pr_uuid != pr_id:
                self.get_pnf_vlan_allocator(other_pr_uuid).reserve(vlan_id)
        unit_alloc = self.get_pnf_unit_allocator(pi_id)
        unit_alloc.reserve(0)
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

    def get_ip(self, key, ip_used_for):
        return self.get_one_col(self._PR_VN_IP_CF, key,
                      DMUtils.get_ip_cs_column_name(ip_used_for))
    # end

    def add_ip(self, key, ip_used_for, ip):
        self.add(self._PR_VN_IP_CF, key, {DMUtils.get_ip_cs_column_name(ip_used_for): ip})
    # end

    def delete_ip(self, key, ip_used_for):
        self.delete(self._PR_VN_IP_CF, key, [DMUtils.get_ip_cs_column_name(ip_used_for)])
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
