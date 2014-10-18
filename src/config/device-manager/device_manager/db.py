#
# Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of data model for physical router
configuration manager
"""
from vnc_api.common.exceptions import NoIdError
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from physical_router_config import PhysicalRouterConfig


class DBBase(object):
    _device_manager = None
    _OBJ_TYPE_MAP = {
        'bgp_router': BgpRouterDM,
        'physical_router': PhysicalRouterDM,
        'physical_interface': PhysicalInterfaceDM,
        'logical_interface': LogicalInterfaceDM,
        'virtual_machine_interface': VirtualMachineInterfaceDM,
        'virtual_network': VirtualNetworkDM,
    }

    class __metaclass__(type):

        def __iter__(cls):
            for i in cls._dict:
                yield i
        # end __iter__

        def values(cls):
            for i in cls._dict.values():
                yield i
        # end values

        def items(cls):
            for i in cls._dict.items():
                yield i
        # end items
    # end __metaclass__

    @classmethod
    def get(cls, name):
        if name in cls._dict:
            return cls._dict[name]
        return None
    # end get

    @classmethod
    def locate(cls, name, *args):
        if name not in cls._dict:
            try:
                cls._dict[name] = cls(name, *args)
            except NoIdError as e:
                cls._device_manager._sandesh._logger.debug(
                    "Exception %s while creating %s for %s",
                    e, cls.__name__, name)
                return None
        return cls._dict[name]
    # end locate

    @classmethod
    def delete(cls, name):
        if name in cls._dict:
            del cls._dict[name]
    # end delete

    def get_ref_uuid_from_dict(self, obj_dict, ref_name):
        if ref_name in obj_dict:
            return obj_dict[ref_name][0]['uuid']
        else:
            return None

    def update_single_ref(self, self_type, ref_type, refs):
        if refs:
            new_id = refs[0]['uuid']
        else:
            new_id = None
        old_id = getattr(self, ref_type, None)
        if old_id == new_id:
            return
        obj = self._OBJ_TYPE_MAP[ref_type].get(old_id)
        if obj and getattr(obj, self_type, None) == self.uuid:
            setattr(obj, self_type, None)
        obj = self._OBJ_TYPE_MAP[ref_type].get(new_id)
        if obj:
            setattr(obj, self_type, self.uuid)
        setattr(self, ref_type, new_id)
    # end update_single_ref

    def update_multiple_refs(self, self_type, ref_type, refs):
        new_refs = set()
        for ref in refs or []:
            new_refs.add(ref['uuid'])
        old_refs = getattr(self, ref_type+'s')
        for ref_id in old_refs - new_refs:
            obj = self._OBJ_TYPE_MAP[ref_type].get(ref_id)
            if obj is not None:
                obj_refs = getattr(obj, self_type+'s')
                obj_refs.discard(self.uuid)
        for ref_id in new_refs - old_refs:
            obj = self._OBJ_TYPE_MAP[ref_type].get(ref_id)
            if obj is not None:
                obj_refs = getattr(obj, self_type+'s')
                obj_refs.add(self.uuid)
        setattr(self, ref_type+'s', new_refs)
    # end update_multiple_refs

    def read_obj(self, obj_type, uuid):
        method_name = "_cassandra_%s_read" % obj_type
        method = getattr(self._device_manager._cassandra, method_name)
        ok, objs = method([uuid])
        if not ok:
            self._device_manager.config_log(
                'Cannot read %s %s, error %s' % (obj_type, uuid, objs),
                SandeshLevel.SYS_ERR)
            raise NoIdError('')
        return objs[0]
    # end read_obj

    def get_parent_uuid(self, obj):
        if 'parent_uuid' in obj:
            return obj['parent_uuid']
        else:
            parent_type = obj['parent_type'].replace('-', '_')
            parent_fq_name = obj['fq_name'][:-1]
            return self._device_manager._cassandra.fq_name_to_uuid(
                parent_type, parent_fq_name)
    # end get_parent_uuid
# end DBBase


class BgpRouterDM(DBBase):
    _dict = {}

    def __init__(self, uuid, obj_dict = None):
        self.uuid = uuid
        self.bgp_routers = {}
        self.physical_router = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj):
        if obj is None:
            obj = self.read_obj('bgp_router', self.uuid)
        self.params = obj['bgp_router_parameters']
        self.update_single_ref('bgp_router', 'physical_router',
                               obj.get('physical_router_backrefs'))
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
# end class BgpRouterDM


class PhysicalRouterDM(DBBase):
    _dict = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.virtual_networks = set()
        self.update(obj_dict)
        self.config_manager = PhysicalRouterConfig(
            self.management_ip, self.user_credentials,
            self._device_manager._sandesh.logger)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj('physical_router', self.uuid)
        self.management_ip = obj.get('physical_router_management_ip')
        self.vendor = obj.get('physical_router_vendor_name')
        self.user_credentials = obj.get('physical_router_user_credentials')
        self.update_single_ref('physical_router', 'bgp_router',
                               obj.get('bgp_router_refs'))
        self.update_multiple_refs('physical_router', 'virtual_network',
                                  obj.get('virtual_network_refs'))
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
        self.update_single_ref('physical_router', 'bgp_router', None)
        self.update_multiple_refs('physical_router', 'virtual_network', None)
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
                self.config_manager.add_bgp_peer(peer.params.address, params)
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
                vn_dict[vn_id].append(li_uuid)
            else:
                vn_dict[vn_id] = [li.name]

        for vn_id, interfaces in vn_dict.items():
            vn_obj = VirtualNetworkDM.get(vn_id)
            if vn_obj is None:
                continue
            for ri in vn_obj.routing_instances:
                # Find the primary RI by matching the name
                ri_obj = self.read_obj('routing_instance', ri)
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


class PhysicalInterfaceDM(DBBase):
    _dict = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
        pr = PhysicalRouterDM.get(self.physical_router)
        if pr:
            pr.physical_interfaces.add(self.uuid)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj('physical_interface', self.uuid)
        self.physical_router = self.get_parent_uuid(obj)
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        pr = PhysicalRouterDM.get(self.physical_router)
        if pr:
            pr.physical_interfaces.discard(self.uuid)
        del cls._dict[uuid]
    # end delete
# end PhysicalInterfaceDM


class LogicalInterfaceDM(DBBase):
    _dict = {}

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
            obj = self.read_obj('logical_interface', self.uuid)
        if obj['parent_type'] == 'physical-router':
            self.physical_router = self.get_parent_uuid(obj)
            self.physical_interface = None
        else:
            self.physical_interface = self.get_parent_uuid(obj)
            self.physical_router = None

        self.update_single_ref('logical_interface', 'virtual_machine_interface',
                               obj.get('virtual_machine_interface_refs'))
        self.name = obj['fq_name'][-1]
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid not in cls._dict:
            return
        obj = cls._dict[uuid]
        if self.physical_interface:
            parent = PhysicalInterfaceDM.get(self.physical_interface)
        elif self.physical_router:
            parent = PhysicalInterfaceDM.get(self.physical_router)
        if parent:
            parent.logical_interfaces.discard(self.uuid)
        self.update_single_ref('logical_interface', 'virtual_machine_interface',
                               None)
        del cls._dict[uuid]
    # end delete
# end LogicalInterfaceDM


class VirtualMachineInterfaceDM(DBBase):
    _dict = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj):
        if obj is None:
            obj = self.read_obj('virtual_machine_interface', self.uuid)
        self.update_single_ref('virtual_machine_interface', 'logical_interface',
                               obj.get('logical_interface_backrefs'))
        self.update_single_ref('virtual_machine_interface', 'virtual_network',
                               obj.get('virtual_network_refs'))
    # end update
# end VirtualMachineInterfaceDM


class VirtualNetworkDM(DBBase):
    _dict = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.physical_routers = None
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj('virtual_network', self.uuid)
        self.update_multiple_refs('virtual_network', 'physical_router',
                                  obj.get('physical_router_backrefs'))
        self.fq_name = obj['fq_name']
        self.routing_instances = set([ri['uuid'] for ri in
                                      obj.get('routing_instances', [])])
        self.virtual_machine_interfaces = set(
            [vmi['uuid'] for vmi in
             obj.get('virtual_machine_interface_backrefs', [])])
        self.prefixes = set()
        for ipam_ref in obj.get('network_ipam_refs', []):
            for subnet in ipam_ref['attr'].get('ipam_subnets', []):
                self.prefixes.add('%s/%d' % (subnet['subnet']['ip_prefix'],
                                             subnet['subnet']['ip_prefix_len'])
                                  )
    # end update
# end VirtualNetworkDM
