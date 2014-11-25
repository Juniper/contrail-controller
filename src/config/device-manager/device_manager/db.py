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


class DBBaseDM(DBBase):
    def get_ref_uuid_from_dict(self, obj_dict, ref_name):
        if ref_name in obj_dict:
            return obj_dict[ref_name][0]['uuid']
        else:
            return None

    def add_ref(self, ref_type, ref):
        if hasattr(self, ref_type):
            setattr(self, ref_type, ref)
        elif hasattr(self, ref_type+'s'):
            ref_set = getattr(self, ref_type+'s')
            ref_set.add(ref)
    # end add_ref

    def delete_ref(self, ref_type, ref):
        if hasattr(self, ref_type) and getattr(self, ref_type) == ref:
            setattr(self, ref_type, None)
        elif hasattr(self, ref_type+'s'):
            ref_set = getattr(self, ref_type+'s')
            ref_set.discard(ref)
    # end delete_ref

    def update_single_ref(self, ref_type, obj):
        refs = obj.get(ref_type+'_refs') or obj.get(ref_type+'_back_refs')
        if refs:
            new_id = refs[0]['uuid']
        else:
            new_id = None
        old_id = getattr(self, ref_type, None)
        if old_id == new_id:
            return
        ref_obj = self._OBJ_TYPE_MAP[ref_type].get(old_id)
        if ref_obj is not None:
            ref_obj.delete_ref(self.obj_type, self.uuid)
        ref_obj = self._OBJ_TYPE_MAP[ref_type].get(new_id)
        if ref_obj is not None:
            ref_obj.add_ref(self.obj_type, self.uuid)
        setattr(self, ref_type, new_id)
    # end update_single_ref

    def update_multiple_refs(self, ref_type, obj):
        refs = obj.get(ref_type+'_refs') or obj.get(ref_type+'_back_refs')
        new_refs = set()
        for ref in refs or []:
            new_refs.add(ref['uuid'])
        old_refs = getattr(self, ref_type+'s')
        for ref_id in old_refs - new_refs:
            ref_obj = self._OBJ_TYPE_MAP[ref_type].get(ref_id)
            if ref_obj is not None:
                ref_obj.delete_ref(self.obj_type, self.uuid)
        for ref_id in new_refs - old_refs:
            ref_obj = self._OBJ_TYPE_MAP[ref_type].get(ref_id)
            if ref_obj is not None:
                ref_obj.add_ref(self.obj_type, self.uuid)
        setattr(self, ref_type+'s', new_refs)
    # end update_multiple_refs

    def read_obj(self, uuid, obj_type=None):
        method_name = "_cassandra_%s_read" % (obj_type or self.obj_type)
        method = getattr(self._cassandra, method_name)
        ok, objs = method([uuid])
        if not ok:
            self._logger.error(
                'Cannot read %s %s, error %s' % (obj_type, uuid, objs))
            raise NoIdError('')
        return objs[0]
    # end read_obj

    def get_parent_uuid(self, obj):
        if 'parent_uuid' in obj:
            return obj['parent_uuid']
        else:
            parent_type = obj['parent_type'].replace('-', '_')
            parent_fq_name = obj['fq_name'][:-1]
            return self._cassandra.fq_name_to_uuid(parent_type, parent_fq_name)
    # end get_parent_uuid

    @classmethod
    def find_by_name_or_uuid(cls, name_or_uuid):
        obj = cls.get(name_or_uuid)
        if obj:
            return obj

        for obj in cls.values():
            if obj.name == name_or_uuid:
                return obj
        return None

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
        self.params = obj['bgp_router_parameters']
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


# end class BgpRouterDM


class PhysicalRouterDM(DBBaseDM):
    _dict = {}
    obj_type = 'physical_router'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_networks = set()
        self.bgp_router = None
        self.update(obj_dict)
        self.config_manager = PhysicalRouterConfig(
            self.management_ip, self.user_credentials, self._logger)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.management_ip = obj.get('physical_router_management_ip')
        self.vendor = obj.get('physical_router_vendor_name')
        self.user_credentials = obj.get('physical_router_user_credentials')
        self.update_single_ref('bgp_router', obj)
        self.update_multiple_refs('virtual_network', obj)
        self.physical_interfaces = set([pi['uuid'] for pi in
                                        obj.get('physical_interfaces', [])])
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.config_manager.delete_bgp_config()
        obj.update_single_ref('bgp_router', {})
        obj.update_multiple_refs('virtual_network', {})
        del cls._dict[uuid]
    # end delete

    def push_config(self):
        self.config_manager.reset_bgp_config()
        bgp_router = BgpRouterDM.get(self.bgp_router)
        if bgp_router:
            for peer_uuid, params in bgp_router.bgp_routers.items():
                peer = BgpRouterDM.get(peer_uuid)
                if peer is None:
                    continue
                self.config_manager.add_bgp_peer(peer.params['address'], params)
            self.config_manager.set_bgp_config(bgp_router.params)

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
            if vn_obj is None:
                continue
            for ri in vn_obj.routing_instances:
                # Find the primary RI by matching the name
                ri_obj = self.read_obj(ri, 'routing_instance')
                if ri_obj['fq_name'][-1] == vn_obj.fq_name[-1]:
                    vrf_name = ':'.join(vn_obj.fq_name)
                    rt = ri_obj['route_target_refs'][0]['to'][0]
                    self.config_manager.add_routing_instance(vrf_name, rt,
                                                             vn_obj.prefixes,
                                                             interfaces)
                    break
        self.config_manager.send_bgp_config()
    # end push_config
# end PhysicalRouterDM


class PhysicalInterfaceDM(DBBaseDM):
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


class LogicalInterfaceDM(DBBaseDM):
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


class VirtualMachineInterfaceDM(DBBaseDM):
    _dict = {}
    obj_type = 'virtual_machine_interface'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_network = None
        self.logical_interface = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.update_single_ref('logical_interface', obj)
        self.update_single_ref('virtual_network', obj)
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        obj.update_single_ref('logical_interface', {})
        obj.update_single_ref('virtual_network', {})
        del cls._dict[uuid]
    # end delete

# end VirtualMachineInterfaceDM


class VirtualNetworkDM(DBBaseDM):
    _dict = {}
    obj_type = 'virtual_network'

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_routers = set()
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj(self.uuid)
        self.update_multiple_refs('physical_router', obj)
        self.fq_name = obj['fq_name']
        self.routing_instances = set([ri['uuid'] for ri in
                                      obj.get('routing_instances', [])])
        self.virtual_machine_interfaces = set(
            [vmi['uuid'] for vmi in
             obj.get('virtual_machine_interface_back_refs', [])])
        self.prefixes = set()
        for ipam_ref in obj.get('network_ipam_refs', []):
            for subnet in ipam_ref['attr'].get('ipam_subnets', []):
                self.prefixes.add('%s/%d' % (subnet['subnet']['ip_prefix'],
                                             subnet['subnet']['ip_prefix_len'])
                                  )
    # end update

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
        self.route_targets = set()
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
        self.route_targets = set([rt['to'][0] for rt in
                                  obj.get('route_targets', [])])

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


DBBaseDM._OBJ_TYPE_MAP = {
    'bgp_router': BgpRouterDM,
    'physical_router': PhysicalRouterDM,
    'physical_interface': PhysicalInterfaceDM,
    'logical_interface': LogicalInterfaceDM,
    'virtual_machine_interface': VirtualMachineInterfaceDM,
    'virtual_network': VirtualNetworkDM,
    'routing_instance': RoutingInstanceDM,
}
