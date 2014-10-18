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
# end DBBase


class BgpRouterDM(DBBase):
    _dict = {}

    def __init__(self, uuid):
        self.uuid = uuid
        self.bgp_routers = {}
        self.update()
    # end __init__

    def update(self):
        obj = self.read_obj('bgp_router', self.uuid)
        self.params = obj['bgp_router_parameters']
        self.physical_router = self.get_ref_uuid_from_dict(
            obj, 'physical_router_backrefs')
        for ref in obj.get('bgp_router_refs', []):
            self.bgp_routers[ref['uuid']] = ref['attr']
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
        self.bgp_router = self.get_ref_uuid_from_dict(obj, 'bgp_router_refs')
        self.management_ip = obj.get('physical_router_management_ip')
        self.vendor = obj.get('physical_router_vendor_name')
        self.user_credentials = obj.get('physical_router_user_credentials')
        self.virtual_networks = set([vn['uuid'] for vn in
                                     obj.get('virtual_network_refs', [])])
        self.physical_interfaces = set([pi['uuid'] for pi in
                                        obj.get('physical_interfaces', [])])
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
    # end update

    @classmethod
    def delete(cls, uuid):
        if uuid in cls._dict:
            obj = cls._dict[uuid]
            obj.config_manager.delete_bgp_config()
            del cls._dict[uuid]
    # end delete


    def push_config(self):
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
                if ri_obj['fq_name'][-1] == vn_obj.name:
                    rt = ri_obj['route_target_refs'][0]['to'][0]
                    self.config_manager.add_routing_instance(vn_obj.name, rt,
                                                             vn_obj.prefixes,
                                                             interfaces)
                    break
    # end push_config

    def add_logical_interface(self, li_name):
        if not self.bgp_router:
            return
        li = LogicalInterfaceDM.get(li_name)
        if not li or not li.virtual_network:
            return
        vn = VirtualNetworkDM.locate(li.virtual_network)
        if not vn:
            return
        bgp_router = BgpRouterDM.get(self.bgp_router)
        if not bgp_router:
            return
        bgp_router.add_routing_instance(
            li.virtual_network, vn.get_route_target(), li.name)
    # end add_logical_interface

    def delete_logical_interface(self, li_name):
        if not self.bgp_router:
            return
        li = LogicalInterfaceDM.get(li_name)
        if not li or not li.virtual_network:
            return
        bgp_router = BgpRouterDM.get(self.bgp_router)
        if not bgp_router:
            return
        bgp_router.delete_routing_instance(li.virtual_network)
    # end add_logical_interface

    def add_physical_interface(self, pi_name):
        pi = PhysicalInterfaceDM.get(pi_name)
        if not pi:
            return
        for li_name in pi.logical_interfaces:
            self.add_logical_interface(li_name)
    # end add_physical_interface

    def delete_physical_interface(self, pi_name):
        pi = PhysicalInterfaceDM.get(pi_name)
        if not pi:
            return
        for li_name in pi.logical_interfaces:
            self.delete_logical_interface(li_name)
    # end delete_physical_interface

    def add_physical_interface_config(self, pi_name):
        if pi_name in self.physical_interfaces:
            return
        self.physical_interfaces.add(pi_name)
        self.add_physical_interface(pi_name)
    # end add_physical_interface_config

    def delete_physical_interface_config(self, pi_name):
        if pi_name not in self.physical_interfaces:
            return
        self.delete_physical_interface(pi_name)
        self.physical_interfaces.discard(pi_name)
    # end delete_physical_interface_config

    def set_bgp_router(self, router_name):
        if self.bgp_router == router_name:
            return
        if self.bgp_router:
            for pi_name in self.physical_interfaces:
                self.delete_physical_interface(pi_name)
        self.bgp_router = router_name
        if router_name:
            for pi_name in self.physical_interfaces:
                self.add_physical_interface(pi_name)
    # end set_bgp_router

# end PhysicalRouterDM


class PhysicalInterfaceDM(DBBase):
    _dict = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj('physical_interface', self.uuid)
        self.physical_router = obj['parent_uuid']
        self.logical_interfaces = set([li['uuid'] for li in
                                       obj.get('logical_interfaces', [])])
    # end update

    def add_logical_interface(self, li_name):
        if not self.physical_router:
            return
        pr = PhysicalRouterDM.get(self.physical_router)
        if not pr:
            return
        pr.add_logical_interface(li_name)
    # end add_logical_interface

    def delete_logical_interface(self, li_name):
        if not self.physical_router:
            return
        pr = PhysicalRouterDM.get(self.physical_router)
        if not pr:
            return
        pr.delete_logical_interface(li_name)
    # end delete_logical_interface

    def add_logical_interface_config(self, li_name):
        if li_name in self.logical_interfaces:
            return
        self.logical_interfaces.add(li_name)
        self.add_logical_interface(li_name)
    # end add_logical_interface

    def delete_logical_interface_config(self, li_name):
        if li_name not in self.logical_interfaces:
            return
        self.delete_logical_interface(li_name)
        self.logical_interfaces.discard(li_name)
    # end add_logical_interface
# end PhysicalInterfaceDM


class LogicalInterfaceDM(DBBase):
    _dict = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj('logical_interface', self.uuid)
        if obj['parent_type'] == 'physical-router':
            self.physical_router = obj['parent_uuid']
            self.physical_interface = None
        else:
            self.physical_interface = obj['parent_uuid']
            self.physical_router = None
        self.virtual_machine_interface = self.get_ref_uuid_from_dict(
            obj, 'virtual_machine_interface_refs')
        self.name = obj['fq_name'][-1]
    # end update
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
        self.logical_interface = self.get_ref_uuid_from_dict(
            obj, 'logical_interface_backrefs')
        self.virtual_network = self.get_ref_uuid_from_dict(
            obj, 'virtual_network_refs')
    # end update
# end VirtualMachineInterfaceDM


class VirtualNetworkDM(DBBase):
    _dict = {}

    def __init__(self, uuid, obj_dict=None):
        self.uuid = uuid
        self.update(obj_dict)
    # end __init__

    def update(self, obj=None):
        if obj is None:
            obj = self.read_obj('virtual_network', self.uuid)
        self.name = obj['fq_name'][-1]
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
