#
# Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
#

"""
This file contains implementation of transforming user-exposed VNC
configuration model/schema to a representation needed by VNC Control Plane
(BGP-based)
"""

import gevent
# Import kazoo.client before monkey patching
from cfgm_common.zkclient import ZookeeperClient,IndexAllocator
from gevent import monkey
monkey.patch_all()
import sys
import requests
import ConfigParser
import cgitb

import copy
import argparse
import socket
import uuid

from lxml import etree
import re
from cfgm_common import vnc_cpu_info
import pycassa
from pycassa.system_manager import *
from pycassa.pool import AllServersUnavailable

import cfgm_common as common
from cfgm_common.exceptions import *
from cfgm_common.imid import *
from cfgm_common import svc_info

from vnc_api.vnc_api import *

from pysandesh.sandesh_base import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common.uve.virtual_network.ttypes import *
from sandesh_common.vns.ttypes import Module, NodeType
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, NodeTypeNames, INSTANCE_ID_DEFAULT
from schema_transformer.sandesh.st_introspect import ttypes as sandesh
from ncclient import manager
import discoveryclient.client as client
try:
    #python2.7
    from collections import OrderedDict
except:
    #python2.6
    from ordereddict import OrderedDict
import jsonpickle
from pysandesh.connection_info import ConnectionState
from pysandesh.gen_py.process_info.ttypes import ConnectionType, \
    ConnectionStatus
from cfgm_common.uve.cfgm_cpuinfo.ttypes import NodeStatusUVE, \
    NodeStatus
from cStringIO import StringIO

_BGP_RTGT_MAX_ID = 1 << 24
_BGP_RTGT_ALLOC_PATH = "/id/bgp/route-targets/"

_VN_MAX_ID = 1 << 24
_VN_ID_ALLOC_PATH = "/id/virtual-networks/"

_SECURITY_GROUP_MAX_ID = 1 << 32
_SECURITY_GROUP_ID_ALLOC_PATH = "/id/security-groups/id/"

_SERVICE_CHAIN_MAX_VLAN = 4093
_SERVICE_CHAIN_VLAN_ALLOC_PATH = "/id/service-chain/vlan/"

_PROTO_STR_TO_NUM = {
    'icmp': '1',
    'tcp': '6',
    'udp': '17',
    'any': 'any',
}

_sandesh = None

# connection to api-server
_vnc_lib = None
# zookeeper client connection
_zookeeper_client = None


def _ports_eq(lhs, rhs):
    return lhs.start_port == rhs.start_port and lhs.end_port == rhs.end_port
# end _ports_eq
PortType.__eq__ = _ports_eq


class DictST(object):

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
                _sandesh._logger.debug("Exception %s while creating %s for %s",
                                       e, cls.__name__, name)
                return None
        return cls._dict[name]
    # end locate

# end DictST


def _access_control_list_update(acl_obj, name, obj, entries):
    if acl_obj is None:
        try:
            # Check if there is any stale object. If there is, delete it
            acl_name = copy.deepcopy(obj.get_fq_name())
            acl_name.append(name)
            _vnc_lib.access_control_list_delete(fq_name=acl_name)
        except NoIdError:
            pass

        if entries is None:
            return None
        acl_obj = AccessControlList(name, obj, entries)
        try:
            _vnc_lib.access_control_list_create(acl_obj)
            return acl_obj
        except HttpError as he:
            _sandesh._logger.debug(
                "HTTP error while creating acl %s for %s: %d, %s",
                name, obj.get_fq_name_str(), he.status_code, he.content)
        return None
    else:
        if entries is None:
            try:
                _vnc_lib.access_control_list_delete(id=acl_obj.uuid)
            except NoIdError:
                pass
            return None

        # Set new value of entries on the ACL
        acl_obj.set_access_control_list_entries(entries)
        try:
            _vnc_lib.access_control_list_update(acl_obj)
        except HttpError as he:
            _sandesh._logger.debug(
                "HTTP error while updating acl %s for %s: %d, %s",
                name, obj.get_fq_name_str(), he.status_code, he.content)
        except NoIdError:
            _sandesh._logger.debug("NoIdError while updating acl %s for %s",
                                   name, obj.get_fq_name_str())
    return acl_obj
# end _access_control_list_update

# a struct to store attributes related to Virtual Networks needed by
# schema transformer


class VirtualNetworkST(DictST):
    _dict = {}
    _rt_cf = None
    _sc_ip_cf = None
    _autonomous_system = 0

    _vn_id_allocator = None
    _sg_id_allocator = None
    _rt_allocator = None
    _sc_vlan_allocator_dict = {}
    
    def __init__(self, name):
        self.obj = _vnc_lib.virtual_network_read(fq_name_str=name)
        self.name = name
        self.policies = OrderedDict()
        self.connections = set()
        self.rinst = {}
        self.acl = None
        self.dynamic_acl = None
        for acl in self.obj.get_access_control_lists() or []:
            acl_obj = _vnc_lib.access_control_list_read(id=acl['uuid'])
            if acl_obj.name == self.obj.name:
                self.acl = acl_obj
            elif acl_obj.name == 'dynamic':
                self.dynamic_acl = acl_obj

        self.ipams = {}
        self.extend = False
        self.rt_list = set()
        self._route_target = 0
        self.route_table_refs = set()
        self.route_table = {}
        self.service_chains = {}
        prop = self.obj.get_virtual_network_properties(
        ) or VirtualNetworkType()
        if prop.network_id is None:
            prop.network_id = self._vn_id_allocator.alloc(name) + 1
            self.obj.set_virtual_network_properties(prop)
            _vnc_lib.virtual_network_update(self.obj)
        if self.obj.get_fq_name() == common.IP_FABRIC_VN_FQ_NAME:
            self._default_ri_name = common.IP_FABRIC_RI_FQ_NAME[-1]
            self.locate_routing_instance_no_target(self._default_ri_name)
        elif self.obj.get_fq_name() == common.LINK_LOCAL_VN_FQ_NAME:
            self._default_ri_name = common.LINK_LOCAL_RI_FQ_NAME[-1]
            self.locate_routing_instance_no_target(self._default_ri_name)
        else:
            self._default_ri_name = self.obj.name
            self.locate_routing_instance(self._default_ri_name)
        for policy in NetworkPolicyST.values():
            if policy.internal and name in policy.network_back_ref:
                self.add_policy(policy.name)
        self.uve_send()
    # end __init__

    @classmethod
    def delete(cls, name):
        vn = cls.get(name)
        analyzer_vn_set = set()
        if vn:
            for service_chain_list in vn.service_chains.values():
                for service_chain in service_chain_list:
                    service_chain.destroy()
            for ri in vn.rinst.values():
                vn.delete_routing_instance(ri)
            if vn.acl:
                _vnc_lib.access_control_list_delete(id=vn.acl.uuid)
            if vn.dynamic_acl:
                _vnc_lib.access_control_list_delete(id=vn.dynamic_acl.uuid)
            props = vn.obj.get_virtual_network_properties()
            if props and props.network_id:
                cls._vn_id_allocator.delete(props.network_id - 1)
            for policy in NetworkPolicyST.values():
                if name in policy.analyzer_vn_set:
                    analyzer_vn_set |= policy.network_back_ref
                    policy.analyzer_vn_set.discard(name)
            del cls._dict[name]
            vn.uve_send(deleted=True)
        return analyzer_vn_set
    # end delete

    @classmethod
    def get_autonomous_system(cls):
        if cls._autonomous_system == 0:
            gsc = _vnc_lib.global_system_config_read(
                fq_name=['default-global-system-config'])
            cls._autonomous_system = int(gsc.get_autonomous_system())
        return cls._autonomous_system
    # end get_autonomous_system

    @classmethod
    def update_autonomous_system(cls, new_asn):
        # ifmap reports as string, obj read could return it as int
        if int(new_asn) == cls._autonomous_system:
            return
        for vn in cls._dict.values():
            if (vn.obj.get_fq_name() in
                    [common.IP_FABRIC_VN_FQ_NAME,
                     common.LINK_LOCAL_VN_FQ_NAME]):
                # for ip-fabric and link-local VN, we don't need to update asn
                continue
            ri = vn.get_primary_routing_instance()
            ri_fq_name = ri.get_fq_name_str()
            rtgt_num = int(cls._rt_cf.get(ri_fq_name)['rtgt_num'])
            old_rtgt_name = "target:%d:%d" % (cls._autonomous_system, rtgt_num)
            new_rtgt_name = "target:%s:%d" % (new_asn, rtgt_num)
            new_rtgt_obj = RouteTargetST.locate(new_rtgt_name)
            old_rtgt_obj = RouteTarget(old_rtgt_name)
            inst_tgt_data = InstanceTargetType()
            ri.obj = _vnc_lib.routing_instance_read(fq_name_str=ri_fq_name)
            ri.obj.del_route_target(old_rtgt_obj)
            ri.obj.add_route_target(new_rtgt_obj, inst_tgt_data)
            _vnc_lib.routing_instance_update(ri.obj)
            for (prefix, nexthop) in vn.route_table.items():
                left_ri = vn._get_routing_instance_from_route(nexthop)
                if left_ri is None:
                    continue
                left_ri.update_route_target_list(
                    set([new_rtgt_name]), set([old_rtgt_name]), "import")
                static_route_entries = left_ri.obj.get_static_route_entries()
                if static_route_entries is None:
                    continue
                for static_route in static_route_entries.get_route() or []:
                    if old_rtgt_name in static_route.route_target:
                        static_route.route_target.remove(old_rtgt_name)
                        static_route.route_target.append(new_rtgt_name)
                _vnc_lib.routing_instance_update(left_ri.obj)
            try:
                _vnc_lib.route_target_delete(
                    fq_name=old_rtgt_obj.get_fq_name())
            except RefsExistError:
                # if other routing instances are referring to this target,
                # it will be deleted when those instances are deleted
                pass
        # end for vn

        for router in BgpRouterST.values():
            router.update_autonomous_system(new_asn)
        # end for router
        for router in LogicalRouterST.values():
            router.update_autonomous_system(new_asn)
        # end for router
        cls._autonomous_system = int(new_asn)
    # end update_autonomous_system

    def add_policy(self, policy_name, attrib=None):
        # Add a policy ref to the vn. Keep it sorted by sequence number
        if attrib is None:
            attrib = VirtualNetworkPolicyType(SequenceType(sys.maxint,
                                                           sys.maxint))
        if attrib.sequence is None:
            _sandesh._logger.debug("Cannot assign policy %s to %s: sequence "
                                   "number is not available", policy_name,
                                   self.name)
            return
        
        self.policies[policy_name] = attrib
        self.policies = OrderedDict(sorted(self.policies.items(),
                                           key=lambda t:(t[1].sequence.major,
                                                         t[1].sequence.minor)))
    #end add_policy

    def get_primary_routing_instance(self):
        return self.rinst[self._default_ri_name]
    # end get_primary_routing_instance

    def add_connection(self, vn_name):
        if vn_name == "any":
            self.connections.add("*")
            # Need to do special processing for "*"
        elif vn_name != self.name:
            self.connections.add(vn_name)
    # end add_connection

    def add_ri_connection(self, virtual_network_2):
        vn2 = VirtualNetworkST.get(virtual_network_2)
        if not vn2:
            # connection published on receiving <vn2>
            return

        if (set([virtual_network_2, "*"]) & self.connections and
                set([self.name, "*"]) & vn2.connections):
            self.get_primary_routing_instance().add_connection(
                vn2.get_primary_routing_instance())
        vn2.uve_send()
    # end add_ri_connection

    def delete_ri_connection(self, virtual_network_2):
        vn2 = VirtualNetworkST.get(virtual_network_2)
        if vn2 is None:
            return
        self.get_primary_routing_instance().delete_connection(
            vn2.get_primary_routing_instance())
        vn2.uve_send()
    # end delete_ri_connection

    def add_service_chain(self, remote_vn, left_vn, right_vn, direction,
                          sp_list, dp_list, proto, service_list):
        service_chain_list = self.service_chains.get(remote_vn)
        if service_chain_list is None:
            service_chain_list = []
            self.service_chains[remote_vn] = service_chain_list
        service_chain = ServiceChain.find_or_create(left_vn, right_vn,
                                         direction, sp_list, dp_list, proto,
                                         service_list)
        if service_chain.service_list != service_list:
            if service_chain.created:
                service_chain.destroy()
                service_chain.service_list = service_list
                service_chain.create()
            else:
                service_chain.service_list = service_list

        if service_chain not in service_chain_list:
            service_chain_list.append(service_chain)
        return service_chain.name
    # end add_service_chain

    def get_vns_in_project(self):
        # return a set of all virtual networks with the same parent as self
        return set((k) for k, v in self._dict.items()
                   if (self.name != v.name and
                       self.obj.get_parent_fq_name() ==
                       v.obj.get_parent_fq_name()))
    # end get_vns_in_project

    def allocate_service_chain_ip(self, sc_name):
        try:
            sc_ip_address = self._sc_ip_cf.get(sc_name)['ip_address']
        except pycassa.NotFoundException:
            try:
                sc_ip_address = _vnc_lib.virtual_network_ip_alloc(
                    self.obj, count=1)[0]
            except (NoIdError, RefsExistError) as e:
                _sandesh._logger.debug(
                    "Error while allocating ip in network %s: %s", self.name,
                    str(e))
                return None
            self._sc_ip_cf.insert(sc_name, {'ip_address': sc_ip_address})
        return sc_ip_address
    # end allocate_service_chain_ip

    def free_service_chain_ip(self, sc_name):
        try:
            sc_ip_address = self._sc_ip_cf.get(sc_name)['ip_address']
            self._sc_ip_cf.remove(sc_name)
            _vnc_lib.virtual_network_ip_free(self.obj, [sc_ip_address])
        except (NoIdError, pycassa.NotFoundException):
            pass
    # end free_service_chain_ip

    @classmethod
    def allocate_service_chain_vlan(cls, service_vm, service_chain):
        alloc_new = False
        if service_vm not in cls._sc_vlan_allocator_dict:
            cls._sc_vlan_allocator_dict[service_vm] = IndexAllocator(
                _zookeeper_client,
                (SchemaTransformer._zk_path_prefix +
                 _SERVICE_CHAIN_VLAN_ALLOC_PATH+service_vm),
                _SERVICE_CHAIN_MAX_VLAN)

        vlan_ia = cls._sc_vlan_allocator_dict[service_vm]

        try:
            vlan = int(
                cls._service_chain_cf.get(service_vm)[service_chain])
            db_sc = vlan_ia.read(vlan)
            if (db_sc is None) or (db_sc != service_chain):
                alloc_new = True
        except (KeyError, pycassa.NotFoundException):
            alloc_new = True

        if alloc_new:
            # TODO handle overflow + check alloc'd id is not in use
            vlan = vlan_ia.alloc(service_chain)
            cls._service_chain_cf.insert(service_vm, {service_chain: str(vlan)})

        # Since vlan tag 0 is not valid, increment before returning
        return vlan + 1
    # end allocate_service_chain_vlan

    @classmethod
    def free_service_chain_vlan(cls, service_vm, service_chain):
        try:
            vlan_ia = cls._sc_vlan_allocator_dict[service_vm]
            vlan = int(cls._service_chain_cf.get(service_vm)[service_chain])
            cls._service_chain_cf.remove(service_vm, [service_chain])
            vlan_ia.delete(vlan)
            if vlan_ia.empty():
                del cls._sc_vlan_allocator_dict[service_vm]
        except (KeyError, pycassa.NotFoundException):
            pass
    # end free_service_chain_vlan

    def set_properties(self, properties):
        if properties:
            new_extend = properties.extend_to_external_routers
        else:
            new_extend = False
        if self.extend != new_extend:
            self.extend_to_external_routers(new_extend)
            self.extend = new_extend
    # end set_properties

    def get_route_target(self):
        return "target:%s:%d" % (self.get_autonomous_system(),
                                 self._route_target)
    # end get_route_target

    def extend_to_external_routers(self, extend):
        for router in BgpRouterST.values():
            if extend:
                router.add_routing_instance(self.name, self.get_route_target)
            else:
                router.delete_routing_instance(self.name)
    # end extend_to_external_routers

    def locate_routing_instance_no_target(self, rinst_name):
        """ locate a routing instance but do not allocate a route target """
        if rinst_name in self.rinst:
            return self.rinst[rinst_name]

        rinst_fq_name_str = '%s:%s' % (self.obj.get_fq_name_str(), rinst_name)
        try:
            rinst_obj = _vnc_lib.routing_instance_read(
                fq_name_str=rinst_fq_name_str)
            rinst = RoutingInstanceST(rinst_obj)
            self.rinst[rinst_name] = rinst
            return rinst
        except NoIdError:
            _sandesh._logger.debug(
                "Cannot read routing instance %s", rinst_fq_name_str)
            return None
    # end locate_routing_instance_no_target

    def locate_routing_instance(self, rinst_name, service_chain=None):
        if rinst_name in self.rinst:
            return self.rinst[rinst_name]

        alloc_new = False
        rinst_fq_name_str = '%s:%s' % (self.obj.get_fq_name_str(), rinst_name)
        old_rtgt = None
        try:
            rtgt_num = int(self._rt_cf.get(rinst_fq_name_str)['rtgt_num'])
            if rtgt_num < common.BGP_RTGT_MIN_ID:
                old_rtgt = rtgt_num
                raise pycassa.NotFoundException
            rtgt_ri_fq_name_str = self._rt_allocator.read(rtgt_num)
            if (rtgt_ri_fq_name_str != rinst_fq_name_str):
                alloc_new = True
        except pycassa.NotFoundException:
            alloc_new = True

        if (alloc_new):
            # TODO handle overflow + check alloc'd id is not in use
            rtgt_num = self._rt_allocator.alloc(rinst_fq_name_str)
            self._rt_cf.insert(rinst_fq_name_str, {'rtgt_num': str(rtgt_num)})

        rt_key = "target:%s:%d" % (self.get_autonomous_system(), rtgt_num)
        rtgt_obj = RouteTargetST.locate(rt_key)
        inst_tgt_data = InstanceTargetType()

        try:
            try:
                rinst_obj = _vnc_lib.routing_instance_read(
                    fq_name_str=rinst_fq_name_str)
                if rinst_obj.parent_uuid != self.obj.uuid:
                    # Stale object. Delete it.
                    _vnc_lib.routing_instance_delete(id=rinst_obj.uuid)
                    rinst_obj = None
                else:
                    rinst_obj.set_route_target(rtgt_obj, inst_tgt_data)
                    _vnc_lib.routing_instance_update(rinst_obj)
            except NoIdError:
                rinst_obj = None
            if rinst_obj is None:
                rinst_obj = RoutingInstance(rinst_name, self.obj)
                rinst_obj.set_route_target(rtgt_obj, inst_tgt_data)
                _vnc_lib.routing_instance_create(rinst_obj)
        except HttpError as he:
            _sandesh._logger.debug(
                "HTTP error while creating routing instance: %d, %s",
                he.status_code, he.content)
            return None

        if rinst_obj.name == self._default_ri_name:
            self._route_target = rtgt_num

        rinst = RoutingInstanceST(rinst_obj, service_chain, rt_key)
        self.rinst[rinst_name] = rinst

        if old_rtgt:
            rt_key = "target:%s:%d" % (self.get_autonomous_system(), old_rtgt)
            _vnc_lib.route_target_delete(fq_name=[rt_key])
        return rinst
    # end locate_routing_instance

    def delete_routing_instance(self, ri, old_ri_list=None):
        if old_ri_list:
            for ri2 in old_ri_list.values():
                if ri.get_fq_name_str() in ri2.connections:
                    ri2.delete_connection(ri)
        for vn in self._dict.values():
            for ri2 in vn.rinst.values():
                if ri.get_fq_name_str() in ri2.connections:
                    ri2.delete_connection(ri)

        ri.delete(self)
        del self.rinst[ri.name]
    # end delete_routing_instance

    def update_ipams(self):
        # update prefixes on service chain routing instances
        for sc_list in self.service_chains.values():
            for service_chain in sc_list:
                service_chain.update_ipams(self.name)

        if self.extend:
            self.extend_to_external_routers(True)
        for li in LogicalInterfaceST.values():
            if self.name == li.virtual_network:
                li.refresh_virtual_network()
    # end update_ipams

    def expand_connections(self):
        if '*' in self.connections:
            conn = self.connections - set(['*']) | self.get_vns_in_project()
            for vn in self._dict.values():
                if self.name in vn.connections:
                    conn.add(vn.name)
            return conn
        return self.connections
    # end expand_connections

    def set_route_target_list(self, rt_list):
        ri = self.get_primary_routing_instance()
        old_rt_list = self.rt_list
        self.rt_list = set(rt_list.get_route_target())
        rt_add = self.rt_list - old_rt_list
        rt_del = old_rt_list - self.rt_list
        if len(rt_add) == 0 and len(rt_del) == 0:
            return
        for rt in rt_add:
            RouteTargetST.locate(rt)
        ri.update_route_target_list(rt_add, rt_del)
        for ri_obj in self.rinst.values():
            if ri == ri_obj:
                continue
            ri_obj.update_route_target_list(rt_add, rt_del,
                                            import_export='export')
        for (prefix, nexthop) in self.route_table.items():
            left_ri = self._get_routing_instance_from_route(nexthop)
            if left_ri is not None:
                left_ri.update_route_target_list(rt_add, rt_del,
                                                 import_export='import')
        for rt in rt_del:
            try:
                _vnc_lib.route_target_delete(fq_name=[rt])
            except RefsExistError:
                # if other routing instances are referring to this target,
                # it will be deleted when those instances are deleted
                pass
    # end set_route_target_list

    # next-hop in a route contains fq-name of a service instance, which must
    # be an auto policy instance. This function will get the left vn for that
    # service instance and get the primary and service routing instances
    def _get_routing_instance_from_route(self, next_hop):
        try:
            si = _vnc_lib.service_instance_read(fq_name_str=next_hop)
            si_props = si.get_service_instance_properties()
            if si_props is None:
                return None
        except NoIdError:
            _sandesh._logger.debug("Cannot read service instance %s", next_hop)
            return None
        if not si_props.auto_policy:
            _sandesh._logger.debug("%s: route table next hop must be service "
                                   "instance with auto policy", self.name)
            return None
        left_vn_str = svc_info.get_left_vn(si.get_parent_fq_name_str(),
            si_props.left_virtual_network)
        right_vn_str = svc_info.get_right_vn(si.get_parent_fq_name_str(),
            si_props.right_virtual_network)
        if (not left_vn_str or not right_vn_str):
            _sandesh._logger.debug("%s: route table next hop service instance "
                                   "must have left and right virtual networks",
                                   self.name)
            return None
        left_vn = VirtualNetworkST.get(left_vn_str)
        if left_vn is None:
            _sandesh._logger.debug("Virtual network %s not present",
                                   left_vn_str)
            return None
        sc = ServiceChain.find(left_vn_str, right_vn_str, '<>',
                               [PortType(0, -1)], [PortType(0, -1)], 'any')
        if sc is None:
            _sandesh._logger.debug("Service chain between %s and %s not "
                                   "present", left_vn_str, right_vn_str)
            return None
        left_ri_name = left_vn.get_service_name(sc.name, next_hop)
        return left_vn.rinst.get(left_ri_name)
    # end _get_routing_instance_from_route

    def add_route(self, prefix, next_hop):
        self.route_table[prefix] = next_hop
        left_ri = self._get_routing_instance_from_route(next_hop)
        if left_ri is None:
            _sandesh._logger.debug(
                "left routing instance is none for %s", next_hop)
            return
        service_info = left_ri.obj.get_service_chain_information()
        if service_info is None:
            _sandesh._logger.debug(
                "Service chain info not found for %s", left_ri.name)
            return
        sc_address = service_info.get_service_chain_address()
        static_route_entries = left_ri.obj.get_static_route_entries(
        ) or StaticRouteEntriesType()
        for static_route in static_route_entries.get_route() or []:
            if prefix == static_route.prefix:
                if self.get_route_target() not in static_route.route_target:
                    static_route.route_target.append(self.get_route_target())
                break
        else:
            static_route = StaticRouteType(
                prefix=prefix, next_hop=sc_address,
                route_target=[self.get_route_target()])
            static_route_entries.add_route(static_route)
        left_ri.obj.set_static_route_entries(static_route_entries)
        _vnc_lib.routing_instance_update(left_ri.obj)
        left_ri.update_route_target_list(
            rt_add=self.rt_list | set([self.get_route_target()]),
            import_export="import")
    # end add_route

    def delete_route(self, prefix):
        if prefix not in self.route_table:
            return
        next_hop = self.route_table[prefix]
        del self.route_table[prefix]
        left_ri = self._get_routing_instance_from_route(next_hop)
        if left_ri is None:
            return
        left_ri.update_route_target_list(rt_add=set(),
                                         rt_del=self.rt_list | set(
                                             [self.get_route_target()]),
                                         import_export="import")
        static_route_entries = left_ri.obj.get_static_route_entries()
        for static_route in static_route_entries.get_route() or []:
            if static_route.prefix != prefix:
                continue
            if self.get_route_target() in static_route.route_target:
                static_route.route_target.remove(self.get_route_target())
                if static_route.route_target == []:
                    static_route_entries.delete_route(static_route)
                left_ri.obj._pending_field_updates.add('static_route_entries')
                _vnc_lib.routing_instance_update(left_ri.obj)
                return
    # end delete_route

    def update_route_table(self):
        stale = {}
        for prefix in self.route_table:
            stale[prefix] = True
        for rt_name in self.route_table_refs:
            route_table = RouteTableST.get(rt_name)
            if route_table is None or route_table.routes is None:
                continue
            for route in route_table.routes or []:
                if route.prefix in self.route_table:
                    stale.pop(route.prefix, None)
                    if route.next_hop == self.route_table[route.prefix]:
                        continue
                    self.delete_route(route.prefix)
                # end if route.prefix
                self.add_route(route.prefix, route.next_hop)
            # end for route
        # end for route_table
        for prefix in stale:
            self.delete_route(prefix)
    # end update_route_table

    def uve_send(self, deleted=False):
        vn_trace = UveVirtualNetworkConfig(name=self.name,
                                           connected_networks=[],
                                           partially_connected_networks=[],
                                           routing_instance_list=[],
                                           total_acl_rules=0)

        if deleted:
            vn_trace.deleted = True
            vn_msg = UveVirtualNetworkConfigTrace(data=vn_trace,
                                                  sandesh=_sandesh)
            vn_msg.send(sandesh=_sandesh)
            return

        if self.acl:
            vn_trace.total_acl_rules += \
                len(self.acl.get_access_control_list_entries().get_acl_rule())
        for ri in self.rinst.values():
            vn_trace.routing_instance_list.append(ri.name)
        for rhs in self.expand_connections():
            rhs_vn = VirtualNetworkST.get(rhs)
            if rhs_vn and self.name in rhs_vn.expand_connections():
                # two way connection
                vn_trace.connected_networks.append(rhs)
            else:
                # one way connection
                vn_trace.partially_connected_networks.append(rhs)
        #end for
        for rhs in self.service_chains:
            rhs_vn = VirtualNetworkST.get(rhs)
            if rhs_vn and self.name in rhs_vn.service_chains:
                # two way connection
                vn_trace.connected_networks.append(rhs)
            else:
                # one way connection
                vn_trace.partially_connected_networks.append(rhs)
        #end for
        vn_msg = UveVirtualNetworkConfigTrace(data=vn_trace, sandesh=_sandesh)
        vn_msg.send(sandesh=_sandesh)
    # end uve_send

    def get_service_name(self, sc_name, si_name):
        name = "service-" + sc_name + "-" + si_name
        return name.replace(':', '_')
    # end get_service_name

    @staticmethod
    def get_analyzer_vn_and_ip(analyzer_name):
        vn_analyzer = None
        ip_analyzer = None
        try:
            si = _vnc_lib.service_instance_read(fq_name_str=analyzer_name)

            vm_analyzer = si.get_virtual_machine_back_refs()
            if vm_analyzer is None:
                return (None, None)
            vm_analyzer_obj = _vnc_lib.virtual_machine_read(
                id=vm_analyzer[0]['uuid'])
            if vm_analyzer_obj is None:
                return (None, None)
            vmi_refs = (vm_analyzer_obj.get_virtual_machine_interfaces() or
                        vm_analyzer_obj.get_virtual_machine_interface_back_refs())
            if vmi_refs is None:
                return (None, None)
            for vmi_ref in vmi_refs:
                vmi = _vnc_lib.virtual_machine_interface_read(
                    id=vmi_ref['uuid'])
                vmi_props = vmi.get_virtual_machine_interface_properties()
                if (vmi_props and
                        vmi_props.get_service_interface_type() == 'left'):
                    vmi_vn_refs = vmi.get_virtual_network_refs()
                    if vmi_vn_refs:
                        vn_analyzer = ':'.join(vmi_vn_refs[0]['to'])
                        if_ip = vmi.get_instance_ip_back_refs()
                    if_ip = vmi.get_instance_ip_back_refs()
                    if if_ip:
                        if_ip_obj = _vnc_lib.instance_ip_read(
                            id=if_ip[0]['uuid'])
                        ip_analyzer = if_ip_obj.get_instance_ip_address()
                    break
            # end for vmi_ref
        except NoIdError:
            pass
        return (vn_analyzer, ip_analyzer)
    # end get_analyzer_vn_and_ip
    
    def process_analyzer(self, action):
        analyzer_name = action.mirror_to.analyzer_name
        try:
            (vn_analyzer, ip) = self.get_analyzer_vn_and_ip(analyzer_name)
            if ip:
                action.mirror_to.set_analyzer_ip_address(ip)
            if vn_analyzer:
                _sandesh._logger.debug("Mirror: adding connection from %s-%s",
                                       self.name, vn_analyzer)
                self.add_connection(vn_analyzer)
                if vn_analyzer in VirtualNetworkST:
                    _sandesh._logger.debug("Mirror: adding reverse connection")
                    VirtualNetworkST.get(vn_analyzer).add_connection(self.name)
            else:
                _sandesh._logger.debug("Mirror: %s: no analyzer vn for %s",
                                       self.name, analyzer_name)
        except NoIdError:
            return
    # end process_analyzer

    @staticmethod
    def protocol_policy_to_acl(pproto):
        # convert policy proto input(in str) to acl proto (num)
        if pproto.isdigit():
            return pproto
        return _PROTO_STR_TO_NUM.get(pproto.lower())
    # end protocol_policy_to_acl

    def policy_to_acl_rule(self, prule, dynamic):
        result_acl_rule_list = AclRuleListST(dynamic=dynamic)
        saddr_list = prule.src_addresses
        sp_list = prule.src_ports
        daddr_list = prule.dst_addresses
        dp_list = prule.dst_ports
        rule_uuid = prule.get_rule_uuid()

        arule_proto = self.protocol_policy_to_acl(prule.protocol)
        if arule_proto is None:
            # TODO log unknown protocol
            return result_acl_rule_list

        for saddr in saddr_list:
            saddr_match = copy.deepcopy(saddr)
            svn = saddr.virtual_network
            spol = saddr.network_policy
            if svn == "local":
                svn = self.name
                saddr_match.virtual_network = self.name
            for daddr in daddr_list:
                daddr_match = copy.deepcopy(daddr)
                dvn = daddr.virtual_network
                dpol = daddr.network_policy
                if dvn == "local":
                    dvn = self.name
                    daddr_match.virtual_network = self.name
                if dvn in [self.name, 'any']:
                    remote_network_name = svn
                elif svn in [self.name, 'any']:
                    remote_network_name = dvn
                elif not dvn and dpol:
                    dp_obj = NetworkPolicyST.get(dpol)
                    if self.name in dp_obj.networks_back_ref:
                        remote_network_name = svn
                        daddr_match.network_policy = None
                        daddr_match.virtual_network = self.name
                    else:
                        _sandesh._logger.debug("network policy rule attached to %s"
                                               "has src = %s, dst = %s. Ignored.",
                                               self.name, svn or spol, dvn or dpol)
                        continue

                elif not svn and spol:
                    sp_obj = NetworkPolicyST.get(spol)
                    if self.name in sp_obj.networks_back_ref:
                        remote_network_name = dvn
                        daddr_match.network_policy = None
                        saddr_match.virtual_network = self.name
                    else:
                        _sandesh._logger.debug("network policy rule attached to %s"
                                               "has src = %s, dst = %s. Ignored.",
                                               self.name, svn or spol, dvn or dpol)
                        continue
                else:
                    _sandesh._logger.debug("network policy rule attached to %s"
                                           "has svn = %s, dvn = %s. Ignored.",
                                           self.name, svn, dvn)
                    continue

                service_list = None
                if prule.action_list and prule.action_list.apply_service != []:
                    if remote_network_name == self.name:
                        _sandesh._logger.debug("Service chain source and dest "
                                               "vn are same: %s", self.name)
                        continue
                    remote_vn = VirtualNetworkST.get(remote_network_name)
                    if remote_vn is None:
                        _sandesh._logger.debug(
                            "Network %s not found while apply service chain "
                            "to network %s", remote_network_name, self.name)
                        continue
                    service_list = copy.deepcopy(
                        prule.action_list.apply_service)
                    sc_name = self.add_service_chain(remote_network_name, svn,
                        dvn, prule.direction, sp_list, dp_list, arule_proto,
                        service_list)

                for sp in sp_list:
                    for dp in dp_list:
                        if prule.action_list:
                            action = copy.deepcopy(prule.action_list)
                            if (service_list and svn in [self.name, 'any']):
                                    service_ri = self.get_service_name(sc_name,
                                        service_list[0])
                                    action.assign_routing_instance = \
                                        self.name + ':' + service_ri
                        else:
                            return result_acl_rule_list

                        if (action.mirror_to is not None and
                                action.mirror_to.analyzer_name is not None):
                            if ((svn in [self.name, 'any']) or
                                    (dvn in [self.name, 'any'])):
                                self.process_analyzer(action)
                            if (service_list and dvn in [self.name, 'any']):
                                    service_ri = self.get_service_name(sc_name,
                                        service_list[-1])
                                    action.assign_routing_instance = \
                                        self.name +':' + service_ri
                            else:
                                action.assign_routing_instance = None

                        if saddr_match.network_policy:
                            pol = NetworkPolicyST.get(saddr_match.network_policy)
                            if not pol:
                                _sandesh._logger.debug(
                                    "Policy %s not found while applying policy "
                                    "to network %s", saddr_match.network_policy,
                                     self.name)
                                continue
                            sa_list = [AddressType(virtual_network=x)
                                       for x in pol.networks_back_ref]
                        else:
                            sa_list = [saddr_match]

                        if daddr_match.network_policy:
                            pol = NetworkPolicyST.get(daddr_match.network_policy)
                            if not pol:
                                _sandesh._logger.debug(
                                    "Policy %s not found while applying policy "
                                    "to network %s", daddr_match.network_policy,
                                     self.name)
                                continue
                            da_list = [AddressType(virtual_network=x)
                                       for x in pol.networks_back_ref]
                        else:
                            da_list = [daddr_match]

                        for sa in sa_list:
                            for da in da_list:
                                match = MatchConditionType(arule_proto,
                                                           sa, sp,
                                                           da, dp)
                                acl = AclRuleType(match, action, rule_uuid)
                                result_acl_rule_list.append(acl)
                                if ((prule.direction == "<>") and
                                    (sa != da or sp != dp)):
                                    rmatch = MatchConditionType(arule_proto,
                                                                da, dp,
                                                                sa, sp)
                                    raction = copy.deepcopy(action)
                                    if (service_list and dvn in [self.name, 'any']):
                                        service_ri = self.get_service_name(
                                            sc_name, service_list[-1])
                                        raction.assign_routing_instance = \
                                            self.name + ':' + service_ri
                                    else:
                                        raction.assign_routing_instance = None

                                    acl = AclRuleType(rmatch, raction, rule_uuid)
                                    result_acl_rule_list.append(acl)
                    # end for dp
                # end for sp
            # end for daddr
        # end for saddr
        return result_acl_rule_list
    # end policy_to_acl_rule

# end class VirtualNetworkST


class RouteTargetST(object):

    @classmethod
    def locate(cls, rt_key):
        try:
            rtgt_obj = _vnc_lib.route_target_read(fq_name=[rt_key])
        except NoIdError:
            rtgt_obj = RouteTarget(rt_key)
            _vnc_lib.route_target_create(rtgt_obj)
        return rtgt_obj
    # end locate
# end RoutTargetST
# a struct to store attributes related to Network Policy needed by schema
# transformer


class NetworkPolicyST(DictST):
    _dict = {}

    def __init__(self, name):
        self.name = name
        self.obj = NetworkPolicy(name)
        self.networks_back_ref = set()
        self.internal = False
        self.rules = []
        self.analyzer_vn_set = set()
        self.policies = set()
    # end __init__

    @classmethod
    def delete(cls, name):
        if name in cls._dict:
            del cls._dict[name]
    # end delete
    
    def add_rules(self, entries):
        network_set = self.networks_back_ref | self.analyzer_vn_set
        if entries is None:
            self.rules = []
        self.rules = entries.policy_rule
        self.policies = set()
        self.analyzer_vn_set = set()
        for prule in self.rules:
            if (prule.action_list and prule.action_list.mirror_to and
                    prule.action_list.mirror_to.analyzer_name):
                (vn, _) = VirtualNetworkST.get_analyzer_vn_and_ip(
                    prule.action_list.mirror_to.analyzer_name)
                if vn:
                    self.analyzer_vn_set.add(vn)
            for addr in prule.src_addresses + prule.dst_addresses:
                if addr.network_policy:
                    self.policies.add(addr.network_policy)
        # end for prule
        
        network_set |= self.analyzer_vn_set
        return network_set
    #end add_rules
# end class NetworkPolicyST


class RouteTableST(DictST):
    _dict = {}

    def __init__(self, name):
        self.name = name
        self.obj = RouteTableType(name)
        self.network_back_refs = set()
        self.routes = None
    # end __init__

    @classmethod
    def delete(cls, name):
        if name in cls._dict:
            del cls._dict[name]
    # end delete
# end RouteTableST

# a struct to store attributes related to Security Group needed by schema
# transformer


class SecurityGroupST(DictST):
    _dict = {}
    _sg_id_allocator = None

    def update_acl(self, from_value, to_value):
        for acl in [self.ingress_acl, self.egress_acl]:
            if acl is None:
                continue
            acl_entries = acl.get_access_control_list_entries()
            update = False
            for acl_rule in acl_entries.get_acl_rule() or []:
                if (acl_rule.match_condition.src_address.security_group ==
                        from_value):
                    acl_rule.match_condition.src_address.security_group =\
                        to_value
                    update = True
            if update:
                _vnc_lib.access_control_list_update(acl)
    # end update_acl
                
    def __init__(self, name):
        self.name = name
        self.obj = _vnc_lib.security_group_read(fq_name_str=name)
        if not self.obj.get_security_group_id():
            # TODO handle overflow + check alloc'd id is not in use
            sg_id_num = self._sg_id_allocator.alloc(name)
            self.obj.set_security_group_id(sg_id_num)
            _vnc_lib.security_group_update(self.obj)
        self.ingress_acl = None
        self.egress_acl = None
        acls = self.obj.get_access_control_lists()
        for acl in acls or []:
            if acl['to'][-1] == 'egress-access-control-list':
                self.egress_acl = _vnc_lib.access_control_list_read(
                    id=acl['uuid'])
            elif acl['to'][-1] == 'ingress-access-control-list':
                self.ingress_acl = _vnc_lib.access_control_list_read(
                    id=acl['uuid'])
            else:
                _vnc_lib.access_control_list_delete(id=acl['uuid'])
        self.update_policy_entries(self.obj.get_security_group_entries())
        for sg in self._dict.values():
            sg.update_acl(from_value=name,
                          to_value=self.obj.get_security_group_id())
        # end for sg
    # end __init__

    @classmethod
    def delete(cls, name):
        sg = cls._dict.get(name)
        if sg is None:
            return
        sg.update_policy_entries(None)
        sg_id = sg.obj.get_security_group_id()
        if sg_id is not None:
            cls._sg_id_allocator.delete(sg.obj.get_security_group_id())
        del cls._dict[name]
        for sg in cls._dict.values():
            sg.update_acl(from_value=sg_id, to_value=name)
        # end for sg
    # end delete

    def update_policy_entries(self, policy_rule_entries):
        ingress_acl_entries = AclEntriesType()
        egress_acl_entries = AclEntriesType()

        if policy_rule_entries:
            prules = policy_rule_entries.get_policy_rule() or []
        else:
            prules = []

        for prule in prules:
            (ingress_list, egress_list) = self.policy_to_acl_rule(prule)
            ingress_acl_entries.acl_rule.extend(ingress_list)
            egress_acl_entries.acl_rule.extend(egress_list)
        # end for prule

        self.ingress_acl = _access_control_list_update(
            self.ingress_acl, "ingress-access-control-list", self.obj,
            ingress_acl_entries)
        self.egress_acl = _access_control_list_update(
            self.egress_acl, "egress-access-control-list", self.obj,
            egress_acl_entries)
    # end update_policy_entries

    def _convert_security_group_name_to_id(self, addr):
        if addr.security_group is None:
            return
        if addr.security_group in ['local', self.name]:
            addr.security_group = self.obj.get_security_group_id()
        elif addr.security_group == 'any':
            addr.security_group = -1
        elif addr.security_group in self._dict:
            addr.security_group = self._dict[
                addr.security_group].obj.get_security_group_id()
    # end _convert_security_group_name_to_id

    def policy_to_acl_rule(self, prule):
        ingress_acl_rule_list = []
        egress_acl_rule_list = []
        rule_uuid = prule.get_rule_uuid()

        # convert policy proto input(in str) to acl proto (num)
        if prule.protocol.isdigit():
            arule_proto = prule.protocol
        elif prule.protocol in _PROTO_STR_TO_NUM:
            arule_proto = _PROTO_STR_TO_NUM[prule.protocol.lower()]
        else:
            # TODO log unknown protocol
            return (ingress_acl_rule_list, egress_acl_rule_list)

        acl_rule_list = None
        for saddr in prule.src_addresses:
            saddr_match = copy.deepcopy(saddr)
            self._convert_security_group_name_to_id(saddr_match)
            if saddr.security_group == 'local':
                saddr_match.security_group = None
                acl_rule_list = egress_acl_rule_list
            for sp in prule.src_ports:
                for daddr in prule.dst_addresses:
                    daddr_match = copy.deepcopy(daddr)
                    self._convert_security_group_name_to_id(daddr_match)
                    if daddr.security_group == 'local':
                        daddr_match.security_group = None
                        acl_rule_list = ingress_acl_rule_list
                    for dp in prule.dst_ports:
                        action = ActionListType(simple_action='pass')
                        match = MatchConditionType(arule_proto,
                                                   saddr_match, sp,
                                                   daddr_match, dp)
                        acl = AclRuleType(match, action, rule_uuid)
                        acl_rule_list.append(acl)
                    # end for dp
                # end for daddr
            # end for sp
        # end for saddr
        return (ingress_acl_rule_list, egress_acl_rule_list)
    # end policy_to_acl_rule
# end class SecurityGroupST

# a struct to store attributes related to Routing Instance needed by
# schema transformer


class RoutingInstanceST(object):

    def __init__(self, ri_obj, service_chain=None, route_target=None):
        self.name = ri_obj.name
        self.obj = ri_obj
        self.service_chain = service_chain
        self.route_target = route_target
        self.connections = set()
    # end __init__

    def get_fq_name(self):
        return self.obj.get_fq_name()
    # end get_fq_name

    def get_fq_name_str(self):
        return self.obj.get_fq_name_str()
    # end get_fq_name_str

    def add_connection(self, ri2):
        self.connections.add(ri2.get_fq_name_str())
        ri2.connections.add(self.get_fq_name_str())
        self.obj = _vnc_lib.routing_instance_read(id=self.obj.uuid)
        ri2.obj = _vnc_lib.routing_instance_read(id=ri2.obj.uuid)

        conn_data = ConnectionType()
        self.obj.add_routing_instance(ri2.obj, conn_data)
        _vnc_lib.routing_instance_update(self.obj)
    # end add_connection

    def delete_connection(self, ri2):
        self.connections.discard(ri2.get_fq_name_str())
        ri2.connections.discard(self.get_fq_name_str())
        self.obj = _vnc_lib.routing_instance_read(id=self.obj.uuid)
        self.obj.del_routing_instance(ri2.obj)
        _vnc_lib.routing_instance_update(self.obj)
    # end delete_connection

    def delete_connection_fq_name(self, ri2_fq_name_str):
        self.connections.discard(ri2_fq_name_str)
        rinst1_obj = _vnc_lib.routing_instance_read(id=self.obj.uuid)
        rinst2_obj = _vnc_lib.routing_instance_read(
            fq_name_str=ri2_fq_name_str)
        rinst1_obj.del_routing_instance(rinst2_obj)
        _vnc_lib.routing_instance_update(rinst1_obj)
    # end delete_connection_fq_name

    def add_service_info(self, remote_vn, service_instance=None,
                         service_chain_address=None, source_ri=None):
        service_info = self.obj.get_service_chain_information(
        ) or ServiceChainInfo()
        if service_instance:
            service_info.set_service_instance(service_instance)
        if service_chain_address:
            service_info.set_routing_instance(
                remote_vn.get_primary_routing_instance().get_fq_name_str())
            service_info.set_service_chain_address(service_chain_address)
        if source_ri:
            service_info.set_source_routing_instance(source_ri)
        prefix = []
        ipams = remote_vn.ipams
        prefix = [s.subnet.ip_prefix + '/' + str(s.subnet.ip_prefix_len)
                  for ipam in ipams.values() for s in ipam.ipam_subnets]
        if (prefix != service_info.get_prefix()):
            service_info.set_prefix(prefix)
        self.obj.set_service_chain_information(service_info)
    # end add_service_info

    def update_route_target_list(self, rt_add, rt_del=None,
                                 import_export=None):
        for rt in rt_add:
            rtgt_obj = RouteTarget(rt)
            inst_tgt_data = InstanceTargetType(import_export=import_export)
            self.obj.add_route_target(rtgt_obj, inst_tgt_data)
        for rt in rt_del or set():
            rtgt_obj = RouteTarget(rt)
            self.obj.del_route_target(rtgt_obj)
        if len(rt_add) or len(rt_del or set()):
            _vnc_lib.routing_instance_update(self.obj)
    # end update_route_target_list

    def delete(self, vn_obj=None):
        rtgt_list = self.obj.get_route_target_refs()
        ri_fq_name_str = self.obj.get_fq_name_str()
        rt_cf = VirtualNetworkST._rt_cf
        try:
            rtgt = int(rt_cf.get(ri_fq_name_str)['rtgt_num'])
            rt_cf.remove(ri_fq_name_str)
            VirtualNetworkST._rt_allocator.delete(rtgt)
        except pycassa.NotFoundException:
            pass

        service_chain = self.service_chain
        if vn_obj is not None and service_chain is not None:
            # self.free_service_chain_ip(service_chain)
            vn_obj.free_service_chain_ip(self.obj.name)

            uve = UveServiceChainData(name=service_chain, deleted=True)
            uve_msg = UveServiceChain(data=uve, sandesh=_sandesh)
            uve_msg.send(sandesh=_sandesh)

        # refresh the ri object because it could have changed
        self.obj = _vnc_lib.routing_instance_read(id=self.obj.uuid)
        vmi_refs = self.obj.get_virtual_machine_interface_back_refs()
        for vmi in vmi_refs or []:
            try:
                vmi_obj = _vnc_lib.virtual_machine_interface_read(
                    id=vmi['uuid'])
                if service_chain is not None:
                    VirtualNetworkST.free_service_chain_vlan(
                        vmi_obj.get_parent_fq_name_str(), service_chain)
            except NoIdError:
                continue

            vmi_obj.del_routing_instance(ri.obj)
            _vnc_lib.virtual_machine_interface_update(vmi_obj)
        # end for vmi
        _vnc_lib.routing_instance_delete(id=self.obj.uuid)
        for rtgt in rtgt_list:
            try:
                _vnc_lib.route_target_delete(id=rtgt['uuid'])
            except RefsExistError:
                # if other routing instances are referring to this target,
                # it will be deleted when those instances are deleted
                pass

# end class RoutingInstanceST


class ServiceChain(DictST):
    _dict = {}
    
    @classmethod
    def init(cls):
        try:
            for (name,columns) in cls._service_chain_uuid_cf.get_range():
                chain = jsonpickle.decode(columns['value'])
                cls._dict[name] = chain
        except pycassa.NotFoundException:
            pass
    # end init
    
    def __init__(self, name, left_vn, right_vn, direction, sp_list, dp_list,
                 protocol, service_list):
        self.name = name
        self.left_vn = left_vn
        self.right_vn = right_vn
        self.direction = direction
        self.sp_list = sp_list
        self.dp_list = dp_list
        self.service_list = service_list
        self.nat_service = False

        self.protocol = protocol
        self.created = False
    # end __init__

    def __eq__(self, other):
        if self.name != other.name:
            return False
        if self.sp_list != other.sp_list:
            return False
        if self.dp_list != other.dp_list:
            return False
        if self.direction != other.direction:
            return False
        if self.service_list != other.service_list:
            return False
        if self.protocol != other.protocol:
            return False
        return True
    # end __eq__

    @classmethod
    def find(cls, left_vn, right_vn, direction, sp_list, dp_list, protocol):
        for sc in ServiceChain.values():
            if (left_vn == sc.left_vn and
                right_vn == sc.right_vn and
                sp_list == sc.sp_list and
                dp_list == sc.dp_list and
                direction == sc.direction and
                protocol == sc.protocol):
                    return sc
        # end for sc
        return None
    # end find
    
    @classmethod
    def find_or_create(cls, left_vn, right_vn, direction, sp_list, dp_list,
                       protocol, service_list):
        sc = cls.find(left_vn, right_vn, direction, sp_list, dp_list, protocol)
        if sc is not None:
            return sc
        
        name = str(uuid.uuid4())
        sc = ServiceChain(name, left_vn, right_vn, direction, sp_list,
                          dp_list, protocol, service_list)
        ServiceChain._dict[name] = sc
        cls._service_chain_uuid_cf.insert(name,
                                          {'value': jsonpickle.encode(sc)})
        return sc
    # end find_or_create

    def update_ipams(self, vn2_name):
        if not self.created:
            return
        if self.left_vn == vn2_name:
            vn1 = VirtualNetworkST.get(self.right_vn)
        if self.right_vn == vn2_name:
            vn1 = VirtualNetworkST.get(self.left_vn)

        vn2 = VirtualNetworkST.get(vn2_name)
        if vn1 is None or vn2 is None:
            return

        for service in self.service_list:
            service_name = vn1.get_service_name(self.name, service)
            service_ri = vn1.locate_routing_instance(service_name, self.name)
            if service_ri is None:
                continue
            service_ri.add_service_info(vn2, service)
            _vnc_lib.routing_instance_update(service_ri.obj)
        # end for service
    #end update_ipams

    def create(self):
        if self.created:
            # already created
            return

        vn1_obj = VirtualNetworkST.locate(self.left_vn)
        vn2_obj = VirtualNetworkST.locate(self.right_vn)
        #sc_ip_address = vn1_obj.allocate_service_chain_ip(sc_name)
        if not vn1_obj or not vn2_obj:
            _sandesh._logger.debug(
                "service chain %s: vn1_obj or vn2_obj is None", self.name)
            return None

        service_ri2 = vn1_obj.get_primary_routing_instance()
        first_node = True
        for service in self.service_list:
            service_name1 = vn1_obj.get_service_name(self.name, service)
            service_name2 = vn2_obj.get_service_name(self.name, service)
            service_ri1 = vn1_obj.locate_routing_instance(
                service_name1, self.name)
            if service_ri1 is None or service_ri2 is None:
                _sandesh._logger.debug("service chain %s: service_ri1 or "
                                       "service_ri2 is None", self.name)
                return None
            service_ri2.add_connection(service_ri1)
            service_ri2 = vn2_obj.locate_routing_instance(
                service_name2, self.name)
            if service_ri2 is None:
                _sandesh._logger.debug(
                    "service chain %s: service_ri2 is None", self.name)
                return None

            if first_node:
                first_node = False
                service_ri1.update_route_target_list(
                    vn1_obj.rt_list, import_export='export')

            try:
                service_instance = _vnc_lib.service_instance_read(
                    fq_name_str=service)
            except NoIdError:
                _sandesh._logger.debug("service chain %s: NoIdError while "
                                       "reading service_instance", self.name)
                return None

            try:
                si_refs = service_instance.get_service_template_refs()
                if not si_refs:
                    _sandesh._logger.debug(
                        "service chain %s: si_refs is None", self.name)
                    return None
                service_template = _vnc_lib.service_template_read(
                    id=si_refs[0]['uuid'])
            except NoIdError:
                _sandesh._logger.debug("service chain %s: NoIdError while "
                                       "reading service template", self.name)
                return None

            mode = service_template.get_service_template_properties(
            ).get_service_mode()
            if mode == "transparent":
                transparent = True
            elif mode == "in-network":
                transparent = False
            elif mode == "in-network-nat":
                transparent = False
                self.nat_service = True
            else:
                transparent = True
            _sandesh._logger.debug("service chain %s: creating %s chain",
                                   self.name, mode)

            if transparent:
                sc_ip_address = vn1_obj.allocate_service_chain_ip(
                    service_name1)
                if sc_ip_address is None:
                    return None
                service_ri1.add_service_info(vn2_obj, service, sc_ip_address)
                if self.direction == "<>":
                    service_ri2.add_service_info(vn1_obj, service,
                                                 sc_ip_address)

            if service_instance:
                try:
                    vm_refs = service_instance.get_virtual_machine_back_refs()
                except NoIdError:
                    _sandesh._logger.debug("service chain %s: NoIdError on "
                                           "service_instance, disappear?",
                                           self.name)
                    return None
                if vm_refs is None:
                    return None
                for service_vm in vm_refs:
                    vm_obj = _vnc_lib.virtual_machine_read(
                        id=service_vm['uuid'])
                    if transparent:
                        result = self.process_transparent_service(
                            vm_obj, sc_ip_address, service_ri1, service_ri2)
                    else:
                        result = self.process_in_network_service(
                            vm_obj, service, vn1_obj, vn2_obj, service_ri1,
                            service_ri2)
                    if not result:
                        return None
            _vnc_lib.routing_instance_update(service_ri1.obj)
            _vnc_lib.routing_instance_update(service_ri2.obj)

        service_ri2.update_route_target_list(
            vn2_obj.rt_list, import_export='export')

        service_ri2.add_connection(vn2_obj.get_primary_routing_instance())

        if not transparent and len(self.service_list) == 1:
            for vn in VirtualNetworkST.values():
                for prefix, nexthop in vn.route_table.items():
                    if nexthop == self.service_list[0]:
                        vn.add_route(prefix, nexthop)

        self.created = True
        uve = UveServiceChainData(name=self.name)
        uve.source_virtual_network = self.left_vn
        uve.destination_virtual_network = self.right_vn
        if self.sp_list:
            uve.source_ports = "%d-%d" % (
                self.sp_list[0].start_port, self.sp_list[0].end_port)
        if self.dp_list:
            uve.destination_ports = "%d-%d" % (
                self.dp_list[0].start_port, self.dp_list[0].end_port)
        uve.protocol = self.protocol
        uve.direction = self.direction
        uve.services = copy.deepcopy(self.service_list)
        uve_msg = UveServiceChain(data=uve, sandesh=_sandesh)
        uve_msg.send(sandesh=_sandesh)

        return self.name
    # end process_service_chain

    def process_transparent_service(self, vm_obj, sc_ip_address, service_ri1,
                                    service_ri2):
        left_found = False
        right_found = False
        vlan = VirtualNetworkST.allocate_service_chain_vlan(
            vm_obj.uuid, self.name)
        for interface in (vm_obj.get_virtual_machine_interfaces() or
                          vm_obj.get_virtual_machine_interface_back_refs()
                          or []):
            if_obj = _vnc_lib.virtual_machine_interface_read(
                id=interface['uuid'])
            props = if_obj.get_virtual_machine_interface_properties()
            if not props:
                return False
            interface_type = props.get_service_interface_type()
            if not interface_type:
                return False
            ri_refs = if_obj.get_routing_instance_refs()
            if interface_type == 'left':
                left_found = True
                pbf = PolicyBasedForwardingRuleType()
                pbf.set_direction('both')
                pbf.set_vlan_tag(vlan)
                pbf.set_src_mac('02:00:00:00:00:01')
                pbf.set_dst_mac('02:00:00:00:00:02')
                pbf.set_service_chain_address(sc_ip_address)
                if (service_ri1.get_fq_name() not in
                        [ref['to'] for ref in (ri_refs or [])]):
                    if_obj.add_routing_instance(service_ri1.obj, pbf)
                    _vnc_lib.virtual_machine_interface_update(if_obj)
            if interface_type == 'right' and self.direction == '<>':
                right_found = True
                pbf = PolicyBasedForwardingRuleType()
                pbf.set_direction('both')
                pbf.set_src_mac('02:00:00:00:00:02')
                pbf.set_dst_mac('02:00:00:00:00:01')
                pbf.set_vlan_tag(vlan)
                pbf.set_service_chain_address(sc_ip_address)
                if (service_ri2.get_fq_name() not in
                        [ref['to'] for ref in (ri_refs or [])]):
                    if_obj.add_routing_instance(service_ri2.obj, pbf)
                    _vnc_lib.virtual_machine_interface_update(if_obj)
        return left_found and (self.direction != '<>' or right_found)
    # end process_transparent_service

    def process_in_network_service(self, vm_obj, service, vn1_obj, vn2_obj,
                                   service_ri1, service_ri2):
        left_found = False
        right_found = False
        for interface in (vm_obj.get_virtual_machine_interfaces() or
                          vm_obj.get_virtual_machine_interface_back_refs()
                          or []):
            if_obj = VirtualMachineInterfaceST.get(':'.join(interface['to']))
            if (if_obj is None or
                if_obj.service_interface_type not in ['left', 'right']):
                continue
            ip_obj = None
            for ip_ref in if_obj.instance_ip_set:
                try:
                    ip_obj = _vnc_lib.instance_ip_read(fq_name_str=ip_ref)
                    break
                except NoIdError as e:
                    _sandesh._logger.debug(
                        "NoIdError while reading ip address for interface "
                        "%s: %s", if_obj.get_fq_name_str(), str(e))
                    return False
            else:
                _sandesh._logger.debug(
                        "No ip address found for interface " + if_obj.name)
                return False

            if if_obj.service_interface_type == 'left':
                left_found = True
                ip_addr = ip_obj.get_instance_ip_address()
                service_ri1.add_service_info(vn2_obj, service, ip_addr,
                     vn1_obj.get_primary_routing_instance().get_fq_name_str())
            elif self.direction == '<>' and not self.nat_service:
                right_found = True
                ip_addr = ip_obj.get_instance_ip_address()
                service_ri2.add_service_info(vn1_obj, service, ip_addr,
                     vn2_obj.get_primary_routing_instance().get_fq_name_str())
        return left_found and (self.nat_service or self.direction != '<>' or
                               right_found)
    # end process_in_network_service

    def destroy(self):
        if not self.created:
            return

        self.created = False
        vn1_obj = VirtualNetworkST.get(self.left_vn)
        vn2_obj = VirtualNetworkST.get(self.right_vn)

        for service in self.service_list:
            if vn1_obj:
                service_name1 = vn1_obj.get_service_name(self.name, service)
                service_ri1 = vn1_obj.rinst.get(service_name1)
                if service_ri1 is not None:
                    vn1_obj.delete_routing_instance(service_ri1)
            if vn2_obj:
                service_name2 = vn2_obj.get_service_name(self.name, service)
                service_ri2 = vn2_obj.rinst.get(service_name2)
                if service_ri2 is not None:
                    vn2_obj.delete_routing_instance(service_ri2)
        # end for service
    # end destroy

    def delete(self):
        del self._dict[self.name]
        try:
            self._service_chain_uuid_cf.remove(self.name)
        except pycassa.NotFoundException:
            pass
    # end delete
    
    def build_introspect(self):
        sc = sandesh.ServiceChain(sc_name=self.name)
        sc.left_virtual_network = self.left_vn
        sc.right_virtual_network = self.right_vn
        sc.protocol = self.protocol
        port_list = []
        for sp in self.sp_list:
            port_list.append("%s-%s"%(sp.start_port, sp.end_port))
        sc.src_ports = ','.join(port_list)
        port_list = []
        for dp in self.dp_list:
            port_list.append("%s-%s"%(dp.start_port, dp.end_port))
        sc.dst_ports = ','.join(port_list)
        sc.direction = self.direction
        sc.service_list = self.service_list
        sc.nat_service = self.nat_service
        sc.created = self.created
        return sc
    # end build_introspect
    
# end ServiceChain


class AclRuleListST(object):

    def __init__(self, rule_list=None, dynamic=False):
        self._list = rule_list or []
        self.dynamic = dynamic
    # end __init__

    def get_list(self):
        return self._list
    # end get_list

    def append(self, rule):
        if not self._rule_is_subset(rule):
            self._list.append(rule)
            return True
        return False
    # end append

    # for types that have start and end integer
    @staticmethod
    def _range_is_subset(lhs, rhs):
        return (lhs.start_port >= rhs.start_port and
               (rhs.end_port == -1 or lhs.end_port <= rhs.end_port))

    def _rule_is_subset(self, rule):
        for elem in self._list:
            lhs = rule.match_condition
            rhs = elem.match_condition
            if (self._range_is_subset(lhs.src_port, rhs.src_port) and
                self._range_is_subset(lhs.dst_port, rhs.dst_port) and
                rhs.protocol in [lhs.protocol, 'any'] and
                (rhs.src_address.virtual_network in
                    [lhs.src_address.virtual_network, 'any']) and
                (rhs.dst_address.virtual_network in
                    [lhs.dst_address.virtual_network, 'any'])):

                if not self.dynamic:
                    return True
                if (rule.action_list.mirror_to.analyzer_name ==
                        elem.action_list.mirror_to.analyzer_name):
                    return True
        # end for elem
        return False
    # end _rule_is_subset

    def update_acl_entries(self, acl_entries):
        old_list = AclRuleListST(acl_entries.get_acl_rule(), self.dynamic)
        self._list[:] = [rule for rule in self._list if old_list.append(rule)]
        acl_entries.set_acl_rule(old_list.get_list())
    # end update_acl_entries
# end AclRuleListST


class BgpRouterST(DictST):
    _dict = {}

    def __init__(self, name):
        self.name = name
        self.config = None
        self.peers = set()
        self.address = None
        self.vendor = None
        self.vnc_managed = False
        self.asn = None
        self.prouter = None
        self.identifier = None
    # end __init__

    @classmethod
    def delete(cls, name):
        if name in cls._dict:
            cls._dict[name].delete_config()
            del cls._dict[name]
    # end delete

    def update_autonomous_system(self, asn):
        if self.vendor not in ["contrail", None]:
            self.asn = asn
            return
        my_asn = int(VirtualNetworkST.get_autonomous_system())
        if asn == my_asn:
            self.asn = asn
            self.update_peering()
            return
        bgp_router_obj = _vnc_lib.bgp_router_read(fq_name_str=self.name)
        params = bgp_router_obj.get_bgp_router_parameters()
        params.autonomous_system = my_asn
        bgp_router_obj.set_bgp_router_parameters(params)
        _vnc_lib.bgp_router_update(bgp_router_obj)
        self.asn = my_asn
        self.update_peering()
    # end update_autonomous_system

    def update_peering(self):
        my_asn = int(VirtualNetworkST.get_autonomous_system())
        if self.asn != my_asn:
            return
        try:
            obj = _vnc_lib.bgp_router_read(fq_name_str=self.name)
        except NoIdError as e:
            _sandesh._logger.debug("NoIdError while reading bgp router "
                                   "%s: %s", self.name, str(e))
            return

        for router in self._dict.values():
            if router.name == self.name:
                continue
            if router.asn != my_asn:
                continue
            if router.vendor not in ['contrail', None]:
                continue
            router_fq_name = router.name.split(':')
            if (router_fq_name in
                    [ref['to'] for ref in (obj.get_bgp_router_refs() or [])]):
                continue
            router_obj = BgpRouter()
            router_obj.fq_name = router_fq_name
            af = AddressFamilies(family=['inet-vpn'])
            bsa = BgpSessionAttributes(address_families=af)
            session = BgpSession(attributes=[bsa])
            attr = BgpPeeringAttributes(session=[session])
            obj.add_bgp_router(router_obj, attr)
            try:
                _vnc_lib.bgp_router_update(obj)
            except NoIdError as e:
                _sandesh._logger.debug("NoIdError while updating bgp router "
                                       "%s: %s", self.name, str(e))
                obj.del_bgp_router(router_obj)
    # end update_peering

    def set_config(self, params):
        self.address = params.address
        self.vendor = params.vendor
        self.vnc_managed = params.vnc_managed
        self.update_autonomous_system(params.autonomous_system)
        self.identifier = params.identifier
        if self.vendor != "mx" or not self.vnc_managed:
            if self.config:
                self.delete_config()
            return

        first_time = False if self.config else True
        self.config = etree.Element("group", operation="replace")
        etree.SubElement(self.config, "name").text = "__contrail__"
        etree.SubElement(self.config, "type").text = "internal"
        etree.SubElement(self.config, "local-address").text = params.address
        if (params.address_families):
            for family in params.address_families.family:
                family_etree = etree.SubElement(self.config, "family")
                family_subtree = etree.SubElement(family_etree, family)
                etree.SubElement(family_subtree, "unicast")
        etree.SubElement(self.config, "keep").text = "all"
        self.push_config()

        if first_time:
            for vn in VirtualNetworkST.values():
                if vn.extend:
                    self.add_routing_instance(vn.name, vn.get_route_target())
            # end for vn
            for prouter in PhysicalRouterST.values():
                for pi_name in prouter.physical_interfaces:
                    prouter.add_physical_interface(pi_name)
            # for prouter_name
        # if first_time
# end set_config

    def add_peer(self, router):
        if router in self.peers:
            return
        self.peers.add(router)
        self.push_config()
    # end add_peer

    def delete_peer(self, router):
        if router in self.peers:
            self.peers.remove(router)
            self.push_config()
    # end delete_peer

    def add_routing_instance(self, name, route_target, interface=None):
        if not self.config:
            return
        ri_config = etree.Element("routing-instances")
        ri = etree.SubElement(ri_config, "instance", operation="replace")
        etree.SubElement(
            ri, "name").text = "__contrail__" + name.replace(':', '_')
        etree.SubElement(ri, "instance-type").text = "vrf"
        if interface:
            if_element = etree.SubElement(ri, "interface")
            etree.SubElement(if_element, "name").text = interface
        rt_element = etree.SubElement(ri, "vrf-target")
        etree.SubElement(rt_element, "community").text = route_target
        etree.SubElement(ri, "vrf-table-label")
        vn = VirtualNetworkST.get(name)
        if vn and vn.ipams:
            ri_opt = etree.SubElement(ri, "routing-options")
            static_config = etree.SubElement(ri_opt, "static")
            for ipam in vn.ipams.values():
                for s in ipam.ipam_subnets:
                    prefix = s.subnet.ip_prefix + \
                        '/' + str(s.subnet.ip_prefix_len)
                    route_config = etree.SubElement(static_config, "route")
                    etree.SubElement(route_config, "name").text = prefix
                    etree.SubElement(route_config, "discard")

        self.send_netconf(ri_config)
    # end add_routing_instance

    def delete_routing_instance(self, name):
        if not self.config:
            return
        ri_config = etree.Element("routing-instances")
        ri = etree.SubElement(ri_config, "instance", operation="delete")
        etree.SubElement(ri, "name").text = "__contrail__" + name
        self.send_netconf(ri_config, default_operation="none")
    # end delete_routing_instance

    def push_config(self):
        if self.config is None:
            return
        group_config = copy.deepcopy(self.config)
        proto_config = etree.Element("protocols")
        bgp = etree.SubElement(proto_config, "bgp")
        bgp.append(group_config)
        for peer in self.peers:
            try:
                address = BgpRouterST._dict[peer].address
                if address:
                    nbr = etree.SubElement(group_config, "neighbor")
                    etree.SubElement(nbr, "name").text = address
            except KeyError:
                continue
        routing_options_config = etree.Element("routing-options")
        etree.SubElement(
            routing_options_config,
            "route-distinguisher-id").text = self.identifier
        self.send_netconf([proto_config, routing_options_config])
    # end push_config

    def delete_config(self):
        if not self.config:
            return
        for vn in VirtualNetworkST.values():
            if vn.extend:
                self.delete_routing_instance(vn.name)
        # end for vn
        for prouter in PhysicalRouterST.values():
            for pi in prouter.physical_interfaces:
                prouter.delete_physical_interface(pi)
        # end for prouter

        self.config = None
        proto_config = etree.Element("protocols")
        bgp = etree.SubElement(proto_config, "bgp")
        group = etree.SubElement(bgp, "group", operation="delete")
        etree.SubElement(group, "name").text = "__contrail__"
        self.send_netconf(proto_config, default_operation="none")
    # end delete_config

    def send_netconf(self, new_config, default_operation="merge"):
        try:
            with manager.connect(host=self.address, port=22,
                                 username="root", password="c0ntrail123",
                                 unknown_host_cb=lambda x, y: True) as m:
                add_config = etree.Element(
                    "config",
                    nsmap={"xc": "urn:ietf:params:xml:ns:netconf:base:1.0"})
                config = etree.SubElement(add_config, "configuration")
                if isinstance(new_config, list):
                    for nc in new_config:
                        config.append(nc)
                else:
                    config.append(new_config)
                m.edit_config(
                    target='candidate', config=etree.tostring(add_config),
                    test_option='test-then-set',
                    default_operation=default_operation)
                m.commit()
        except Exception as e:
            _sandesh._logger.debug("Router %s: %s" % (self.address, e.message))

# end class BgpRouterST


class PhysicalRouterST(DictST):
    _dict = {}

    def __init__(self, name):
        self.name = name
        self.bgp_router = None
        self.physical_interfaces = set()
    # end __init__

    @classmethod
    def delete(cls, name):
        if name in cls._dict:
            del cls._dict[name]
    # end delete

    def add_logical_interface(self, li_name):
        if not self.bgp_router:
            return
        li = LogicalInterfaceST.get(li_name)
        if not li or not li.virtual_network:
            return
        vn = VirtualNetworkST.locate(li.virtual_network)
        if not vn:
            return
        bgp_router = BgpRouterST.get(self.bgp_router)
        if not bgp_router:
            return
        bgp_router.add_routing_instance(
            li.virtual_network, vn.get_route_target(), li.name)
    # end add_logical_interface

    def delete_logical_interface(self, li_name):
        if not self.bgp_router:
            return
        li = LogicalInterfaceST.get(li_name)
        if not li or not li.virtual_network:
            return
        bgp_router = BgpRouterST.get(self.bgp_router)
        if not bgp_router:
            return
        bgp_router.delete_routing_instance(li.virtual_network)
    # end add_logical_interface

    def add_physical_interface(self, pi_name):
        pi = PhysicalInterfaceST.get(pi_name)
        if not pi:
            return
        for li_name in pi.logical_interfaces:
            self.add_logical_interface(li_name)
    # end add_physical_interface

    def delete_physical_interface(self, pi_name):
        pi = PhysicalInterfaceST.get(pi_name)
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

# end PhysicalRouterST


class PhysicalInterfaceST(DictST):
    _dict = {}

    def __init__(self, name):
        self.name = name
        self.physical_router = None
        self.logical_interfaces = set()
    # end __init__

    @classmethod
    def delete(cls, name):
        if name in cls._dict:
            del cls._dict[name]
    # end delete

    def add_logical_interface(self, li_name):
        if not self.physical_router:
            return
        pr = PhysicalRouterST.get(self.physical_router)
        if not pr:
            return
        pr.add_logical_interface(li_name)
    # end add_logical_interface

    def delete_logical_interface(self, li_name):
        if not self.physical_router:
            return
        pr = PhysicalRouterST.get(self.physical_router)
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
# end PhysicalInterfaceST


class LogicalInterfaceST(DictST):
    _dict = {}

    def __init__(self, name):
        self.name = name
        self.physical_interface = None
        self.virtual_network = None
    # end __init__

    @classmethod
    def delete(cls, name):
        if name in cls._dict:
            del cls._dict[name]
    # end delete

    def set_virtual_network(self, vn_name):
        if self.virtual_network == vn_name:
            return
        if self.virtual_network and self.physical_interface:
            pi = PhysicalInterfaceST.get(self.physical_interface)
            if pi:
                pi.delete_logical_interface(self.name)
        self.virtual_network = vn_name
        if self.virtual_network and self.physical_interface:
            pi = PhysicalInterfaceST.get(self.physical_interface)
            if pi:
                pi.add_logical_interface(self.name)
    # end set_virtual_network

    def refresh_virtual_network(self):
        if self.virtual_network and self.physical_interface:
            pi = PhysicalInterfaceST.get(self.physical_interface)
            if pi:
                pi.add_logical_interface(self.name)
    # end refresh_virtual_network
# end LogicalInterfaceST


class VirtualMachineInterfaceST(DictST):
    _dict = {}
    def __init__(self, name):
        self.name = name
        self.service_interface_type = None
        self.interface_mirror = None
        self.virtual_network = None
        self.instance_ip_set = set()
    # end __init__
    @classmethod
    
    def delete(cls, name):
        if name in cls._dict:
            del cls._dict[name]
    # end delete
    
    def add_instance_ip(self, ip_name):
        self.instance_ip_set.add(ip_name)
    # end add_instance_ip
    
    def delete_instance_ip(self, ip_name):
        self.instance_ip_set.discard(ip_name)
    # end delete_instance_ip
    
    def set_service_interface_type(self, service_interface_type):
        self.service_interface_type = service_interface_type
    # end set_service_interface_type

    def set_interface_mirror(self, interface_mirror):
        self.interface_mirror = interface_mirror
    # end set_interface_mirror
    
    def set_virtual_network(self, vn_name):
        self.virtual_network = vn_name
        virtual_network = VirtualNetworkST.locate(vn_name)
        if virtual_network is None:
            return
        try:
            if_obj = _vnc_lib.virtual_machine_interface_read(
                fq_name_str=self.name)
        except NoIdError:
            _sandesh._logger.debug("NoIdError while reading interface " +
                                   self.name)
            return
        ri = virtual_network.get_primary_routing_instance().obj
        refs = if_obj.get_routing_instance_refs()
        if ri.get_fq_name() not in [r['to'] for r in (refs or [])]:
            if_obj.add_routing_instance(
                ri, PolicyBasedForwardingRuleType(direction="both"))
            try:
                _vnc_lib.virtual_machine_interface_update(if_obj)
            except NoIdError:
                _sandesh._logger.debug("NoIdError while updating interface " +
                                       self.name)

        for lr in LogicalRouterST.values():
            if self.name in lr.interfaces:
                lr.add_interface(self.name)
    #end set_virtual_network
    
    def process_analyzer(self):
        if self.interface_mirror is None or self.virtual_network is None:
            return
        vn = VirtualNetworkST.get(self.virtual_network)
        if vn is None:
            return
        
        try:
            if_obj = _vnc_lib.virtual_machine_interface_read(
                fq_name_str=self.name)
        except NoIdError:
            _sandesh._logger.debug("NoIdError while reading interface " +
                                   self.name)
            return
        vn.process_analyzer(self.interface_mirror)
        vmi_props = if_obj.get_virtual_machine_interface_properties()
        if vmi_props is None:
            return
        if_mirror = vmi_props.get_interface_mirror()
        if if_mirror is None:
            return
        mirror_to = if_mirror.get_mirror_to()
        if mirror_to is None:
            return
        if mirror_to.__dict__ == self.interface_mirror.mirror_to.__dict__:
            return
        vmi_props.set_interface_mirror(self.interface_mirror)
        try:
            _vnc_lib.virtual_machine_interface_update(if_obj)
        except NoIdError:
            _sandesh._logger.debug("NoIdError while updating interface " +
                                   self.name)
    # end process_analyzer

    def rebake(self):
        network_set = set()
        if self.virtual_network in VirtualNetworkST:
            network_set.add(self.virtual_network)
        # if this interface is left or right interface of a service instance,
        # then get all policies that refer to that service instance and
        # return all networks that refer to those policies
        if self.service_interface_type not in ['left', 'right']:
            return network_set
        try:
            vmi_obj = _vnc_lib.virtual_machine_interface_read(
                fq_name_str=self.name)
        except NoIdError:
            _sandesh._logger.debug("NoIdError while reading interface %s",
                                   self.name)
            return network_set
        vm_id = get_vm_id_from_interface(vmi_obj)
        if vm_id is None:
            return network_set
        try:
            vm_obj = _vnc_lib.virtual_machine_read(id=vm_id)
        except NoIdError:
            _sandesh._logger.debug("NoIdError while reading virtual machine " +
                                   vm_id)
            return network_set
        vm_si_refs = vm_obj.get_service_instance_refs()
        if not vm_si_refs:
            return network_set
        service_instance = vm_obj.get_service_instance_refs()[0]
        try:
            si_obj = _vnc_lib.service_instance_read(id=service_instance['uuid'])
        except NoIdError:
            _sandesh._logger.debug("NoIdError while reading service instance "
                                   + service_instance['uuid'])
            return network_set
        si_name = si_obj.get_fq_name_str()
        for policy in NetworkPolicyST.values():
            for prule in policy.rules:
                if prule.action_list is not None:
                    if si_name in prule.action_list.apply_service:
                        network_set |= policy.networks_back_ref
                    elif (prule.action_list.mirror_to and
                          si_name == prule.action_list.mirror_to.analyzer_name):
                        (vn, _) = VirtualNetworkST.get_analyzer_vn_and_ip(
                            si_name)
                        if vn:
                            policy.analyzer_vn_set.add(vn)
                        network_set |= policy.networks_back_ref
            # end for prule
        # end for policy
        return network_set
    # end rebake

    def recreate_vrf_assign_table(self):
        if self.service_interface_type not in ['left', 'right']:
            return
        vn = VirtualNetworkST.get(self.virtual_network)
        if vn is None:
            return

        vrf_table = VrfAssignTableType()
        for ip in self.instance_ip_set:
            try:
                ip_obj = _vnc_lib.instance_ip_read(fq_name_str=ip)
            except NoIdError as e:
                _sandesh._logger.debug(
                    "NoIdError while reading ip address for interface %s: %s",
                    self.name, str(e))
                continue

            address = AddressType(subnet=SubnetType(
                ip_obj.get_instance_ip_address(), 32))
            mc = MatchConditionType(src_address=address)

            ri_name = vn.obj.get_fq_name_str() + ':' + vn._default_ri_name
            vrf_rule = VrfAssignRuleType(match_condition=mc,
                                         routing_instance=ri_name,
                                         ignore_acl=False)
            vrf_table.add_vrf_assign_rule(vrf_rule)

        try:
            vmi_obj = _vnc_lib.virtual_machine_interface_read(
                fq_name_str=self.name)
        except NoIdError as e:
            _sandesh._logger.debug(
                "NoIdError while reading virtual machine interface %s: %s",
                self.name, str(e))
            return

        vm_id = get_vm_id_from_interface(vmi_obj)
        if vm_id is None:
            _sandesh._logger.debug("vm id is None for interface %s", self.name)
            return
        try:
            vm_obj = _vnc_lib.virtual_machine_read(id=vm_id)
        except NoIdError as e:
            _sandesh._logger.debug(
                "NoIdError while reading virtual machine %s: %s",
                vm_id, str(e))
            return
        vm_si_refs = vm_obj.get_service_instance_refs()
        if not vm_si_refs:
            return
        try:
            si_obj = _vnc_lib.service_instance_read(id=vm_si_refs[0]['uuid'])
        except NoIdError:
            _sandesh._logger.debug("NoIdError while reading service instance "
                                   + vm_si_refs[0]['uuid'])
            return

        st_refs = si_obj.get_service_template_refs()
        if st_refs is None:
            return

        try:
            st_obj = _vnc_lib.service_template_read(id=st_refs[0]['uuid'])
        except NoIdError:
            _sandesh._logger.debug("NoIdError while reading service instance "
                                   + st_refs[0]['uuid'])
            return

        smode = st_obj.get_service_template_properties().get_service_mode()
        if smode not in ['in-network', 'in-network-nat']:
            return

        policy_rule_count = 0
        si_name = si_obj.get_fq_name_str()
        for policy_name in vn.policies:
            policy = NetworkPolicyST.get(policy_name)
            if policy is None:
                continue
            policy_rule_entries = policy.obj.get_network_policy_entries()
            if policy_rule_entries is None:
                continue
            for prule in policy_rule_entries.policy_rule:
                if si_name not in prule.action_list.apply_service or []:
                    continue
                proto = VirtualNetworkST.protocol_policy_to_acl(prule.protocol)
                if proto is None:
                    continue
                for saddr in prule.src_addresses:
                    svn = saddr.virtual_network
                    if svn == "local":
                        svn = self.virtual_network
                    for daddr in prule.dst_addresses:
                        dvn = daddr.virtual_network
                        if dvn == "local":
                            dvn = self.virtual_network
                        service_chain = ServiceChain.find(svn, dvn,
                                                          prule.direction,
                                                          prule.src_ports,
                                                          prule.dst_ports,
                                                          proto)
                        if service_chain is None or not service_chain.created:
                            continue
                        ri_name = vn.obj.get_fq_name_str() + ':' + \
                            vn.get_service_name(service_chain.name, si_name)
                        for sp in prule.src_ports:
                            for dp in prule.dst_ports:
                                mc = MatchConditionType(src_port=sp,
                                                        dst_port=dp,
                                                        protocol=proto)

                                vrf_rule = VrfAssignRuleType(
                                    match_condition=mc,
                                    routing_instance=ri_name,
                                    ignore_acl=True)
                                vrf_table.add_vrf_assign_rule(vrf_rule)
                                policy_rule_count += 1
            # end for prule
        # end for policy_name

        if policy_rule_count == 0:
            vrf_table = None
        if (jsonpickle.encode(vrf_table) !=
            jsonpickle.encode(vmi_obj.get_vrf_assign_table())):
                vmi_obj.set_vrf_assign_table(vrf_table)
                _vnc_lib.virtual_machine_interface_update(vmi_obj)

    # end recreate_vrf_assign_table
# end VirtualMachineInterfaceST


class LogicalRouterST(DictST):
    _dict = {}

    def __init__(self, name):
        self.name = name
        self.interfaces = set()
        self.virtual_networks = set()
        obj = _vnc_lib.logical_router_read(fq_name_str=name)
        rt_ref = obj.get_route_target_refs()
        old_rt_key = None
        if rt_ref:
            rt_key = rt_ref[0]['to'][0]
            rtgt_num = int(rt_key.split(':')[-1])
            if rtgt_num < common.BGP_RTGT_MIN_ID:
                old_rt_key = rt_key
                rt_ref = None
        if not rt_ref:
            rtgt_num = VirtualNetworkST._rt_allocator.alloc(name)
            rt_key = "target:%s:%d" % (
                VirtualNetworkST.get_autonomous_system(), rtgt_num)
            rtgt_obj = RouteTargetST.locate(rt_key)
            obj.set_route_target(rtgt_obj)
            _vnc_lib.logical_router_update(obj)

        if old_rt_key:
            _vnc_lib.route_target_delete(fq_name=[old_rt_key])
        self.route_target = rt_key
    # end __init__

    @classmethod
    def delete(cls, name):
        if name in cls._dict:
            lr = cls._dict[name]
            rtgt_num = int(lr.route_target.split(':')[-1])
            VirtualNetworkST._rt_allocator.delete(rtgt_num)
            _vnc_lib.route_target_delete(fq_name=[lr.route_target])
            for interface in lr.interfaces:
                lr.delete_interface(interface)
            del cls._dict[name]
    # end delete

    def add_interface(self, intf_name):
        self.interfaces.add(intf_name)
        vmi_obj = VirtualMachineInterfaceST.get(intf_name)
        if vmi_obj is not None and vmi_obj.virtual_network is not None:
            vn_set = self.virtual_networks | set([vmi_obj.virtual_network])
            self.set_virtual_networks(vn_set)
    # end add_interface

    def delete_interface(self, intf_name):
        self.interfaces.discard(intf_name)

        vn_set = set()
        for vmi in self.interfaces:
            vmi_obj = VirtualMachineInterfaceST.get(vmi)
            if vmi_obj is not None:
                vn_set.add(vmi_obj.virtual_network)
        self.set_virtual_networks(vn_set)
    # end delete_interface

    def set_virtual_networks(self, vn_set):
        for vn in self.virtual_networks - vn_set:
            vn_obj = VirtualNetworkST.get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                ri_obj.update_route_target_list(rt_add=set(),
                                                rt_del=[self.route_target])
        for vn in vn_set - self.virtual_networks:
            vn_obj = VirtualNetworkST.get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                ri_obj.update_route_target_list(rt_add=[self.route_target])
        self.virtual_networks = vn_set
    # end set_virtual_networks

    def update_autonomous_system(self, asn):
        old_rt = self.route_target
        rtgt_num = int(old_rt.split(':')[-1])
        rt_key = "target:%s:%d" % (asn, rtgt_num)
        rtgt_obj = RouteTargetST.locate(rt_key)
        try:
            obj = _vnc_lib.logical_router_read(fq_name_str=self.name)
            obj.set_route_target(rtgt_obj)
            _vnc_lib.logical_router_update(obj)
        except NoIdError:
            _sandesh._logger.debug(
                "NoIdError while accessing logical router %s" % self.name)
        for vn in self.virtual_networks:
            vn_obj = VirtualNetworkST.get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                ri_obj.update_route_target_list(rt_del=[old_rt],
                                                rt_add=[rt_key])
        _vnc_lib.route_target_delete(fq_name=[old_rt])
        self.route_target = rt_key
    # end update_autonomous_system

# end LogicaliRouterST


class SchemaTransformer(object):

    """
    data + methods used/referred to by ssrc and arc greenlets
    """

    _KEYSPACE = 'to_bgp_keyspace'
    _RT_CF = 'route_target_table'
    _SC_IP_CF = 'service_chain_ip_address_table'
    _SERVICE_CHAIN_CF = 'service_chain_table'
    _SERVICE_CHAIN_UUID_CF = 'service_chain_uuid_table'
    _zk_path_prefix = ''

    def __init__(self, args=None):
        global _sandesh
        self._args = args

        if args.cluster_id:
            self._zk_path_pfx = args.cluster_id + '/'
            SchemaTransformer._zk_path_prefix = self._zk_path_pfx
            self._keyspace = '%s_%s' %(args.cluster_id, SchemaTransformer._KEYSPACE)
        else:
            self._zk_path_pfx = ''
            self._keyspace = SchemaTransformer._KEYSPACE

        self._fabric_rt_inst_obj = None
        self._cassandra_init()

        # reset zookeeper config
        if self._args.reset_config:
            _zookeeper_client.delete_node(self._zk_path_pfx + "/id", True)

        ServiceChain.init()
        VirtualNetworkST._vn_id_allocator = IndexAllocator(_zookeeper_client,
            self._zk_path_pfx+_VN_ID_ALLOC_PATH,
            _VN_MAX_ID)
        SecurityGroupST._sg_id_allocator = IndexAllocator(
            _zookeeper_client, self._zk_path_pfx+_SECURITY_GROUP_ID_ALLOC_PATH,
            _SECURITY_GROUP_MAX_ID)
        # 0 is not a valid sg id any more. So, if it was previously allocated,
        # delete it and reserve it
        if SecurityGroupST._sg_id_allocator.read(0) != '__reserved__':
            SecurityGroupST._sg_id_allocator.delete(0)
        SecurityGroupST._sg_id_allocator.reserve(0, '__reserved__')
        VirtualNetworkST._rt_allocator = IndexAllocator(
            _zookeeper_client, self._zk_path_pfx+_BGP_RTGT_ALLOC_PATH, _BGP_RTGT_MAX_ID,
            common.BGP_RTGT_MIN_ID)

        # Initialize discovery client
        self._disc = None
        if self._args.disc_server_ip and self._args.disc_server_port:
            self._disc = client.DiscoveryClient(
                self._args.disc_server_ip,
                self._args.disc_server_port,
                ModuleNames[Module.SCHEMA_TRANSFORMER])

        _sandesh = Sandesh()
        sandesh.VnList.handle_request = self.sandesh_vn_handle_request
        sandesh.ServiceChainList.handle_request = \
            self.sandesh_sc_handle_request
        sandesh.RoutintInstanceList.handle_request = \
            self.sandesh_ri_handle_request
        sandesh.ServiceChainList.handle_request = \
            self.sandesh_sc_handle_request
        module = Module.SCHEMA_TRANSFORMER
        module_name = ModuleNames[module]
        node_type = Module2NodeType[module]
        node_type_name = NodeTypeNames[node_type]
        instance_id = INSTANCE_ID_DEFAULT
        hostname = socket.gethostname()
        _sandesh.init_generator(
            module_name, hostname, node_type_name, instance_id,
            self._args.collectors, 'to_bgp_context',
            int(args.http_server_port),
            ['cfgm_common', 'schema_transformer.sandesh'], self._disc)
        _sandesh.set_logging_params(enable_local_log=args.log_local,
                                    category=args.log_category,
                                    level=args.log_level,
                                    file=args.log_file,
                                    enable_syslog=args.use_syslog,
                                    syslog_facility=args.syslog_facility)
        ConnectionState.init(_sandesh, hostname, module_name,
                instance_id,
                staticmethod(ConnectionState.get_process_state_cb),
                NodeStatusUVE, NodeStatus)


        self.reinit()
        # create cpu_info object to send periodic updates
        sysinfo_req = False
        cpu_info = vnc_cpu_info.CpuInfo(
            module_name, instance_id, sysinfo_req, _sandesh, 60)
        self._cpu_info = cpu_info

    # end __init__

    # Clean up stale objects
    def reinit(self):
        ri_list = _vnc_lib.routing_instances_list()['routing-instances']
        for ri in ri_list:
            try:
                ri_obj = _vnc_lib.routing_instance_read(id=ri['uuid'])
            except NoIdError:
                continue
            try:
                _vnc_lib.virtual_network_read(id=ri_obj.parent_uuid)
            except NoIdError:
                try:
                    ri_obj = RoutingInstanceST(ri_obj)
                    ri_obj.delete()
                except NoIdError:
                    pass
        # end for ri

        acl_list = _vnc_lib.access_control_lists_list()['access-control-lists']
        for acl in acl_list or []:
            try:
                acl_obj = _vnc_lib.access_control_list_read(id=acl['uuid'])
            except NoIdError:
                continue
            try:
                if acl_obj.parent_type == 'virtual-network':
                    _vnc_lib.virtual_network_read(id=acl_obj.parent_uuid)
                elif acl_obj.parent_type == 'security-group':
                    _vnc_lib.security_group_read(id=acl_obj.parent_uuid)
            except NoIdError:
                try:
                    _vnc_lib.access_control_list_delete(id=acl.uuid)
                except NoIdError:
                    pass
        # end for acl
    # end reinit

    def cleanup(self):
        # TODO cleanup sandesh context
        pass
    # end cleanup

    def delete_virtual_network_network_policy(self, idents, meta):
        # Virtual network's reference to network policy is being deleted
        network_name = idents['virtual-network']
        policy_name = idents['network-policy']
        virtual_network = VirtualNetworkST.get(network_name)
        policy = NetworkPolicyST.get(policy_name)

        if policy:
            policy.networks_back_ref.discard(network_name)
            self.current_network_set |= policy.analyzer_vn_set
        if virtual_network:
            del virtual_network.policies[policy_name]
            self.current_network_set.add(network_name)
        for pol in NetworkPolicyST.values():
            if policy_name in pol.policies:
                self.current_network_set |= pol.networks_back_ref
    # end delete_virtual_network_network_policy

    def delete_project_virtual_network(self, idents, meta):
        network_name = idents['virtual-network']
        self.current_network_set |= VirtualNetworkST.delete(network_name)
        self.current_network_set.discard(network_name)
    # end delete_project_virtual_network

    def delete_project_network_policy(self, idents, meta):
        policy_name = idents['network-policy']
        NetworkPolicyST.delete(policy_name)
    # end delete_project_network_policy

    def delete_virtual_network_network_ipam(self, idents, meta):
        network_name = idents['virtual-network']
        ipam_name = idents['network-ipam']
        virtual_network = VirtualNetworkST.get(network_name)
        if virtual_network is None:
            return
        subnet = VnSubnetsType()
        subnet.build(meta)
        del virtual_network.ipams[ipam_name]
        virtual_network.update_ipams()
    # end delete_virtual_network_network_ipam

    def delete_project_security_group(self, idents, meta):
        sg_fq_name_str = idents['security-group']
        SecurityGroupST.delete(sg_fq_name_str)
    # end delete_project_security_group

    def add_autonomous_system(self, idents, meta):
        asn = meta.text
        VirtualNetworkST.update_autonomous_system(asn)
    # end add_autonomous_system

    def add_project_virtual_network(self, idents, meta):
        # New virtual network
        network_name = idents['virtual-network']
        VirtualNetworkST.locate(network_name)
    # end add_project_virtual_network

    def add_project_network_policy(self, idents, meta):
        policy_name = idents['network-policy']
        NetworkPolicyST.locate(policy_name)
    # end add_project_network_policy

    def add_project_security_group(self, idents, meta):
        sg_fq_name_str = idents['security-group']
        SecurityGroupST.locate(sg_fq_name_str)
    # end add_project_security_group

    def add_security_group_entries(self, idents, meta):
        # Network policy entries arrived or modified
        sg_name = idents['security-group']
        sg = SecurityGroupST.locate(sg_name)

        entries = PolicyEntriesType()
        try:
            entries.build(meta)
        except ValueError:
            # For compatibility, ignore if we can't build. In 1.0, we allowed
            # security group with direction set to '<', but it is not allowed
            # any more
            _sandesh._logger.debug("%s: Cannot read security group entries",
                                   si_name)
            return
        if sg:
            sg.update_policy_entries(entries)
    # end add_security_group_entries

    def delete_security_group_entries(self, idents, meta):
        sg_name = idents['security-group']
        sg = SecurityGroupST.get(sg_name)
        if sg:
            sg.update_policy_entries(None)
    # end delete_security_group_entries

    def add_network_policy_entries(self, idents, meta):
        # Network policy entries arrived or modified
        policy_name = idents['network-policy']
        policy = NetworkPolicyST.locate(policy_name)

        entries = PolicyEntriesType()
        entries.build(meta)
        policy.obj.set_network_policy_entries(entries)
        self.current_network_set |= policy.add_rules(entries)
    # end add_network_policy_entries

    def add_virtual_network_network_policy(self, idents, meta):
        # Virtual network is referring to new network policy
        network_name = idents['virtual-network']
        policy_name = idents['network-policy']
        virtual_network = VirtualNetworkST.locate(network_name)
        policy = NetworkPolicyST.locate(policy_name)

        policy.networks_back_ref.add(virtual_network.name)
        vnp = VirtualNetworkPolicyType()
        vnp.build(meta)
        virtual_network.add_policy(policy_name, vnp)
        self.current_network_set |= policy.networks_back_ref
        for pol in NetworkPolicyST.values():
            if policy_name in pol.policies:
                self.current_network_set |= pol.networks_back_ref
    # end add_virtual_network_network_policy

    def add_virtual_network_network_ipam(self, idents, meta):
        network_name = idents['virtual-network']
        ipam_name = idents['network-ipam']
        virtual_network = VirtualNetworkST.locate(network_name)
        if virtual_network is None:
            _sandesh._logger.debug("Cannot read virtual network %s",
                                   network_name)
            return
        subnet = VnSubnetsType()
        subnet.build(meta)
        virtual_network.ipams[ipam_name] = subnet
        virtual_network.update_ipams()
    # end add_virtual_network_network_ipam

    def add_virtual_machine_interface_virtual_network(self, idents, meta):
        vmi_name = idents['virtual-machine-interface']
        vn_name = idents['virtual-network']
        vmi = VirtualMachineInterfaceST.locate(vmi_name)
        if vmi is not None:
            vmi.set_virtual_network(vn_name)
            self.current_network_set |= vmi.rebake()
    # end add_virtual_machine_interface_virtual_network

    def add_instance_ip_virtual_machine_interface(self, idents, meta):
        vmi_name = idents['virtual-machine-interface']
        ip_name = idents['instance-ip']
        vmi = VirtualMachineInterfaceST.locate(vmi_name)
        if vmi is not None:
            vmi.add_instance_ip(ip_name)
            self.current_network_set |= vmi.rebake()
    # end add_instance_ip_virtual_machine_interface

    def delete_instance_ip_virtual_machine_interface(self, idents, meta):
        vmi_name = idents['virtual-machine-interface']
        ip_name = idents['instance-ip']
        vmi = VirtualMachineInterfaceST.get(vmi_name)
        if vmi is not None:
            vmi.delete_instance_ip(ip_name)
    # end delete_instance_ip_virtual_machine_interface

    def add_virtual_machine_interface_properties(self, idents, meta):
        vmi_name = idents['virtual-machine-interface']
        prop = VirtualMachineInterfacePropertiesType()
        vmi = VirtualMachineInterfaceST.locate(vmi_name)
        prop.build(meta)
        if vmi is not None:
            vmi.set_service_interface_type(prop.get_service_interface_type())
            self.current_network_set |= vmi.rebake()
            vmi.set_interface_mirror(prop.get_interface_mirror())
    # end add_virtual_machine_interface_properties

    def add_virtual_machine_service_instance(self, idents, meta):
        si_name = idents['service-instance']
        for sc in ServiceChain._dict.values():
            if si_name in sc.service_list:
                if VirtualNetworkST.get(sc.left_vn) is not None:
                    self.current_network_set.add(sc.left_vn)
                if VirtualNetworkST.get(sc.right_vn):
                    self.current_network_set.add(sc.right_vn)

    # end add_virtual_machine_service_instance(self, idents, meta):

    def add_route_target_list(self, idents, meta):
        vn_name = idents['virtual-network']
        rt_list = RouteTargetList()
        rt_list.build(meta)
        vn = VirtualNetworkST.locate(vn_name)
        vn.set_route_target_list(rt_list)
        self.current_network_set.add(vn_name)
    # end add_route_target_list

    def delete_route_target_list(self, idents, meta):
        vn_name = idents['virtual-network']
        vn = VirtualNetworkST.get(vn_name)
        if vn:
            rt_list = RouteTargetList()
            vn.set_route_target_list(rt_list)
            self.current_network_set.add(vn_name)
    # end delete_route_target_list

    def add_bgp_router_parameters(self, idents, meta):
        router_name = idents['bgp-router']
        router_params = BgpRouterParams()
        router_params.build(meta)

        router = BgpRouterST.locate(router_name)
        router.set_config(router_params)
    # end add_bgp_router_parameters

    def delete_bgp_router_parameters(self, idents, meta):
        router_name = idents['bgp-router']
        BgpRouterST.delete(router_name)
    # end delete_bgp_router_parameters

    def add_physical_router_bgp_router(self, idents, meta):
        prouter = PhysicalRouterST.locate(idents['physical-router'])
        prouter.set_bgp_router(idents['bgp-router'])
    # end add_physical_router_bgp_router

    def delete_physical_router_bgp_router(self, idents, meta):
        prouter = PhysicalRouterST.get(idents['physical-router'])
        if prouter:
            prouter.set_bgp_router(None)
    # end add_physical_router_bgp_router

    def add_physical_router_physical_interface(self, idents, meta):
        pr_name = idents['physical-router']
        pi_name = idents['physical-interface']
        prouter = PhysicalRouterST.locate(pr_name)
        pi = PhysicalInterfaceST.locate(pi_name)
        pi.physical_router = pr_name
        prouter.add_physical_interface_config(pi_name)
    # end add_physical_router_physical_interface

    def delete_physical_router_physical_interface(self, idents, meta):
        pr_name = idents['physical-router']
        pi_name = idents['physical-interface']
        prouter = PhysicalRouterST.get(pr_name)
        if prouter:
            prouter.delete_physical_interface_config(pi_name)
        pi = PhysicalInterfaceST.get(pi_name)
        if pi:
            pi.physical_router = None
    # end delete_physical_router_physical_interface

    def add_physical_interface_logical_interface(self, idents, meta):
        pi_name = idents['physical-interface']
        li_name = idents['logical-interface']
        pi = PhysicalInterfaceST.locate(pi_name)
        li = LogicalInterfaceST.locate(li_name)
        pi.add_logical_interface_config(li_name)
        li.physical_interface = pi_name
    # end add_physical_interface_logical_interface

    def delete_physical_interface_logical_interface(self, idents, meta):
        pi_name = idents['physical-interface']
        li_name = idents['logical-interface']
        pi = PhysicalInterfaceST.get(pi_name)
        if pi:
            pi.delete_logical_interface_config(li_name)
        li = LogicalInterfaceST.get(li_name)
        if li:
            li.physical_interface = None
    # end delete_physical_interface_logical_interface

    def add_logical_interface_virtual_network(self, idents, meta):
        li = LogicalInterfaceST.locate(idents['logical-interface'])
        li.set_virtual_network(idents['virtual-network'])
    # end add_logical_interface_virtual_network

    def delete_logical_interface_virtual_network(self, idents, meta):
        li = LogicalInterfaceST.locate(idents['logical-interface'])
        li.set_virtual_network(None)
    # end add_logical_interface_virtual_network

    def add_bgp_peering(self, idents, meta):
        routers = idents['bgp-router']
        BgpRouterST.locate(routers[0]).add_peer(routers[1])
        BgpRouterST.locate(routers[1]).add_peer(routers[0])
    # end add_bgp_peering

    def delete_bgp_peering(self, idents, meta):
        routers = idents['bgp-router']
        r0 = BgpRouterST.get(routers[0])
        r1 = BgpRouterST.get(routers[0])
        if r0:
            r0.delete_peer(routers[1])
        if r1:
            r1.delete_peer(routers[0])
    # end delete_bgp_peering

    def add_service_instance_properties(self, idents, meta):
        si_name = idents['service-instance']
        si_props = ServiceInstanceType()
        si_props.build(meta)
        if not si_props.auto_policy:
            self.delete_service_instance_properties(idents, meta)
            return
        try:
            si = _vnc_lib.service_instance_read(fq_name_str=si_name)
        except NoIdError:
            _sandesh._logger.debug("NoIdError while reading service "
                                   "instance %s", si_name)
            return
        si_props = si.get_service_instance_properties()
        left_vn_str = svc_info.get_left_vn(si.get_parent_fq_name_str(),
                                           si_props.left_virtual_network)
        right_vn_str = svc_info.get_right_vn(si.get_parent_fq_name_str(),
                                             si_props.right_virtual_network)
        if (not left_vn_str or not right_vn_str):
            _sandesh._logger.debug(
                "%s: route table next hop service instance must "
                "have left and right virtual networks", si_name)
            self.delete_service_instance_properties(idents, meta)
            return

        policy_name = "_internal_" + si_name
        policy = NetworkPolicyST.locate(policy_name)
        addr1 = AddressType(virtual_network=left_vn_str)
        addr2 = AddressType(virtual_network=right_vn_str)
        action_list = ActionListType(apply_service=[si_name])

        prule = PolicyRuleType(direction="<>", protocol="any",
                               src_addresses=[addr1], dst_addresses=[addr2],
                               src_ports=[PortType(0, -1)],
                               dst_ports=[PortType(0, -1)],
                               action_list=action_list)
        pentry = PolicyEntriesType([prule])
        policy.obj = NetworkPolicy(policy_name, network_policy_entries=pentry)
        policy.network_back_ref = set([left_vn_str, right_vn_str])
        policy.internal = True
        policy.add_rules(pentry)
        vn1 = VirtualNetworkST.get(left_vn_str)
        if vn1:
            vn1.add_policy(policy_name)
            self.current_network_set.add(left_vn_str)
        vn2 = VirtualNetworkST.get(right_vn_str)
        if vn2:
            vn2.add_policy(policy_name)
            self.current_network_set.add(right_vn_str)
    # end add_service_instance_properties

    def delete_service_instance_properties(self, idents, meta):
        si_name = idents['service-instance']
        policy_name = '_internal_' + si_name
        policy = NetworkPolicyST.get(policy_name)
        if policy is None:
            return
        for vn_name in policy.network_back_ref:
            vn = VirtualNetworkST.get(vn_name)
            if vn is None:
                continue
            del vn.policies[policy_name]
            self.current_network_set.add(vn_name)
        # end for vn_name
        NetworkPolicyST.delete(policy_name)
    # end delete_service_instance_properties

    def add_virtual_network_properties(self, idents, meta):
        network_name = idents['virtual-network']
        properties = VirtualNetworkType()
        properties.build(meta)
        network = VirtualNetworkST.locate(network_name)
        if network:
            network.set_properties(properties)
    # end add_virtual_network_properties

    def delete_virtual_network_properties(self, idents, meta):
        network_name = idents['virtual-network']
        network = VirtualNetworkST.get(network_name)
        if network:
            network.set_properties(None)
    # end delete_virtual_network_properties

    def add_virtual_network_route_table(self, idents, meta):
        network_name = idents['virtual-network']
        route_table_name = idents['route-table']
        network = VirtualNetworkST.locate(network_name)
        route_table = RouteTableST.locate(route_table_name)
        if route_table:
            route_table.network_back_refs.add(network_name)
        if network:
            network.route_table_refs.add(route_table_name)
            self.current_network_set.add(network_name)
    # end add_virtual_network_route_table

    def delete_virtual_network_route_table(self, idents, meta):
        network_name = idents['virtual-network']
        route_table_name = idents['route-table']
        network = VirtualNetworkST.get(network_name)
        route_table = RouteTableST.get(route_table_name)
        if route_table:
            route_table.network_back_refs.discard(network_name)
        if network:
            network.route_table_refs.discard(route_table_name)
            self.current_network_set.add(network_name)
    # end delete_virtual_network_route_table

    def add_routes(self, idents, meta):
        route_table_name = idents['route-table']
        route_table = RouteTableST.locate(route_table_name)
        if route_table:
            routes = RouteTableType()
            routes.build(meta)
            route_table.routes = routes.get_route() or []
            self.current_network_set |= route_table.network_back_refs
    # end add_routes

    def delete_routes(self, idents, meta):
        route_table_name = idents['route-table']
        route_table = RouteTableST.get(route_table_name)
        if route_table:
            route_table.routes = []
            self.current_network_set |= route_table.network_back_refs
    # end delete_routes

    def add_logical_router_interface(self, idents, meta):
        lr_name = idents['logical-router']
        vmi_name = idents['virtual-machine-interface']

        lr_obj = LogicalRouterST.locate(lr_name)
        if lr_obj is not None:
            lr_obj.add_interface(vmi_name)
    # end add_logical_router_interface

    def delete_logical_router_interface(self, idents, meta):
        lr_name = idents['logical-router']
        vmi_name = idents['virtual-machine-interface']

        lr_obj = LogicalRouterST.get(lr_name)
        if lr_obj is not None:
            lr_obj.delete_interface(vmi_name)

        VirtualMachineInterfaceST.delete(vmi_name)
    # end delete_logical_router_interface

    def delete_project_logical_router(self, idents, meta):
        lr_name = idents['logical-router']
        LogicalRouterST.delete(lr_name)
    # end delete_project_logical_router

    def process_poll_result(self, poll_result_str):
        result_list = parse_poll_result(poll_result_str)
        self.current_network_set = set()

        # first pass thru the ifmap message and build data model
        for (result_type, idents, metas) in result_list:
            for meta in metas:
                meta_name = re.sub('{.*}', '', meta.tag)
                if result_type == 'deleteResult':
                    funcname = "delete_" + meta_name.replace('-', '_')
                elif result_type in ['searchResult', 'updateResult']:
                    funcname = "add_" + meta_name.replace('-', '_')
                # end if result_type
                try:
                    func = getattr(self, funcname)
                except AttributeError:
                    pass
                else:
                    _sandesh._logger.debug("%s %s/%s/%s. Calling '%s'.",
                                           result_type.split('Result')[0].title(),
                                           meta_name, idents, meta, funcname)
                    func(idents, meta)
            # end for meta
        # end for result_type

        # Second pass to construct ACL entries and connectivity table
        for network_name in self.current_network_set:
            virtual_network = VirtualNetworkST.get(network_name)
            old_virtual_network_connections =\
                virtual_network.expand_connections()
            old_service_chains = virtual_network.service_chains
            virtual_network.connections = set()
            virtual_network.service_chains = {}

            static_acl_entries = None
            dynamic_acl_entries = None
            for policy_name in virtual_network.policies:
                timer = virtual_network.policies[policy_name].get_timer()
                if timer is None:
                    if static_acl_entries is None:
                        static_acl_entries = AclEntriesType(dynamic="false")
                    acl_entries = static_acl_entries
                    dynamic = False
                else:
                    if dynamic_acl_entries is None:
                        dynamic_acl_entries = AclEntriesType(dynamic="true")
                    acl_entries = dynamic_acl_entries
                    dynamic = True
                policy = NetworkPolicyST.get(policy_name)
                if policy is None:
                    continue
                for prule in policy.rules:
                    acl_rule_list = virtual_network.policy_to_acl_rule(
                        prule, dynamic)
                    acl_rule_list.update_acl_entries(acl_entries)
                    for arule in acl_rule_list.get_list():
                        match = arule.get_match_condition()
                        action = arule.get_action_list()
                        if action.simple_action == 'deny':
                            continue
                        if (match.dst_address.virtual_network in
                                [network_name, "any"]):
                            connected_network =\
                                match.src_address.virtual_network
                        elif (match.src_address.virtual_network in
                              [network_name, "any"]):
                            connected_network =\
                                match.dst_address.virtual_network
                        if (len(action.apply_service) != 0):
                            # if a service was applied, the ACL should have a
                            # pass action, and we should not make a connection
                            # between the routing instances
                            action.simple_action = "pass"
                            action.apply_service = []
                            continue

                        if action.simple_action:
                            virtual_network.add_connection(connected_network)

                    # end for acl_rule_list
                # end for policy_rule_entries.policy_rule
            # end for virtual_network.policies

            if static_acl_entries is not None:
                # if a static acl is created, then for each rule, we need to
                # add a deny rule and add a my-vn to any allow rule in the end
                acl_list = AclRuleListST()

                # Create my-vn to my-vn allow
                match = MatchConditionType(
                    "any", AddressType(virtual_network=network_name),
                    PortType(-1, -1),
                    AddressType(virtual_network=network_name),
                    PortType(-1, -1))
                action = ActionListType("pass")
                acl = AclRuleType(match, action)
                acl_list.append(acl)

                for rule in static_acl_entries.get_acl_rule():
                    match = MatchConditionType(
                        "any", rule.match_condition.src_address,
                        PortType(-1, -1), rule.match_condition.dst_address,
                        PortType(-1, -1))

                    acl = AclRuleType(match, ActionListType("deny"),
                                      rule.get_rule_uuid())
                    acl_list.append(acl)

                    match = MatchConditionType(
                        "any", rule.match_condition.dst_address,
                        PortType(-1, -1), rule.match_condition.src_address,
                        PortType(-1, -1))

                    acl = AclRuleType(match, ActionListType("deny"),
                                      rule.get_rule_uuid())
                    acl_list.append(acl)
                # end for rule

                # Create any-vn to any-vn allow
                match = MatchConditionType("any",
                                           AddressType(virtual_network="any"),
                                           PortType(-1, -1),
                                           AddressType(virtual_network="any"),
                                           PortType(-1, -1))
                action = ActionListType("pass")
                acl = AclRuleType(match, action)
                acl_list.append(acl)
                acl_list.update_acl_entries(static_acl_entries)

            virtual_network.acl = _access_control_list_update(
                virtual_network.acl, virtual_network.obj.name,
                virtual_network.obj, static_acl_entries)
            virtual_network.dynamic_acl = _access_control_list_update(
                virtual_network.dynamic_acl, 'dynamic', virtual_network.obj,
                dynamic_acl_entries)

            for vmi in VirtualMachineInterfaceST.values():
                if (vmi.virtual_network == network_name and
                    vmi.interface_mirror is not None and
                    vmi.interface_mirror.mirror_to is not None and
                    vmi.interface_mirror.mirror_to.analyzer_name is not None):
                        vmi.process_analyzer()

            # This VN could be the VN for an analyzer interface. If so, we need
            # to create a link from all VNs containing a policy with that
            # analyzer
            for policy in NetworkPolicyST.values():
                policy_rule_entries = policy.obj.get_network_policy_entries()
                if policy_rule_entries is None:
                    continue
                for prule in policy_rule_entries.policy_rule:
                    if prule.action_list is None:
                        continue
                    if prule.action_list.mirror_to is None:
                        continue
                    (vn_analyzer, _) = VirtualNetworkST.get_analyzer_vn_and_ip(
                        prule.action_list.mirror_to.analyzer_name)
                    if vn_analyzer != network_name:
                        continue
                    for net_name in policy.networks_back_ref:
                        net = VirtualNetworkST.get(net_name)
                        if net is not None:
                            virtual_network.add_connection(net_name)

            # Derive connectivity changes between VNs
            new_connections = virtual_network.expand_connections()
            for network in old_virtual_network_connections - new_connections:
                virtual_network.delete_ri_connection(network)
            for network in new_connections - old_virtual_network_connections:
                virtual_network.add_ri_connection(network)

            # copy the routing instance connections from old to new
            for network in old_virtual_network_connections & new_connections:
                try:
                    ri1 = virtual_network.get_primary_routing_instance()
                    vn2 = VirtualNetworkST.get(network)
                    ri2 = vn2.get_primary_routing_instance()
                    ri1.connections.add(ri2.get_fq_name_str())
                except Exception:
                    pass

            # First create the newly added service chains
            for remote_vn_name in virtual_network.service_chains:
                remote_vn = VirtualNetworkST.get(remote_vn_name)
                if remote_vn is None:
                    continue
                remote_service_chain_list = remote_vn.service_chains.get(
                    network_name)
                service_chain_list = virtual_network.service_chains[
                    remote_vn_name]
                for service_chain in service_chain_list or []:
                    if service_chain in (remote_service_chain_list or []):
                        service_chain.create()
                # end for service_chain
            # end for remote_vn_name

            # Delete the service chains that are no longer active
            for remote_vn_name in old_service_chains:
                remote_vn = VirtualNetworkST.get(remote_vn_name)
                if remote_vn is None:
                    continue
                remote_service_chain_list = remote_vn.service_chains.get(
                    network_name)
                service_chain_list = old_service_chains[remote_vn_name]
                for service_chain in service_chain_list or []:
                    if service_chain in (
                        virtual_network.service_chains.get(remote_vn_name)
                            or []):
                        continue
                    if service_chain in (remote_service_chain_list or []):
                        service_chain.destroy()
                    else:
                        service_chain.delete()
            # for remote_vn_name

            virtual_network.update_route_table()
        # end for self.current_network_set
        for network_name in self.current_network_set:
            virtual_network = VirtualNetworkST.get(network_name)
            virtual_network.uve_send()
        # end for self.current_network_set

        for vmi in VirtualMachineInterfaceST.values():
            vmi.recreate_vrf_assign_table()
    # end process_poll_result

    def _log_exceptions(self, func):
        def wrapper(*args, **kwargs):
            try:
                return func(*args, **kwargs)
            except AllServersUnavailable:
                ConnectionState.update(conn_type = ConnectionType.DATABASE,
                    name = 'Cassandra', status = ConnectionStatus.DOWN,
                    message = '', server_addrs = self._args.cassandra_server_list)
                raise
        return wrapper
    # end _log_exceptions

    def _cassandra_init(self):
        result_dict = {}

        server_idx = 0
        num_dbnodes = len(self._args.cassandra_server_list)
        connected = False
        # Update connection info
        ConnectionState.update(conn_type = ConnectionType.DATABASE,
            name = 'Database', status = ConnectionStatus.INIT,
            message = '', server_addrs = self._args.cassandra_server_list)

        while not connected:
            try:
                cass_server = self._args.cassandra_server_list[server_idx]
                sys_mgr = SystemManager(cass_server)
                connected = True
            except Exception as e:
                # Update connection info
                ConnectionState.update(conn_type = ConnectionType.DATABASE,
                    name = 'Database', status = ConnectionStatus.DOWN,
                    message = '', server_addrs = [cass_server])
                server_idx = (server_idx + 1) % num_dbnodes
                time.sleep(3)

       # Update connection info
        ConnectionState.update(conn_type = ConnectionType.DATABASE,
            name = 'Database', status = ConnectionStatus.UP,
            message = '', server_addrs = self._args.cassandra_server_list)

        if self._args.reset_config:
            try:
                sys_mgr.drop_keyspace(self._keyspace)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                print "Warning! " + str(e)

        try:
            sys_mgr.create_keyspace(
                self._keyspace, SIMPLE_STRATEGY,
                {'replication_factor': str(num_dbnodes)})
        except pycassa.cassandra.ttypes.InvalidRequestException as e:
            # TODO verify only EEXISTS
            print "Warning! " + str(e)

        column_families = [self._RT_CF, self._SC_IP_CF, self._SERVICE_CHAIN_CF,
                           self._SERVICE_CHAIN_UUID_CF]
        conn_pool = pycassa.ConnectionPool(
            self._keyspace,
            server_list=self._args.cassandra_server_list,
            max_overflow=10,
            use_threadlocal=True,
            prefill=True,
            pool_size=10,
            pool_timeout=30,
            max_retries=-1,
            timeout=0.5)

        rd_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
        wr_consistency = pycassa.cassandra.ttypes.ConsistencyLevel.QUORUM
        for cf in column_families:
            try:
                sys_mgr.create_column_family(self._keyspace, cf)
            except pycassa.cassandra.ttypes.InvalidRequestException as e:
                # TODO verify only EEXISTS
                print "Warning! " + str(e)
            result_dict[cf] = pycassa.ColumnFamily(
                conn_pool, cf,
                read_consistency_level=rd_consistency,
                write_consistency_level=wr_consistency)

        VirtualNetworkST._rt_cf = result_dict[self._RT_CF]
        VirtualNetworkST._sc_ip_cf = result_dict[self._SC_IP_CF]
        VirtualNetworkST._service_chain_cf = result_dict[
            self._SERVICE_CHAIN_CF]
        ServiceChain._service_chain_uuid_cf = result_dict[
            self._SERVICE_CHAIN_UUID_CF]

        pycassa.ColumnFamily.get = self._log_exceptions(pycassa.ColumnFamily.get)
        pycassa.ColumnFamily.xget = self._log_exceptions(pycassa.ColumnFamily.xget)
        pycassa.ColumnFamily.get_range = self._log_exceptions(pycassa.ColumnFamily.get_range)
        pycassa.ColumnFamily.insert = self._log_exceptions(pycassa.ColumnFamily.insert)
        pycassa.ColumnFamily.remove = self._log_exceptions(pycassa.ColumnFamily.remove)
        pycassa.batch.Mutator.send = self._log_exceptions(pycassa.batch.Mutator.send)

    # end _cassandra_init

    def sandesh_ri_build(self, vn_name, ri_name):
        vn = VirtualNetworkST.get(vn_name)
        sandesh_ri_list = []
        for ri in vn.rinst.values():
            sandesh_ri = sandesh.RoutingInstance(name=ri.obj.get_fq_name_str())
            sandesh_ri.service_chain = ri.service_chain
            sandesh_ri.connections = list(ri.connections)
            sandesh_ri_list.append(sandesh_ri)
        return sandesh_ri_list
    # end sandesh_ri_build

    def sandesh_ri_handle_request(self, req):
        # Return the list of VNs
        ri_resp = sandesh.RoutingInstanceListResp(routing_instances=[])
        if req.vn_name is None:
            for vn in VirtualNetworkST:
                sandesh_ri = self.sandesh_ri_build(vn, req.ri_name)
                ri_resp.routing_instances.extend(sandesh_ri)
        elif req.vn_name in VirtualNetworkST:
            self.sandesh_ri_build(req.vn_name, req.ri_name)
            ri_resp.routing_instances.extend(sandesh_ri)
        ri_resp.response(req.context())
    # end sandesh_ri_handle_request

    def sandesh_vn_build(self, vn_name):
        vn = VirtualNetworkST.get(vn_name)
        sandesh_vn = sandesh.VirtualNetwork(name=vn_name)
        sandesh_vn.policies = vn.policies.keys()
        sandesh_vn.connections = list(vn.connections)
        sandesh_vn.routing_instances = vn.rinst.keys()
        if vn.acl:
            sandesh_vn.acl = vn.acl.get_fq_name_str()
        if vn.dynamic_acl:
            sandesh_vn.dynamic_acl = vn.dynamic_acl.get_fq_name_str()

        return sandesh_vn
    # end sandesh_vn_build

    def sandesh_vn_handle_request(self, req):
        # Return the list of VNs
        vn_resp = sandesh.VnListResp(vn_names=[])
        if req.vn_name is None:
            for vn in VirtualNetworkST:
                sandesh_vn = self.sandesh_vn_build(vn)
                vn_resp.vn_names.append(sandesh_vn)
        elif req.vn_name in VirtualNetworkST:
            sandesh_vn = self.sandesh_vn_build(req.vn_name)
            vn_resp.vn_names.append(sandesh_vn)
        vn_resp.response(req.context())
    # end sandesh_vn_handle_request

    def sandesh_sc_handle_request(self, req):
        sc_resp = sandesh.ServiceChainListResp(service_chains=[])
        if req.sc_name is None:
            for sc in ServiceChain.values():
                sandesh_sc = sc.build_introspect()
                sc_resp.service_chains.append(sandesh_sc)
        elif req.sc_name in ServiceChain:
            sandesh_sc = ServiceChain[req.sc_name].build_introspect()
            sc_resp.service_chains.append(sandesh_sc)
        sc_resp.response(req.context())
    # end sandesh_sc_handle_request
# end class SchemaTransformer


def launch_arc(transformer, ssrc_mapc):
    arc_mapc = arc_initialize(transformer._args, ssrc_mapc)
    while True:
        # If not connected to zookeeper Pause the operations
        if not _zookeeper_client.is_connected():
            time.sleep(1)
            continue
        pollreq = PollRequest(arc_mapc.get_session_id())
        result = arc_mapc.call('poll', pollreq)
        try:
            transformer.process_poll_result(result)
        except Exception as e:
            string_buf = StringIO()
            cgitb.Hook(
                file=string_buf,
                format="text",
                ).handle(sys.exc_info())
            try:
                with open('/var/log/contrail/schema.err', 'a') as err_file:
                    err_file.write(string_buf.getvalue())
            except IOError:
                with open('./schema.err', 'a') as err_file:
                    err_file.write(string_buf.getvalue())
            raise e
# end launch_arc


def launch_ssrc(transformer):
    ssrc_mapc = ssrc_initialize(transformer._args)
    arc_glet = gevent.spawn(launch_arc, transformer, ssrc_mapc)
    arc_glet.join()
# end launch_ssrc


def parse_args(args_str):
    '''
    Eg. python to_bgp.py --ifmap_server_ip 192.168.1.17
                         --ifmap_server_port 8443
                         --ifmap_username test
                         --ifmap_password test
                         --cassandra_server_list 10.1.2.3:9160
                         --api_server_ip 10.1.2.3
                         --api_server_port 8082
                         --zk_server_ip 10.1.2.3
                         --zk_server_port 2181
                         --collectors 127.0.0.1:8086
                         --disc_server_ip 127.0.0.1
                         --disc_server_port 5998
                         --http_server_port 8090
                         --log_local
                         --log_level SYS_DEBUG
                         --log_category test
                         --log_file <stdout>
                         --use_syslog
                         --syslog_facility LOG_USER
                         --cluster_id <testbed-name>
                         [--reset_config]
    '''

    # Source any specified config/ini file
    # Turn off help, so we      all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file",
                             help="Specify config file", metavar="FILE")
    args, remaining_argv = conf_parser.parse_known_args(args_str.split())

    defaults = {
        'ifmap_server_ip': '127.0.0.1',
        'ifmap_server_port': '8443',
        'ifmap_username': 'schema-transformer',
        'ifmap_password': 'schema-transformer',
        'cassandra_server_list': '127.0.0.1:9160',
        'api_server_ip': '127.0.0.1',
        'api_server_port': '8082',
        'zk_server_ip': '127.0.0.1',
        'zk_server_port': '2181',
        'collectors': None,
        'disc_server_ip': None,
        'disc_server_port': None,
        'http_server_port': '8087',
        'log_local': False,
        'log_level': SandeshLevel.SYS_DEBUG,
        'log_category': '',
        'log_file': Sandesh._DEFAULT_LOG_FILE,
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'cluster_id': '',
    }
    secopts = {
        'use_certs': False,
        'keyfile': '',
        'certfile': '',
        'ca_certs': '',
        'ifmap_certauth_port': "8444",
    }
    ksopts = {
        'admin_user': 'user1',
        'admin_password': 'password1',
        'admin_tenant_name': 'default-domain'
    }

    if args.conf_file:
        config = ConfigParser.SafeConfigParser()
        config.read([args.conf_file])
        defaults.update(dict(config.items("DEFAULTS")))
        if ('SECURITY' in config.sections() and
                'use_certs' in config.options('SECURITY')):
            if config.getboolean('SECURITY', 'use_certs'):
                secopts.update(dict(config.items("SECURITY")))
        if 'KEYSTONE' in config.sections():
            ksopts.update(dict(config.items("KEYSTONE")))

    # Override with CLI options
    # Don't surpress add_help here so it will handle -h
    parser = argparse.ArgumentParser(
        # Inherit options from config_parser
        parents=[conf_parser],
        # print script description with -h/--help
        description=__doc__,
        # Don't mess with format of description
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    defaults.update(secopts)
    defaults.update(ksopts)
    parser.set_defaults(**defaults)

    parser.add_argument(
        "--ifmap_server_ip", help="IP address of ifmap server")
    parser.add_argument("--ifmap_server_port", help="Port of ifmap server")

    # TODO should be from certificate
    parser.add_argument("--ifmap_username",
                        help="Username known to ifmap server")
    parser.add_argument("--ifmap_password",
                        help="Password known to ifmap server")
    parser.add_argument(
        "--cassandra_server_list",
        help="List of cassandra servers in IP Address:Port format",
        nargs='+')
    parser.add_argument(
        "--reset_config", action="store_true",
        help="Warning! Destroy previous configuration and start clean")
    parser.add_argument("--api_server_ip",
                        help="IP address of API server")
    parser.add_argument("--api_server_port",
                        help="Port of API server")
    parser.add_argument("--zk_server_ip",
                        help="IP address:port of zookeeper server")
    parser.add_argument("--collectors",
                        help="List of VNC collectors in ip:port format",
                        nargs="+")
    parser.add_argument("--disc_server_ip",
                        help="IP address of the discovery server")
    parser.add_argument("--disc_server_port",
                        help="Port of the discovery server")
    parser.add_argument("--http_server_port",
                        help="Port of local HTTP server")
    parser.add_argument("--log_local", action="store_true",
                        help="Enable local logging of sandesh messages")
    parser.add_argument(
        "--log_level",
        help="Severity level for local logging of sandesh messages")
    parser.add_argument(
        "--log_category",
        help="Category filter for local logging of sandesh messages")
    parser.add_argument("--log_file",
                        help="Filename for the logs to be written to")
    parser.add_argument("--use_syslog", action="store_true",
                        help="Use syslog for logging")
    parser.add_argument("--syslog_facility",
                        help="Syslog facility to receive log lines")
    parser.add_argument("--admin_user",
                        help="Name of keystone admin user")
    parser.add_argument("--admin_password",
                        help="Password of keystone admin user")
    parser.add_argument("--admin_tenant_name",
                        help="Tenant name for keystone admin user")
    parser.add_argument("--cluster_id",
                        help="Used for database keyspace separation")
    args = parser.parse_args(remaining_argv)
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()

    return args
# end parse_args


def run_schema_transformer(args):
    global _vnc_lib

    # Retry till API server is up
    connected = False
    ConnectionState.update(conn_type = ConnectionType.APISERVER,
        name = 'ApiServer', status = ConnectionStatus.INIT,
        message = '', server_addrs = ['%s:%s' % (args.api_server_ip,
                                                 args.api_server_port)])
    while not connected:
        try:
            _vnc_lib = VncApi(
                args.admin_user, args.admin_password, args.admin_tenant_name,
                args.api_server_ip, args.api_server_port)
            connected = True
            ConnectionState.update(conn_type = ConnectionType.APISERVER,
                name = 'ApiServer', status = ConnectionStatus.UP,
                message = '', server_addrs = ['%s:%s    ' % (args.api_server_ip,
                                                         args.api_server_port)])
        except requests.exceptions.ConnectionError as e:
            # Update connection info
            ConnectionState.update(conn_type = ConnectionType.APISERVER,
                name = 'ApiServer', status = ConnectionStatus.DOWN,
                message = str(e),
                server_addrs = ['%s:%s' % (args.api_server_ip,
                                           args.api_server_port)])
            time.sleep(3)
        except ResourceExhaustionError:  # haproxy throws 503
            time.sleep(3)

    transformer = SchemaTransformer(args)
    ssrc_task = gevent.spawn(launch_ssrc, transformer)

    gevent.joinall([ssrc_task])
# end run_schema_transformer


def main(args_str=None):
    global _zookeeper_client
    if not args_str:
        args_str = ' '.join(sys.argv[1:])
    args = parse_args(args_str)
    if args.cluster_id:
        client_pfx = args.cluster_id + '-'
        zk_path_pfx = args.cluster_id + '/'
    else:
        client_pfx = ''
        zk_path_pfx = ''
    _zookeeper_client = ZookeeperClient(client_pfx+"schema", args.zk_server_ip)
    _zookeeper_client.master_election(zk_path_pfx+"/schema-transformer", os.getpid(),
                                      run_schema_transformer, args)
# end main


def server_main():
    cgitb.enable(format='text')
    main()
# end server_main

if __name__ == '__main__':
    server_main()
