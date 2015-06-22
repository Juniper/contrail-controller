#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for physical router
configuration manager
"""
from vnc_api.common.exceptions import NoIdError
from physical_router_config import PhysicalRouterConfig
from sandesh.dm_introspect import ttypes as sandesh
from cfgm_common.vnc_db import DBBase
from cfgm_common.uve.physical_router.ttypes import *
import copy
import socket
import gevent
from gevent import queue

class BgpRouterDM(DBBase):
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
        if self.params is not None:
            if self.params.get('autonomous_system') is None:
                self.params['autonomous_system'] = GlobalSystemConfigDM.get_global_asn()
        self.update_single_ref('physical_router', obj)
        new_peers = {}
        for ref in obj.get('bgp_router_refs', []):
            new_peers[ref['uuid']] = ref['attr']
        for peer_id in set(self.bgp_routers.keys()) - set(new_peers.keys()):
            peer = BgpRouterDM.get(peer_id)
            if self.uuid in peer.bgp_routers:
                del peer.bgp_routers[self.uuid]
        for peer_id, attrs in new_peers.items():
            peer = BgpRouterDM.get(peer_id)
            if peer:
                peer.bgp_routers[self.uuid] = attrs
        self.bgp_routers = new_peers

    def sandesh_build(self):
        return sandesh.BgpRouter(name=self.name, uuid=self.uuid,
                                 peers=self.bgp_routers,
                                 physical_router=self.physical_router)

    @classmethod
    def sandesh_request(cls, req):
        # Return the list of BGP routers
        resp = sandesh.BgpRouterListResp(bgp_routers=[])
        if req.name_or_uuid is None:
            for router in cls.values():
                sandesh_router = router.sandesh_build()
                resp.bgp_routers.extend(sandesh_router)
        else:
            router = cls.find_by_name_or_uuid(req.name_or_uuid)
            if router:
                sandesh_router = router.sandesh_build()
                resp.bgp_routers.extend(sandesh_router)
        resp.response(req.context())

    def get_all_bgp_router_ips(self):
        if self.params['address'] is not None:
            bgp_router_ips = set([self.params['address']]) 
        else:
            bgp_router_ips = set()
        for peer_uuid in self.bgp_routers:
            peer = BgpRouterDM.get(peer_uuid)
            if peer is None or peer.params['address'] is None:
                continue
            bgp_router_ips.add(peer.params['address'])
        return bgp_router_ips
    #end get_all_bgp_router_ips

# end class BgpRouterDM


class PhysicalRouterDM(DBBase):
    _dict = {}
    obj_type = 'physical_router'
    _sandesh = None

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_networks = set()
        self.bgp_router = None
        self.config_manager = None
        self.nc_q = queue.Queue(maxsize=1)
        self.nc_handler_gl = gevent.spawn(self.nc_handler)
        self.update(obj_dict)
        self.config_manager = PhysicalRouterConfig(
            self.management_ip, self.user_credentials, self.vendor,
            self.product, self.vnc_managed, self._logger)
        self.uve_send()
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.name = obj['fq_name'][-1]
        self.management_ip = obj.get('physical_router_management_ip')
        self.dataplane_ip = obj.get('physical_router_dataplane_ip')
        self.vendor = obj.get('physical_router_vendor_name', '')
        self.product = obj.get('physical_router_product_name', '')
        self.vnc_managed = obj.get('physical_router_vnc_managed')
        self.user_credentials = obj.get('physical_router_user_credentials')
        self.junos_service_ports = obj.get('physical_router_junos_service_ports')
        self.update_single_ref('bgp_router', obj)
        self.update_multiple_refs('virtual_network', obj)
        self.physical_interfaces = set([pi['uuid'] for pi in
                                        obj.get('physical_interfaces', [])])
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
        if self.config_manager is not None:
            self.config_manager.update(
                self.management_ip, self.user_credentials, self.vendor,
                self.product, self.vnc_managed)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.config_manager.delete_bgp_config()
        obj.uve_send(True)
        obj.update_single_ref('bgp_router', {})
        obj.update_multiple_refs('virtual_network', {})
        del cls._dict[uuid]
    # end delete

    def is_junos_service_ports_enabled(self):
        if self.junos_service_ports is not None and self.junos_service_ports.get('service_port') is not None:
            return True
        return False
    #end is_junos_service_ports_enabled

    def set_config_state(self):
        try:
            self.nc_q.put_nowait(1)
        except queue.Full:
            pass
    #end

    def nc_handler(self):
        while self.nc_q.get() is not None:
            try:
                self.push_config()
            except Exception as e:
                self._logger.error("Exception: " + str(e))
    #end

    def is_valid_ip(self, ip_str):
        try:
            socket.inet_aton(ip_str)
            return True
        except socket.error:
            return False
    #end

    def push_config(self):
        self.config_manager.reset_bgp_config()
        bgp_router = BgpRouterDM.get(self.bgp_router)
        if bgp_router:
            for peer_uuid, params in bgp_router.bgp_routers.items():
                peer = BgpRouterDM.get(peer_uuid)
                if peer is None:
                    continue
                external = (bgp_router.params['autonomous_system'] !=
                            peer.params['autonomous_system'])
                self.config_manager.add_bgp_peer(peer.params['address'],
                                                 params, external)
            self.config_manager.set_bgp_config(bgp_router.params)
            self.config_manager.set_global_routing_options(bgp_router.params)
            bgp_router_ips = bgp_router.get_all_bgp_router_ips()
            if self.dataplane_ip is not None and self.is_valid_ip(self.dataplane_ip):
                self.config_manager.add_dynamic_tunnels(self.dataplane_ip,
                              GlobalSystemConfigDM.ip_fabric_subnets, bgp_router_ips)
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
            if vn_id in vn_dict:
                vn_dict[vn_id].append(li.name)
            else:
                vn_dict[vn_id] = [li.name]

        for vn_id, interfaces in vn_dict.items():
            vn_obj = VirtualNetworkDM.get(vn_id)
            if vn_obj is None or vn_obj.vxlan_vni is None or vn_obj.vn_network_id is None:
                continue
            export_set = None
            import_set = None
            for ri_id in vn_obj.routing_instances:
                # Find the primary RI by matching the name
                ri_obj = RoutingInstanceDM.get(ri_id)
                if ri_obj is None:
                    continue
                if ri_obj.fq_name[-1] == vn_obj.fq_name[-1]:
                    vrf_name = vn_obj.get_vrf_name()
                    vrf_name_l2 = vn_obj.get_vrf_name(vrf_type='l2')
                    vrf_name_l3 = vn_obj.get_vrf_name(vrf_type='l3')
                    export_set = copy.copy(ri_obj.export_targets)
                    import_set = copy.copy(ri_obj.import_targets)
                    for ri2_id in ri_obj.routing_instances:
                        ri2 = RoutingInstanceDM.get(ri2_id)
                        if ri2 is None:
                            continue
                        import_set |= ri2.export_targets

                    if vn_obj.router_external == False:
                        self.config_manager.add_routing_instance(vrf_name_l3,
                                                             import_set,
                                                             export_set,
                                                             vn_obj.prefixes,
                                                             vn_obj.gateways,
                                                             vn_obj.router_external,
                                                             ["irb" + "." + str(vn_obj.vxlan_vni)])
                        self.config_manager.add_routing_instance(vrf_name_l2,
                                                             import_set,
                                                             export_set,
                                                             vn_obj.prefixes,
                                                             vn_obj.gateways,
                                                             vn_obj.router_external,
                                                             interfaces,
                                                             vn_obj.vxlan_vni)
                    else:
                        self.config_manager.add_routing_instance(vrf_name_l3,
                                                             import_set,
                                                             export_set,
                                                             vn_obj.prefixes,
                                                             vn_obj.gateways,
                                                             vn_obj.router_external,
                                                             interfaces,
                                                             vn_obj.vxlan_vni)

                    break

            if export_set is not None and self.is_junos_service_ports_enabled() and len(vn_obj.instance_ip_map) > 0:
                service_port_id = 2*vn_obj.vn_network_id - 1
                if self.is_service_port_id_valid(service_port_id) == False:
                    self._logger.error("DM can't allocate service interfaces for \
                                          (vn, vn-id)=(%s,%s)" % (vn_obj.fq_name, vn_obj.vn_network_id))
                else:
                    vrf_name = vrf_name[:123] + '-nat'
                    interfaces = []
                    service_ports = self.junos_service_ports.get('service_port')
                    interfaces.append(service_ports[0] + "." + str(service_port_id))
                    interfaces.append(service_ports[0] + "." + str(service_port_id + 1))
                    self.config_manager.add_routing_instance(vrf_name,
                                                         import_set,
                                                         export_set,
                                                         None,
                                                         None,
                                                         False,
                                                         interfaces,
                                                         None,
                                                         vn_obj.instance_ip_map, vn_obj.vxlan_vni)

        self.config_manager.send_bgp_config()
        self.uve_send()
    # end push_config

    def is_service_port_id_valid(self, service_port_id):
        #mx allowed ifl unit number range is (1, 16385) for service ports
        if service_port_id < 1 or service_port_id > 16384:
            return False
        return True  
    #end is_service_port_id_valid

    def uve_send(self, deleted=False):
        pr_trace = UvePhysicalRouterConfig(name=self.name,
                                           ip_address=self.management_ip,
                                           connected_bgp_router=self.bgp_router,
                                           auto_conf_enabled=self.vnc_managed,
                                           product_info=self.vendor + ':' + self.product)
        if deleted:
            pr_trace.deleted = True
            pr_msg = UvePhysicalRouterConfigTrace(data=pr_trace,
                                                  sandesh=PhysicalRouterDM._sandesh)
            pr_msg.send(sandesh=PhysicalRouterDM._sandesh)
            return

        commit_stats = self.config_manager.get_commit_stats()

        if commit_stats['netconf_enabled'] is True:
            pr_trace.last_commit_time = commit_stats['last_commit_time']
            pr_trace.last_commit_duration = commit_stats['last_commit_duration']
            pr_trace.commit_status_message = commit_stats['commit_status_message']
            pr_trace.total_commits_sent_since_up = commit_stats['total_commits_sent_since_up']
        else:
            pr_trace.netconf_enabled_status = commit_stats['netconf_enabled_status']

        pr_msg = UvePhysicalRouterConfigTrace(data=pr_trace, sandesh=PhysicalRouterDM._sandesh)
        pr_msg.send(sandesh=PhysicalRouterDM._sandesh)
    # end uve_send

# end PhysicalRouterDM

class GlobalVRouterConfigDM(DBBase):
    _dict = {}
    obj_type = 'global_vrouter_config'
    global_vxlan_id_mode = None

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        new_global_vxlan_id_mode = obj.get('vxlan_network_identifier_mode')
        if GlobalVRouterConfigDM.global_vxlan_id_mode != new_global_vxlan_id_mode:
            GlobalVRouterConfigDM.global_vxlan_id_mode = new_global_vxlan_id_mode
            self.update_physical_routers()
    # end update

    def update_physical_routers(self):
        for vn in VirtualNetworkDM.values():
            vn.set_vxlan_vni()

        for pr in PhysicalRouterDM.values():
            pr.set_config_state()

    #end update_physical_routers

    @classmethod
    def is_global_vxlan_id_mode_auto(cls):
        if cls.global_vxlan_id_mode is not None and cls.global_vxlan_id_mode == 'automatic':
            return True
        return False

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
    # end delete
# end GlobalVRouterConfigDM

class GlobalSystemConfigDM(DBBase):
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

class PhysicalInterfaceDM(DBBase):
    _dict = {}
    obj_type = 'physical_interface'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
        pr = PhysicalRouterDM.get(self.physical_router)
        if pr:
            pr.physical_interfaces.add(self.uuid)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.physical_router = self.get_parent_uuid(obj)
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        pr = PhysicalRouterDM.get(obj.physical_router)
        if pr:
            pr.physical_interfaces.discard(obj.uuid)
        del cls._dict[uuid]
    # end delete
# end PhysicalInterfaceDM


class LogicalInterfaceDM(DBBase):
    _dict = {}
    obj_type = 'logical_interface'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_machine_interface = None
        self.update(obj_dict)
        if self.physical_interface:
            parent = PhysicalInterfaceDM.get(self.physical_interface)
        elif self.physical_router:
            parent = PhysicalRouterDM.get(self.physical_router)
        if parent:
            parent.logical_interfaces.add(self.uuid)
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

        self.update_single_ref('virtual_machine_interface', obj)
        self.name = obj['fq_name'][-1]
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
        del cls._dict[uuid]
    # end delete
# end LogicalInterfaceDM

class FloatingIpDM(DBBase):
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
        self.public_network = self.get_pool_public_network(self.get_parent_uuid(obj))
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

#end FloatingIpDM

class InstanceIpDM(DBBase):
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

#end InstanceIpDM

class VirtualMachineInterfaceDM(DBBase):
    _dict = {}
    obj_type = 'virtual_machine_interface'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_network = None
        self.floating_ip = None
        self.instance_ip = None
        self.logical_interface = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.update_single_ref('logical_interface', obj)
        self.update_single_ref('virtual_network', obj)
        self.update_single_ref('floating_ip', obj)
        self.update_single_ref('instance_ip', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('logical_interface', {})
        obj.update_single_ref('virtual_network', {})
        obj.update_single_ref('floating_ip', {})
        obj.update_single_ref('instance_ip', {})
        del cls._dict[uuid]
    # end delete

# end VirtualMachineInterfaceDM


class VirtualNetworkDM(DBBase):
    _dict = {}
    obj_type = 'virtual_network'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_routers = set()
        self.router_external = False
        self.vxlan_vni = None
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
        self.set_vxlan_vni(obj)
        self.routing_instances = set([ri['uuid'] for ri in
                                      obj.get('routing_instances', [])])
        self.virtual_machine_interfaces = set(
            [vmi['uuid'] for vmi in
             obj.get('virtual_machine_interface_back_refs', [])])
        self.prefixes = set()
        self.gateways = set()
        for ipam_ref in obj.get('network_ipam_refs', []):
            for subnet in ipam_ref['attr'].get('ipam_subnets', []):
                self.prefixes.add('%s/%d' % (subnet['subnet']['ip_prefix'],
                                             subnet['subnet']['ip_prefix_len'])
                                  )
                self.gateways.add(subnet['default_gateway'])
    # end update

    def get_vrf_name(self, vrf_type=None):
        if vrf_type is None:
            vrf_name = '__contrail__' + self.uuid + '_' + self.fq_name[-1]
        elif vrf_type == 'l2':
            vrf_name = '__contrail__l2_' + self.uuid + '_' + self.fq_name[-1]
        else:
            vrf_name = '__contrail__l3_' + self.uuid + '_' + self.fq_name[-1]
        #mx has limitation for vrf name, allowed max 127 chars
        return vrf_name[:127]
    #end

    def set_vxlan_vni(self, obj=None):
        self.vxlan_vni = None
        if obj is None:
            obj = self.read_obj(self.uuid)
        if GlobalVRouterConfigDM.is_global_vxlan_id_mode_auto():
            self.vxlan_vni = obj.get('virtual_network_network_id')
        else:
            try:
                prop = obj['virtual_network_properties']
                if prop['vxlan_network_identifier'] is not None:
                    self.vxlan_vni = prop['vxlan_network_identifier']
            except KeyError:
                pass
    #end set_vxlan_vni

    def update_instance_ip_map(self):
        self.instance_ip_map = {}
        for vmi_uuid in self.virtual_machine_interfaces:
            vmi = VirtualMachineInterfaceDM.get(vmi_uuid)
            if vmi is None:
                continue
            if vmi.floating_ip is not None and vmi.instance_ip is not None:
                fip = FloatingIpDM.get(vmi.floating_ip)
                inst_ip  = InstanceIpDM.get(vmi.instance_ip)
                if fip is None or inst_ip is None:
                    continue
                instance_ip = inst_ip.instance_ip_address
                floating_ip = fip.floating_ip_address
                public_vn = VirtualNetworkDM.get(fip.public_network)
                if public_vn is None:
                    continue
                public_vrf_name = public_vn.get_vrf_name(vrf_type='l3')
                self.instance_ip_map[instance_ip] = {'floating_ip': floating_ip,
                                                     'vrf_name': public_vrf_name}
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


class RoutingInstanceDM(DBBase):
    _dict = {}
    obj_type = 'routing_instance'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_network = None
        self.import_targets = set()
        self.export_targets = set()
        self.routing_instances = set()
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

DBBase._OBJ_TYPE_MAP = {
    'bgp_router': BgpRouterDM,
    'physical_router': PhysicalRouterDM,
    'physical_interface': PhysicalInterfaceDM,
    'logical_interface': LogicalInterfaceDM,
    'virtual_machine_interface': VirtualMachineInterfaceDM,
    'virtual_network': VirtualNetworkDM,
    'routing_instance': RoutingInstanceDM,
    'floating_ip': FloatingIpDM,
    'instance_ip': InstanceIpDM,
    'global_system_config': GlobalSystemConfigDM,
    'global_vrouter_config': GlobalVRouterConfigDM,
}
