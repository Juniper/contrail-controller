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
from cfgm_common.zkclient import ZookeeperClient
from gevent import monkey
monkey.patch_all()
import sys
reload(sys)
sys.setdefaultencoding('UTF8')
import requests
import ConfigParser
import cgitb

import copy
import argparse
import socket
import uuid

from lxml import etree
import re
import cfgm_common as common
from cfgm_common import vnc_cpu_info

from cfgm_common.exceptions import *
from cfgm_common.imid import *
from cfgm_common import svc_info
from cfgm_common.vnc_db import DBBase
from vnc_api.vnc_api import *

from pysandesh.sandesh_base import *
from pysandesh.sandesh_logger import *
from pysandesh.gen_py.sandesh.ttypes import SandeshLevel
from cfgm_common.uve.virtual_network.ttypes import *
from sandesh_common.vns.ttypes import Module
from sandesh_common.vns.constants import ModuleNames, Module2NodeType, \
    NodeTypeNames, INSTANCE_ID_DEFAULT, SCHEMA_KEYSPACE_NAME, \
    CASSANDRA_DEFAULT_GC_GRACE_SECONDS
from schema_transformer.sandesh.st_introspect import ttypes as sandesh
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
from cfgm_common.uve.cfgm_cpuinfo.ttypes import NodeStatusUVE, NodeStatus
from cStringIO import StringIO
from db import SchemaTransformerDB
from cfgm_common.utils import cgitb_hook

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

SGID_MIN_ALLOC = common.SGID_MIN_ALLOC
_sandesh = None

# connection to api-server
_vnc_lib = None
# zookeeper client connection
_zookeeper_client = None


def get_si_vns(si_obj, si_props):
    left_vn = None
    right_vn = None

    st_refs = si_obj.get_service_template_refs()
    uuid = st_refs[0]['uuid']
    st_obj = _vnc_lib.service_template_read(id=uuid)
    st_props = st_obj.get_service_template_properties()
    if st_props.get_ordered_interfaces():
        st_if_list = st_props.get_interface_type()
        si_if_list = si_props.get_interface_list()
        for idx in range(0, len(st_if_list)):
            st_if = st_if_list[idx]
            si_if = si_if_list[idx]
            if st_if.get_service_interface_type() == 'left':
                left_vn = si_if.get_virtual_network()
            elif st_if.get_service_interface_type() == 'right':
                right_vn = si_if.get_virtual_network()
    else:
        left_vn = si_props.get_left_virtual_network()
        right_vn = si_props.get_right_virtual_network()

    if left_vn == "":
        left_vn = parent_str + ':' + svc_info.get_left_vn_name()
    if right_vn == "":
        right_vn = parent_str + ':' + svc_info.get_right_vn_name()

    return left_vn, right_vn
# end get_si_vns

def _access_control_list_update(acl_obj, name, obj, entries):
    if acl_obj is None:
        if entries is None:
            return None
        acl_obj = AccessControlList(name, obj, entries)
        try:
            _vnc_lib.access_control_list_create(acl_obj)
            return acl_obj
        except (NoIdError, BadRequest) as e:
            _sandesh._logger.error(
                "Error while creating acl %s for %s: %s",
                name, obj.get_fq_name_str(), str(e))
        return None
    else:
        if entries is None:
            try:
                _vnc_lib.access_control_list_delete(id=acl_obj.uuid)
            except NoIdError:
                pass
            return None

        # if entries did not change, just return the object
        if acl_obj.get_access_control_list_entries() == entries:
            return acl_obj

        # Set new value of entries on the ACL
        acl_obj.set_access_control_list_entries(entries)
        try:
            _vnc_lib.access_control_list_update(acl_obj)
        except HttpError as he:
            _sandesh._logger.error(
                "HTTP error while updating acl %s for %s: %d, %s",
                name, obj.get_fq_name_str(), he.status_code, he.content)
        except NoIdError:
            _sandesh._logger.error("NoIdError while updating acl %s for %s",
                                   name, obj.get_fq_name_str())
    return acl_obj
# end _access_control_list_update

# a struct to store attributes related to Virtual Networks needed by
# schema transformer


class VirtualNetworkST(DBBase):
    _dict = {}
    _autonomous_system = 0

    def __init__(self, name, obj=None, acl_dict=None, ri_dict=None):
        self.obj = obj or _vnc_lib.virtual_network_read(fq_name_str=name)
        self.name = name
        self.policies = OrderedDict()
        self.connections = set()
        self.rinst = {}
        self.acl = None
        self.dynamic_acl = None
        # FIXME: due to a bug in contrail api, calling
        # get_access_control_lists() may result in another call to
        # virtual_network_read. Avoid it for now.
        for acl in getattr(self.obj, 'access_control_lists', None) or []:
            if acl_dict:
                acl_obj = acl_dict[acl['uuid']]
            else:
                acl_obj = _vnc_lib.access_control_list_read(id=acl['uuid'])
            if acl_obj.name == self.obj.name:
                self.acl = acl_obj
            elif acl_obj.name == 'dynamic':
                self.dynamic_acl = acl_obj

        self.ipams = {}
        rt_list = self.obj.get_route_target_list()
        if rt_list:
            self.rt_list = set(rt_list.get_route_target())
            for rt in self.rt_list:
                RouteTargetST.locate(rt)
        else:
            self.rt_list = set()
        self._route_target = 0
        self.route_table_refs = set()
        self.route_table = {}
        self.service_chains = {}
        prop = self.obj.get_virtual_network_properties(
        ) or VirtualNetworkType()
        self.allow_transit = prop.allow_transit
        nid = self.obj.get_virtual_network_network_id()
        if nid is None:
            nid = prop.network_id or self._cassandra.alloc_vn_id(name) + 1
            self.obj.set_virtual_network_network_id(nid)
            _vnc_lib.virtual_network_update(self.obj)
        if self.obj.get_fq_name() == common.IP_FABRIC_VN_FQ_NAME:
            self._default_ri_name = common.IP_FABRIC_RI_FQ_NAME[-1]
            self.locate_routing_instance_no_target(self._default_ri_name,
                                                   ri_dict)
        elif self.obj.get_fq_name() == common.LINK_LOCAL_VN_FQ_NAME:
            self._default_ri_name = common.LINK_LOCAL_RI_FQ_NAME[-1]
            self.locate_routing_instance_no_target(self._default_ri_name,
                                                   ri_dict)
        else:
            self._default_ri_name = self.obj.name
            ri_obj = self.locate_routing_instance(self._default_ri_name, None,
                                                  ri_dict)
            if ri_obj is not None:
                # if primary RI is connected to another primary RI, we need to
                # also create connection between the VNs
                for connection in ri_obj.connections:
                    remote_ri_fq_name = connection.split(':')
                    if remote_ri_fq_name[-1] == remote_ri_fq_name[-2]:
                        self.connections.add(':'.join(remote_ri_fq_name[0:-1] ))

        for ri in getattr(self.obj, 'routing_instances', None) or []:
            ri_name = ri['to'][-1]
            if ri_name not in self.rinst:
                sc_id = self._get_service_id_from_ri(ri_name)
                self.locate_routing_instance(ri_name, sc_id, ri_dict)
        for policy in NetworkPolicyST.values():
            if policy.internal and name in policy.network_back_ref:
                self.add_policy(policy.name)
        self.uve_send()
    # end __init__

    @staticmethod
    def _get_service_id_from_ri(ri_name):
        if not ri_name.startswith('service-'):
            return None
        sc_id = ri_name[8:44]
        return sc_id
    # end _get_service_id_from_ri

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
            nid = vn.obj.get_virtual_network_network_id()
            if nid is None:
                props = vn.obj.get_virtual_network_properties()
                if props:
                    nid = props.network_id
            if nid:
                cls._cassandra.free_vn_id(nid - 1)
            for policy in NetworkPolicyST.values():
                if name in policy.analyzer_vn_set:
                    analyzer_vn_set |= policy.networks_back_ref
                    policy.analyzer_vn_set.discard(name)

            vn.route_table_refs = set()
            vn.update_route_table()
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
            rtgt_num = cls._cassandra.get_route_target(ri_fq_name)
            old_rtgt_name = "target:%d:%d" % (cls._autonomous_system, rtgt_num)
            new_rtgt_name = "target:%s:%d" % (new_asn, rtgt_num)
            new_rtgt_obj = RouteTargetST.locate(new_rtgt_name)
            old_rtgt_obj = RouteTarget(old_rtgt_name)
            inst_tgt_data = InstanceTargetType()
            ri.obj = _vnc_lib.routing_instance_read(fq_name_str=ri_fq_name)
            ri.obj.del_route_target(old_rtgt_obj)
            ri.obj.add_route_target(new_rtgt_obj.obj, inst_tgt_data)
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
                left_ri.obj.set_static_route_entries(static_route_entries)
                _vnc_lib.routing_instance_update(left_ri.obj)
            try:
                RouteTargetST.delete(old_rtgt_obj.get_fq_name()[0])
            except RefsExistError:
                # if other routing instances are referring to this target,
                # it will be deleted when those instances are deleted
                pass
        # end for vn

        cls._autonomous_system = int(new_asn)
        for router in BgpRouterST.values():
            router.update_global_asn(new_asn)
        # end for router
        for router in LogicalRouterST.values():
            router.update_autonomous_system(new_asn)
        # end for router
    # end update_autonomous_system

    def add_policy(self, policy_name, attrib=None):
        # Add a policy ref to the vn. Keep it sorted by sequence number
        if attrib is None:
            attrib = VirtualNetworkPolicyType(SequenceType(sys.maxint,
                                                           sys.maxint))
        if attrib.sequence is None:
            _sandesh._logger.error("Cannot assign policy %s to %s: sequence "
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
        sc_ip_address = self._cassandra.get_service_chain_ip(sc_name)
        if sc_ip_address:
            return
        try:
            sc_ip_address = _vnc_lib.virtual_network_ip_alloc(
                self.obj, count=1)[0]
        except (NoIdError, RefsExistError) as e:
            _sandesh._logger.error(
                "Error while allocating ip in network %s: %s", self.name,
                str(e))
            return None
        self._cassandra.add_service_chain_ip(sc_name, sc_ip_address)
        return sc_ip_address
    # end allocate_service_chain_ip

    def free_service_chain_ip(self, sc_name):
        sc_ip_address = self._cassandra.get_service_chain_ip(sc_name)
        if sc_ip_address == None:
            return
        self._cassandra.remove_service_chain_ip(sc_name)
        try:
            _vnc_lib.virtual_network_ip_free(self.obj, [sc_ip_address])
        except NoIdError :
            pass
    # end free_service_chain_ip

    def get_route_target(self):
        return "target:%s:%d" % (self.get_autonomous_system(),
                                 self._route_target)
    # end get_route_target

    def _ri_needs_external_rt(self, ri_name, sc_id):
        if sc_id is None:
            return False
        sc = ServiceChain.get(sc_id)
        if sc is None:
            return False
        if self.name == sc.left_vn:
            return ri_name.endswith(sc.service_list[0].replace(':', '_'))
        elif self.name == sc.right_vn:
            return ri_name.endswith(sc.service_list[-1].replace(':', '_'))
        return False
    # end _ri_needs_external_rt

    def locate_routing_instance_no_target(self, rinst_name, ri_dict):
        """ locate a routing instance but do not allocate a route target """
        if rinst_name in self.rinst:
            return self.rinst[rinst_name]

        rinst_fq_name_str = '%s:%s' % (self.obj.get_fq_name_str(), rinst_name)
        try:
            rinst_obj = ri_dict[rinst_fq_name_str]
        except KeyError:
            _sandesh._logger.error("Cannot read routing instance %s",
                                   rinst_fq_name_str)
            return None
        rinst = RoutingInstanceST(rinst_obj)
        self.rinst[rinst_name] = rinst
        return rinst
    # end locate_routing_instance_no_target

    def locate_routing_instance(self, rinst_name, service_chain=None,
                                ri_dict=None):
        if rinst_name in self.rinst:
            return self.rinst[rinst_name]

        is_default = (rinst_name == self._default_ri_name)
        rinst_fq_name_str = '%s:%s' % (self.obj.get_fq_name_str(), rinst_name)
        old_rtgt = self._cassandra.get_route_target(rinst_fq_name_str)
        rtgt_num = self._cassandra.alloc_route_target(rinst_fq_name_str)

        rt_key = "target:%s:%d" % (self.get_autonomous_system(), rtgt_num)
        rtgt_obj = RouteTargetST.locate(rt_key).obj
        if is_default:
            inst_tgt_data = InstanceTargetType()
        elif self._ri_needs_external_rt(rinst_name, service_chain):
            inst_tgt_data = InstanceTargetType(import_export="export")
        else:
            inst_tgt_data = None

        try:
            try:
                if ri_dict:
                    rinst_obj = ri_dict[rinst_fq_name_str]
                else:
                    rinst_obj = _vnc_lib.routing_instance_read(
                        fq_name_str=rinst_fq_name_str)
                if rinst_obj.parent_uuid != self.obj.uuid:
                    # Stale object. Delete it.
                    _vnc_lib.routing_instance_delete(id=rinst_obj.uuid)
                    rinst_obj = None
                else:
                    need_update = False
                    if (rinst_obj.get_routing_instance_is_default() !=
                            is_default):
                        rinst_obj.set_routing_instance_is_default(is_default)
                        need_update = True
                    old_rt_refs = copy.deepcopy(rinst_obj.get_route_target_refs())
                    rinst_obj.set_route_target(rtgt_obj, InstanceTargetType())
                    if inst_tgt_data:
                        for rt in self.rt_list:
                            rtgt_obj = RouteTarget(rt)
                            rinst_obj.add_route_target(rtgt_obj, inst_tgt_data)
                        if not is_default and self.allow_transit:
                            rtgt_obj = RouteTarget(self.get_route_target())
                            rinst_obj.add_route_target(rtgt_obj, inst_tgt_data)
                    if (not compare_refs(rinst_obj.get_route_target_refs(),
                                         old_rt_refs)):
                        need_update = True
                    if need_update:
                        _vnc_lib.routing_instance_update(rinst_obj)
            except (NoIdError, KeyError):
                rinst_obj = None
            if rinst_obj is None:
                rinst_obj = RoutingInstance(rinst_name, self.obj)
                rinst_obj.set_route_target(rtgt_obj, InstanceTargetType())
                rinst_obj.set_routing_instance_is_default(is_default)
                if inst_tgt_data:
                    for rt in self.rt_list:
                        rtgt_obj = RouteTarget(rt)
                        rinst_obj.add_route_target(rtgt_obj, inst_tgt_data)
                    if not is_default and self.allow_transit:
                        rtgt_obj = RouteTarget(self.get_route_target())
                        rinst_obj.add_route_target(rtgt_obj, inst_tgt_data)
                _vnc_lib.routing_instance_create(rinst_obj)
        except (BadRequest, HttpError) as e:
            _sandesh._logger.error(
                "Error while creating routing instance: " + str(e))
            return None

        if rinst_obj.name == self._default_ri_name:
            self._route_target = rtgt_num

        rinst = RoutingInstanceST(rinst_obj, service_chain, rt_key)
        self.rinst[rinst_name] = rinst

        if 0 < old_rtgt < common.BGP_RTGT_MIN_ID:
            rt_key = "target:%s:%d" % (self.get_autonomous_system(), old_rtgt)
            RouteTargetST.delete(rt_key)
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

    def set_properties(self, properties):
        if self.allow_transit == properties.allow_transit:
            # If allow_transit didn't change, then we have nothing to do
            return
        self.allow_transit = properties.allow_transit
        for sc_list in self.service_chains.values():
            for service_chain in sc_list:
                if not service_chain.created:
                    continue
                if self.name == service_chain.left_vn:
                    si_name = service_chain.service_list[0]
                elif self.name == service_chain.right_vn:
                    si_name = service_chain.service_list[-1]
                else:
                    continue
                ri_name = self.get_service_name(service_chain.name, si_name)
                ri = self.rinst.get(ri_name)
                if not ri:
                    continue
                if self.allow_transit:
                    # if the network is now a transit network, add the VN's
                    # route target to all service RIs
                    ri.update_route_target_list([self.get_route_target()], [],
                                                import_export='export')
                else:
                    # if the network is not a transit network any more, then we
                    # need to delete the route target from service RIs
                    ri.update_route_target_list([], [self.get_route_target()])
    # end set_properties

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
            if self._ri_needs_external_rt(ri_obj.name, ri_obj.service_chain):
                ri_obj.update_route_target_list(rt_add, rt_del,
                                                import_export='export')
        for (prefix, nexthop) in self.route_table.items():
            left_ri = self._get_routing_instance_from_route(nexthop)
            if left_ri is None:
                continue
            left_ri.update_route_target_list(rt_add, rt_del,
                                             import_export='import')
            static_route_entries = left_ri.obj.get_static_route_entries()
            update = False
            for static_route in static_route_entries.get_route() or []:
                if static_route.prefix != prefix:
                    continue
                for rt in rt_del:
                    if rt in static_route.route_target:
                        static_route.route_target.remove(rt)
                        update = True
                for rt in rt_add:
                    if rt not in static_route.route_target:
                        static_route.route_target.append(rt)
                        update = True
                if static_route.route_target == []:
                    static_route_entries.delete_route(static_route)
                    update = True
            if update:
                left_ri.obj.set_static_route_entries(static_route_entries)
                _vnc_lib.routing_instance_update(left_ri.obj)
        for rt in rt_del:
            try:
                RouteTargetST.delete(rt)
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
            _sandesh._logger.error("Cannot read service instance %s", next_hop)
            return None
        if not si_props.auto_policy:
            _sandesh._logger.error("%s: route table next hop must be service "
                                   "instance with auto policy", self.name)
            return None
        left_vn_str, right_vn_str = get_si_vns(si, si_props)
        if (not left_vn_str or not right_vn_str):
            _sandesh._logger.error("%s: route table next hop service instance "
                                   "must have left and right virtual networks",
                                   self.name)
            return None
        left_vn = VirtualNetworkST.get(left_vn_str)
        if left_vn is None:
            _sandesh._logger.error("Virtual network %s not present",
                                   left_vn_str)
            return None
        sc = ServiceChain.find(left_vn_str, right_vn_str, '<>',
                               [PortType(0, -1)], [PortType(0, -1)], 'any')
        if sc is None:
            _sandesh._logger.error("Service chain between %s and %s not "
                                   "present", left_vn_str, right_vn_str)
            return None
        left_ri_name = left_vn.get_service_name(sc.name, next_hop)
        return left_vn.rinst.get(left_ri_name)
    # end _get_routing_instance_from_route

    def add_route(self, prefix, next_hop):
        self.route_table[prefix] = next_hop
        left_ri = self._get_routing_instance_from_route(next_hop)
        if left_ri is None:
            _sandesh._logger.error(
                "left routing instance is none for %s", next_hop)
            return
        service_info = left_ri.obj.get_service_chain_information()
        if service_info is None:
            _sandesh._logger.error(
                "Service chain info not found for %s", left_ri.name)
            return
        sc_address = service_info.get_service_chain_address()
        static_route_entries = left_ri.obj.get_static_route_entries(
        ) or StaticRouteEntriesType()
        update = False
        for static_route in static_route_entries.get_route() or []:
            if prefix == static_route.prefix:
                for rt in self.rt_list | set([self.get_route_target()]):
                    if rt not in static_route.route_target:
                        static_route.route_target.append(rt)
                        update = True
                break
        else:
            rt_list = list(self.rt_list | set([self.get_route_target()]))
            static_route = StaticRouteType(prefix=prefix, next_hop=sc_address,
                                           route_target=rt_list)
            static_route_entries.add_route(static_route)
            update = True
        if update:
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
        update = False
        static_route_entries = left_ri.obj.get_static_route_entries()
        if static_route_entries is None:
            return
        for static_route in static_route_entries.get_route() or []:
            if static_route.prefix != prefix:
                continue
            for rt in self.rt_list | set([self.get_route_target()]):
                if rt in static_route.route_target:
                    static_route.route_target.remove(rt)
                    update = True
            if static_route.route_target == []:
                static_route_entries.delete_route(static_route)
                update = True
        if update:
            left_ri.obj.set_static_route_entries(static_route_entries)
            _vnc_lib.routing_instance_update(left_ri.obj)
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
            vm_analyzer_obj = VirtualMachineST.get(':'.join(vm_analyzer[0]['to']))
            if vm_analyzer_obj is None:
                return (None, None)
            vmis = vm_analyzer_obj.interfaces
            for vmi_name in vmis:
                vmi = VirtualMachineInterfaceST.get(vmi_name)
                if vmi and vmi.service_interface_type == 'left':
                    vn_analyzer = vmi.virtual_network
                    ip_analyzer = vmi.get_any_instance_ip_address()
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
                _sandesh._logger.error("Mirror: adding connection: %s to %s",
                                       self.name, vn_analyzer)
                self.add_connection(vn_analyzer)
            else:
                _sandesh._logger.error("Mirror: %s: no analyzer vn for %s",
                                       self.name, analyzer_name)
        except NoIdError:
            return
    # end process_analyzer

    @staticmethod
    def protocol_policy_to_acl(pproto):
        # convert policy proto input(in str) to acl proto (num)
        if pproto is None:
            return 'any'
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

        if prule.action_list is None:
            _sandesh._logger.error("No action specified in policy rule "
                                   "attached to %s. Ignored.", self.name)
            return result_acl_rule_list

        for saddr in saddr_list:
            saddr_match = copy.deepcopy(saddr)
            svn = saddr.virtual_network
            spol = saddr.network_policy
            s_cidr = saddr.subnet
            if svn == "local":
                svn = self.name
                saddr_match.virtual_network = self.name
            for daddr in daddr_list:
                daddr_match = copy.deepcopy(daddr)
                dvn = daddr.virtual_network
                dpol = daddr.network_policy
                d_cidr = daddr.subnet

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
                        _sandesh._logger.error("network policy rule attached to %s"
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
                        _sandesh._logger.error("network policy rule attached to %s"
                                               "has src = %s, dst = %s. Ignored.",
                                               self.name, svn or spol, dvn or dpol)
                        continue
                elif not svn and not dvn and not spol and not dpol and s_cidr and d_cidr:
                    if prule.action_list.apply_service:
                        _sandesh._logger.error("service chains not allowed in cidr only rules"
                                               " network %s, src = %s, dst = %s. Ignored.",
                                               self.name, s_cidr, d_cidr)
                        continue
                else:
                    _sandesh._logger.error("network policy rule attached to %s"
                                           "has svn = %s, dvn = %s. Ignored.",
                                           self.name, svn, dvn)
                    continue

                service_list = None
                if prule.action_list.apply_service != []:
                    if remote_network_name == self.name:
                        _sandesh._logger.error("Service chain source and dest "
                                               "vn are same: %s", self.name)
                        continue
                    remote_vn = VirtualNetworkST.get(remote_network_name)
                    if remote_vn is None:
                        _sandesh._logger.error(
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
                        action = copy.deepcopy(prule.action_list)
                        if (service_list and svn in [self.name, 'any']):
                                service_ri = self.get_service_name(sc_name,
                                    service_list[0])
                                action.assign_routing_instance = \
                                    self.name + ':' + service_ri

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
                                _sandesh._logger.error(
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
                                _sandesh._logger.error(
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


class RouteTargetST(DBBase):
    _dict = {}

    def __init__(self, rt_key, obj=None):
        self.name = rt_key
        try:
            self.obj = obj or _vnc_lib.route_target_read(fq_name=[rt_key])
        except NoIdError:
            self.obj = RouteTarget(rt_key)
            _vnc_lib.route_target_create(self.obj)
    # end __init__

    @classmethod
    def delete(cls, rt_key):
        if rt_key not in cls._dict:
            return
        _vnc_lib.route_target_delete(fq_name=[rt_key])
        del cls._dict[rt_key]
    # end delete
# end RoutTargetST
# a struct to store attributes related to Network Policy needed by schema
# transformer


class NetworkPolicyST(DBBase):
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


class RouteTableST(DBBase):
    _dict = {}

    def __init__(self, name):
        self.name = name
        self.obj = RouteTableType(name)
        self.network_back_refs = set()
        self.routes = None
    # end __init__

# end RouteTableST

# a struct to store attributes related to Security Group needed by schema
# transformer


class SecurityGroupST(DBBase):
    _dict = {}

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
                if (acl_rule.match_condition.dst_address.security_group ==
                        from_value):
                    acl_rule.match_condition.dst_address.security_group =\
                        to_value
                    update = True
            if update:
                acl.set_access_control_list_entries(acl_entries)
                _vnc_lib.access_control_list_update(acl)
    # end update_acl

    def __init__(self, name, obj=None, acl_dict=None):
        def _get_acl(uuid):
            if acl_dict:
                return acl_dict[uuid]
            return _vnc_lib.access_control_list_read(id=uuid)

        self.name = name
        self.obj = obj or _vnc_lib.security_group_read(fq_name_str=name)
        self.config_sgid = None
        self.sg_id = None
        self.ingress_acl = None
        self.egress_acl = None
        acls = self.obj.get_access_control_lists()
        for acl in acls or []:
            if acl['to'][-1] == 'egress-access-control-list':
                self.egress_acl = _get_acl(acl['uuid'])
            elif acl['to'][-1] == 'ingress-access-control-list':
                self.ingress_acl = _get_acl(acl['uuid'])
            else:
                _vnc_lib.access_control_list_delete(id=acl['uuid'])
        config_id = self.obj.get_configured_security_group_id() or 0
        self.set_configured_security_group_id(config_id, False)
    # end __init__

    def set_configured_security_group_id(self, config_id, update_acl=True):
        if self.config_sgid == config_id:
            return
        self.config_sgid = config_id
        sg_id = self.obj.get_security_group_id()
        if sg_id is not None:
            sg_id = int(sg_id)
        if config_id:
            if sg_id is not None:
                if int(sg_id) > SGID_MIN_ALLOC:
                    self._cassandra.free_sg_id(sg_id - SGID_MIN_ALLOC)
                else:
                    if self.name == self._cassandra.get_sg_from_id(sg_id):
                        self._cassandra.free_sg_id(sg_id)
            self.obj.set_security_group_id(str(config_id))
        else:
            do_alloc = False
            if sg_id is not None:
                if int(sg_id) < SGID_MIN_ALLOC:
                    if self.name == self._cassandra.get_sg_from_id(int(sg_id)):
                        self.obj.set_security_group_id(int(sg_id) + SGID_MIN_ALLOC)
                    else:
                        do_alloc = True
            else:
                do_alloc = True
            if do_alloc:
                sg_id_num = self._cassandra.alloc_sg_id(self.name)
                self.obj.set_security_group_id(sg_id_num + SGID_MIN_ALLOC)
        if sg_id != int(self.obj.get_security_group_id()):
            _vnc_lib.security_group_update(self.obj)
        from_value = self.sg_id or self.name
        if update_acl:
            for sg in self._dict.values():
                sg.update_acl(from_value=from_value,
                              to_value=self.obj.get_security_group_id())
        # end for sg
        self.sg_id = self.obj.get_security_group_id()
    # end set_configured_security_group_id

    @classmethod
    def delete(cls, name):
        sg = cls._dict.get(name)
        if sg is None:
            return
        if sg.ingress_acl:
            _vnc_lib.access_control_list_delete(id=sg.ingress_acl.uuid)
        if sg.egress_acl:
            _vnc_lib.access_control_list_delete(id=sg.egress_acl.uuid)
        sg_id = sg.obj.get_security_group_id()
        if sg_id is not None and not sg.config_sgid:
            if sg_id < SGID_MIN_ALLOC:
                cls._cassandra.free_sg_id(sg_id)
            else:
                cls._cassandra.free_sg_id(sg_id-SGID_MIN_ALLOC)
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

        ethertype = prule.ethertype

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
                                                   daddr_match, dp,
                                                   ethertype)
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
        for ri_ref in self.obj.get_routing_instance_refs() or []:
            self.connections.add(':'.join(ri_ref['to']))
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
        try:
            self.obj = _vnc_lib.routing_instance_read(id=self.obj.uuid)
        except NoIdError:
            return
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
        # refresh the ri object because it could have changed
        self.obj = _vnc_lib.routing_instance_read(id=self.obj.uuid)
        rtgt_list = self.obj.get_route_target_refs()
        ri_fq_name_str = self.obj.get_fq_name_str()
        DBBase._cassandra.free_route_target(ri_fq_name_str)

        service_chain = self.service_chain
        if vn_obj is not None and service_chain is not None:
            # self.free_service_chain_ip(service_chain)
            vn_obj.free_service_chain_ip(self.obj.name)

            uve = UveServiceChainData(name=service_chain, deleted=True)
            uve_msg = UveServiceChain(data=uve, sandesh=_sandesh)
            uve_msg.send(sandesh=_sandesh)

        vmi_refs = self.obj.get_virtual_machine_interface_back_refs()
        for vmi in vmi_refs or []:
            try:
                vmi_obj = _vnc_lib.virtual_machine_interface_read(
                    id=vmi['uuid'])
                if service_chain is not None:
                    DBBase._cassandra.free_service_chain_vlan(
                        vmi_obj.get_parent_fq_name_str(), service_chain)
            except NoIdError:
                continue

            vmi_obj.del_routing_instance(self.obj)
            _vnc_lib.virtual_machine_interface_update(vmi_obj)
        # end for vmi
        _vnc_lib.routing_instance_delete(id=self.obj.uuid)
        for rtgt in rtgt_list or []:
            try:
                RouteTargetST.delete(rtgt['to'][0])
            except RefsExistError:
                # if other routing instances are referring to this target,
                # it will be deleted when those instances are deleted
                pass

# end class RoutingInstanceST


class ServiceChain(DBBase):
    _dict = {}
    
    @classmethod
    def init(cls):
        # When schema transformer restarts, read all service chains from cassandra
        for (name, columns) in cls._cassandra.list_service_chain_uuid():
            chain = jsonpickle.decode(columns['value'])

            # Some service chains may not be valid any more. We may need to
            # delete such service chain objects or we have to destroy them.
            # To handle each case, we mark them with two separate flags,
            # which will be reset when appropriate calls are made.
            # Any service chains for which these flags are still set after
            # all search results are received from ifmap, we will delete/
            # destroy them
            chain.present_stale = True
            chain.created_stale = chain.created
            if not hasattr(chain, 'partially_created'):
                chain.partially_created = False
            cls._dict[name] = chain
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

        self.protocol = protocol
        self.created = False
        self.partially_created = False

        self.present_stale = False
        self.created_stale = False

        self.error_msg = None
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
            sc.present_stale = False
            return sc
        
        name = str(uuid.uuid4())
        sc = ServiceChain(name, left_vn, right_vn, direction, sp_list,
                          dp_list, protocol, service_list)
        ServiceChain._dict[name] = sc
        cls._cassandra.add_service_chain_uuid(name, jsonpickle.encode(sc))
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
    # end update_ipams

    def log_error(self, msg):
        self.error_msg = msg
        _sandesh._logger.error('service chain %s: ' + msg)
    # end log_error

    def check_create(self):
        # Check if this service chain can be created:
        # - all service instances have VMs
        # - all service instances have proper service mode
        # - all service VMs have left and if needed, right interface
        # If checks pass, return a dict containing mode and vm_list for all
        # service instances so that we don't have to find them again
        ret_dict = {}
        for service in self.service_list:
            vm_list = VirtualMachineST.get_by_service_instance(service)
            if not vm_list:
                self.log_error("No vms found for service instance " + service)
                return None
            vm_info_list = []
            mode = None
            for service_vm in vm_list:
                vm_obj = VirtualMachineST.get(service_vm)
                if vm_obj is None:
                    self.log_error('virtual machine %s not found' % service_vm)
                    return None
                if mode is None:
                    mode = vm_obj.get_service_mode()
                    if mode is None:
                        self.log_error("service mode not found: %s" % service_vm)
                        return None
                vm_info = {'vm_obj': vm_obj}

                for interface_name in vm_obj.interfaces:
                    interface = VirtualMachineInterfaceST.get(interface_name)
                    if not interface:
                        continue
                    if interface.service_interface_type not in ['left',
                                                                'right']:
                        continue
                    ip_addr = None
                    if mode != 'transparent':
                        ip_addr = interface.get_any_instance_ip_address()
                        if ip_addr is None:
                            self.log_error("No ip address found for interface "
                                           + interface_name)
                            return None
                    vmi_info = {'vmi': interface, 'address': ip_addr}
                    vm_info[interface.service_interface_type] = vmi_info

                if 'left' not in vm_info:
                    self.log_error('Left interface not found for %s' %
                                   service_vm)
                    return None
                if ('right' not in vm_info and mode != 'in-network-nat' and
                    self.direction == '<>'):
                    self.log_error('Right interface not found for %s' %
                                   service_vm)
                    return None
                vm_info_list.append(vm_info)
            ret_dict[service] = {'mode': mode, 'vm_list': vm_info_list}
        return ret_dict
    # check_create

    def create(self):
        if self.created:
            if self.created_stale:
                self.uve_send()
                self.created_stale = False
            # already created
            return

        si_info = self.check_create()
        if si_info is None:
            return
        self._create(si_info)
        if self.partially_created:
            self.destroy()
            return
        self.uve_send()
    # end create

    def uve_send(self):
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
    # end uve_send

    def _create(self, si_info):
        self.partially_created = True
        vn1_obj = VirtualNetworkST.locate(self.left_vn)
        vn2_obj = VirtualNetworkST.locate(self.right_vn)
        #sc_ip_address = vn1_obj.allocate_service_chain_ip(sc_name)
        if not vn1_obj or not vn2_obj:
            self.log_error("vn1_obj or vn2_obj is None")
            return

        service_ri2 = vn1_obj.get_primary_routing_instance()
        first_node = True
        for service in self.service_list:
            service_name1 = vn1_obj.get_service_name(self.name, service)
            service_name2 = vn2_obj.get_service_name(self.name, service)
            service_ri1 = vn1_obj.locate_routing_instance(
                service_name1, self.name)
            if service_ri1 is None or service_ri2 is None:
                self.log_error("service_ri1 or service_ri2 is None")
                return
            service_ri2.add_connection(service_ri1)
            service_ri2 = vn2_obj.locate_routing_instance(
                service_name2, self.name)
            if service_ri2 is None:
                self.log_error("service_ri2 is None")
                return

            if first_node:
                first_node = False
                rt_list = set(vn1_obj.rt_list)
                if vn1_obj.allow_transit:
                    rt_list.add(vn1_obj.get_route_target())
                service_ri1.update_route_target_list(rt_list,
                                                     import_export='export')

            mode = si_info[service]['mode']
            nat_service = (mode == "in-network-nat")
            transparent = (mode not in ["in-network", "in-network-nat"])
            _sandesh._logger.info("service chain %s: creating %s chain",
                                   self.name, mode)

            if transparent:
                sc_ip_address = vn1_obj.allocate_service_chain_ip(
                    service_name1)
                if sc_ip_address is None:
                    self.log_error('Cannot allocate service chain ip address')
                    return
                service_ri1.add_service_info(vn2_obj, service, sc_ip_address)
                if self.direction == "<>":
                    service_ri2.add_service_info(vn1_obj, service,
                                                 sc_ip_address)

            for vm_info in si_info[service]['vm_list']:
                if transparent:
                    result = self.process_transparent_service(
                        vm_info, sc_ip_address, service_ri1, service_ri2)
                else:
                    result = self.process_in_network_service(
                        vm_info, service, vn1_obj, vn2_obj, service_ri1,
                        service_ri2, nat_service)
                if not result:
                    return
            _vnc_lib.routing_instance_update(service_ri1.obj)
            _vnc_lib.routing_instance_update(service_ri2.obj)

        rt_list = set(vn2_obj.rt_list)
        if vn2_obj.allow_transit:
            rt_list.add(vn2_obj.get_route_target())
        service_ri2.update_route_target_list(rt_list, import_export='export')

        service_ri2.add_connection(vn2_obj.get_primary_routing_instance())

        if not transparent and len(self.service_list) == 1:
            for vn in VirtualNetworkST.values():
                for prefix, nexthop in vn.route_table.items():
                    if nexthop == self.service_list[0]:
                        vn.add_route(prefix, nexthop)

        self.created = True
        self.partially_created = False
        self.error_msg = None
        self._cassandra.add_service_chain_uuid(self.name, jsonpickle.encode(self))
    # end _create

    def add_pbf_rule(self, vmi, ri1, ri2, ip_address, vlan):
        if vmi.service_interface_type not in ["left", "right"]:
            return
        vmi.obj = _vnc_lib.virtual_machine_interface_read(id=vmi.uuid)
        refs = vmi.obj.get_routing_instance_refs() or []
        ri_refs = [ref['to'] for ref in refs]

        pbf = PolicyBasedForwardingRuleType()
        pbf.set_direction('both')
        pbf.set_vlan_tag(vlan)
        pbf.set_service_chain_address(ip_address)

        update = False
        if vmi.service_interface_type == 'left':
            pbf.set_src_mac('02:00:00:00:00:01')
            pbf.set_dst_mac('02:00:00:00:00:02')
            if (ri1.get_fq_name() not in ri_refs):
                vmi.obj.add_routing_instance(ri1.obj, pbf)
                update = True
        if vmi.service_interface_type == 'right' and self.direction == '<>':
            pbf.set_src_mac('02:00:00:00:00:02')
            pbf.set_dst_mac('02:00:00:00:00:01')
            if (ri2.get_fq_name() not in ri_refs):
                vmi.obj.add_routing_instance(ri2.obj, pbf)
                update = True
        if update:
            _vnc_lib.virtual_machine_interface_update(vmi.obj)
    # end add_pbf_rule

    def process_transparent_service(self, vm_info, sc_ip_address,
                                    service_ri1, service_ri2):
        vm_uuid = vm_info['vm_obj'].uuid
        vlan = self._cassandra.allocate_service_chain_vlan(vm_uuid,
                                                           self.name)
        self.add_pbf_rule(vm_info['left']['vmi'], service_ri1, service_ri2,
                              sc_ip_address, vlan)
        self.add_pbf_rule(vm_info['right']['vmi'], service_ri1, service_ri2,
                              sc_ip_address, vlan)
        return True
    # end process_transparent_service

    def process_in_network_service(self, vm_info, service, vn1_obj, vn2_obj,
                                   service_ri1, service_ri2, nat_service):
        service_ri1.add_service_info(
            vn2_obj, service, vm_info['left']['address'],
            vn1_obj.get_primary_routing_instance().get_fq_name_str())
        if self.direction == '<>' and not nat_service:
            service_ri2.add_service_info(
                vn1_obj, service, vm_info['right']['address'],
                vn2_obj.get_primary_routing_instance().get_fq_name_str())
        return True
    # end process_in_network_service

    def destroy(self):
        if not self.created and not self.partially_created:
            return

        self.created = False
        self.partially_created = False
        self._cassandra.add_service_chain_uuid(self.name,
                                               jsonpickle.encode(self))

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
        if self.created or self.partially_created:
            self.destroy()
        del self._dict[self.name]
        self._cassandra.remove_service_chain_uuid(self.name)
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
        sc.created = self.created
        sc.error_msg = self.error_msg
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
    def _port_is_subset(lhs, rhs):
        return (lhs.start_port >= rhs.start_port and
               (rhs.end_port == -1 or lhs.end_port <= rhs.end_port))

    @staticmethod
    def _address_is_subset(lhs, rhs):
        if rhs.subnet is None and lhs.subnet is None:
            return rhs.virtual_network in [lhs.virtual_network, 'any']
        if rhs.subnet is not None and lhs.subnet is not None:
            return (rhs.subnet.ip_prefix == lhs.subnet.ip_prefix and
                    rhs.subnet.ip_prefix_len <= lhs.subnet.ip_prefix_len)
        return False

    def _rule_is_subset(self, rule):
        for elem in self._list:
            lhs = rule.match_condition
            rhs = elem.match_condition
            if (self._port_is_subset(lhs.src_port, rhs.src_port) and
                self._port_is_subset(lhs.dst_port, rhs.dst_port) and
                rhs.protocol in [lhs.protocol, 'any'] and
                self._address_is_subset(lhs.src_address, rhs.src_address) and
                self._address_is_subset(lhs.dst_address, rhs.dst_address)):

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


class BgpRouterST(DBBase):
    _dict = {}
    _ibgp_auto_mesh = None
    def __init__(self, name, params):
        self.name = name
        self.vendor = (params.vendor or 'contrail').lower()
        self.asn = None
        self.identifier = params.identifier

        if self._ibgp_auto_mesh is None:
            gsc = _vnc_lib.global_system_config_read(
                fq_name=['default-global-system-config'])
            self._ibgp_auto_mesh = gsc.get_ibgp_auto_mesh()
            if self._ibgp_auto_mesh is None:
                self._ibgp_auto_mesh = True
    # end __init__

    def set_params(self, params):
        if self.vendor == 'contrail':
            self.update_global_asn(VirtualNetworkST.get_autonomous_system())
        else:
            self.update_autonomous_system(params.autonomous_system)
    # end set_params

    def update_global_asn(self, asn):
        if self.vendor != 'contrail' or self.asn == int(asn):
            return
        router_obj = _vnc_lib.bgp_router_read(fq_name_str=self.name)
        params = router_obj.get_bgp_router_parameters()
        params.autonomous_system = int(asn)
        router_obj.set_bgp_router_parameters(params)
        _vnc_lib.bgp_router_update(router_obj)
        self.update_autonomous_system(asn)
    # end update_global_asn

    def update_autonomous_system(self, asn):
        if self.asn == int(asn):
            return
        self.asn = int(asn)
        self.update_peering()
    # end update_autonomous_system

    @classmethod
    def update_ibgp_auto_mesh(cls, value):
        if cls._ibgp_auto_mesh == value:
            return
        cls._ibgp_auto_mesh = value
        if not value:
            return
        for router in cls._dict.values():
            router.update_peering()
    # end update_ibgp_auto_mesh

    def update_peering(self):
        if not self._ibgp_auto_mesh:
            return
        global_asn = int(VirtualNetworkST.get_autonomous_system())
        if self.asn != global_asn:
            return
        try:
            obj = _vnc_lib.bgp_router_read(fq_name_str=self.name)
        except NoIdError as e:
            _sandesh._logger.error("NoIdError while reading bgp router "
                                   "%s: %s", self.name, str(e))
            return

        peerings = [ref['to'] for ref in (obj.get_bgp_router_refs() or [])]
        for router in self._dict.values():
            if router.name == self.name:
                continue
            if router.asn != global_asn:
                continue
            router_fq_name = router.name.split(':')
            if router_fq_name in peerings:
                continue
            router_obj = BgpRouter()
            router_obj.fq_name = router_fq_name
            af = AddressFamilies(family=[])
            bsa = BgpSessionAttributes(address_families=af)
            session = BgpSession(attributes=[bsa])
            attr = BgpPeeringAttributes(session=[session])
            obj.add_bgp_router(router_obj, attr)
            try:
                _vnc_lib.bgp_router_update(obj)
            except NoIdError as e:
                _sandesh._logger.error("NoIdError while updating bgp router "
                                       "%s: %s", self.name, str(e))
    # end update_peering
# end class BgpRouterST


class VirtualMachineInterfaceST(DBBase):
    _dict = {}
    _vn_dict = {}
    _service_vmi_list = []

    def __init__(self, name, obj=None):
        self.name = name
        self.service_interface_type = None
        self.interface_mirror = None
        self.virtual_network = None
        self.virtual_machine = None
        self.uuid = None
        self.instance_ips = set()
        self.floating_ips = set()
        self.obj = obj or _vnc_lib.virtual_machine_interface_read(fq_name_str=name)
        self.uuid = self.obj.uuid
        self.vrf_table = jsonpickle.encode(self.obj.get_vrf_assign_table())
    # end __init__

    @classmethod
    def delete(cls, name):
        vmi = cls.get(name)
        if vmi is None:
            return
        try:
            if vmi.virtual_network and vmi.virtual_network in cls._vn_dict:
                cls._vn_dict[vmi.virtual_network].remove(vmi)
            cls._service_vmi_list.remove(vmi)
        except ValueError:
            pass
        del cls._dict[name]
    # end delete

    @classmethod
    def get_vmi_on_network(cls, network_name):
        return cls._vn_dict.get(network_name, [])

    @classmethod
    def get_service_interfaces(cls):
        return cls._service_vmi_list

    def add_instance_ip(self, ip_name):
        self.instance_ips.add(ip_name)
    # end add_instance_ip
    
    def delete_instance_ip(self, ip_name):
        self.instance_ips.discard(ip_name)
    # end delete_instance_ip

    def get_any_instance_ip_address(self):
        for ip_name in self.instance_ips:
            ip = InstanceIpST.get(ip_name)
            if ip and ip.address:
                return ip.address
        return None
    # end get_any_instance_ip_address

    def add_floating_ip(self, ip_name):
        self.floating_ips.add(ip_name)
    # end add_floating_ip

    def delete_floating_ip(self, ip_name):
        self.floating_ips.discard(ip_name)
    # end delete_floating_ip

    def set_service_interface_type(self, service_interface_type):
        if self.service_interface_type == service_interface_type:
            return
        if service_interface_type is not None:
            self._service_vmi_list.append(self)
        self.service_interface_type = service_interface_type
        self._add_pbf_rules()
    # end set_service_interface_type

    def set_interface_mirror(self, interface_mirror):
        self.interface_mirror = interface_mirror
    # end set_interface_mirror

    def set_virtual_machine(self, virtual_machine):
        if self.virtual_machine == virtual_machine:
            return
        self.virtual_machine = virtual_machine
        self._add_pbf_rules()
    # end set_virtual_machine

    def _add_pbf_rules(self):
        if (not self.virtual_machine or
            self.service_interface_type not in ['left', 'right']):
            return

        vm_obj = VirtualMachineST.get(self.virtual_machine)
        if vm_obj is None:
            return
        smode = vm_obj.get_service_mode()
        if smode != 'transparent':
            return
        for service_chain in ServiceChain.values():
            if vm_obj.service_instance not in service_chain.service_list:
                continue
            if not service_chain.created:
                continue
            if self.service_interface_type == 'left':
                vn_obj = VirtualNetworkST.locate(service_chain.left_vn)
                vn1_obj = vn_obj
            else:
                vn1_obj = VirtualNetworkST.locate(service_chain.left_vn)
                vn_obj = VirtualNetworkST.locate(service_chain.right_vn)

            service_name = vn_obj.get_service_name(service_chain.name,
                                                   vm_obj.service_instance)
            service_ri = vn_obj.locate_routing_instance(
                service_name, service_chain.name)
            sc_ip_address = vn1_obj.allocate_service_chain_ip(
                service_name)
            vlan = self._cassandra.allocate_service_chain_vlan(
                vm_obj.uuid, service_chain.name)

            service_chain.add_pbf_rule(self, service_ri, service_ri,
                                       sc_ip_address, vlan)
    # end _add_pbf_rules

    def set_virtual_network(self, vn_name):
        self.virtual_network = vn_name
        self._vn_dict.setdefault(vn_name, []).append(self)
        virtual_network = VirtualNetworkST.locate(vn_name)
        if virtual_network is None:
            return
        ri = virtual_network.get_primary_routing_instance().obj
        refs = self.obj.get_routing_instance_refs()
        if ri.get_fq_name() not in [r['to'] for r in (refs or [])]:
            self.obj.add_routing_instance(
                ri, PolicyBasedForwardingRuleType(direction="both"))
            try:
                _vnc_lib.virtual_machine_interface_update(self.obj)
            except NoIdError:
                _sandesh._logger.error("NoIdError while updating interface " +
                                       self.name)

        for lr in LogicalRouterST.values():
            if self.name in lr.interfaces:
                lr.add_interface(self.name)
    # end set_virtual_network
    
    def process_analyzer(self):
        if self.interface_mirror is None or self.virtual_network is None:
            return
        vn = VirtualNetworkST.get(self.virtual_network)
        if vn is None:
            return

        vn.process_analyzer(self.interface_mirror)
        vmi_props = self.obj.get_virtual_machine_interface_properties()
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
        self.obj.set_virtual_machine_interface_properties(vmi_props)
        try:
            _vnc_lib.virtual_machine_interface_update(self.obj)
        except NoIdError:
            _sandesh._logger.error("NoIdError while updating interface " +
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
        if self.virtual_machine is None:
            return network_set
        vm_obj = VirtualMachineST.get(self.virtual_machine)
        if vm_obj is None:
            _sandesh._logger.error("Virtual machine %s not found " %
                                   self.virtual_machine)
            return network_set
        if not vm_obj.service_instance:
            return network_set
        si_name = vm_obj.service_instance
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
        if self.virtual_machine is None:
            _sandesh._logger.error("vm is None for interface %s", self.name)
            return

        vrf_table = VrfAssignTableType()
        ip_list = []
        for ip_name in self.instance_ips:
            ip = InstanceIpST.get(ip_name)
            if ip and ip.address:
                ip_list.append(ip.address)
        for ip_name in self.floating_ips:
            ip = FloatingIpST.get(ip_name)
            if ip and ip.address:
                ip_list.append(ip.address)
        for ip in ip_list:
            address = AddressType(subnet=SubnetType(ip, 32))
            mc = MatchConditionType(src_address=address,
                                    src_port=PortType(),
                                    dst_port=PortType())

            ri_name = vn.obj.get_fq_name_str() + ':' + vn._default_ri_name
            vrf_rule = VrfAssignRuleType(match_condition=mc,
                                         routing_instance=ri_name,
                                         ignore_acl=False)
            vrf_table.add_vrf_assign_rule(vrf_rule)

        vm_obj = VirtualMachineST.get(self.virtual_machine)
        if vm_obj is None:
            _sandesh._logger.error(
                "virtual machine %s not found", self.virtual_machine)
            return
        smode = vm_obj.get_service_mode()
        if smode not in ['in-network', 'in-network-nat']:
            return

        policy_rule_count = 0
        si_name = vm_obj.service_instance
        for policy_name in vn.policies:
            policy = NetworkPolicyST.get(policy_name)
            if policy is None:
                continue
            policy_rule_entries = policy.obj.get_network_policy_entries()
            if policy_rule_entries is None:
                continue
            for prule in policy_rule_entries.policy_rule:
                if (prule.action_list is None or
                    si_name not in prule.action_list.apply_service or []):
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
        vrf_table_pickle = jsonpickle.encode(vrf_table)
        if vrf_table_pickle != self.vrf_table:
            self.obj.set_vrf_assign_table(vrf_table)
            try:
                _vnc_lib.virtual_machine_interface_update(self.obj)
                self.vrf_table = vrf_table_pickle
            except NoIdError as e:
                if e._unknown_id == self.uuid:
                    VirtualMachineInterfaceST.delete(self.name)

    # end recreate_vrf_assign_table
# end VirtualMachineInterfaceST


class InstanceIpST(DBBase):
    _dict = {}

    def __init__(self, name, address):
        self.name = name
        self.address = address
    # end __init
# end InstanceIpST


class FloatingIpST(DBBase):
    _dict = {}

    def __init__(self, name, address):
        self.name = name
        self.address = address
    # end __init
# end FloatingIpST


class VirtualMachineST(DBBase):
    _dict = {}
    _si_dict = {}
    def __init__(self, name, si):
        self.name = name
        self.interfaces = set()
        self.service_instance = si
        self._si_dict.setdefault(si, []).append(name)
        for vmi in VirtualMachineInterfaceST.values():
            if vmi.virtual_machine == name:
                self.add_interface(vmi.name)
        self.obj = _vnc_lib.virtual_machine_read(fq_name_str=name)
        self.uuid = self.obj.uuid
    # end __init__

    @classmethod
    def delete(cls, name):
        vm = cls.get(name)
        if vm is None:
            return
        cls._si_dict[vm.service_instance].remove(name)
        if not cls._si_dict[vm.service_instance]:
            del cls._si_dict[vm.service_instance]
        del cls._dict[name]
    # end delete

    def add_interface(self, interface):
        self.interfaces.add(interface)
    # end add_interface

    def delete_interface(self, interface):
        self.interfaces.discard(interface)
    # end delete_interface

    @classmethod
    def get_by_service_instance(cls, service_instance):
        return cls._si_dict.get(service_instance, [])
    # end get_by_service_instance

    def get_service_mode(self):
        if self.service_instance is None:
            return None

        if hasattr(self, 'service_mode'):
            return self.service_mode

        try:
            si_obj = _vnc_lib.service_instance_read(
                fq_name_str=self.service_instance)
        except NoIdError:
            _sandesh._logger.error("NoIdError while reading service instance "
                                   + self.service_instance)
            return None
        st_refs = si_obj.get_service_template_refs()
        if not st_refs:
            _sandesh._logger.error("st_refs is None for service instance "
                                   + self.service_instance)
            return None
        try:
            st_obj = _vnc_lib.service_template_read(id=st_refs[0]['uuid'])
        except NoIdError:
            _sandesh._logger.error("NoIdError while reading service template "
                                   + st_refs[0]['uuid'])
            return None

        self.service_mode = st_obj.get_service_template_properties().get_service_mode() or 'transparent'
        return self.service_mode
    # end get_service_mode
# end VirtualMachineST


class LogicalRouterST(DBBase):
    _dict = {}

    def __init__(self, name, obj=None):
        self.name = name
        self.interfaces = set()
        self.virtual_networks = set()
        if not obj:
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
            rtgt_num = self._cassandra.alloc_route_target(name, True)
            rt_key = "target:%s:%d" % (
                VirtualNetworkST.get_autonomous_system(), rtgt_num)
            rtgt_obj = RouteTargetST.locate(rt_key)
            obj.set_route_target(rtgt_obj.obj)
            _vnc_lib.logical_router_update(obj)

        if old_rt_key:
            RouteTargetST.delete(old_rt_key)
        self.route_target = rt_key
    # end __init__

    @classmethod
    def delete(cls, name):
        if name in cls._dict:
            lr = cls._dict[name]
            rtgt_num = int(lr.route_target.split(':')[-1])
            cls._cassandra.free_route_target_by_number(rtgt_num)
            RouteTargetST.delete(lr.route_target)
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
            obj.set_route_target(rtgt_obj.obj)
            _vnc_lib.logical_router_update(obj)
        except NoIdError:
            _sandesh._logger.error(
                "NoIdError while accessing logical router %s" % self.name)
        for vn in self.virtual_networks:
            vn_obj = VirtualNetworkST.get(vn)
            if vn_obj is not None:
                ri_obj = vn_obj.get_primary_routing_instance()
                ri_obj.update_route_target_list(rt_del=[old_rt],
                                                rt_add=[rt_key])
        RouteTargetST.delete(old_rt)
        self.route_target = rt_key
    # end update_autonomous_system

# end LogicaliRouterST


class SchemaTransformer(object):

    """
    data + methods used/referred to by ssrc and arc greenlets
    """
    def __init__(self, args=None):
        global _sandesh
        self._args = args
        self._fabric_rt_inst_obj = None

        # Initialize discovery client
        self._disc = None
        if self._args.disc_server_ip and self._args.disc_server_port:
            self._disc = client.DiscoveryClient(
                self._args.disc_server_ip,
                self._args.disc_server_port,
                ModuleNames[Module.SCHEMA_TRANSFORMER])

        _sandesh = Sandesh()
        sandesh.VnList.handle_request = self.sandesh_vn_handle_request
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
            ['cfgm_common', 'schema_transformer.sandesh'], self._disc,
            logger_class=args.logger_class,
            logger_config_file=args.logging_conf)
        _sandesh.set_logging_params(enable_local_log=args.log_local,
                                    category=args.log_category,
                                    level=args.log_level,
                                    file=args.log_file,
                                    enable_syslog=args.use_syslog,
                                    syslog_facility=args.syslog_facility)
        ConnectionState.init(_sandesh, hostname, module_name, instance_id,
                staticmethod(ConnectionState.get_process_state_cb),
                NodeStatusUVE, NodeStatus)

        self._cassandra = SchemaTransformerDB(self, _zookeeper_client)
        DBBase.init(self, _sandesh.logger(), self._cassandra)
        ServiceChain.init()
        self.reinit()
        self.ifmap_search_done = False
        # create cpu_info object to send periodic updates
        sysinfo_req = False
        cpu_info = vnc_cpu_info.CpuInfo(
            module_name, instance_id, sysinfo_req, _sandesh, 60)
        self._cpu_info = cpu_info

    # end __init__

    def config_log(self, msg, level):
        _sandesh.logger().log(SandeshLogger.get_py_logger_level(level),
                              msg)

    # Clean up stale objects
    def reinit(self):
        lr_list = _vnc_lib.logical_routers_list(detail=True)
        for lr in lr_list:
           LogicalRouterST.locate(lr.get_fq_name_str(), lr)
        vn_list = _vnc_lib.virtual_networks_list(detail=True,
                                                 fields=['routing_instances',
                                                         'access_control_lists'])
        vn_id_list = [vn.uuid for vn in vn_list]
        ri_list = _vnc_lib.routing_instances_list(detail=True)
        ri_dict = {}
        ri_deleted = {}
        for ri in ri_list:
            delete = False
            if ri.parent_uuid not in vn_id_list:
                delete = True
            else:
                # if the RI was for a service chain and service chain no
                # longer exists, delete the RI
                sc_id = VirtualNetworkST._get_service_id_from_ri(ri.name)
                if sc_id and sc_id not in ServiceChain:
                    delete = True
                    ri_deleted.setdefault(ri.parent_uuid, []).append(ri.uuid)
                else:
                    ri_dict[ri.get_fq_name_str()] = ri
            if delete:
                try:
                    ri_obj = RoutingInstanceST(ri)
                    ri_obj.delete()
                except NoIdError:
                    pass
                except Exception as e:
                    _sandesh._logger.error(
                            "Error while deleting routing instance %s: %s",
                            ri.get_fq_name_str(), str(e))

        # end for ri

        sg_list = _vnc_lib.security_groups_list(detail=True,
                                                fields=['access_control_lists'])
        sg_id_list = [sg.uuid for sg in sg_list]
        acl_list = _vnc_lib.access_control_lists_list(detail=True)
        sg_acl_dict = {}
        vn_acl_dict = {}
        for acl in acl_list or []:
            delete = False
            if acl.parent_type == 'virtual-network':
                if acl.parent_uuid in vn_id_list:
                    vn_acl_dict[acl.uuid] = acl
                else:
                    delete = True
            elif acl.parent_type == 'security-group':
                if acl.parent_uuid in sg_id_list:
                    sg_acl_dict[acl.uuid] = acl
                else:
                    delete = True
            else:
                delete = True

            if delete:
                try:
                    _vnc_lib.access_control_list_delete(id=acl.uuid)
                except NoIdError:
                    pass
                except Exception as e:
                    _sandesh._logger.error(
                            "Error while deleting acl %s: %s",
                            acl.uuid, str(e))
        # end for acl

        _SLEEP_TIMEOUT=0.001
        start_time = time.time()
        for index, sg in enumerate(sg_list):
            SecurityGroupST.locate(sg.get_fq_name_str(), sg, sg_acl_dict)
            if not index % 100:
                gevent.sleep(_SLEEP_TIMEOUT)
        elapsed_time = time.time() - start_time
        _sandesh._logger.info("Initialized %d security groups in %.3f", len(sg_list), elapsed_time)

        rt_list = _vnc_lib.route_targets_list()['route-targets']
        start_time = time.time()
        for index, rt in enumerate(rt_list):
            rt_name = ':'.join(rt['fq_name'])
            RouteTargetST.locate(rt_name, RouteTarget(rt_name))
            if not index % 100:
                gevent.sleep(_SLEEP_TIMEOUT)
        elapsed_time = time.time() - start_time
        _sandesh._logger.info("Initialized %d route targets in %.3f", len(rt_list), elapsed_time)

        start_time = time.time()
        for index, vn in enumerate(vn_list):
            if vn.uuid in ri_deleted:
                vn_ri_list = vn.get_routing_instances() or []
                new_vn_ri_list = [vn_ri for vn_ri in vn_ri_list
                                  if vn_ri['uuid'] not in ri_deleted[vn.uuid]]
                vn.routing_instances = new_vn_ri_list
            VirtualNetworkST.locate(vn.get_fq_name_str(), vn, vn_acl_dict,
                                    ri_dict)
            if not index % 100:
                gevent.sleep(_SLEEP_TIMEOUT)
        elapsed_time = time.time() - start_time
        _sandesh._logger.info("Initialized %d virtual networks in %.3f", len(vn_list), elapsed_time)

        vmi_list = _vnc_lib.virtual_machine_interfaces_list(detail=True)
        start_time = time.time()
        for index, vmi in enumerate(vmi_list):
            VirtualMachineInterfaceST.locate(vmi.get_fq_name_str(), vmi)
            if not index % 100:
                gevent.sleep(_SLEEP_TIMEOUT)
        elapsed_time = time.time() - start_time
        _sandesh._logger.info("Initialized %d virtual machine interfaces in %.3f", len(vmi_list), elapsed_time)

        vm_list = _vnc_lib.virtual_machines_list(detail=True)
        start_time = time.time()
        for index, vm in enumerate(vm_list):
            si_refs = vm.get_service_instance_refs()
            if si_refs:
                si_fq_name_str = ':'.join(si_refs[0]['to'])
                VirtualMachineST.locate(vm.get_fq_name_str(), 
                   si_fq_name_str)
            if not index % 100:
                gevent.sleep(_SLEEP_TIMEOUT)
        elapsed_time = time.time() - start_time
        _sandesh._logger.info("Initialized %d virtual machines in %.3f", len(vm_list), elapsed_time)
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

    def add_ibgp_auto_mesh(self, idents, meta):
        value = meta.text
        BgpRouterST.update_ibgp_auto_mesh(value.lower() == "true")
    # end add_ibgp_auto_mesh

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
            _sandesh._logger.error("%s: Cannot read security group entries",
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

    def add_configured_security_group_id(self, idents, meta):
        sg_name = idents['security-group']
        sg = SecurityGroupST.locate(sg_name)
        config_id = int(meta.text)

        if sg:
            sg.set_configured_security_group_id(config_id)
    # end add_configured_security_group_id

    def add_network_policy_entries(self, idents, meta):
        # Network policy entries arrived or modified
        policy_name = idents['network-policy']
        policy = NetworkPolicyST.locate(policy_name)

        entries = PolicyEntriesType()
        entries.build(meta)
        policy.obj.set_network_policy_entries(entries)
        self.current_network_set |= policy.add_rules(entries)
    # end add_network_policy_entries

    def add_virtual_network_properties(self, idents, meta):
        network_name = idents['virtual-network']
        virtual_network = VirtualNetworkST.get(network_name)
        if not virtual_network:
            return
        prop = VirtualNetworkType()
        prop.build(meta)
        virtual_network.set_properties(prop)
    # end add_virtual_network_properties

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
        self.current_network_set |= (
            policy.networks_back_ref | policy.analyzer_vn_set)
        for pol in NetworkPolicyST.values():
            if policy_name in pol.policies:
                self.current_network_set |= pol.networks_back_ref
    # end add_virtual_network_network_policy

    def add_virtual_network_network_ipam(self, idents, meta):
        network_name = idents['virtual-network']
        ipam_name = idents['network-ipam']
        virtual_network = VirtualNetworkST.locate(network_name)
        if virtual_network is None:
            _sandesh._logger.error("Cannot read virtual network %s",
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

    def add_instance_ip_address(self, idents, meta):
        ip_name = idents['instance-ip']
        address = meta.text

        ip = InstanceIpST.locate(ip_name, address)
    # end add_instance_ip_address

    def delete_instance_ip_address(self, idents, meta):
        ip_name = idents['instance-ip']
        InstanceIpST.delete(ip_name)
    # end delete_instance_ip_address

    def add_floating_ip_virtual_machine_interface(self, idents, meta):
        vmi_name = idents['virtual-machine-interface']
        ip_name = idents['floating-ip']
        vmi = VirtualMachineInterfaceST.locate(vmi_name)
        if vmi is not None:
            vmi.add_floating_ip(ip_name)
            self.current_network_set |= vmi.rebake()
    # end add_floating_ip_virtual_machine_interface

    def delete_floating_ip_virtual_machine_interface(self, idents, meta):
        vmi_name = idents['virtual-machine-interface']
        ip_name = idents['floating-ip']
        vmi = VirtualMachineInterfaceST.get(vmi_name)
        if vmi is not None:
            vmi.delete_floating_ip(ip_name)
    # end delete_floating_ip_virtual_machine_interface

    def add_floating_ip_address(self, idents, meta):
        ip_name = idents['floating-ip']
        address = meta.text
        FloatingIpST.locate(ip_name, address)
    # end add_floating_ip_address

    def delete_floating_ip_address(self, idents, meta):
        ip_name = idents['floating-ip']
        FloatingIpST.delete(ip_name)
    # end delete_floating_ip_address

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

    def add_virtual_machine_interface_virtual_machine(self, idents, meta):
        vmi_name = idents['virtual-machine-interface']
        vm_name = idents['virtual-machine']
        vmi = VirtualMachineInterfaceST.locate(vmi_name)
        if vmi is not None:
            vmi.set_virtual_machine(vm_name)
        vm = VirtualMachineST.get(vm_name)
        if vm is not None:
            vm.add_interface(vmi_name)
    # end add_virtual_machine_interface_virtual_machine

    def add_virtual_machine_virtual_machine_interface(self, idents, meta):
        self.add_virtual_machine_interface_virtual_machine(idents, meta)
    # end add_virtual_machine_virtual_machine_interface

    def delete_virtual_machine_interface_virtual_machine(self, idents, meta):
        vmi_name = idents['virtual-machine-interface']
        vm_name = idents['virtual-machine']
        VirtualMachineInterfaceST.delete(vmi_name)
        vm = VirtualMachineST.get(vm_name)
        if vm is not None:
            vm.delete_interface(vmi_name)
    # end delete_virtual_machine_interface_virtual_machine

    def delete_project_virtual_machine_interface(self, idents, meta):
        vmi_name = idents['virtual-machine-interface']
        VirtualMachineInterfaceST.delete(vmi_name)
    # end delete_project_virtual_machine_interface

    def add_virtual_machine_service_instance(self, idents, meta):
        vm_name = idents['virtual-machine']
        si_name = idents['service-instance']
        vm = VirtualMachineST.locate(vm_name, si_name)
        for sc in ServiceChain._dict.values():
            if si_name in sc.service_list:
                if VirtualNetworkST.get(sc.left_vn) is not None:
                    self.current_network_set.add(sc.left_vn)
                if VirtualNetworkST.get(sc.right_vn):
                    self.current_network_set.add(sc.right_vn)
    # end add_virtual_machine_service_instance(self, idents, meta):

    def delete_virtual_machine_service_instance(self, idents, meta):
        vm_name = idents['virtual-machine']
        si_name = idents['service-instance']
        vm = VirtualMachineST.delete(vm_name)
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
        if vn is None:
            return
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
        params = BgpRouterParams()
        params.build(meta)
        router = BgpRouterST.locate(router_name, params)
        if router:
            router.set_params(params)
    # end add_bgp_router_parameters

    def delete_bgp_router_parameters(self, idents, meta):
        router_name = idents['bgp-router']
        BgpRouterST.delete(router_name)
    # end delete_bgp_router_parameters

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
            _sandesh._logger.error("NoIdError while reading service "
                                   "instance %s", si_name)
            return
        si_props = si.get_service_instance_properties()
        left_vn_str, right_vn_str = get_si_vns(si, si_props)
        if (not left_vn_str or not right_vn_str):
            _sandesh._logger.error(
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

    def process_stale_objects(self):
        for sc in ServiceChain.values():
            if sc.created_stale:
                sc.destroy()
            if sc.present_stale:
                sc.delete()
    # end process_stale_objects

    def process_poll_result(self, poll_result_str):
        something_done = False
        result_list = parse_poll_result(poll_result_str)
        self.current_network_set = set()

        # first pass thru the ifmap message and build data model
        for (result_type, idents, metas) in result_list:
            if result_type != 'searchResult' and not self.ifmap_search_done:
                self.ifmap_search_done = True
                self.process_stale_objects()
                self.current_network_set = set(VirtualNetworkST.keys())
                something_done = True
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
                    something_done = True
                    _sandesh._logger.debug("%s %s/%s/%s. Calling '%s'.",
                                           result_type.split('Result')[0].title(),
                                           meta_name, idents, meta, funcname)
                    func(idents, meta)
            # end for meta
        # end for result_type

        if not something_done:
            _sandesh._logger.debug("Process IF-MAP: Nothing was done, skip.")
            return
        if self.ifmap_search_done:
            self.process_networks()
    # end process_poll_results

    def process_networks(self):
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
                        connected_network = None
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

                        if connected_network and action.simple_action:
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

            for vmi in VirtualMachineInterfaceST.get_vmi_on_network(network_name):
                if (vmi.interface_mirror is not None and
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
                    if (prule.action_list is None or
                        prule.action_list.mirror_to is None or
                        prule.action_list.mirror_to.analyzer_name is None):
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

        for vmi in VirtualMachineInterfaceST.get_service_interfaces():
            vmi.recreate_vrf_assign_table()
    # end process_poll_result

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

    @staticmethod
    def reset():
        VirtualNetworkST.reset()
        RouteTargetST.reset()
        NetworkPolicyST.reset()
        RouteTableST.reset()
        SecurityGroupST.reset()
        ServiceChain.reset()
        BgpRouterST.reset()
        VirtualMachineInterfaceST.reset()
        InstanceIpST.reset()
        FloatingIpST.reset()
        VirtualMachineST.reset()
        LogicalRouterST.reset()
    # end reset
# end class SchemaTransformer


def set_ifmap_search_done(transformer):
    gevent.sleep(60)
    transformer.ifmap_search_done = True
    transformer.process_stale_objects()
    transformer.current_network_set = set(VirtualNetworkST.keys())
    transformer.process_networks()
# end set_ifmap_search_done

def launch_arc(transformer, ssrc_mapc):
    arc_mapc = arc_initialize(transformer._args, ssrc_mapc)
    while True:
        try:
            # If not connected to zookeeper Pause the operations
            if not _zookeeper_client.is_connected():
                time.sleep(1)
                continue
            pollreq = PollRequest(arc_mapc.get_session_id())
            glet = None
            if not transformer.ifmap_search_done:
                glet = gevent.spawn(set_ifmap_search_done, transformer)
            result = arc_mapc.call('poll', pollreq)
            if glet:
                glet.kill()
            transformer.process_poll_result(result)
        except Exception as e:
            if type(e) == socket.error:
                time.sleep(3)
            else:
                string_buf = StringIO()
                cgitb_hook(
                    file=string_buf,
                    format="text",
                    )
                try:
                    with open(transformer._args.trace_file, 'a') as err_file:
                        err_file.write(string_buf.getvalue())
                except IOError:
                    _sandesh._logger.error(
                        "Failed to open trace file %s: %s" %
                        (transformer._args.trace_file, IOError))
                if type(e) == InvalidSessionID:
                    return
                raise e
# end launch_arc


def launch_ssrc(transformer):
    while True:
        ssrc_mapc = ssrc_initialize(transformer._args)
        transformer.ifmap_search_done = False
        transformer.arc_task = gevent.spawn(launch_arc, transformer, ssrc_mapc)
        transformer.arc_task.join()
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
                         --api_server_use_ssl False
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
                         --trace_file /var/log/contrail/schema.err
                         --use_syslog
                         --syslog_facility LOG_USER
                         --cluster_id <testbed-name>
                         [--reset_config]
    '''

    # Source any specified config/ini file
    # Turn off help, so we      all options in response to -h
    conf_parser = argparse.ArgumentParser(add_help=False)

    conf_parser.add_argument("-c", "--conf_file", action='append',
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
        'api_server_use_ssl': False,
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
        'trace_file': '/var/log/contrail/schema.err',
        'use_syslog': False,
        'syslog_facility': Sandesh._DEFAULT_SYSLOG_FACILITY,
        'cluster_id': '',
        'logging_conf': '',
        'logger_class': None,
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
        config.read(args.conf_file)
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
    parser.add_argument("--api_server_use_ssl",
                        help="Use SSL to connect with API server")
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
    parser.add_argument("--trace_file", help="Filename for the error "
                        "backtraces to be written to")
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
    parser.add_argument(
        "--logging_conf",
        help=("Optional logging configuration file, default: None"))
    parser.add_argument(
        "--logger_class",
        help=("Optional external logger class, default: None"))

    args = parser.parse_args(remaining_argv)
    if type(args.cassandra_server_list) is str:
        args.cassandra_server_list = args.cassandra_server_list.split()
    if type(args.collectors) is str:
        args.collectors = args.collectors.split()

    return args
# end parse_args

transformer = None


def run_schema_transformer(args):
    global _vnc_lib

    def connection_state_update(status, message=None):
        ConnectionState.update(
            conn_type=ConnectionType.APISERVER, name='ApiServer',
            status=status, message=message or '',
            server_addrs=['%s:%s' % (args.api_server_ip,
                                     args.api_server_port)])
    # end connection_state_update

    # Retry till API server is up
    connected = False
    connection_state_update(ConnectionStatus.INIT)
    while not connected:
        try:
            _vnc_lib = VncApi(
                args.admin_user, args.admin_password, args.admin_tenant_name,
                args.api_server_ip, args.api_server_port,
                api_server_use_ssl=args.api_server_use_ssl)
            connected = True
            connection_state_update(ConnectionStatus.UP)
        except requests.exceptions.ConnectionError as e:
            # Update connection info
            connection_state_update(ConnectionStatus.DOWN, str(e))
            time.sleep(3)
        except (RuntimeError, ResourceExhaustionError):
            # auth failure or haproxy throws 503
            time.sleep(3)

    global transformer
    transformer = SchemaTransformer(args)
    transformer.ssrc_task = gevent.spawn(launch_ssrc, transformer)

    gevent.joinall([transformer.ssrc_task])
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
    _zookeeper_client.master_election(zk_path_pfx+"/schema-transformer",
                                      os.getpid(), run_schema_transformer,
                                      args)
# end main


def server_main():
    cgitb.enable(format='text')
    main()
# end server_main

if __name__ == '__main__':
    server_main()
